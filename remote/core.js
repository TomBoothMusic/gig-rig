/* The remote screen: the part every screen shares (docs/remote.md).
 *
 * The page is plain JavaScript with no libraries, so the rig can serve it with no internet.
 * It talks to one "backend": the real rig over the network (/api/state, /api/live, /api/cmd),
 * or the demo in demo.js, which answers the same three questions from a copy kept in the page.
 *
 *   GR.state   what changes only when somebody changes it (RemoteApi::state)
 *   GR.live    what moves by itself, about eight times a second (RemoteApi::live)
 *   GR.ui      what this screen is showing (which tab, which sheet), kept in the page only
 *   GR.send    asks the rig to do one thing (RemoteApi::command)
 *
 * A screen is a function that builds its part of the page from GR.state. The whole screen is
 * built again when the state changes; only the moving parts (meters, beat, loop positions)
 * are touched on each live tick, through GR.onLive.
 */
'use strict';

const GR = window.GR = {
    state: null,
    live: null,
    ui: { tab: 'looper', open: {}, mixView: 'main', fxChain: 0, buildTab: 'tracks', buildBank: 0, songFilter: '' },
    screens: {},        // name -> function (root)
    onLive: null,       // set by the screen now showing
    sheetStack: [],
    dragging: 0,
    demo: false,
    lost: false,
};

// ---------------------------------------------------------------- small helpers

/** h('div', { class: 'card', onclick: fn }, child, 'text', [more]) */
function h (tag, attrs, ...kids)
{
    const e = document.createElement (tag);

    for (const k in (attrs || {}))
    {
        const v = attrs[k];

        if (v === undefined || v === null || v === false) continue;

        if (k === 'class')                      e.className = v;
        else if (k === 'text')                  e.textContent = v;
        else if (k === 'style')                 Object.assign (e.style, v);
        else if (k.startsWith ('on'))           e.addEventListener (k.slice (2), v);
        else if (k === 'value' || k === 'checked' || k === 'disabled' || k === 'selected') e[k] = v;
        else                                    e.setAttribute (k, v === true ? '' : v);
    }

    const add = (kid) =>
    {
        if (kid === undefined || kid === null || kid === false) return;
        if (Array.isArray (kid))                kid.forEach (add);
        else                                    e.append (kid instanceof Node ? kid : document.createTextNode (String (kid)));
    };

    kids.forEach (add);
    return e;
}

GR.h = h;
GR.copy = (x) => JSON.parse (JSON.stringify (x));
GR.clamp = (v, lo, hi) => Math.min (hi, Math.max (lo, v));

GR.remember = (key, value) => { try { localStorage.setItem ('gigrig.' + key, JSON.stringify (value)); } catch (e) { /* private window */ } };
GR.recall = (key, otherwise) =>
{
    try { const v = localStorage.getItem ('gigrig.' + key); return v === null ? otherwise : JSON.parse (v); }
    catch (e) { return otherwise; }
};

/** "-6.0", "+2.5", "off" */
GR.db = (v) =>
{
    const off = GR.state ? GR.state.mixer.offDb : -90;
    if (v <= off + 0.5) return 'off';
    return (v > 0.04 ? '+' : '') + (Math.abs (v) < 0.05 ? '0.0' : v.toFixed (1));
};

// A fader's travel (0 at the bottom, 1 at the top) to decibels and back: 0 dB sits three quarters of the way up.
GR.faderDb = (p) =>
{
    const off = GR.state ? GR.state.mixer.offDb : -90;
    if (p <= 0.03)  return off;
    if (p >= 0.5)   return -10 + (p - 0.5) * 40;
    if (p >= 0.25)  return -30 + (p - 0.25) * 80;
    return -60 + (p - 0.03) / 0.22 * 30;
};

GR.faderPos = (db) =>
{
    if (db <= -60)  return db <= -89 ? 0 : 0.03;
    if (db >= -10)  return Math.min (1, 0.5 + (db + 10) / 40);
    if (db >= -30)  return 0.25 + (db + 30) / 80;
    return 0.03 + (db + 60) / 30 * 0.22;
};

/** A meter's peak (0 to 1 and over) to how much of the bar is lit. */
GR.meterPos = (peak) =>
{
    if (! (peak > 0.00001)) return 0;
    const db = 20 * Math.log10 (peak);
    return GR.clamp ((db + 60) / 60, 0, 1);
};

