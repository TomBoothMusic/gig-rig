/* The remote screen: the mixer EQ as a curve with a point to drag for each band, and a live
 * picture of the sound behind it (docs/remote.md, "The EQ curve"; Tom's ask, 27 and 28 Nov 2026).
 *
 * It sits wherever a chain page shows a "mixeq" block: every mixer channel, every output, the EQ
 * in front of a mixer effect and the recording mix. It changes the same settings as the sliders
 * it replaced, through the same order (mix.param), so the sound and the saved files are as before.
 *
 * The curve is worked out here in the page, with the same sums as the EQ in the rig
 * (src/effects/mixer/MixerBlocks.cpp, MixEq). KEEP THE TWO IN STEP: the self-test holds the rig's
 * EQ to these sums (src/effects/mixer/MixerBlockChecks.cpp, "the curve the screen draws"), so if
 * the rig's EQ ever changes, that check fails and this file must follow.
 *
 * The live picture comes from the rig (the mix.rta order asks for it, GR.live.rta brings it):
 * how loud each sixth of an octave is. It is only drawn here; nothing in it touches the sound.
 */
'use strict';

(() =>
{
const h = GR.h;
const svgNS = 'http://www.w3.org/2000/svg';
const s = (tag, attrs) => { const e = document.createElementNS (svgNS, tag); for (const k in (attrs || {})) e.setAttribute (k, attrs[k]); return e; };

// The eight points, in the order the sound meets them. `f`, `g` and `q` name the block's settings.
const bands = [
    { id: 'lc', mark: 'HP', name: 'HPF',        kind: 'lowcut',  f: 'lowcut',  colour: '#9aa7b4' },
    { id: 'ls', mark: 'L',  name: 'LS',         kind: 'lowshelf', f: 'lsf', g: 'lsg', colour: '#ff7a45' },
    { id: 'b1', mark: '1',  name: 'B1',         kind: 'bell', f: 'b1f', g: 'b1g', q: 'b1q', colour: '#ffd23f' },
    { id: 'b2', mark: '2',  name: 'B2',         kind: 'bell', f: 'b2f', g: 'b2g', q: 'b2q', colour: '#34d27b' },
    { id: 'b3', mark: '3',  name: 'B3',         kind: 'bell', f: 'b3f', g: 'b3g', q: 'b3q', colour: '#46b3ff' },
    { id: 'b4', mark: '4',  name: 'B4',         kind: 'bell', f: 'b4f', g: 'b4g', q: 'b4q', colour: '#a98bff' },
    { id: 'hs', mark: 'H',  name: 'HS',         kind: 'highshelf', f: 'hsf', g: 'hsg', colour: '#ff6fb1' },
    { id: 'hc', mark: 'LP', name: 'LPF',        kind: 'highcut', f: 'highcut', colour: '#9aa7b4' },
];

// ---------------------------------------------------------------- the sums (the same as MixEq's)

const warp = (hz, rate) => Math.tan (Math.PI * GR.clamp (hz, 5, rate * 0.45) / rate);
// "20 Hz" on the low cut and "20 kHz" on the high cut mean off: the cut moves out of what can be heard.
const lowCutHz = (knob) => knob * GR.clamp (0.25 + 0.075 * (knob - 20), 0.25, 1);
const highCutHz = (knob) => knob * (1 + Math.max (0, (knob - 18000) / 2000));
const shelfK = 1 / 0.7071;

/** One filter of the EQ as the five numbers the rig's own filter uses. */
const section = (band, v, rate) =>
{
    const a = Math.pow (10, (band.g ? v[band.g] : 0) / 40);

    switch (band.kind)
    {
        case 'lowcut':    return { g: warp (lowCutHz (v.lowcut), rate), k: shelfK, m0: 1, m1: -shelfK, m2: -1 };
        case 'highcut':   return { g: warp (highCutHz (v.highcut), rate), k: shelfK, m0: 0, m1: 0, m2: 1 };
        case 'lowshelf':  return { g: warp (v.lsf, rate) / Math.sqrt (a), k: shelfK, m0: 1, m1: shelfK * (a - 1), m2: a * a - 1 };
        case 'highshelf': return { g: warp (v.hsf, rate) * Math.sqrt (a), k: shelfK, m0: a * a, m1: shelfK * (1 - a) * a, m2: 1 - a * a };
        default:          { const k = 1 / (v[band.q] * a); return { g: warp (v[band.f], rate), k, m0: 1, m1: k * (a * a - 1), m2: 0 }; }
    }
};

/** How much one filter lifts or cuts at one frequency, in dB. */
const sectionDb = (sec, hz, rate) =>
{
    const w = Math.tan (Math.PI * Math.min (hz, rate * 0.499) / rate) / sec.g;
    const dr = 1 - w * w, di = sec.k * w;
    const nr = sec.m0 * dr + sec.m2, ni = (sec.m0 * sec.k + sec.m1) * w;
    return 10 * Math.log10 (Math.max (1e-12, (nr * nr + ni * ni) / (dr * dr + di * di)));
};

/** The whole EQ at each of the frequencies given: what the curve shows. Also used by the demo. */
GR.eqResponse = (v, freqs, rate) =>
{
    const r = rate > 8000 ? rate : 96000;
    const secs = bands.map ((b) => section (b, v, r));
    return freqs.map ((hz) => secs.reduce ((sum, sec) => sum + sectionDb (sec, hz, r), 0));
};

// ---------------------------------------------------------------- the picture's own memory

const lowHz = 20, highHz = 20000, topDb = 18;       // the graph's edges: 20 Hz to 20 kHz, 18 dB up and down
const rtaTop = -6, rtaBottom = -90;                 // the live picture's own scale, top and bottom of the graph
const curveFreqs = Array.from ({ length: 241 }, (_, i) => lowHz * Math.pow (highHz / lowHz, i / 240));
const shown = { key: '', db: [], at: 0 };           // the live picture as drawn, so it falls back smoothly
let asked = { key: '', at: 0 };
let lastTap = { key: '', i: -1, at: 0 };          // kept here because the screen is built again after every touch

const hzText = (f) => f >= 10000 ? (f / 1000).toFixed (1) + ' kHz' : f >= 1000 ? (f / 1000).toFixed (2) + ' kHz' : Math.round (f) + ' Hz';
const dbText = (g) => (g > 0.04 ? '+' : '') + (Math.abs (g) < 0.05 ? '0.0' : g.toFixed (1)) + ' dB';

/** Asks the rig to look at this channel or output (and again every two seconds: the rig stops by itself when nobody asks). */
const ask = (rta, mode, now) =>
{
    const key = rta.where + rta.i + mode;
    if (asked.key === key && now - asked.at < 2000) return;
    asked = { key, at: now };
    GR.send (mode === 'off' ? { op: 'mix.rta', where: 'off' } : { op: 'mix.rta', where: rta.where, i: rta.i, before: mode === 'before' }, true);
};

// ---------------------------------------------------------------- the control

/** The EQ of one "mixeq" block. o.setOn and o.setParam send the changes (as GR.block's do);
    rta = { where: 'ch' or 'out', i } where the rig can show the live picture, or nothing. */
GR.eqCurve = (key, block, o, rta) =>
{
    const type = (GR.state.types || {})[block.type] || { params: [] };
    const facts = {};
    type.params.forEach ((p) => { facts[p.id] = p; });
    const v = block.v;
    const rate = (GR.state.rig || {}).rate;
    const limit = (id, value) => facts[id] ? GR.clamp (value, facts[id].min, facts[id].max) : value;
    const isOff = (b) => (b.kind === 'lowcut' && v.lowcut <= 20.5) || (b.kind === 'highcut' && v.highcut >= 19950);

    GR.ui.eqBand = GR.ui.eqBand || {};
    let picked = GR.clamp (GR.ui.eqBand[key] === undefined ? 2 : GR.ui.eqBand[key], 0, bands.length - 1);
    const mode = rta ? GR.recall ('rta', 'after') : 'off';

    // ---- the graph
    const svg = s ('svg', { class: 'eq-svg', role: 'img', 'aria-label': 'The EQ curve. Drag a point sideways for its frequency and up or down to boost or cut.' });
    const grid = s ('g', { class: 'eq-grid' }), live = s ('path', { class: 'eq-live' }), fill = s ('path', { class: 'eq-fill' }), line = s ('path', { class: 'eq-line' }), dots = s ('g');
    svg.append (grid, live, fill, line, dots);
    const readout = h ('div', { class: 'eq-readout' });
    const box = h ('div', { class: 'eq-box' + (block.on ? '' : ' off') }, svg);

    let W = 600, H = 280;
    const pad = { l: 16, r: 16, t: 14, b: 22 };
    const xOf = (hz) => pad.l + Math.log (hz / lowHz) / Math.log (highHz / lowHz) * (W - pad.l - pad.r);
    const hzOf = (x) => lowHz * Math.pow (highHz / lowHz, GR.clamp ((x - pad.l) / (W - pad.l - pad.r), 0, 1));
    const yOf = (db) => pad.t + (topDb - GR.clamp (db, -topDb, topDb)) / (2 * topDb) * (H - pad.t - pad.b);
    const dbOf = (y) => topDb - GR.clamp ((y - pad.t) / (H - pad.t - pad.b), 0, 1) * 2 * topDb;
    // The two cuts have no boost or cut of their own, so their points sit on a row of their own, clear of the bands on the middle line.
    const cutRow = -12;
    const place = (b) => ({ x: xOf (GR.clamp (v[b.f], lowHz, highHz)), y: yOf (b.g ? v[b.g] : cutRow) });

    const dotEls = bands.map ((b, i) =>
    {
        const g = s ('g', { class: 'eq-dot' });
        const c = s ('circle', { r: 13, fill: b.colour });
        const t = s ('text', { 'text-anchor': 'middle', dy: '0.36em' });
        t.textContent = b.mark;
        g.append (c, t);
        dots.append (g);
        return g;
    });

    const chipEls = bands.map ((x, i) => h ('button', { class: 'chip' + (isOff (x) || (x.g && Math.abs (v[x.g]) < 0.05) ? ' dim' : ''), onclick: () => { GR.ui.eqBand[key] = i; GR.render(); } },
        h ('span', { class: 'eq-chip' }, h ('i', { style: { background: x.colour } }), x.name)));

    const drawGrid = () =>
    {
        grid.replaceChildren();
        [50, 100, 200, 500, 1000, 2000, 5000, 10000].forEach ((f) =>
        {
            grid.append (s ('line', { x1: xOf (f), x2: xOf (f), y1: pad.t, y2: H - pad.b }));
            if (W < 430 && ! [100, 1000, 10000].includes (f)) return;      // a narrow phone has room for three numbers
            const t = s ('text', { x: xOf (f), y: H - 6, 'text-anchor': 'middle' });
            t.textContent = f >= 1000 ? (f / 1000) + 'k' : f;
            grid.append (t);
        });
        [-12, -6, 0, 6, 12].forEach ((g) =>
        {
            grid.append (s ('line', { x1: pad.l, x2: W - pad.r, y1: yOf (g), y2: yOf (g), class: g === 0 ? 'zero' : '' }));
            const t = s ('text', { x: pad.l + 3, y: yOf (g) - 3 });
            t.textContent = g > 0 ? '+' + g : g;
            grid.append (t);
        });
    };

    const draw = () =>
    {
        const db = GR.eqResponse (v, curveFreqs, rate);
        const d = db.map ((g, i) => (i ? 'L' : 'M') + xOf (curveFreqs[i]).toFixed (1) + ' ' + yOf (g).toFixed (1)).join ('');
        line.setAttribute ('d', d);
        fill.setAttribute ('d', d + 'L' + xOf (highHz).toFixed (1) + ' ' + yOf (0).toFixed (1) + 'L' + xOf (lowHz).toFixed (1) + ' ' + yOf (0).toFixed (1) + 'Z');

        bands.forEach ((b, i) =>
        {
            const p = place (b);
            dotEls[i].setAttribute ('transform', 'translate(' + p.x.toFixed (1) + ' ' + p.y.toFixed (1) + ')');
            dotEls[i].setAttribute ('class', 'eq-dot' + (i === picked ? ' on' : '') + (isOff (b) ? ' idle' : ''));
            dotEls[i].firstChild.setAttribute ('r', i === picked ? 16 : 13);
        });

        dots.append (dotEls[picked]);       // the picked point is drawn last, on top
        chipEls.forEach ((c, i) => c.classList.toggle ('on', i === picked));

        const b = bands[picked];
        readout.replaceChildren (
            h ('i', { style: { background: b.colour } }), h ('b', { text: b.name }),
            h ('span', { text: isOff (b) ? 'off' : hzText (v[b.f]) }),
            b.g ? h ('span', { text: dbText (v[b.g]) }) : null,
            b.q ? h ('span', { text: 'Q ' + Number (v[b.q]).toFixed (2) }) : null);
    };

    const size = () =>
    {
        const r = box.getBoundingClientRect();
        if (r.width < 40 || r.height < 40) return;
        W = r.width; H = r.height;
        svg.setAttribute ('viewBox', '0 0 ' + W.toFixed (0) + ' ' + H.toFixed (0));
        drawGrid();
        draw();
    };

    svg.setAttribute ('viewBox', '0 0 ' + W + ' ' + H);
    drawGrid();
    draw();
    if (window.ResizeObserver) new ResizeObserver (size).observe (box);

    const set = (id, value) => { v[id] = value; o.setParam (id, value); };
    const tidyHz = (f) => f >= 1000 ? Math.round (f / 10) * 10 : Math.round (f);

    // ---- dragging: a touch near a point takes hold of it; the graph never scrolls the page
    let hold = null;

    const nearest = (x, y) =>
    {
        let best = -1, bestD = 40;
        bands.forEach ((b, i) =>
        {
            const p = place (b);
            const d = Math.hypot (p.x - x, p.y - y) - (i === picked ? 6 : 0);      // the picked point wins a near tie
            if (d < bestD) { bestD = d; best = i; }
        });
        return best;
    };

    const where = (ev) => { const r = svg.getBoundingClientRect(); return { x: (ev.clientX - r.left) / r.width * W, y: (ev.clientY - r.top) / r.height * H }; };

    svg.addEventListener ('pointerdown', (ev) =>
    {
        const at = where (ev), i = nearest (at.x, at.y);
        if (i < 0) return;
        ev.preventDefault();
        svg.setPointerCapture (ev.pointerId);
        const p = place (bands[i]);
        hold = { i, dx: p.x - at.x, dy: p.y - at.y, x: ev.clientX, y: ev.clientY, moved: false };
        picked = i;
        GR.ui.eqBand[key] = i;
        GR.dragging++;
        box.classList.add ('moving');
        draw();
    });

    svg.addEventListener ('pointermove', (ev) =>
    {
        if (! hold) return;
        if (! hold.moved && Math.hypot (ev.clientX - hold.x, ev.clientY - hold.y) < 5) return;
        hold.moved = true;
        const b = bands[hold.i], at = where (ev);
        set (b.f, limit (b.f, tidyHz (hzOf (at.x + hold.dx))));
        if (b.g) set (b.g, limit (b.g, Math.round (dbOf (at.y + hold.dy) * 10) / 10));
        draw();
    });

    const release = (ev) =>
    {
        if (! hold) return;
        const was = hold;
        hold = null;
        GR.dragging = Math.max (0, GR.dragging - 1);
        box.classList.remove ('moving');

        // Two taps on a point put it back: a band to no boost or cut, a cut to off.
        const now = Date.now();
        if (! was.moved && ev.type === 'pointerup' && lastTap.key === key && lastTap.i === was.i && now - lastTap.at < 400)
        {
            const b = bands[was.i];
            if (b.g) set (b.g, 0); else set (b.f, b.kind === 'lowcut' ? 20 : 20000);
            lastTap = { key: '', i: -1, at: 0 };
        }
        else
            lastTap = was.moved ? { key: '', i: -1, at: 0 } : { key, i: was.i, at: now };

        GR.render();
    };

    svg.addEventListener ('pointerup', release);
    svg.addEventListener ('pointercancel', release);

    // A mouse wheel (or two fingers on a trackpad) over the graph sets the picked band's width.
    let wheelDone = 0;
    svg.addEventListener ('wheel', (ev) =>
    {
        const b = bands[picked];
        if (! b.q) return;
        ev.preventDefault();
        set (b.q, Math.round (limit (b.q, v[b.q] * Math.pow (1.12, ev.deltaY < 0 ? 1 : -1)) * 100) / 100);
        draw();
        clearTimeout (wheelDone);
        wheelDone = setTimeout (() => GR.render(), 400);
    }, { passive: false });

    // ---- the live picture
    const drawLive = (l) =>
    {
        if (! rta) return;
        const now = Date.now();
        ask (rta, mode, now);
        const r = l && l.rta;

        if (mode === 'off' || ! r || r.where !== rta.where || r.i !== rta.i || ! r.db || ! r.db.length) { live.setAttribute ('d', ''); return; }

        const k = rta.where + rta.i + mode;
        if (shown.key !== k || shown.db.length !== r.db.length) { shown.key = k; shown.db = r.db.slice(); shown.at = now; }

        // Up at once, down at 30 dB a second: easy to read, and a short note is not missed.
        const fall = Math.min (12, (now - shown.at) / 1000 * 30);
        shown.at = now;
        shown.db = shown.db.map ((old, i) => Math.max (r.db[i], old - fall));

        const yLive = (db) => pad.t + GR.clamp ((rtaTop - db) / (rtaTop - rtaBottom), 0, 1) * (H - pad.t - pad.b);
        const per = r.perOctave || 6, low = r.lowHz || 20;
        let d = '';
        shown.db.forEach ((db, i) =>
        {
            const hz = low * Math.pow (2, i / per);
            if (hz < lowHz * 0.99 || hz > highHz * 1.02) return;
            d += (d ? 'L' : 'M') + xOf (Math.min (hz, highHz)).toFixed (1) + ' ' + yLive (db).toFixed (1);
        });
        live.setAttribute ('d', d ? d + 'L' + xOf (highHz).toFixed (1) + ' ' + (H - pad.b) + 'L' + xOf (lowHz).toFixed (1) + ' ' + (H - pad.b) + 'Z' : '');
    };

    if (rta) { GR.onLive = drawLive; drawLive (GR.live); }

    // ---- under the graph: the picked band, big enough for a finger
    const b = bands[picked];
    const slider = (id, label) => facts[id] ? GR.param (Object.assign ({}, facts[id], { label }), v[id] !== undefined ? v[id] : facts[id].def, (value, done) =>
    {
        set (id, value);
        draw();
        if (done && ! GR.dragging) GR.render();
    }) : null;

    const flat = () => GR.ask ('Flatten the EQ? Gains to 0 dB, HPF and LPF off.', 'Flat', () =>
    {
        bands.forEach ((x) => { if (x.g) set (x.g, 0); else set (x.f, x.kind === 'lowcut' ? 20 : 20000); });
        GR.render();
    });

    const liveWords = { ch: { before: 'Pre', after: 'Post' }, out: { before: 'Pre', after: 'Post' } };

    return [
        h ('div', { class: 'row between wrap' },
            h ('label', { class: 'inline' }, block.on ? 'EQ on' : 'EQ off', GR.toggle (block.on, (on) => { block.on = on; o.setOn (on); GR.render(); }, 'EQ')),
            h ('button', { class: 'btn', onclick: flat }, 'Flat')),
        readout,
        box,
        h ('div', { class: 'chips eq-bands' }, chipEls),
        h ('div', { class: 'eq-band' },
            slider (b.f, 'Freq'),
            b.g ? slider (b.g, 'Gain') : null,
            b.q ? slider (b.q, 'Q') : null),
        rta ? h ('div', { class: 'eq-rta' },
            h ('span', { class: 'hint', text: 'RTA' }),
            GR.chips ([{ id: 'off', label: 'Off' }, { id: 'before', label: liveWords[rta.where].before }, { id: 'after', label: liveWords[rta.where].after }], mode,
                (m) => { GR.remember ('rta', m); asked = { key: '', at: 0 }; GR.render(); }),
            ! GR.state.mixer.on ? h ('p', { class: 'hint', text: 'Mixer off: no signal.' }) : null) : null,
    ];
};
})();
