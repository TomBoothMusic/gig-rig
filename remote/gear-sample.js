/* A STYLE SAMPLE, in the demo only (docs/remote.md, "The gear look sample").

   Tom's idea (27 Nov 2026): each effect should look like the gear it copies. This file shows the
   idea on one block, the 1176 style compressor (effect id `comp`): a black studio face, knobs that
   turn by dragging, ratio buttons, and a backlit VU meter whose needle follows the gain reduction.

   It is NOT part of the rig's own remote screen yet: it is not in index.html and not in the app's
   built-in files. tools/remote_demo.py adds it to the one-file demo page only. It changes no other
   file: it wraps GR.block and brings its own styles, so taking it out is deleting this file.

   What a real version needs is listed at the end of docs/remote.md. In short: the needle here is
   fed by a pretend signal (the demo has no sound). In the app it would read the gain reduction
   the engine already reports (`live.mix.gr` for a mixer channel), which costs the audio nothing:
   all drawing is in the browser, one needle turn per screen frame, and only while a meter is on
   screen. */

'use strict';

(() =>
{
    const h = GR.h, clamp = GR.clamp;

    // ------------------------------------------------------------ the look

    const css = `
.g1176 { --face: #101113; --ink: #e9e6dc; --ink2: #a9a69b; grid-column: 1 / -1; container-type: inline-size; }
.g1176-face { position: relative; border-radius: 6px; padding: 14px 16px 12px; color: var(--ink);
    background:
        repeating-linear-gradient(90deg, #ffffff07 0 1px, #0000 1px 3px),
        linear-gradient(180deg, #1c1d20, #0c0d0e 55%, #141517);
    box-shadow: inset 0 1px 0 #ffffff26, inset 0 -1px 0 #000, 0 2px 6px #0009;
    display: grid; gap: 14px 18px; align-items: center; justify-items: center;
    grid-template-columns: 1fr 1fr; grid-template-areas: "meter meter" "in out" "small small" "foot foot";
    font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; user-select: none; -webkit-user-select: none; }
.g1176-face::before, .g1176-face::after { content: ""; position: absolute; top: 50%; width: 9px; height: 9px; margin-top: -5px; border-radius: 50%;
    background: radial-gradient(circle at 35% 30%, #9a9a9a, #2b2b2b 70%); box-shadow: 0 0 0 1px #000; }
.g1176-face::before { left: 4px; } .g1176-face::after { right: 4px; }
@container (min-width: 600px) {
    .g1176-face { grid-template-columns: 1fr 1fr 1.3fr; grid-template-areas: "in out meter" "small small small" "foot foot foot"; padding: 16px 26px 12px; }
}
@container (min-width: 940px) {
    .g1176-face { grid-template-columns: auto auto 1fr auto; grid-template-areas: "in out small meter" "foot foot foot foot"; }
}
.g-in { grid-area: in; } .g-out { grid-area: out; } .g-meter { grid-area: meter; } .g-small { grid-area: small; } .g-foot { grid-area: foot; }
.g-small { display: flex; gap: 14px; align-items: flex-end; flex-wrap: wrap; justify-content: center; }
.g-foot { width: 100%; display: flex; justify-content: space-between; align-items: center; gap: 8px; font-size: 10px; letter-spacing: .14em; color: var(--ink2); text-transform: uppercase; }
.g-foot button { background: none; border: 0; color: var(--ink2); font-size: 11px; text-decoration: underline; padding: 8px 0; letter-spacing: 0; text-transform: none; }

.g-knob { display: flex; flex-direction: column; align-items: center; gap: 2px; }
.g-knob-cap { font-size: 11px; font-weight: 700; letter-spacing: .16em; text-transform: uppercase; }
.g-knob-val { font-size: 11px; color: var(--ink2); font-variant-numeric: tabular-nums; min-height: 15px; }
.g-dial { position: relative; width: var(--d); height: var(--d); touch-action: none; cursor: ns-resize; border-radius: 50%; }
.g-dial:focus-visible { outline: 2px solid var(--accent); outline-offset: 4px; }
.g-dial svg { position: absolute; inset: 0; width: 100%; height: 100%; overflow: visible; }
.g-dial text { fill: var(--ink); font-size: 8.5px; font-weight: 600; text-anchor: middle; dominant-baseline: middle; }
.g-turn { position: absolute; inset: 21%; border-radius: 50%; will-change: transform;
    background: conic-gradient(from 0deg, #d9d9d9, #8d8d8d 12%, #f1f1f1 25%, #7c7c7c 38%, #dcdcdc 50%, #8a8a8a 62%, #f3f3f3 75%, #808080 88%, #d9d9d9);
    box-shadow: 0 3px 6px #000c, 0 0 0 1px #000; }
.g-turn::before { content: ""; position: absolute; inset: 16%; border-radius: 50%;
    background: radial-gradient(circle at 40% 30%, #3a3b3e, #0b0b0c 70%); box-shadow: inset 0 1px 1px #ffffff30, 0 1px 2px #000; }
.g-turn::after { content: ""; position: absolute; left: 50%; top: 1%; width: 3px; height: 30%; margin-left: -1.5px; border-radius: 2px; background: #fff; box-shadow: 0 0 2px #000; }
.g-dial.moving .g-turn { box-shadow: 0 3px 6px #000c, 0 0 0 1px #000, 0 0 0 4px #f2a93b55; }

.g-btns { display: flex; flex-direction: column; align-items: center; gap: 4px; }
.g-btns-row { display: flex; gap: 3px; }
.g-btn { min-width: 34px; height: 40px; padding: 0 4px; border-radius: 3px; border: 1px solid #000; color: #d8d5cc; font-size: 12px; font-weight: 700;
    background: linear-gradient(180deg, #4a4b4f, #2a2b2e 60%, #1d1e20); box-shadow: inset 0 1px 0 #ffffff40, 0 2px 0 #000; }
.g-btn.in { background: linear-gradient(180deg, #1b1c1e, #2c2d30); box-shadow: inset 0 2px 3px #000; color: #ffd27a; transform: translateY(2px); }

.g-vu { width: min(100%, 250px); min-width: 190px; padding: 6px; border-radius: 5px; background: linear-gradient(180deg, #2a2b2e, #0a0a0b); box-shadow: inset 0 1px 0 #ffffff30, 0 2px 4px #000; }
.g-vu svg { display: block; width: 100%; height: auto; border-radius: 3px; }
.g-vu .lamp { transition: opacity .5s; }
.g-vu.dark .lamp { opacity: .12; }
.g-vu .needle { transform-origin: 100px 118px; will-change: transform; }
`;

    if (! document.getElementById ('g1176-css'))
        document.head.append (h ('style', { id: 'g1176-css', text: css }));

    const svg = (tag, attrs, ...kids) =>
    {
        const e = document.createElementNS ('http://www.w3.org/2000/svg', tag);
        Object.keys (attrs || {}).forEach ((k) => e.setAttribute (k, attrs[k]));
        kids.flat (Infinity).forEach ((k) => { if (k) e.append (k); });
        return e;
    };

    // ------------------------------------------------------------ a knob that turns by dragging

    const SWEEP = 150;    // degrees each side of straight up

    /** p is the setting as the rig describes it (min, max, def, label, unit). marks are the numbers printed round it. */
    const knob = (p, value, change, o) =>
    {
        const size = o.size, marks = o.marks;
        const frac = (v) => (clamp (v, p.min, p.max) - p.min) / (p.max - p.min);
        const text = (v) => (p.unit === 'dB' && v > 0 ? '+' : '') + v.toFixed (p.unit === '%' ? 0 : 1) + (p.unit ? ' ' + p.unit : '');
        const tidy = (v) => { const m = p.max - p.min > 20 ? 10 : 100; return Math.round (v * m) / m; };

        let v = clamp (value, p.min, p.max);
        const turn = h ('div', { class: 'g-turn' });
        const val = h ('div', { class: 'g-knob-val', text: text (v) });

        const scale = svg ('svg', { viewBox: '0 0 100 100', 'aria-hidden': 'true' },
            marks.map ((m) =>
            {
                const a = (-SWEEP + 2 * SWEEP * frac (m)) * Math.PI / 180;
                return [
                    svg ('line', { x1: 50 + 33 * Math.sin (a), y1: 50 - 33 * Math.cos (a), x2: 50 + 37 * Math.sin (a), y2: 50 - 37 * Math.cos (a), stroke: '#e9e6dc', 'stroke-width': 1.2 }),
                    Object.assign (svg ('text', { x: 50 + 45 * Math.sin (a), y: 50 - 45 * Math.cos (a), style: 'font-size:' + Math.min (13, 8.5 * 120 / size).toFixed (1) + 'px' }), { textContent: String (m) }),
                ];
            }));

        const dial = h ('div', { class: 'g-dial', tabindex: '0', role: 'slider',
                                 'aria-label': p.label, 'aria-valuemin': p.min, 'aria-valuemax': p.max }, scale, turn);
        dial.style.setProperty ('--d', size + 'px');

        const place = () =>
        {
            turn.style.transform = 'rotate(' + (-SWEEP + 2 * SWEEP * frac (v)) + 'deg)';
            val.textContent = text (v);
            dial.setAttribute ('aria-valuenow', v);
            dial.setAttribute ('aria-valuetext', text (v));
        };
        place();

        let from = null, lastTap = 0;

        dial.addEventListener ('pointerdown', (ev) =>
        {
            dial.setPointerCapture (ev.pointerId);
            from = { x: ev.clientX, y: ev.clientY, f: frac (v), moved: false };
            GR.dragging++;
            dial.classList.add ('moving');
            ev.preventDefault();
            dial.focus ({ preventScroll: true });
        });

        dial.addEventListener ('pointermove', (ev) =>
        {
            if (! from) return;
            const d = (from.y - ev.clientY) + (ev.clientX - from.x);    // up or right turns it up
            if (! from.moved && Math.abs (d) < 3) return;
            from.moved = true;
            v = tidy (p.min + (p.max - p.min) * clamp (from.f + d / 220, 0, 1));    // 220 px of finger for the whole turn
            place();
            change (v, false);
        });

        const end = () =>
        {
            if (! from) return;
            const moved = from.moved;
            from = null;
            if (GR.dragging > 0) GR.dragging--;
            dial.classList.remove ('moving');

            if (! moved)
            {
                const t = Date.now();
                if (t - lastTap < 350) { v = p.def; place(); change (v, true); }    // two quick taps: back to where it starts
                lastTap = t;
                return;
            }

            change (v, true);
        };

        dial.addEventListener ('pointerup', end);
        dial.addEventListener ('pointercancel', end);

        dial.addEventListener ('keydown', (ev) =>
        {
            const step = (p.max - p.min) / 50;
            const d = ev.key === 'ArrowUp' || ev.key === 'ArrowRight' ? step : ev.key === 'ArrowDown' || ev.key === 'ArrowLeft' ? -step : 0;
            if (! d) return;
            ev.preventDefault();
            v = tidy (clamp (v + d, p.min, p.max));
            place();
            change (v, true);
        });

        return h ('div', { class: 'g-knob ' + (o.cls || '') }, h ('div', { class: 'g-knob-cap', text: o.name }), dial, val);
    };

    // ------------------------------------------------------------ the meter

    /** Where a VU needle sits for a reading in VU (0 VU is about 71% of the way across, as on a real one). */
    const vuAngle = (vu) => -47 + 94 * clamp (Math.pow (10, vu / 20) / Math.pow (10, 3 / 20), 0, 1.04);

    const meterFace = () =>
    {
        const cx = 100, cy = 118, r = 86;
        const pt = (vu, rad) => { const a = vuAngle (vu) * Math.PI / 180; return [cx + rad * Math.sin (a), cy - rad * Math.cos (a)]; };
        const arc = (from, to, rad) => { const a = pt (from, rad), b = pt (to, rad); return 'M' + a[0] + ' ' + a[1] + ' A' + rad + ' ' + rad + ' 0 0 1 ' + b[0] + ' ' + b[1]; };
        const marks = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
        const needle = svg ('g', { class: 'needle' }, svg ('line', { x1: cx, y1: cy - 12, x2: cx, y2: cy - r - 6, stroke: '#16110a', 'stroke-width': 1.3, 'stroke-linecap': 'round' }));

        const face = svg ('svg', { viewBox: '0 0 200 112', role: 'img', 'aria-label': 'VU meter' },
            svg ('defs', {},
                svg ('radialGradient', { id: 'g1176-lamp', cx: '50%', cy: '108%', r: '95%' },
                    svg ('stop', { offset: '0', 'stop-color': '#fff6cf' }), svg ('stop', { offset: '.45', 'stop-color': '#ffd98a' }),
                    svg ('stop', { offset: '.8', 'stop-color': '#f0a94a' }), svg ('stop', { offset: '1', 'stop-color': '#c9742a' })),
                svg ('linearGradient', { id: 'g1176-glass', x1: '0', y1: '0', x2: '0', y2: '1' },
                    svg ('stop', { offset: '0', 'stop-color': '#fff', 'stop-opacity': '.28' }), svg ('stop', { offset: '.4', 'stop-color': '#fff', 'stop-opacity': '0' }))),
            svg ('rect', { width: 200, height: 112, fill: '#3b2a17' }),                                  // the card with the lamp off
            svg ('rect', { class: 'lamp', width: 200, height: 112, fill: 'url(#g1176-lamp)' }),          // and with it on
            svg ('path', { d: arc (-20, 0, r), fill: 'none', stroke: '#16110a', 'stroke-width': 1.6 }),
            svg ('path', { d: arc (0, 3, r - 1.2), fill: 'none', stroke: '#b3261a', 'stroke-width': 4 }),
            marks.map ((m) =>
            {
                const a = pt (m, r), b = pt (m, r + (m % 5 === 0 || m >= -3 ? 8 : 5)), t = pt (m, r + 15);
                return [
                    svg ('line', { x1: a[0], y1: a[1], x2: b[0], y2: b[1], stroke: m > 0 ? '#b3261a' : '#16110a', 'stroke-width': 1.3 }),
                    Object.assign (svg ('text', { x: t[0], y: t[1], 'font-size': 8.5, 'font-weight': 700, 'text-anchor': 'middle', fill: m > 0 ? '#b3261a' : '#16110a',
                                                 'font-family': 'Helvetica, Arial, sans-serif' }), { textContent: String (Math.abs (m)) }),
                ];
            }),
            Object.assign (svg ('text', { x: 100, y: 84, 'font-size': 15, 'font-weight': 700, 'text-anchor': 'middle', fill: '#16110a', 'font-family': 'Georgia, serif' }), { textContent: 'VU' }),
            Object.assign (svg ('text', { class: 'mode', x: 100, y: 96, 'font-size': 6.5, 'text-anchor': 'middle', fill: '#16110a', 'letter-spacing': '.12em',
                                         'font-family': 'Helvetica, Arial, sans-serif' }), { textContent: 'GAIN REDUCTION' }),
            needle,
            svg ('rect', { y: 100, width: 200, height: 12, fill: '#0d0d0e' }),
            svg ('rect', { width: 200, height: 112, fill: 'url(#g1176-glass)' }));

        return { face, needle, mode: face.querySelector ('.mode') };
    };

    // What the needles are doing, kept by block so a redraw of the screen does not make them jump.
    const needles = new Map();      // key -> { pos, vel, gr, el, block, mode }
    let running = false, last = 0;

    /** The demo has no sound, so it makes a level that rises and falls like strummed chords, in dB. */
    const pretendLevel = (t, seed) =>
    {
        const beat = 520 + seed * 37, x = (t % beat) / beat, bar = Math.floor (t / beat) % 8;
        const hit = [1, 0.55, 0.8, 0.5, 0.95, 0.6, 0.85, 0.35][bar];
        return -30 + 22 * hit * Math.exp (-3.2 * x) + 1.5 * Math.sin (t / 90 + seed);
    };

    /** Gain reduction in dB (0 or below) for one block right now. */
    const reductionNow = (n, t, dt) =>
    {
        const b = n.block, v = (id, d) => b.v[id] !== undefined ? b.v[id] : d;
        if (! b.on) { n.gr = 0; n.out = -40; return; }

        if (! GR.demo)
        {
            // The real rig: the engine's own figure, as the mixer screen's small bars use it.
            const g = n.channel >= 0 && GR.live && GR.live.mix ? GR.live.mix.gr[n.channel] || 0 : 0;
            n.gr = g; n.out = -40;
            return;
        }

        const level = pretendLevel (t, n.seed) + v ('input', 0) + 3;
        const ratio = [4, 8, 12, 20, 30][Math.round (v ('ratio', 0))] || 4;
        const over = level - (-10) - (ratio > 20 ? -3 : 0);
        const want = over > 0 ? -over * (1 - 1 / ratio) : 0;
        const releaseMs = 1100 * Math.pow (50 / 1100, (v ('release', 5) - 1) / 6);     // 1 slowest, 7 fastest
        const attackMs = 12 * Math.pow (0.2 / 12, (v ('attack', 3) - 1) / 6);
        const k = 1 - Math.exp (-dt / (want < n.gr ? attackMs : releaseMs));
        n.gr += (want - n.gr) * k;
        n.out = level + n.gr + v ('output', 0);
    };

    const frame = (t) =>
    {
        const dt = Math.min (50, t - last || 16);
        last = t;
        let alive = 0;

        needles.forEach ((n, key) =>
        {
            if (! n.el || ! n.el.isConnected) { if (t - n.seen > 4000) needles.delete (key); return; }
            alive++;
            n.seen = t;
            reductionNow (n, t, dt);

            const vu = n.mode === 'off' ? -60 : n.mode === 'out' ? n.out + 4 : n.gr;
            const target = vuAngle (Math.max (-40, vu));

            // A real needle has weight: it swings to the reading and settles (about a third of a second).
            const steps = Math.max (1, Math.round (dt / 4)), hStep = dt / steps / 1000, w = 2 * Math.PI * 3.2, z = 0.72;
            for (let i = 0; i < steps; ++i)
            {
                n.vel += (w * w * (target - n.pos) - 2 * z * w * n.vel) * hStep;
                n.pos += n.vel * hStep;
            }

            n.pos = clamp (n.pos, -50, 52);
            n.el.style.transform = 'rotate(' + n.pos.toFixed (2) + 'deg)';
        });

        if (alive > 0 && ! document.hidden) requestAnimationFrame (frame);
        else running = false;
    };

    const watch = (key, block, needleEl) =>
    {
        const ch = /^ch(\d+)\./.exec (key);
        const n = needles.get (key) || { pos: vuAngle (-60), vel: 0, gr: 0, out: -40, mode: 'gr', seed: needles.size };
        n.el = needleEl; n.block = block; n.channel = ch ? Number (ch[1]) : -1; n.seen = performance.now();
        needleEl.style.transform = 'rotate(' + n.pos.toFixed (2) + 'deg)';
        needles.set (key, n);
        if (! running) { running = true; last = performance.now(); requestAnimationFrame (frame); }
        return n;
    };

    document.addEventListener ('visibilitychange', () => { if (! document.hidden && ! running && needles.size) { running = true; last = performance.now(); requestAnimationFrame (frame); } });

    // ------------------------------------------------------------ the 1176 style face

    const face1176 = (key, block, o, type) =>
    {
        const P = {};
        type.params.forEach ((p) => { P[p.id] = p; });
        const val = (id) => block.v[id] !== undefined ? block.v[id] : P[id].def;
        const set = (id) => (v, done) => { block.v[id] = v; o.setParam (id, v); if (done && ! GR.dragging) GR.render(); };

        const m = meterFace();
        const n = watch (key, block, m.needle);
        const vu = h ('div', { class: 'g-vu' + (block.on && n.mode !== 'off' ? '' : ' dark') }, m.face);

        const modes = [['gr', 'GR'], ['out', '+4'], ['off', 'OFF']];
        const modeBtns = modes.map (([id, label]) => h ('button', { class: 'g-btn' + (n.mode === id ? ' in' : ''), 'aria-pressed': n.mode === id ? 'true' : 'false',
            title: id === 'gr' ? 'The needle shows how much it is turning the sound down' : id === 'out' ? 'The needle shows the level coming out' : 'Meter and its lamp off',
            onclick: () =>
            {
                n.mode = id;
                modeBtns.forEach ((b, i) => { b.classList.toggle ('in', modes[i][0] === id); b.setAttribute ('aria-pressed', modes[i][0] === id ? 'true' : 'false'); });
                vu.classList.toggle ('dark', ! block.on || id === 'off');
                m.mode.textContent = id === 'out' ? 'OUTPUT LEVEL' : 'GAIN REDUCTION';
            } }, label));
        m.mode.textContent = n.mode === 'out' ? 'OUTPUT LEVEL' : 'GAIN REDUCTION';

        const ratio = Math.round (val ('ratio'));
        const ratioBtns = P.ratio.choices.map ((c, i) => h ('button', { class: 'g-btn' + (i === ratio ? ' in' : ''), 'aria-pressed': i === ratio ? 'true' : 'false',
            onclick: () => set ('ratio') (i, true) }, c.toUpperCase()));

        return h ('div', { class: 'g1176' },
            h ('div', { class: 'g1176-face' },
                h ('div', { class: 'g-in' }, knob (P.input, val ('input'), set ('input'), { name: 'Input', size: 120, marks: [-20, -10, 0, 10, 20, 30] })),
                h ('div', { class: 'g-out' }, knob (P.output, val ('output'), set ('output'), { name: 'Output', size: 120, marks: [-30, -20, -10, 0, 10, 20] })),
                h ('div', { class: 'g-small' },
                    knob (P.attack, val ('attack'), set ('attack'), { name: 'Attack', size: 84, marks: [1, 3, 5, 7] }),
                    knob (P.release, val ('release'), set ('release'), { name: 'Release', size: 84, marks: [1, 3, 5, 7] }),
                    h ('div', { class: 'g-btns' }, h ('div', { class: 'g-knob-cap', text: 'Ratio' }), h ('div', { class: 'g-btns-row' }, ratioBtns)),
                    knob (P.mix, val ('mix'), set ('mix'), { name: 'Mix', size: 64, marks: [0, 50, 100] })),
                h ('div', { class: 'g-meter g-btns' }, vu, h ('div', { class: 'g-btns-row' }, h ('span', { class: 'g-knob-cap', style: { alignSelf: 'center', marginRight: '6px' }, text: 'Meter' }), modeBtns)),
                h ('div', { class: 'g-foot' },
                    h ('span', { text: 'Peak limiter · 1176 style' }),
                    h ('button', { onclick: () => { GR.ui.plain = GR.ui.plain || {}; GR.ui.plain[key] = true; GR.render(); } }, 'Show as sliders'))),
            h ('p', { class: 'hint', text: 'Style sample. Drag a knob up or down to turn it, tap it twice to put it back. '
                + (GR.demo ? 'The demo has no sound, so the needle follows a pretend guitar: turn Input up and watch it work harder.' : '') }));
    };

    // ------------------------------------------------------------ hooking it in, without touching the other files

    const plainBlock = GR.block;

    GR.block = (key, block, o) =>
    {
        const el = plainBlock (key, block, o);
        const type = (GR.state.types || {})[block.type];
        const body = el.querySelector ('.block-body');
        if (block.type !== 'comp' || ! type || ! body) return el;

        if (GR.ui.plain && GR.ui.plain[key])
        {
            body.append (h ('button', { class: 'btn tiny ghost', onclick: () => { GR.ui.plain[key] = false; GR.render(); } }, 'Show as the real thing'));
            return el;
        }

        body.replaceChildren (face1176 (key, block, o, type));
        return el;
    };
})();