// ---------------------------------------------------------------- the backend

const real =
{
    queue: Promise.resolve(),
    stateV: -1,

    async get (path)
    {
        const r = await fetch (path, { cache: 'no-store' });
        if (! r.ok) throw new Error ('the rig answered ' + r.status);
        return r.json();
    },

    // Commands go one at a time, in the order they were made (a switch going down must arrive before it comes up).
    cmd (c)
    {
        const run = async () =>
        {
            const r = await fetch ('/api/cmd', { method: 'POST', body: JSON.stringify (c), headers: { 'Content-Type': 'application/json' } });
            if (! r.ok) throw new Error ('the rig answered ' + r.status);
            return r.json();
        };

        const p = real.queue.then (run, run);
        real.queue = p.catch (() => {});
        return p;
    },

    async tick()
    {
        const l = await real.get ('/api/live');
        GR.live = l.l;

        if (l.v !== real.stateV)
        {
            const s = await real.get ('/api/state');
            real.stateV = s.v;
            GR.state = s.s;
            return true;
        }

        return false;
    },

    async report()   { const r = await fetch ('/api/report', { cache: 'no-store' }); return r.text(); },
};

const demoBackend =
{
    cmd (c)          { return Promise.resolve (GR.demoRig.command (c)); },
    async tick()
    {
        const changed = GR.demoRig.changed;
        if (changed) { GR.demoRig.changed = false; GR.state = GR.demoRig.state; }
        GR.live = GR.demoRig.live();
        return changed;
    },
    async report()   { return GR.demoRig.report(); },
};

let backend = real;

/** Asks the rig to do one thing. Shows what went wrong, or the rig's note. Returns the rig's answer. */
GR.send = async (c, quiet) =>
{
    try
    {
        const r = await backend.cmd (c);

        if (r && r.ok === false)        GR.toast (r.error || 'The rig could not do that.', true);
        else if (r && r.note && ! quiet) GR.toast (r.note);

        return r || { ok: false };
    }
    catch (e)
    {
        GR.toast ('That did not reach the rig. Check the Wi-Fi.', true);
        return { ok: false, error: String (e) };
    }
};

// A moving slider or fader sends at most every 50 ms, and always its last value.
const pending = new Map();
let pendingTimer = 0;

GR.sendValue = (key, c) =>
{
    pending.set (key, c);

    if (! pendingTimer)
        pendingTimer = setTimeout (() =>
        {
            pendingTimer = 0;
            const all = [...pending.values()];
            pending.clear();
            all.forEach ((x) => GR.send (x, true));
        }, 50);
};

GR.report = () => backend.report();

// ---------------------------------------------------------------- foot switches

// A finger on a switch is a foot on the real one: down when it lands, up when it lifts,
// even if the screen was built again in between (so the lift is caught on the window).
const held = new Map();     // pointer id -> switch

GR.switchDown = (ev, sw) =>
{
    ev.preventDefault();
    if ([...held.values()].includes (sw)) return;
    held.set (ev.pointerId, sw);
    GR.send ({ op: 'pedal', sw, down: true }, true);
    document.querySelectorAll ('[data-sw="' + sw + '"]').forEach ((e) => e.classList.add ('down'));
};

const switchUp = (ev) =>
{
    if (! held.has (ev.pointerId)) return;
    const sw = held.get (ev.pointerId);
    held.delete (ev.pointerId);
    GR.send ({ op: 'pedal', sw, down: false }, true);
    document.querySelectorAll ('[data-sw="' + sw + '"]').forEach ((e) => e.classList.remove ('down'));
};

window.addEventListener ('pointerup', switchUp);
window.addEventListener ('pointercancel', switchUp);
GR.switchHeld = (sw) => [...held.values()].includes (sw);

// ---------------------------------------------------------------- messages, questions, sheets

let toastTimer = 0;

GR.toast = (text, bad) =>
{
    const t = document.getElementById ('toast');
    if (! t) return;
    t.textContent = text;
    t.className = 'toast show' + (bad ? ' bad' : '');
    clearTimeout (toastTimer);
    toastTimer = setTimeout (() => { t.className = 'toast'; }, bad ? 6000 : 3500);
};

/** A question with two answers, in the page (a browser's own pop-up boxes do not work everywhere). */
GR.ask = (text, yes, then, opts) =>
{
    const o = opts || {};
    GR.openSheet ({
        small: true,
        title: o.title || 'Are you sure?',
        build: (close) => [
            h ('p', { class: 'ask-text', text }),
            o.extra ? o.extra() : null,
            h ('div', { class: 'row end' },
                h ('button', { class: 'btn', onclick: close }, o.no || 'Cancel'),
                h ('button', { class: 'btn ' + (o.danger ? 'danger' : 'primary'), onclick: () => { close(); then(); } }, yes)),
        ],
    });
};

/** Asks for a name in a small sheet, then calls then (name). */
GR.askName = (title, hint, first, yes, then) =>
{
    let name = first || '';
    GR.openSheet ({ small: true, title, build: (close) => [
        h ('p', { class: 'hint', text: hint }),
        h ('label', { class: 'field' }, 'Name', h ('input', { type: 'text', value: name, maxlength: 60, oninput: (ev) => { name = ev.target.value; } })),
        h ('div', { class: 'row end' }, h ('button', { class: 'btn', onclick: close }, 'Cancel'),
            h ('button', { class: 'btn primary', onclick: () => { if (! name.trim()) return GR.toast ('Type a name first.', true); close(); then (name.trim()); } }, yes)),
    ] });
};

/** A sheet over the screen. build (close) returns its contents and is called again when the rig's state changes. */
GR.openSheet = (sheet) =>
{
    GR.sheetStack.push (sheet);
    GR.renderSheets();
};

GR.closeSheet = () => { GR.sheetStack.pop(); GR.renderSheets(); };

GR.renderSheets = () =>
{
    const host = document.getElementById ('sheets');
    if (! host) return;

    const keep = [...host.querySelectorAll ('.sheet-body')].map ((e) => e.scrollTop);
    host.replaceChildren();

    GR.sheetStack.forEach ((sheet, i) =>
    {
        const close = () => { GR.sheetStack.splice (i, 1); GR.renderSheets(); if (sheet.onClose) sheet.onClose(); };
        const body = h ('div', { class: 'sheet-body' }, sheet.build (close));

        host.append (h ('div', { class: 'sheet-back', onclick: (ev) => { if (ev.target === ev.currentTarget) close(); } },
            h ('div', { class: 'sheet' + (sheet.small ? ' small' : ''), role: 'dialog', 'aria-label': sheet.title },
                h ('div', { class: 'sheet-head' },
                    h ('h2', { text: sheet.title }),
                    h ('button', { class: 'btn ghost', onclick: close, 'aria-label': 'Close' }, 'Close')),
                body)));

        if (keep[i]) body.scrollTop = keep[i];
    });
};

// ---------------------------------------------------------------- controls shared by the screens

/** An on/off switch. */
GR.toggle = (on, change, label) =>
    h ('button', { class: 'toggle' + (on ? ' on' : ''), role: 'switch', 'aria-checked': on ? 'true' : 'false', 'aria-label': label || 'On or off',
                   onclick: (ev) => { ev.stopPropagation(); change (! on); } }, h ('span'));

/** A row of choices where one is picked. */
GR.chips = (items, picked, pick, cls) =>
    h ('div', { class: 'chips ' + (cls || '') }, items.map ((it) =>
        h ('button', { class: 'chip' + (it.id === picked ? ' on' : '') + (it.cls ? ' ' + it.cls : ''), onclick: () => pick (it.id) }, it.label)));

GR.select = (options, value, change, label) =>
    h ('select', { 'aria-label': label, onchange: (ev) => change (ev.target.value) },
        options.map ((o) => h ('option', { value: String (o.id), selected: String (o.id) === String (value) }, o.label)));

const paramText = (p, v) =>
{
    if (p.kind === 'choice' && p.choices)   return p.choices[Math.round (v)] || String (v);
    if (p.kind === 'toggle')                return v >= 0.5 ? 'On' : 'Off';
    const span = p.max - p.min;
    const digits = span > 200 ? 0 : span > 20 ? 1 : 2;
    return (p.unit === 'dB' && v > 0 ? '+' : '') + Number (v).toFixed (digits) + (p.unit ? ' ' + p.unit : '');
};

/** One setting of an effect: a slider, a switch or a list, by what kind of setting it is. */
GR.param = (p, value, change) =>
{
    if (p.kind === 'toggle')
        return h ('div', { class: 'param' }, h ('label', { text: p.label }), GR.toggle (value >= 0.5, (on) => change (on ? 1 : 0, true), p.label));

    if (p.kind === 'choice' && p.choices)
        return h ('div', { class: 'param' }, h ('label', { text: p.label }),
            GR.select (p.choices.map ((c, i) => ({ id: i, label: c })), Math.round (value), (v) => change (Number (v), true), p.label));

    // Frequencies and times that span a wide range move evenly by ratio, as on a real knob.
    const log = p.min > 0 && p.max / p.min >= 20;
    const toPos = (v) => log ? Math.log (v / p.min) / Math.log (p.max / p.min) * 1000 : (v - p.min) / (p.max - p.min) * 1000;
    const toVal = (x) => log ? p.min * Math.pow (p.max / p.min, x / 1000) : p.min + (p.max - p.min) * x / 1000;
    const tidy = (v) => { const span = p.max - p.min; const m = span > 200 ? 1 : span > 20 ? 10 : 100; return Math.round (v * m) / m; };

    const out = h ('output', { text: paramText (p, value) });
    const slider = h ('input', { type: 'range', min: 0, max: 1000, step: 1, value: String (Math.round (toPos (GR.clamp (value, p.min, p.max)))), 'aria-label': p.label,
        oninput: (ev) => { const v = tidy (toVal (Number (ev.target.value))); out.textContent = paramText (p, v); change (v, false); },
        onpointerdown: () => { GR.dragging++; },
        onchange: (ev) => { change (tidy (toVal (Number (ev.target.value))), true); } });

    const release = () => { if (GR.dragging > 0) GR.dragging--; };
    slider.addEventListener ('pointerup', release);
    slider.addEventListener ('pointercancel', release);

    return h ('div', { class: 'param' },
        h ('label', {}, p.label, out),
        slider,
        h ('button', { class: 'btn tiny ghost', title: 'Back to where it starts', onclick: () => change (p.def, true) }, 'Reset'));
};

/** One effect block (a pedal in a chain, or a block of a mixer channel): its name, on/off, and its settings when opened. */
GR.block = (key, block, o) =>
{
    const type = (GR.state.types || {})[block.type] || { name: block.type, params: [] };
    const open = !! GR.ui.open[key];

    const head = h ('div', { class: 'block-head', onclick: () => { GR.ui.open[key] = ! open; GR.render(); } },
        h ('span', { class: 'caret', text: open ? '▾' : '▸' }),
        h ('div', { class: 'block-name' }, h ('b', { text: type.name }), type.group ? h ('small', { text: type.group }) : null),
        GR.toggle (block.on, (on) => { block.on = on; o.setOn (on); GR.render(); }, type.name));

    const body = open ? h ('div', { class: 'block-body' },
        type.params.map ((p) => GR.param (p, block.v[p.id] !== undefined ? block.v[p.id] : p.def, (v, done) =>
        {
            block.v[p.id] = v;
            o.setParam (p.id, v);
            if (done && ! GR.dragging) GR.render();
        })),
        block.t ? Object.keys (block.t).filter ((k) => block.t[k]).map ((k) => h ('p', { class: 'hint', text: k + ': ' + block.t[k] })) : null,
        type.params.length === 0 ? h ('p', { class: 'hint', text: 'This one has no settings.' }) : null,
        o.tools ? h ('div', { class: 'row wrap block-tools' }, o.tools) : null) : null;

    return h ('div', { class: 'block' + (block.on ? '' : ' off') }, head, body);
};

/** A tall fader that is dragged, never jumped: a touch anywhere on it moves it from where it is. */
GR.fader = (db, change, o) =>
{
    const opts = o || {};
    let pos = GR.faderPos (db);
    const cap = h ('div', { class: 'fader-cap' });
    const fill = h ('div', { class: 'fader-fill' });
    const meter = h ('div', { class: 'meter' }, h ('i'));
    const value = h ('div', { class: 'fader-val', text: GR.db (db) });
    const track = h ('div', { class: 'fader-track' }, h ('div', { class: 'fader-zero' }), fill, cap);
    const place = () => { cap.style.bottom = (pos * 100) + '%'; fill.style.height = (pos * 100) + '%'; };
    place();

    let from = null;

    track.addEventListener ('pointerdown', (ev) =>
    {
        track.setPointerCapture (ev.pointerId);
        from = { y: ev.clientY, pos, moved: false };
        GR.dragging++;
        track.classList.add ('moving');
    });

    track.addEventListener ('pointermove', (ev) =>
    {
        if (! from) return;
        const hgt = track.clientHeight || 1;
        const dy = from.y - ev.clientY;
        if (! from.moved && Math.abs (dy) < 4) return;
        from.moved = true;
        pos = GR.clamp (from.pos + dy / hgt, 0, 1);
        place();
        const v = Math.round (GR.faderDb (pos) * 10) / 10;
        value.textContent = GR.db (v);
        change (v, false);
    });

    const end = () =>
    {
        if (! from) return;
        const moved = from.moved;
        from = null;
        GR.dragging = Math.max (0, GR.dragging - 1);
        track.classList.remove ('moving');
        if (moved) change (Math.round (GR.faderDb (pos) * 10) / 10, true);
    };

    track.addEventListener ('pointerup', end);
    track.addEventListener ('pointercancel', end);

    const el = h ('div', { class: 'fader' + (opts.cls ? ' ' + opts.cls : '') }, h ('div', { class: 'fader-well' }, meter, track), value);
    el.meter = meter.firstChild;
    return el;
};

/** A level as a slider lying down, for sheets. */
GR.level = (label, db, change) =>
{
    const out = h ('output', { text: GR.db (db) });
    const slider = h ('input', { type: 'range', min: 0, max: 1000, value: String (Math.round (GR.faderPos (db) * 1000)), 'aria-label': label,
        oninput: (ev) => { const v = Math.round (GR.faderDb (Number (ev.target.value) / 1000) * 10) / 10; out.textContent = GR.db (v); change (v); },
        onpointerdown: () => { GR.dragging++; } });
    const release = () => { if (GR.dragging > 0) GR.dragging--; };
    slider.addEventListener ('pointerup', release);
    slider.addEventListener ('pointercancel', release);
    return h ('div', { class: 'param' }, h ('label', {}, label, out), slider);
};

// ---------------------------------------------------------------- the frame and the loop

const tabs = [
    { id: 'looper',  label: 'Looper' },
    { id: 'songs',   label: 'Songs' },
    { id: 'build',   label: 'Build' },
    { id: 'effects', label: 'Effects' },
    { id: 'mixer',   label: 'Mixer' },
    { id: 'tools',   label: 'Tools' },
    { id: 'rig',     label: 'Rig' },
];

let liveHead = null;

function frame()
{
    const app = document.getElementById ('app');
    const beat = h ('div', { class: 'beat', 'aria-hidden': 'true' });
    const tempo = h ('span', { class: 'tempo' });
    const song = h ('button', { class: 'song-name', onclick: () => GR.go ('songs') });
    const dot = h ('span', { class: 'dot' });

    app.replaceChildren (
        h ('header', { class: 'top' },
            h ('div', { class: 'top-left' }, dot, song),
            h ('div', { class: 'top-right' }, tempo, beat, GR.demo ? h ('button', { class: 'tag demo', onclick: () => GR.go ('rig') }, 'DEMO') : null)),
        h ('div', { id: 'banner', class: 'banner', hidden: true }),
        h ('main', { id: 'main' }),
        h ('nav', { class: 'tabs', 'aria-label': 'Screens' }, tabs.map ((t) =>
            h ('button', { class: 'tab', 'data-tab': t.id, onclick: () => GR.go (t.id) }, h ('span', { class: 'tab-ico ico-' + t.id }), h ('span', { text: t.label })))),
        h ('div', { id: 'sheets' }),
        h ('div', { id: 'toast', class: 'toast', role: 'status' }));

    liveHead = { beat, tempo, song, dot, beats: 0 };
}

GR.go = (tab) =>
{
    GR.ui.tab = tab;
    GR.remember ('tab', tab);
    const main = document.getElementById ('main');
    if (main) main.scrollTop = 0;
    GR.render();
};

/** Builds the screen now showing again. Put off while a finger is on a slider or the keyboard is in a box. */
let renderOwed = false;

GR.render = (force) =>
{
    const main = document.getElementById ('main');
    if (! main || ! GR.state) return;

    const typing = document.activeElement && main.contains (document.activeElement)
                   && (document.activeElement.tagName === 'INPUT' && document.activeElement.type !== 'range' && document.activeElement.type !== 'checkbox');

    if (! force && (GR.dragging > 0 || typing)) { renderOwed = true; return; }
    renderOwed = false;

    document.querySelectorAll ('.tab').forEach ((b) => b.classList.toggle ('on', b.dataset.tab === GR.ui.tab));
    document.getElementById ('app').dataset.screen = GR.ui.tab;

    const top = main.scrollTop;
    const sides = [...main.querySelectorAll ('[data-keep-scroll]')].map ((e) => [e.dataset.keepScroll, e.scrollLeft]);
    GR.onLive = null;
    const root = h ('div', { class: 'screen screen-' + GR.ui.tab });
    root.append = (...kids) => kids.flat (Infinity).forEach ((k) => { if (k !== null && k !== undefined && k !== false) Element.prototype.append.call (root, k); });    // a screen may hand over nothing, or a list
    (GR.screens[GR.ui.tab] || GR.screens.looper) (root);
    main.replaceChildren (root);
    main.scrollTop = top;
    sides.forEach (([k, x]) => { const e = main.querySelector ('[data-keep-scroll="' + k + '"]'); if (e) e.scrollLeft = x; });

    const s = GR.state;
    liveHead.song.textContent = (s.songs[s.current] || { name: 'No song' }).name;

    GR.renderSheets();
    if (GR.live) liveTick();
};

document.addEventListener ('focusout', () => { setTimeout (() => { if (renderOwed) GR.render(); }, 50); });
window.addEventListener ('pointerup', () => { setTimeout (() => { if (renderOwed && GR.dragging === 0) GR.render(); }, 80); });

function liveTick()
{
    const l = GR.live, lo = l.looper, s = GR.state;
    const beats = (s.layout && s.layout.beats) || 4;

    if (liveHead.beats !== beats)
    {
        liveHead.beat.replaceChildren (...Array.from ({ length: beats }, () => h ('i')));
        liveHead.beats = beats;
    }

    const known = lo.bpm > 0;
    [...liveHead.beat.children].forEach ((d, i) => { d.className = known && lo.running && i === lo.beat ? (i === 0 ? 'one' : 'on') : ''; });
    liveHead.tempo.textContent = known ? lo.bpm.toFixed (1) + ' BPM' : 'no tempo yet';
    liveHead.dot.className = 'dot ' + (GR.lost ? 'bad' : l.open || GR.demo ? 'ok' : 'warn');
    liveHead.dot.title = GR.lost ? 'No answer from the rig' : l.open ? 'Connected, audio running' : 'Connected, but the rig has no audio device open';

    if (GR.strobe) GR.strobe.follow (l);
    if (GR.onLive) GR.onLive (l);
}

let failures = 0;

async function loop()
{
    try
    {
        const changed = await backend.tick();

        if (failures > 0 || GR.lost) { failures = 0; GR.lost = false; banner(); }
        if (changed) GR.render();
        else if (GR.state) liveTick();
    }
    catch (e)
    {
        if (failures === 0) console.warn ('GigRig remote:', e);
        if (++failures >= 6 && ! GR.lost) { GR.lost = true; banner(); if (GR.live) liveTick(); }
    }

    setTimeout (loop, document.hidden ? 1000 : GR.demo ? 80 : 120);
}

function banner()
{
    const b = document.getElementById ('banner');
    if (! b) return;
    b.hidden = ! GR.lost;
    b.textContent = GR.lost ? 'No answer from the rig. This screen is showing what it last heard. Check that this device is on the rig\'s Wi-Fi; it will carry on by itself.' : '';
}

GR.start = () =>
{
    GR.demo = !! (window.GIGRIG_DEMO && (window.GIGRIG_DEMO_ONLY || location.hash === '#demo'));

    if (GR.demo)
    {
        GR.demoRig = GR.makeDemoRig (window.GIGRIG_DEMO);
        backend = demoBackend;
    }

    GR.ui.tab = GR.recall ('tab', 'looper');
    if (! tabs.some ((t) => t.id === GR.ui.tab)) GR.ui.tab = 'looper';
    frame();
    document.getElementById ('main').append (h ('p', { class: 'hint pad', text: 'Asking the rig…' }));
    loop();
};

window.addEventListener ('hashchange', () => { if (window.GIGRIG_DEMO && ! window.GIGRIG_DEMO_ONLY) location.reload(); });
