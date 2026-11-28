/* The remote screen: Looper, Songs, Effects, Mixer and Rig (docs/remote.md). The builder is builder.js. */
'use strict';

(() =>
{
const h = GR.h;

// ---------------------------------------------------------------- what a switch does, in words

const shortWords = {
    'nothing': '',
    'record play': 'Rec / Play', 'record play, hold undo': 'Rec / Play', 'record play, hold undo, double stop': 'Rec / Play',
    'record play, hold undo or clear, double stop': 'Rec / Play',
    'play stop': 'Play / Stop', 'play stop, hold undo': 'Play / Stop', 'play stop, hold undo or clear': 'Play / Stop',
    'stop': 'Stop', 'stop, hold clear': 'Stop', 'stop, double clear': 'Stop', 'clear': 'Clear', 'undo redo': 'Undo',
    'all start stop': 'All start / stop', 'all start stop, hold clear all': 'All start / stop', 'all start stop, double clear all': 'All start / stop',
    'clear all': 'Clear all', 'bank': 'Bank',
};

/** An RC-600 function number as { track (0-5, -1 = current, null = none), fn (an entry of state.functions), short, long }. */
GR.describe = (code) =>
{
    const fns = GR.state.functions;
    let index = -2, track = null;

    if (code >= 1 && code <= 182)
    {
        const current = code >= 157;
        index = current ? code - 157 : (code - 1) % 26;
        track = current ? -1 : Math.floor ((code - 1) / 26);
        if (index === 8) index = 7;
        if (index === 10) index = 9;
    }
    else if (code >= 217 && code <= 222)   { index = 26; track = code - 217; }
    else if (code === 194)                 code = 193;

    const fn = code <= 0 ? fns[0] : fns.find ((f) => index >= 0 ? f.index === index : (f.index < 0 && f.code === code && f.words !== 'nothing'));

    if (! fn)
        return { track: null, fn: null, short: 'RC ' + code, long: 'RC-600 function ' + code + ' (this looper does not copy it)' };

    const who = track === null ? '' : track < 0 ? 'Current track' : 'Track ' + (track + 1);
    return { track, fn, short: shortWords[fn.words] !== undefined ? shortWords[fn.words] : fn.label, who,
             long: (who ? who + ': ' : '') + fn.label };
};

/** The number for a choice on a track, the same sum as layout::codeFor in the rig. */
GR.codeFor = (fn, track) =>
{
    if (fn.index < 0)       return fn.code;
    if (fn.index === 26)    return track >= 0 ? 217 + track : 162;
    return track >= 0 ? track * 26 + fn.index + 1 : 157 + fn.index;
};

GR.sourceNames = (inputs) =>
{
    const names = GR.state.sources.filter ((s) => inputs & s.bit).map ((s) => s.name);
    return names.length ? names.join (' + ') : 'nothing';
};

/** How many loops side by side a track keeps: one for each socket it listens to. */
GR.lanesFor = (inputs) => new Set (GR.state.sources.filter ((s) => inputs & s.bit).map ((s) => s.socket)).size;

const stateWords = ['Empty', 'Waiting', 'Recording', 'Finishing', 'Playing', 'Overdubbing', 'Stopping', 'Stopped'];
const stateClass = ['empty', 'armed', 'rec', 'rec', 'play', 'dub', 'stopping', 'stopped'];

// ---------------------------------------------------------------- Looper

GR.screens.looper = (root) =>
{
    const s = GR.state, lay = s.layout;
    const tiles = [], pads = [];

    const trackGrid = h ('div', { class: 'tracks' }, lay.tracks.map ((t, i) =>
    {
        const bar = h ('i');
        const stateEl = h ('span', { class: 'tk-state' });
        const len = h ('span', { class: 'tk-len' });
        const badge = h ('span', { class: 'tk-badge' });
        const el = h ('div', { class: 'track' },
            h ('div', { class: 'tk-top' }, h ('b', { text: String (i + 1) }), h ('span', { class: 'tk-src', text: GR.sourceNames (t.inputs) })),
            stateEl,
            h ('div', { class: 'tk-bar' }, bar),
            h ('div', { class: 'tk-foot' }, len, badge));
        tiles.push ({ el, bar, stateEl, len, badge });
        return el;
    }));

    const bankLabel = h ('span', { class: 'bank-now' });

    const pad = (sw) =>
    {
        const fnEl = h ('span', { class: 'pad-fn' });
        const whoEl = h ('span', { class: 'pad-who' });
        const use = s.switches[sw] && s.switches[sw].effects;
        const el = h ('button', { class: 'pad' + (GR.switchHeld (sw) ? ' down' : ''), 'data-sw': sw,
                                  onpointerdown: (ev) => GR.switchDown (ev, sw), oncontextmenu: (ev) => ev.preventDefault() },
            h ('span', { class: 'pad-n', text: sw < 9 ? String (sw + 1) : sw === 9 ? 'CTL3' : 'CTL4' }),
            whoEl, fnEl,
            use ? h ('span', { class: 'pad-fx', text: use }) : null);
        pads[sw] = { el, fnEl, whoEl, was: null };
        return el;
    };

    root.append (
        s.rig.warnings.length && ! GR.demo ? h ('div', { class: 'note warn', text: s.rig.warnings[0] }) : null,
        s.layoutTried ? h ('div', { class: 'note', text: 'Layout on trial, not saved (Build screen).' }) : null,
        h ('div', { class: 'looper-grid' },
            h ('section', { class: 'looper-tracks' }, trackGrid),
            h ('section', { class: 'looper-pads' },
                h ('div', { class: 'pad-head' },
                    h ('span', {}, 'Switches, ', bankLabel),
                    h ('button', { class: 'btn tiny danger-ghost', onclick: () => GR.ask ('Clear every loop on every track? This cannot be undone.', 'Clear all loops',
                        () => GR.send ({ op: 'looper.clearAll' }), { danger: true }) }, 'Clear all loops')),
                h ('div', { class: 'pads' }, [0, 1, 2, 3, 4, 5, 6, 7, 8].map (pad)),
                h ('div', { class: 'pads ctl' }, pad (9), pad (10)))));

    const linksFor = (bank, sw) =>
    {
        const src = sw >= 9 ? 42 + (sw - 9) : 13 + bank * 9 + sw;
        return lay.links.filter ((l) => l.src === src && GR.linkKnown (l.tgt));
    };

    GR.onLive = (l) =>
    {
        const lo = l.looper;
        bankLabel.textContent = 'bank ' + (lo.bank + 1);

        lo.tracks.forEach ((t, i) =>
        {
            const tile = tiles[i];
            if (! tile) return;
            tile.el.className = 'track ' + stateClass[t.s] + (i === lo.cur ? ' cur' : '') + (t.u ? ' undone' : '');
            tile.stateEl.textContent = stateWords[t.s];
            const whole = t.s === 2 || t.s === 3 || t.s === 1;
            tile.bar.style.width = (whole ? 100 : t.a ? GR.clamp (t.pos, 0, 1) * 100 : 0) + '%';
            tile.len.textContent = t.s === 2 || t.s === 3 ? t.pos.toFixed (1) + ' s' : ! t.a ? '' : t.bars > 0 ? (Math.round (t.bars * 100) / 100) + (t.bars === 1 ? ' bar' : ' bars') : t.sec.toFixed (1) + ' s';
            tile.badge.textContent = t.u ? 'UNDONE' : t.d ? 'layers' : t.l > 1 ? t.l + ' lanes' : '';
        });

        pads.forEach ((p, sw) =>
        {
            const code = sw < 9 ? lay.banks[lo.bank][sw] : 0;
            const key = lo.bank + ':' + code;
            const d = GR.describe (code);
            const extra = linksFor (lo.bank, sw);

            if (p.was !== key)
            {
                p.was = key;
                p.whoEl.textContent = d.track === null ? '' : d.track < 0 ? 'Current' : 'Track ' + (d.track + 1);
                p.fnEl.textContent = d.short || (extra.length ? GR.linkTarget (extra[0].tgt).replace (/^.*: /, '').replace ('Record, play, overdub', 'Rec / Play').replace (' or ', ' / ') : '');
                if (! d.short && extra.length) p.whoEl.textContent = GR.linkTarget (extra[0].tgt).includes (':') ? GR.linkTarget (extra[0].tgt).replace (/:.*$/, '') : '';
                if (extra.length && d.short) p.fnEl.textContent += ' +' + extra.length;
            }

            const t = d.track === null ? null : lo.tracks[d.track < 0 ? lo.cur : d.track];
            const want = 'pad' + (t ? ' ' + stateClass[t.s] : '') + (p.el.classList.contains ('down') ? ' down' : '') + (! d.short && ! extra.length && ! (s.switches[sw] || {}).effects ? ' idle' : '');
            if (p.el.className !== want) p.el.className = want;
        });
    };
};

// ---------------------------------------------------------------- Songs

GR.screens.songs = (root) =>
{
    const s = GR.state, cur = s.songs[s.current];
    const filter = GR.ui.songFilter.toLowerCase();

    const load = (i) =>
    {
        const busy = GR.live && GR.live.looper.tracks.some ((t) => t.a || t.s !== 0);
        const go = () => GR.send ({ op: 'song.load', index: i });
        if (busy) GR.ask ('Load "' + s.songs[i].name + '"? The loops you have now are cleared.', 'Load it', go, { danger: true });
        else go();
    };

    const newSong = () =>
    {
        let name = '', program = 0;
        GR.openSheet ({ small: true, title: 'New song', build: (close) => [
            h ('label', { class: 'field' }, 'Name', h ('input', { type: 'text', value: name, oninput: (ev) => { name = ev.target.value; } })),
            h ('label', { class: 'field' }, 'PC (0 = none)', h ('input', { type: 'number', min: 0, max: 128, value: String (program), oninput: (ev) => { program = Number (ev.target.value) || 0; } })),
            h ('div', { class: 'row end' }, h ('button', { class: 'btn', onclick: close }, 'Cancel'),
                h ('button', { class: 'btn primary', onclick: async () => { if (! name.trim()) return GR.toast ('Name it first.', true); const r = await GR.send ({ op: 'song.new', name: name.trim(), program }); if (r.ok) close(); } }, 'Add')),
        ] });
    };

    root.append (
        h ('section', { class: 'card now' },
            h ('div', { class: 'card-title' }, h ('small', { text: 'Loaded' }), h ('h2', { text: cur ? cur.name : 'No song' })),
            h ('p', { class: 'hint', text: s.songStatus }),
            s.songNote ? h ('p', { class: 'hint', text: s.songNote }) : null,
            s.songProblems.length ? h ('div', { class: 'note warn' }, h ('b', { text: 'Unusable lines in the song file:' }), s.songProblems.map ((p) => h ('div', { text: p }))) : null,
            h ('div', { class: 'row wrap' },
                h ('button', { class: 'btn', onclick: () => GR.ask ('Save the current sounds (all chains) to this song?', 'Save', () => GR.send ({ op: 'song.keep' })) }, 'Save sounds to song'),
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'song.again' }) }, 'Reload sounds'),
                h ('button', { class: 'btn', onclick: () => GR.go ('build') }, 'Looper layout'))),
        h ('div', { class: 'row' },
            h ('input', { type: 'search', class: 'grow', placeholder: 'Search', value: GR.ui.songFilter, 'aria-label': 'Find a song',
                          oninput: (ev) => { GR.ui.songFilter = ev.target.value; list.replaceChildren (...rows()); } }),
            h ('button', { class: 'btn', onclick: newSong }, 'New song')));

    const rows = () =>
    {
        const f = GR.ui.songFilter.toLowerCase();
        const out = s.songs.map ((song, i) => ({ song, i })).filter ((x) => ! f || x.song.name.toLowerCase().includes (f) || String (x.song.program) === f)
            .map (({ song, i }) => h ('button', { class: 'song' + (i === s.current ? ' on' : ''), onclick: () => load (i) },
                h ('span', { class: 'song-pc', text: song.program > 0 ? String (song.program) : '' }),
                h ('span', { class: 'song-title', text: song.name }),
                h ('span', { class: 'song-tags' },
                    song.sounds ? h ('span', { class: 'tag', text: 'sounds' }) : null,
                    song.ownLayout ? h ('span', { class: 'tag', text: 'own layout' }) : null,
                    song.builtIn ? h ('span', { class: 'tag dim', text: 'built in' }) : null)));
        return out.length ? out : [h ('p', { class: 'hint pad', text: 'No match.' })];
    };

    const list = h ('div', { class: 'songs' }, rows());
    void filter;
    root.append (list);
};

// ---------------------------------------------------------------- pages inside a screen, and small pictures
//
// Tom's rule (28 Nov 2026): many pages with few, big controls, never a crowded screen that
// scrolls. So a screen may hold pages: the Mixer's faders lead to one page per channel, and the
// Effects pedalboard leads to one page per pedal. Every such page starts with the way back.

/** The top of a page that was entered from another one: the way back, its name, and what sits on the right. */
const pageHead = (back, backTo, title, sub, right) =>
    h ('div', { class: 'page-head' },
        h ('button', { class: 'btn back', onclick: back }, '‹ ' + backTo),
        h ('div', { class: 'page-title' }, h ('b', { text: title }), sub ? h ('small', { text: sub }) : null),
        right ? h ('div', { class: 'row' }, right) : null);

/** Previous and next, for stepping along a row of channels or a chain of pedals without going back. */
const stepButtons = (i, count, go, what) => [
    h ('button', { class: 'btn step', disabled: i <= 0, 'aria-label': 'Previous ' + what, onclick: () => go (i - 1) }, '◂'),
    h ('button', { class: 'btn step', disabled: i >= count - 1, 'aria-label': 'Next ' + what, onclick: () => go (i + 1) }, '▸'),
];

// Line drawings for the signal chain strip, 24 by 24, drawn with the text colour.
const icons = {
    input:    '<circle cx="8" cy="12" r="5"/><path d="M13 12h9M18 8l4 4-4 4"/>',
    gate:     '<path d="M2 18h6V6h8v12h6"/>',
    eq:       '<path d="M2 14c4 0 4-8 8-8s4 10 8 10 3-3 4-3"/>',
    comp:     '<path d="M3 21l9-9 9-4"/><path d="M3 21h18" opacity=".4"/>',
    limiter:  '<path d="M3 21l9-12h9"/><path d="M3 21h18" opacity=".4"/>',
    notch:    '<path d="M2 8h7l3 10 3-10h7"/>',
    enhancer: '<path d="M12 2v6M12 16v6M2 12h6M16 12h6M5 5l4 4M15 15l4 4M19 5l-4 4M9 15l-4 4"/>',
    warmth:   '<path d="M8 21V8a4 4 0 0 1 8 0v13M6 21h12M10 12h4M10 16h4"/>',
    tape:     '<circle cx="7" cy="12" r="4"/><circle cx="17" cy="12" r="4"/><path d="M7 16h10"/>',
    delay:    '<path d="M4 20V4M10 20V8M15 20v-8M19 20v-5"/>',
    reverb:   '<path d="M5 8a6 6 0 0 1 0 8M10 5a10 10 0 0 1 0 14M15 2a14 14 0 0 1 0 20"/>',
    sends:    '<path d="M3 12h8M11 12l8-7M11 12h10M11 12l8 7"/>',
    level:    '<path d="M12 3v18"/><rect x="7" y="8" width="10" height="5" rx="1"/>',
    block:    '<rect x="4" y="6" width="16" height="12" rx="2"/>',
};

const typeIcon = { gate: 'gate', enhancer: 'enhancer', warmth: 'warmth', mixeq: 'eq', peq: 'eq', eq: 'eq', mixcomp: 'comp', comp: 'comp', la2a: 'comp', buscomp: 'comp',
                   limiter: 'limiter', notches: 'notch', tape: 'tape', linedelay: 'delay', digitaldelay: 'delay', tapedelay: 'delay', analogdelay: 'delay',
                   plate: 'reverb', room: 'reverb', spring: 'reverb' };

const icon = (name) =>
{
    const s = h ('span', { class: 'ico', 'aria-hidden': 'true' });
    s.innerHTML = '<svg viewBox="0 0 24 24">' + (icons[name] || icons.block) + '</svg>';
    return s;
};

const typeOf = (id) => (GR.state.types || {})[id] || { name: id, params: [] };

// Tom's chain colours (28 Nov 2026), the same ones planned for the foot switch screens: red for
// the vocal, yellow for the acoustic, blue for the electric, orange for the bass. A mixer channel
// takes the colour of the effects chain on its input socket, so one colour means one sound
// everywhere. The stomp is purple and the duo partner green (Tom, 28 Nov 2026).
const chainColours = { vocal: '#ff5a52', acoustic: '#ffd23f', electric: '#4c8dff', bass: '#ff9330', stomp: '#c08cff', kick: '#c08cff' };

const colourOfName = (name) =>
{
    const n = String (name || '').toLowerCase();
    if (n.startsWith ('duo')) return '#34d27b';
    return chainColours[Object.keys (chainColours).find ((k) => n.includes (k))] || '';
};

const channelColour = (ch) =>
{
    if (String (ch.name).toLowerCase().startsWith ('duo')) return colourOfName (ch.name);
    const chain = ((GR.state.effects || {}).chains || []).find ((x) => x.socket === ch.socket);
    return (chain && colourOfName (chain.name)) || colourOfName (ch.name);
};

/** Gives a part of the screen its sound's colour (the styles use it as --chain). */
const tint = (el, colour) => { if (colour) { el.classList.add ('hue'); el.style.setProperty ('--chain', colour); } return el; };

/** An effect's name cut down to fit a small picture: "1176 style compressor" becomes "1176 comp". */
const shortName = (name) =>
{
    const s = name.replace (/\s*\(.*?\)/g, '').replace (/ style/i, '').replace (/^Mixer /, '').replace (/compressor/i, 'comp').replace (/ (pedal|copy)$/i, '').trim();
    return s ? s[0].toUpperCase() + s.slice (1) : name;
};

/** A page made of a signal chain: the chain runs down the left as small pictures, and the one that is picked fills the rest. */
const chainPage = (items, picked, pick) =>
    h ('div', { class: 'chain-page' },
        h ('nav', { class: 'rail', 'aria-label': 'The signal chain, top to bottom' }, items.map ((it, i) =>
            h ('button', { class: 'rail-item' + (i === picked ? ' on' : '') + (it.on === false ? ' off' : ''), 'aria-current': i === picked ? 'true' : null, onclick: () => pick (i) },
                icon (it.ico), h ('span', { text: it.label }), it.on === undefined ? null : h ('i', { class: 'rail-led' })))),
        h ('div', { class: 'pane' }, h ('h3', { class: 'pane-title', text: items[picked].title || items[picked].label }), items[picked].build()));

/** One block of a chain as a chain page item: its controls are always open, with nothing to unfold. */
const blockItem = (key, block, o, after) =>
{
    const type = typeOf (block.type);
    return { ico: typeIcon[block.type] || 'block', label: shortName (type.name), title: type.name, on: block.on,
             build: () =>
             {
                 // The mixer's EQ is a curve with a point to drag for each band (eq-curve.js); every other block is its sliders.
                 if (block.type === 'mixeq' && GR.eqCurve) return [GR.eqCurve (key, block, o, o.rta), after ? after() : null];
                 GR.ui.open[key] = true;
                 return [h ('div', { class: 'blocks solo' }, GR.block (key, block, o)), after ? after() : null];
             } };
};

// ---------------------------------------------------------------- Effects: the pedalboard, and a page for each pedal

// How each effect is drawn on the pedalboard. These are simple stand-ins (a shape and a colour),
// not likenesses: the finished look of each one comes last (docs/remote.md, "The gear look sample").
// k: stomp (a pedal), rack (a studio unit), amp, rocker (a pedal the foot tilts), sliders (a graphic EQ).
const pedalLook = {
    comp: { k: 'rack', c: '#17181b' }, la2a: { k: 'rack', c: '#b9bcc0' }, peq: { k: 'rack', c: '#2b3440' }, plate: { k: 'rack', c: '#8f979f' }, room: { k: 'rack', c: '#3a4652' },
    harmony: { k: 'rack', c: '#4a3a6e' }, ir: { k: 'rack', c: '#4f4436' }, body: { k: 'stomp', c: '#8a5a2b' },
    eq: { k: 'sliders', c: '#c9d2d8' }, gate: { k: 'stomp', c: '#6b7480' },
    bd: { k: 'stomp', c: '#2f6fd0' }, klon: { k: 'stomp', c: '#b89b4a' }, ts: { k: 'stomp', c: '#3fa35a' }, marshall: { k: 'stomp', c: '#26262a' },
    fuzz: { k: 'stomp', c: '#b23a2e' }, boost: { k: 'stomp', c: '#e6e6e2' }, bass9: { k: 'stomp', c: '#1c1c1f' },
    amp: { k: 'amp', c: '#20201f' },
    wah: { k: 'rocker', c: '#1b1b1d' }, whammy: { k: 'rocker', c: '#b3202a' }, volume: { k: 'rocker', c: '#5a6068' },
    flanger: { k: 'stomp', c: '#7d4bc2' }, chorus: { k: 'stomp', c: '#5db6e8' }, vibrato: { k: 'stomp', c: '#2aa39a' }, univibe: { k: 'stomp', c: '#3b3f46' },
    tremolo: { k: 'stomp', c: '#d98a2b' }, analogdelay: { k: 'stomp', c: '#2d6b4f' }, tapedelay: { k: 'stomp', c: '#d8cfb8' }, digitaldelay: { k: 'stomp', c: '#dfe6ec' },
    spring: { k: 'stomp', c: '#a8875a' },
};

const lightColour = (hex) =>
{
    const n = parseInt (hex.slice (1), 16);
    return ((n >> 16) * 299 + ((n >> 8) & 255) * 587 + (n & 255) * 114) / 1000 > 150;
};

/** A simple drawing of one effect: a pedal, a rack unit, an amp, a rocker or a graphic EQ, with a lamp that is lit when it is on. */
const pedalPicture = (slot) =>
{
    const type = typeOf (slot.type), look = pedalLook[slot.type] || { k: 'stomp', c: '#56616d' };
    const knobs = GR.clamp (type.params.filter ((p) => p.kind === 'knob').length, 1, look.k === 'sliders' ? 7 : look.k === 'rack' ? 4 : 3);
    const dots = (cls, n) => h ('div', { class: cls }, Array.from ({ length: n }, () => h ('i')));
    const name = h ('div', { class: 'pedal-name', text: shortName (type.name) });
    const lamp = h ('i', { class: 'pedal-lamp' });
    let parts;

    if (look.k === 'rack')          parts = [h ('div', { class: 'pedal-face' }, dots ('pedal-knobs', knobs), h ('div', { class: 'pedal-window' }), lamp), name];
    else if (look.k === 'amp')      parts = [h ('div', { class: 'pedal-face' }, dots ('pedal-knobs', 4), lamp), h ('div', { class: 'pedal-cloth' }), name];
    else if (look.k === 'rocker')   parts = [h ('div', { class: 'pedal-tread' }), name, lamp];
    else if (look.k === 'sliders')  parts = [dots ('pedal-sliders', 7), lamp, name, h ('div', { class: 'pedal-switch' })];
    else                            parts = [dots ('pedal-knobs', knobs), lamp, name, h ('div', { class: 'pedal-switch' })];

    const el = h ('div', { class: 'pedal k-' + look.k + (slot.on ? ' on' : '') + (lightColour (look.c) ? ' light' : '') }, parts);
    el.style.setProperty ('--pc', look.c);
    el.querySelectorAll ('.pedal-knobs i').forEach ((e, i) => e.style.setProperty ('--n', String (i)));
    el.querySelectorAll ('.pedal-sliders i').forEach ((e, i) => e.style.setProperty ('--t', [55, 35, 20, 30, 50, 40, 60][i % 7] + '%'));
    return el;
};

/** The list "Add an effect" offers: every effect the rig has, under its kind. */
function addEffectSheet (c)
{
    GR.openSheet ({ title: 'Add effect', build: (close) =>
    {
        const s = GR.state, chain = s.effects.chains[c];
        if (! chain) return [];
        const full = chain.slots.length >= s.fxMaxSlots;
        const groups = [];
        s.fxCatalogue.forEach ((e) => { let g = groups.find ((x) => x.name === e.group); if (! g) groups.push (g = { name: e.group, list: [] }); g.list.push (e); });

        return [
            full ? h ('p', { class: 'hint', text: 'Chain full (' + s.fxMaxSlots + '). Remove one first.' }) : null,
            groups.map ((g) => [
                h ('h3', { text: g.name }),
                h ('div', { class: 'row wrap' }, g.list.map ((e) => h ('button', { class: 'btn', disabled: full, onclick: async () =>
                {
                    const place = chain.slots.length;
                    const r = await GR.send ({ op: 'fx.add', chain: c, type: e.id }, true);
                    if (r.ok) { GR.ui.fxPage = { c, i: place }; GR.render(); GR.toast ('Added ' + e.name + ' to ' + chain.name + '.'); close(); }
                } }, e.name))),
            ]),
        ];
    } });
}

/** Loading and saving a chain's presets, on a sheet of its own so the pedalboard stays clear. */
function chainPresetSheet (c)
{
    GR.openSheet ({ title: 'Presets', build: (close) =>
    {
        const fx = GR.state.effects, chain = fx.chains[c];
        if (! chain) return [];
        let preset = GR.ui.fxPreset && fx.presets.includes (GR.ui.fxPreset) ? GR.ui.fxPreset : (fx.presets[0] || '');

        const savePreset = () => GR.askName ('Save preset', 'Whole ' + chain.name + ' chain. An existing name is overwritten.',
            chain.recipe.length === 1 ? chain.recipe[0] : '', 'Save it', (name) => GR.send ({ op: 'fx.save', chain: c, name }));

        const updatePresets = () => GR.ask ('Save the changes made to ' + chain.name + ' back into the ' + (chain.recipe.length === 1 ? 'preset' : 'presets') + ' it was made from ('
            + chain.recipe.join (', ') + ')? Every song that uses ' + (chain.recipe.length === 1 ? 'that preset' : 'those presets') + ' gets the change.', 'Update', () => GR.send ({ op: 'fx.update', chain: c }));

        return [
            h ('p', { class: 'hint', text: chain.recipe.length ? 'From: ' + chain.recipe.join (' + ') : 'No preset loaded.' }),
            fx.presets.length ? h ('div', { class: 'row' },
                GR.select (fx.presets.map ((p) => ({ id: p, label: p })), preset, (v) => { preset = v; GR.ui.fxPreset = v; }, 'Preset'),
                h ('button', { class: 'btn', onclick: () => GR.ask ('Load the preset "' + preset + '" onto ' + chain.name + '? It replaces what the chain has now.', 'Load it',
                    () => { close(); GR.send ({ op: 'fx.preset', chain: c, name: preset }); }) }, 'Load')) : h ('p', { class: 'hint', text: 'No presets.' }),
            h ('div', { class: 'row wrap' },
                h ('button', { class: 'btn', onclick: savePreset }, 'Save as'),
                chain.recipe.length ? h ('button', { class: 'btn', onclick: updatePresets }, 'Update') : null),
        ];
    } });
}

/** One pedal's own page: its picture, its controls, and its place in the chain. */
function pedalPage (root, c, i)
{
    const chain = GR.state.effects.chains[c], slot = chain.slots[i], type = typeOf (slot.type);
    const key = 'fx' + c + '.' + i + slot.type;
    const go = (to) => { GR.ui.fxPage = { c, i: to }; GR.render(); };
    const back = () => { GR.ui.fxPage = null; GR.render(); };

    const move = async (to) => { const r = await GR.send ({ op: 'fx.move', chain: c, slot: i, to }, true); if (r.ok) go (to); };
    const remove = () => GR.ask ('Take ' + type.name + ' out of ' + chain.name + '? Its settings go with it.', 'Remove it',
        () => { GR.ui.fxPage = null; GR.send ({ op: 'fx.remove', chain: c, slot: i }, true); }, { danger: true });

    GR.ui.open[key] = true;
    tint (root, colourOfName (chain.name));

    root.append (
        pageHead (back, 'Pedalboard', shortName (type.name), chain.name + ', ' + (i + 1) + ' of ' + chain.slots.length, stepButtons (i, chain.slots.length, go, 'effect')),
        h ('div', { class: 'pedal-page' },
            h ('div', { class: 'pedal-hero' }, pedalPicture (slot)),
            h ('div', { class: 'blocks solo' }, GR.block (key, slot, {
                setOn: (on) => GR.send ({ op: 'fx.slotOn', chain: c, slot: i, on }, true),
                setParam: (id, value) => GR.sendValue ('fx' + c + '.' + i + id, { op: 'fx.param', chain: c, slot: i, id, value }),
                tools: [
                    h ('button', { class: 'btn tiny', disabled: i === 0, onclick: () => move (i - 1) }, '◂ Move'),
                    h ('button', { class: 'btn tiny', disabled: i === chain.slots.length - 1, onclick: () => move (i + 1) }, 'Move ▸'),
                    h ('button', { class: 'btn tiny danger-ghost', onclick: remove }, 'Remove'),
                ],
            }))));
}

// The pedalboard's rows and the number of faders in a bank follow the width of the screen.
window.addEventListener ('resize', () => { if ((GR.ui.tab === 'effects' && ! GR.ui.fxPage) || (GR.ui.tab === 'mixer' && ! GR.ui.mixPage)) GR.render(); });

GR.screens.effects = (root) =>
{
    const fx = GR.state.effects;
    const c = GR.clamp (GR.ui.fxChain, 0, fx.chains.length - 1);
    const chain = fx.chains[c];
    const page = GR.ui.fxPage;

    if (page && page.c === c && chain.slots[page.i]) return pedalPage (root, c, page.i);
    GR.ui.fxPage = null;

    // The pedalboard: the sound comes in at the top left and snakes along each row and back along the next.
    const main = document.getElementById ('main');
    const coarse = window.matchMedia && window.matchMedia ('(pointer: coarse)').matches;
    const width = Math.min (1100, (main ? main.clientWidth : 360)) - 32 - (coarse ? 40 : 0);
    const gap = 14;
    const cols = GR.clamp (Math.floor ((width + gap) / ((width < 520 ? 84 : 118) + gap)), 2, 8);
    const items = [{ jack: 'IN', sub: 'socket ' + chain.socket }].concat (chain.slots.map ((slot, i) => ({ slot, i })), [{ jack: 'OUT', sub: 'mixer' }]);

    const cells = items.map ((it, n) =>
    {
        const row = Math.floor (n / cols), pos = n % cols, forward = row % 2 === 0;
        const into = n === 0 ? '' : pos === 0 ? 't' : forward ? 'l' : 'r';
        const out = n === items.length - 1 ? '' : pos === cols - 1 ? 'b' : forward ? 'r' : 'l';
        const type = it.slot ? typeOf (it.slot.type) : null;

        const cell = h ('div', { class: 'board-cell' },
            into ? h ('i', { class: 'cable ' + into }) : null,
            out ? h ('i', { class: 'cable ' + out }) : null,
            it.jack ? h ('div', { class: 'jack' }, h ('b', { text: it.jack }), h ('small', { text: it.sub }))
                    : h ('button', { class: 'pedal-btn', 'aria-label': 'Open ' + type.name + (it.slot.on ? ', on' : ', off'), onclick: () => { GR.ui.fxPage = { c, i: it.i }; GR.render(); } }, pedalPicture (it.slot)));
        cell.style.gridRow = String (row + 1);
        cell.style.gridColumn = String (forward ? pos + 1 : cols - pos);
        return cell;
    });

    const board = tint (h ('div', { class: 'board' + (chain.on && fx.enabled ? '' : ' dark') }, cells), colourOfName (chain.name));
    const chainChips = GR.chips (fx.chains.map ((ch, i) => ({ id: i, label: ch.name, cls: ch.on ? '' : 'dim' })), c, (i) => { GR.ui.fxChain = i; GR.ui.fxPage = null; GR.render(); }, 'scroll');
    [...chainChips.children].forEach ((chip, i) => tint (chip, colourOfName (fx.chains[i].name)));
    board.style.gridTemplateColumns = 'repeat(' + cols + ', minmax(0, 1fr))';

    root.append (
        h ('div', { class: 'row between' },
            h ('h2', { text: 'Effects' }),
            h ('label', { class: 'inline' }, fx.enabled ? 'FX on' : 'FX off',
                GR.toggle (fx.enabled, (on) =>
                {
                    const go = () => { fx.enabled = on; GR.send ({ op: 'fx.enabled', on }); GR.render(); };
                    if (on) go(); else GR.ask ('Switch every effects chain off? The instruments go quiet or dry at once.', 'Switch the effects off', go, { danger: true });
                }, 'All effects'))),
        chainChips,
        h ('div', { class: 'row between' },
            h ('button', { class: 'btn', onclick: () => chainPresetSheet (c) }, 'Presets'),
            h ('label', { class: 'inline' }, chain.name + (chain.on ? ' on' : ' off'), GR.toggle (chain.on, (on) => { chain.on = on; GR.send ({ op: 'fx.chainOn', chain: c, on }); GR.render(); }, chain.name))),
        chain.slots.length === 0 ? h ('p', { class: 'hint', text: 'Empty chain.' }) : null,
        board,
        h ('div', { class: 'row between' },
            h ('button', { class: 'btn primary', onclick: () => addEffectSheet (c) }, 'Add effect'),
            h ('span', { class: 'hint', text: chain.slots.length + '/' + GR.state.fxMaxSlots + ' · Tempo ' + fx.tempo })));
};

// ---------------------------------------------------------------- Mixer: the faders, and a page for each channel, effect and output

const mixViews = () =>
{
    const m = GR.state.mixer;
    return [{ id: 'main', label: 'Main', cls: 'v-main' }]
        .concat (m.outputs.slice (1).map ((o, i) => ({ id: 'mon' + i, label: o.name, cls: 'v-mon' })))
        .concat (m.fx.map ((f, i) => ({ id: 'fx' + i, label: f.name, cls: 'v-fx' })));
};

/** The signal chain of one mixer channel, top to bottom: the input, its own processing in the order the sound meets it, then where it is sent. */
function channelItems (c)
{
    const m = GR.state.mixer, ch = m.channels[c];
    const set = (key, value) => { GR.sendValue ('ch' + c + key, { op: 'mix.ch', c, key, value }); };

    return [
        { ico: 'input', label: 'Input', build: () => [
            h ('div', { class: 'row between' },
                h ('span', { class: 'hint', text: 'In ' + ch.socket + (ch.linked ? ' · R of pair' : ch.link ? ' · L of pair' : '') }),
                h ('label', { class: 'inline' }, ch.on ? 'On' : 'Off', GR.toggle (ch.on, (on) => { ch.on = on; GR.send ({ op: 'mix.ch', c, key: 'on', value: on }); GR.render(); }))),
            GR.param ({ id: 'trim', label: 'Trim', unit: 'dB', min: -24, max: 24, def: 0, kind: 'knob' }, ch.trim, (v) => { ch.trim = v; set ('trim', v); }),
            GR.param ({ id: 'pan', label: 'Pan', unit: '', min: -1, max: 1, def: 0, kind: 'knob' }, ch.pan, (v) => { ch.pan = v; set ('pan', v); }) ] },
    ].concat (ch.strip.map ((b, i) => blockItem ('ch' + c + '.' + i, b, {
        rta: { where: 'ch', i: c },
        setOn: (on) => GR.send ({ op: 'mix.param', where: 'ch', i: c, slot: i, id: 'on', value: on }, true),
        setParam: (id, value) => GR.sendValue ('chp' + c + '.' + i + id, { op: 'mix.param', where: 'ch', i: c, slot: i, id, value }),
    })), [
        { ico: 'sends', label: 'Sends', build: () => [
            GR.level ('Main', ch.fader, (v) => { ch.fader = v; set ('fader', v); }),
            m.outputs.slice (1).map ((o, i) => GR.level (o.name, ch.mon[i], (v) => { ch.mon[i] = v; set ('mon' + i, v); })),
            m.fx.map ((f, i) => GR.level (f.name + (ch.fxSong[i] ? ' (song)' : ''), ch.fx[i], (v) => { ch.fx[i] = v; set ('fx' + i, v); })) ] },
    ]);
}

/** One of the mixer's four reverbs and delays: its levels, the effect itself, then its tone. */
function fxItems (f)
{
    const m = GR.state.mixer, e = m.fx[f];
    const set = (key, value) => GR.sendValue ('fx' + f + key, { op: 'mix.fx', f, key, value });

    return [
        { ico: 'level', label: 'Levels', build: () => [
            h ('div', { class: 'row between' },
                h ('span', { class: 'hint', text: e.song ? 'Song settings' : 'Venue settings' }),
                h ('label', { class: 'inline' }, e.on ? 'On' : 'Off', GR.toggle (e.on, (on) => { e.on = on; GR.send ({ op: 'mix.fx', f, key: 'on', value: on }); GR.render(); }))),
            GR.level ('Return', e.ret, (v) => { e.ret = v; set ('ret', v); }),
            m.outputs.slice (1).map ((o, i) => GR.level ('To ' + o.name, e.mon[i], (v) => { e.mon[i] = v; set ('mon' + i, v); })) ] },
    ].concat (e.effect.map ((b, i) => blockItem ('mfx' + f + '.' + i, b, {
        setOn: (on) => GR.send ({ op: 'mix.param', where: 'fx', i: f, slot: i, id: 'on', value: on }, true),
        setParam: (id, value) => GR.sendValue ('mfxp' + f + '.' + i + id, { op: 'mix.param', where: 'fx', i: f, slot: i, id, value }),
    }, () => h ('button', { class: 'btn', onclick: () => GR.ask ('Save this effect and its settings to the loaded song? The venue is not changed.', 'Save', () => GR.send ({ op: 'mix.keepFx', f })) }, 'Save to song'))), [
        { ico: 'eq', label: 'Filters', title: 'Wet HPF / LPF (venue)', build: () => [
            GR.param ({ id: 'lowCut', label: 'HPF', unit: 'Hz', min: 20, max: 2000, def: 180, kind: 'knob' }, e.lowCut, (v) => { e.lowCut = v; set ('lowCut', v); }),
            GR.param ({ id: 'highCut', label: 'LPF', unit: 'Hz', min: 1000, max: 20000, def: 9000, kind: 'knob' }, e.highCut, (v) => { e.highCut = v; set ('highCut', v); }) ] },
    ], e.eq.map ((b, i) => blockItem ('mfxeq' + f + '.' + i, b, {
        setOn: (on) => GR.send ({ op: 'mix.param', where: 'fxeq', i: f, slot: i, id: 'on', value: on }, true),
        setParam: (id, value) => GR.sendValue ('mfxeqp' + f + '.' + i + id, { op: 'mix.param', where: 'fxeq', i: f, slot: i, id, value }),
    })));
}

/** One output: its chain, then its level. */
function outputItems (o)
{
    const out = GR.state.mixer.outputs[o];

    return out.chain.map ((b, i) => blockItem ('out' + o + '.' + i, b, {
        rta: { where: 'out', i: o },
        setOn: (on) => GR.send ({ op: 'mix.param', where: 'out', i: o, slot: i, id: 'on', value: on }, true),
        setParam: (id, value) => GR.sendValue ('outp' + o + '.' + i + id, { op: 'mix.param', where: 'out', i: o, slot: i, id, value }),
    })).concat ([
        { ico: 'level', label: 'Level', build: () => [
            h ('p', { class: 'hint', text: (out.connected ? '' : 'No output socket set. ') + (out.mono ? 'Mono' : 'Stereo') + ' · venue' }),
            GR.level ('Level', out.level, (v) => { out.level = v; GR.sendValue ('out' + o, { op: 'mix.out', o, key: 'level', value: v }); }) ] },
    ]);
}

/** The page behind a fader's Open button. */
function mixPage (root, page)
{
    const m = GR.state.mixer, kind = page.kind, i = page.i;
    const list = kind === 'ch' ? m.channels : kind === 'fx' ? m.fx : m.outputs;
    const items = kind === 'ch' ? channelItems (i) : kind === 'fx' ? fxItems (i) : outputItems (i);
    const what = kind === 'ch' ? 'channel' : kind === 'fx' ? 'effect' : 'output';

    // The place in the chain is remembered by kind, so stepping to the next channel stays on the same block.
    GR.ui.mixRail = GR.ui.mixRail || {};
    const picked = GR.clamp (GR.ui.mixRail[kind] || 0, 0, items.length - 1);
    tint (root, kind === 'ch' ? channelColour (list[i]) : kind === 'fx' ? colourOfName (list[i].name) : '');

    root.append (
        pageHead (() => { GR.ui.mixPage = null; GR.render(); }, 'Faders', list[i].name + (kind === 'out' ? ' output' : ''),
            (kind === 'ch' ? 'Ch ' : kind === 'fx' ? 'FX ' : 'Out ') + (i + 1) + '/' + list.length,
            stepButtons (i, list.length, (to) => { GR.ui.mixPage = { kind, i: to }; GR.render(); }, what)),
        chainPage (items, picked, (n) => { GR.ui.mixRail[kind] = n; GR.render(); }));
}

function presetSheet()
{
    GR.openSheet ({ title: 'Presets', build: () =>
    {
        const m = GR.state.mixer;

        const row = (kind, title, now, changed, list) =>
        {
            let pick = list.includes (now) ? now : (list[0] || '');
            return h ('div', { class: 'card' },
                h ('div', { class: 'row between' }, h ('b', { text: title }), h ('span', { class: 'hint', text: (now || 'none') + (changed ? ' *' : '') })),
                list.length ? h ('div', { class: 'row' },
                    GR.select (list.map ((n) => ({ id: n, label: n })), pick, (v) => { pick = v; }, title),
                    h ('button', { class: 'btn', onclick: () => GR.ask ('Load the ' + title.toLowerCase() + ' "' + pick + '"?' + (changed ? ' Unsaved changes are lost.' : ''), 'Load it',
                        () => GR.send ({ op: 'mix.preset', kind, name: pick })) }, 'Load'))
                    : h ('p', { class: 'hint', text: 'None saved.' }),
                h ('div', { class: 'row wrap' },
                    now ? h ('button', { class: 'btn', onclick: () => GR.ask ('Overwrite "' + now + '"?', 'Save', () => GR.send ({ op: 'mix.save', kind, name: now })) }, 'Save') : null,
                    h ('button', { class: 'btn', onclick: () => GR.askName ('Save ' + title.toLowerCase() + ' as', 'An existing name is overwritten.', '', 'Save',
                        (name) => GR.send ({ op: 'mix.save', kind, name })) }, 'Save as')));
        };

        return [
            row ('act', 'Act', m.act, m.actChanged, m.acts),
            row ('venue', 'Venue', m.venue, m.venueChanged, m.venues),
            row ('monitors', 'Monitors', m.monitors, m.monitorsChanged, m.monitorSets),
        ];
    } });
}

// The fader that is picked moves by the nudge buttons, and by the up and down arrow keys on a
// keyboard. Both stand in for the board's Adjust knob (ENC 6) until the board exists.
let nudgePicked = null;

window.addEventListener ('keydown', (ev) =>
{
    if (GR.ui.tab !== 'mixer' || GR.ui.mixPage || GR.sheetStack.length || ! nudgePicked) return;
    if (ev.key !== 'ArrowUp' && ev.key !== 'ArrowDown') return;
    const t = ev.target;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA')) return;
    ev.preventDefault();
    nudgePicked ((ev.key === 'ArrowUp' ? 1 : -1) * (ev.shiftKey ? 0.1 : 1));
});

GR.screens.mixer = (root) =>
{
    const m = GR.state.mixer;
    nudgePicked = null;

    if (! m.on)
    {
        root.append (h ('section', { class: 'card' },
            h ('h2', { text: 'Mixer off' }),
            h ('button', { class: 'btn primary', onclick: () => GR.ask ('Switch the mixer on? What you hear changes at once: the rig starts mixing the channels to its outputs.', 'Switch the mixer on',
                () => GR.send ({ op: 'mix.on', on: true })) }, 'Mixer on')));
        return;
    }

    const page = GR.ui.mixPage;
    if (page && (page.kind === 'ch' ? m.channels : page.kind === 'fx' ? m.fx : m.outputs)[page.i]) return mixPage (root, page);
    GR.ui.mixPage = null;
    root.classList.add ('faders');

    // Three banks of faders, as on the CQ: the inputs, the mixer's four effects, the outputs.
    // An upright phone has room for five faders wide enough for a thumb, so there the inputs take two banks.
    const narrow = window.matchMedia && window.matchMedia ('(max-width: 520px)').matches;
    const per = narrow ? 5 : 8;
    const inBanks = [];
    for (let from = 0; from < m.channels.length; from += per)
        inBanks.push ({ id: 'in' + inBanks.length, from, to: Math.min (m.channels.length, from + per),
                        label: m.channels.length <= per ? 'Inputs' : 'In ' + (from + 1) + '-' + Math.min (m.channels.length, from + per) });
    const banks = inBanks.concat ([{ id: 'fx', label: 'FX' }, { id: 'out', label: 'Outputs' }]);
    const bank = banks.some ((b) => b.id === GR.ui.mixBank) ? GR.ui.mixBank : 'in0';
    const inBank = inBanks.find ((b) => b.id === bank);

    const views = mixViews();
    if (! views.some ((v) => v.id === GR.ui.mixView)) GR.ui.mixView = 'main';
    const view = GR.ui.mixView, kind = view.replace (/\d+$/, ''), n = Number (view.replace (/\D+/g, '')) || 0;
    const viewNow = views.find ((v) => v.id === view);
    const meters = { ch: [], fx: [], out: [], gr: [] };
    const built = {};       // strip key -> { el, o }

    const nudgeName = h ('b'), nudgeVal = h ('output');
    const nudgeBtns = [-1, -0.1, 0.1, 1].map ((d) => h ('button', { class: 'btn nudge-btn', onclick: () => nudge (d) }, (d > 0 ? '+' : '−') + Math.abs (d)));

    const showPicked = () =>
    {
        const p = built[GR.ui.mixSel];
        Object.keys (built).forEach ((k) => built[k].el.classList.toggle ('picked', k === GR.ui.mixSel));
        nudgeName.textContent = p ? p.o.name : 'Tap a fader';
        nudgeVal.textContent = p ? GR.db (p.o.db) + (GR.db (p.o.db) === 'off' ? '' : ' dB') : '';
        nudgeBtns.forEach ((b) => { b.disabled = ! p; });
    };

    const pick = (key) => { GR.ui.mixSel = key; showPicked(); };

    function nudge (d)
    {
        const p = built[GR.ui.mixSel];
        if (! p) return;
        const from = p.o.db <= m.offDb + 0.5 ? (d > 0 ? -60 - d : m.offDb) : p.o.db;
        const to = Math.round ((from + d) * 10) / 10;
        const v = to < -60 ? m.offDb : Math.min (10, to);
        p.o.db = v;
        p.o.change (v);
        GR.render();
    }

    nudgePicked = nudge;

    const strip = (o) =>
    {
        const f = GR.fader (o.db, (v) => { o.db = v; o.change (v); if (GR.ui.mixSel === o.key) showPicked(); }, { cls: o.cls });
        if (o.meter) o.meter (f.meter);
        const gr = h ('div', { class: 'gr', title: 'GR' }, h ('i'));
        if (o.gr) o.gr (gr.firstChild);

        const el = h ('div', { class: 'strip' + (o.dim ? ' dim' : '') + (o.cls ? ' ' + o.cls : ''), onpointerdown: () => pick (o.key) },
            h ('div', { class: 'strip-name' }, o.name, o.sub ? h ('small', { text: o.sub }) : null),
            h ('button', { class: 'strip-open', onclick: o.open, 'aria-label': 'Open ' + o.name }, 'Open'),
            o.gr ? gr : h ('div', { class: 'gr none' }),
            f,
            o.mute ? h ('button', { class: 'mute' + (o.muted ? ' on' : ''), onclick: o.mute }, 'Mute') : h ('div', { class: 'mute none' }));

        tint (el, o.colour);
        built[o.key] = { el, o };
        return el;
    };

    const strips = [];

    if (inBank)
        m.channels.forEach ((ch, c) =>
        {
            if (c < inBank.from || c >= inBank.to) return;
            const key = kind === 'main' ? 'fader' : kind === 'mon' ? 'mon' + n : 'fx' + n;
            const db = kind === 'main' ? ch.fader : kind === 'mon' ? ch.mon[n] : ch.fx[n];

            strips.push (strip ({
                key: 'ch' + c, name: ch.name, db, dim: ! ch.on || ch.mute, colour: channelColour (ch),
                sub: ! ch.on ? 'off' : kind === 'fx' && ch.fxSong[n] ? 'song' : ch.linked ? 'R' : '',
                change: (v) => { if (kind === 'main') ch.fader = v; else if (kind === 'mon') ch.mon[n] = v; else ch.fx[n] = v; GR.sendValue ('ch' + c + key, { op: 'mix.ch', c, key, value: v }); },
                open: () => { GR.ui.mixPage = { kind: 'ch', i: c }; GR.render(); },
                meter: (el) => { meters.ch[c] = el; },
                gr: (el) => { meters.gr[c] = el; },
                mute: () => { ch.mute = ! ch.mute; GR.send ({ op: 'mix.ch', c, key: 'mute', value: ch.mute }, true); GR.render(); },
                muted: ch.mute,
            }));
        });

    if (bank === 'fx')
        m.fx.forEach ((e, f) =>
        {
            const toMon = kind === 'mon', key = toMon ? 'mon' + n : 'ret';
            strips.push (strip ({
                key: 'fx' + f, name: e.name, cls: 'fxret', colour: colourOfName (e.name), db: toMon ? e.mon[n] : e.ret, dim: ! e.on, sub: e.on ? '' : 'off',
                change: (v) => { if (toMon) e.mon[n] = v; else e.ret = v; GR.sendValue ('fx' + f + key, { op: 'mix.fx', f, key, value: v }); },
                open: () => { GR.ui.mixPage = { kind: 'fx', i: f }; GR.render(); },
                meter: (el) => { meters.fx[f] = el; },
            }));
        });

    if (bank === 'out')
        m.outputs.forEach ((out, o) =>
        {
            strips.push (strip ({
                key: 'out' + o, name: out.name, cls: 'master', db: out.level, dim: out.mute, sub: out.connected ? '' : 'no socket',
                change: (v) => { out.level = v; GR.sendValue ('out' + o, { op: 'mix.out', o, key: 'level', value: v }); },
                open: () => { GR.ui.mixPage = { kind: 'out', i: o }; GR.render(); },
                meter: (el) => { meters.out[o] = el; },
                mute: () => { out.mute = ! out.mute; GR.send ({ op: 'mix.out', o, key: 'mute', value: out.mute }, true); GR.render(); },
                muted: out.mute,
            }));
        });

    const limit = h ('span', { class: 'tag' });

    // What the faders send to only matters to the inputs and the effects; an output's fader is its own level.
    const sendViews = bank === 'fx' ? views.filter ((v) => ! v.id.startsWith ('fx')) : views;
    const sendsRow = bank === 'out' ? h ('p', { class: 'hint sends-none', text: 'Output levels' })
        : h ('div', { class: 'sends' },
            h ('span', { class: 'hint', text: bank === 'fx' ? 'Returns to' : 'Sends on faders' }),
            GR.chips (sendViews, bank === 'fx' && kind === 'fx' ? 'main' : view, (id) => { GR.ui.mixView = id; GR.render(); }, 'scroll'));

    const bankTabs = h ('div', { class: 'banks', role: 'tablist', 'aria-label': 'Fader banks' }, banks.map ((b) =>
        h ('button', { class: 'bank' + (b.id === bank ? ' on' : ''), role: 'tab', 'aria-selected': b.id === bank ? 'true' : 'false', onclick: () => { GR.ui.mixBank = b.id; GR.render(); } }, b.label)));
    bankTabs.style.gridTemplateColumns = 'repeat(' + banks.length + ', minmax(0, 1fr))';
    const stripsRow = h ('div', { class: 'strips ' + (bank === 'out' ? 'v-main' : bank === 'fx' && kind === 'fx' ? 'v-main' : viewNow.cls) }, strips);
    stripsRow.style.gridTemplateColumns = 'repeat(' + per + ', minmax(0, 1fr))';

    root.append (
        h ('div', { class: 'row between mix-top' },
            h ('button', { class: 'btn', onclick: presetSheet }, h ('b', { text: 'Presets' }), h ('span', { class: 'setups-now', text: ': ' + (m.act || 'no act') + ' / ' + (m.venue || 'no venue') })),
            h ('div', { class: 'row' }, limit,
                h ('button', { class: 'btn tiny ghost', onclick: () => GR.ask ('Switch the mixer off? The rig goes back to sounding exactly as it did without a mixer, at once.', 'Switch the mixer off',
                    () => GR.send ({ op: 'mix.on', on: false }), { danger: true }) }, 'Mixer off'))),
        m.warnings.map ((w) => h ('div', { class: 'note warn', text: w })),
        bankTabs,
        sendsRow,
        stripsRow,
        h ('div', { class: 'nudge' },
            h ('div', { class: 'nudge-what' }, nudgeName, nudgeVal),
            h ('div', { class: 'nudge-btns' }, nudgeBtns)));

    if (! built[GR.ui.mixSel]) GR.ui.mixSel = null;
    showPicked();

    GR.onLive = (l) =>
    {
        if (! l.mix) return;
        const set = (el, peak) => { if (el) { el.style.height = (GR.meterPos (peak) * 100) + '%'; el.className = peak >= 0.98 ? 'clip' : peak > 0.5 ? 'hot' : ''; } };
        meters.ch.forEach ((el, c) => set (el, l.mix.ch[c]));
        meters.fx.forEach ((el, f) => set (el, l.mix.fx[f]));
        meters.gr.forEach ((el, c) => { if (el) el.style.width = GR.clamp (-(l.mix.gr[c] || 0) / 18, 0, 1) * 100 + '%'; });
        meters.out.forEach ((el, o) => set (el, Math.max (l.mix.out[o][0], l.mix.out[o][1])));
        const lim = l.mix.limit || 0;
        limit.textContent = l.mix.guard ? 'Level guard tripped' : lim < -0.5 ? 'Lim ' + lim.toFixed (1) + ' dB' : 'Lim 0 dB';
        limit.className = 'tag' + (l.mix.guard ? ' bad' : lim < -0.5 ? ' warn' : ' dim');
    };
};

// ---------------------------------------------------------------- Tools: the tuner, the break music, the venue knob

GR.screens.tools = (root) =>
{
    const s = GR.state, tu = s.tuner, bm = s.breakMusic;
    const live0 = GR.live || {};
    const sockets = [{ id: -1, label: 'Auto (guitar being played)' }].concat ((s.effects.chains || []).map ((ch) => ({ id: ch.socket - 1, label: ch.name + ' (input ' + ch.socket + ')' })));
    if (! sockets.some ((x) => x.id === tu.socket)) sockets.push ({ id: tu.socket, label: 'Input ' + (tu.socket + 1) });

    // ---- the tuner ----
    const note = h ('div', { class: 'tuner-note', text: '--' });
    const words = h ('div', { class: 'tuner-words hint' });
    const needle = h ('i');
    const tunerOn = !! (live0.tuner && live0.tuner.on);
    const tunerBody = h ('div', { class: 'tuner' + (tunerOn ? '' : ' off') }, note, h ('div', { class: 'tuner-bar' }, h ('b'), needle), words);

    // ---- the break music ----
    let pick = GR.ui.breakPick && bm.mixes.includes (GR.ui.breakPick) ? GR.ui.breakPick : (bm.mixes.includes (bm.mix) ? bm.mix : (bm.mixes[0] || ''));
    const state = h ('b'), line = h ('p', { class: 'hint' }), fade = h ('i');
    const knobLine = h ('p', { class: 'knob-line' });
    const set = (key, value) => GR.send ({ op: 'break.set', key, value }, true);
    const tick = (label, key) => h ('label', { class: 'inline' }, label, GR.toggle (!! bm[key], (on) => { bm[key] = on; set (key, on); GR.render(); }, label));

    root.append (
        h ('section', { class: 'card' },
            h ('div', { class: 'row between' },
                h ('h2', { text: 'Tuner' }),
                h ('label', { class: 'inline' }, tunerOn ? 'On' : 'Off', GR.toggle (tunerOn, (on) => { GR.send ({ op: 'tuner.on', on }, true).then (() => GR.render()); if (GR.live && GR.live.tuner) GR.live.tuner.on = on; GR.render(); }, 'Tuner'))),
            tunerBody,
            tunerOn && GR.ui.strobeHidden ? h ('button', { class: 'btn primary', onclick: () => { GR.ui.strobeHidden = false; GR.render(); } }, 'Full-screen strobe') : null,
            h ('p', { class: 'hint', text: GR.strobe.words (s) }),
            h ('div', { class: 'row wrap' },
                h ('label', { class: 'field grow' }, 'Mute', GR.select ([{ id: 'guitars', label: 'Guitars only' }, { id: 'main', label: 'Main mix' }, { id: 'nothing', label: 'Nothing' }],
                    GR.strobe.silence (tu), (v) => { tu.silence = v; tu.mutes = v !== 'nothing'; GR.send ({ op: 'tuner.set', key: 'silence', value: v }, true).then (() => GR.render()); GR.render(); }, 'Mute while tuning')),
                h ('label', { class: 'field grow' }, 'Input', GR.select (sockets, tu.socket, (v) => { tu.socket = Number (v); GR.send ({ op: 'tuner.set', key: 'socket', value: Number (v) }, true); }, 'Listen to')),
                h ('label', { class: 'field' }, 'A =', GR.select ([432, 436, 437, 438, 439, 440, 441, 442, 443, 444].map ((hz) => ({ id: hz, label: hz + ' Hz' })).concat (
                    [432, 436, 437, 438, 439, 440, 441, 442, 443, 444].includes (tu.reference) ? [] : [{ id: tu.reference, label: tu.reference + ' Hz' }]), tu.reference,
                    (v) => { tu.reference = Number (v); GR.send ({ op: 'tuner.set', key: 'reference', value: Number (v) }, true); }, 'Tune A to')),
                h ('label', { class: 'inline' }, 'Full screen', GR.toggle (tu.fullScreen !== false, (on) => { tu.fullScreen = on; GR.ui.strobeHidden = false; GR.send ({ op: 'tuner.set', key: 'fullScreen', value: on }, true); GR.render(); }, 'Fill the screen with the strobe while tuning'))),
            GR.strobe.silence (tu) === 'guitars' ? h ('label', { class: 'inline' }, 'Guitars stay in monitors', GR.toggle (tu.monitors !== false, (on) => { tu.monitors = on; GR.send ({ op: 'tuner.set', key: 'monitors', value: on }, true); GR.render(); }, 'Keep the guitars in the monitors while tuning')) : null,
            GR.strobe.silence (tu) === 'guitars' && (tu.channels || []).length ? h ('div', {},
                h ('h3', { text: 'Guitar channels' }),
                h ('div', { class: 'row wrap' }, tu.channels.map ((ch) =>
                    h ('button', { class: 'chip' + (ch.silent ? ' on' : ''), 'aria-pressed': ch.silent ? 'true' : 'false',
                                   onclick: () => { ch.silent = ! ch.silent; GR.send ({ op: 'tuner.set', key: 'channel', name: ch.name, value: ch.silent }, true).then (() => GR.render()); GR.render(); } },
                        ch.name + (ch.silent ? ': mute' : ': live'))))) : null),

        h ('section', { class: 'card' },
            h ('div', { class: 'row between' }, h ('h2', { text: 'Break music' }), state),
            line,
            h ('div', { class: 'break-fade', title: 'Fade' }, fade),
            bm.mixes.length ? h ('div', { class: 'row wrap' },
                GR.select (bm.mixes.map ((m) => ({ id: m, label: m })), pick, (v) => { pick = v; GR.ui.breakPick = v; }, 'Mix'),
                h ('button', { class: 'btn primary', onclick: () => GR.send ({ op: 'break.play', name: pick }, true) }, 'Play'),
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'break.play', name: pick, fromStart: true }, true) }, 'Restart'),
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'break.stop' }, true) }, 'Stop'))
                : h ('p', { class: 'note', text: 'No mixes. Add WAV, FLAC or MP3 files on the rig (Break music tab).' }),
            h ('div', { class: 'row wrap' },
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'break.up' }, true) }, 'Fade up'),
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'break.down' }, true) }, 'Fade down')),
            GR.param ({ id: 'level', label: 'Level', unit: 'dB', min: bm.minLevel, max: bm.maxLevel, def: -12, kind: 'knob' }, bm.level, (v) => { bm.level = v; GR.sendValue ('breakLevel', { op: 'break.level', value: v }); }),
            h ('h3', { text: 'Auto fade down on' }),
            h ('div', { class: 'row wrap' }, tick ('Footswitch', 'onSwitch'), tick ('Song load', 'onSong'), tick ('Input signal', 'onSound')),
            bm.onSound ? GR.param ({ id: 'soundDb', label: 'Thresh', unit: 'dB', min: -60, max: 0, def: -30, kind: 'knob' }, bm.soundDb, (v) => { bm.soundDb = v; GR.sendValue ('breakSound', { op: 'break.set', key: 'soundDb', value: v }); }) : null,
            GR.param ({ id: 'fadeDown', label: 'Fade down', unit: 's', min: 0.2, max: 15, def: 2, kind: 'knob' }, bm.fadeDown, (v) => { bm.fadeDown = v; GR.sendValue ('breakDown', { op: 'break.set', key: 'fadeDown', value: v }); }),
            GR.param ({ id: 'fadeUp', label: 'Fade up', unit: 's', min: 0.2, max: 15, def: 4, kind: 'knob' }, bm.fadeUp, (v) => { bm.fadeUp = v; GR.sendValue ('breakUp', { op: 'break.set', key: 'fadeUp', value: v }); })),

        h ('section', { class: 'card' },
            h ('h2', { text: 'ENC 5 (venue / break music)' }),
            knobLine,
            h ('div', { class: 'row wrap' }, ['left', 'right', 'push', 'hold'].map ((what) =>
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'knob', do: what }, true) }, { left: '◂ Turn', right: 'Turn ▸', push: 'Push', hold: 'Hold' }[what])))));

    let wasOn = tunerOn;

    GR.onLive = (l) =>
    {
        const t = l.tuner || {}, b = l.breakMusic || {};
        if (!! t.on !== wasOn) { wasOn = !! t.on; GR.render(); return; }

        const cents = Math.round (Math.abs (t.cents || 0));
        const close = t.heard && Math.abs (t.cents) < 3;
        note.textContent = t.on && t.heard ? t.name : '--';
        note.className = 'tuner-note' + (close ? ' good' : '');
        needle.style.left = (50 + GR.clamp (t.heard ? t.cents : 0, -50, 50)) + '%';
        needle.className = ! t.heard ? 'rest' : close ? 'good' : Math.abs (t.cents) < 15 ? 'near' : 'far';
        words.textContent = ! t.on ? 'Off' : ! t.heard ? 'No note' + (t.socket >= 0 ? ' (input ' + (t.socket + 1) + ')' : '')
            : (cents === 0 ? 'In tune' : cents + (cents === 1 ? ' cent ' : ' cents ') + (t.cents < 0 ? 'flat' : 'sharp')) + '. ' + Number (t.hz).toFixed (1) + ' Hz, input ' + (t.socket + 1) + '.';

        state.textContent = ! b.playing ? 'Stopped' : b.up ? 'Playing' : 'Playing, faded down';
        state.className = ! b.playing ? 'tag dim' : b.up ? 'tag ok' : 'tag warn';
        line.textContent = b.line || '';
        fade.style.width = Math.round (GR.clamp (b.fade || 0, 0, 1) * 100) + '%';
        knobLine.textContent = (b.knobOnBreak ? 'Break music: ' : 'Venue: ') + (b.knob || '');
    };
};

// ---------------------------------------------------------------- Record: one big button, and a page for each thing to set

const recClock = (sec) => Math.floor (sec / 3600) + ':' + String (Math.floor (sec / 60) % 60).padStart (2, '0') + ':' + String (Math.floor (sec % 60)).padStart (2, '0');
const recLow = -30;      // the bottom of a recording mix level: not in the mix at all
const recLevelParam = (label) => ({ id: 'recLevel', label, unit: 'dB', min: recLow, max: 12, def: 0, kind: 'knob' });
const recLength = (min) => min >= 60 ? Math.floor (min / 60) + ' h ' + Math.round (min % 60) + ' min' : min >= 1 ? Math.round (min) + ' min' : Math.round (min * 60) + ' s';
const recPointOf = (m, id) => m.rec.points.find ((p) => p.id === id) || m.rec.points[0];
const recOff = (v) => v <= recLow + 0.05 ? GR.state.mixer.offDb : v;

/** The recording mix as a chain page: its balance, its pans, then its own EQ, compressor and limiter, then its level. */
function recMixItems()
{
    const m = GR.state.mixer, r = m.rec;
    const chans = m.channels.map ((ch, c) => ({ ch, c })).filter ((x) => x.ch.on && ! x.ch.linked);
    const setCh = (c, key, value) => GR.sendValue ('rec' + c + key, { op: 'mix.ch', c, key, value });

    return [
        { ico: 'level', label: 'Balance', title: 'Levels (post-fader, 0 dB = main mix)', build: () => [
            chans.map ((x) => tint (GR.param (recLevelParam (x.ch.name), Math.max (recLow, x.ch.recLevel), (v) => { x.ch.recLevel = recOff (v); setCh (x.c, 'recLevel', recOff (v)); }), channelColour (x.ch))),
            m.fx.map ((e, f) => e.on ? GR.param (recLevelParam (e.name), Math.max (recLow, e.recLevel), (v) => { e.recLevel = recOff (v); GR.sendValue ('recfx' + f, { op: 'mix.fx', f, key: 'recLevel', value: recOff (v) }); }) : null) ] },
        { ico: 'sends', label: 'Pan', title: 'Pan', build: () => [
            chans.map ((x) => x.ch.link ? h ('p', { class: 'hint', text: x.ch.name + ': stereo pair, hard L/R' })
                : tint (GR.param ({ id: 'recPan', label: x.ch.name, unit: '', min: -1, max: 1, def: 0, kind: 'knob' }, x.ch.recPan, (v) => { x.ch.recPan = v; setCh (x.c, 'recPan', v); }), channelColour (x.ch))) ] },
    ].concat (r.chain.map ((b, i) => blockItem ('rec.' + i, b, {
        setOn: (on) => GR.send ({ op: 'mix.param', where: 'rec', i: 0, slot: i, id: 'on', value: on }, true),
        setParam: (id, value) => GR.sendValue ('recp' + i + id, { op: 'mix.param', where: 'rec', i: 0, slot: i, id, value }),
    })), [
        { ico: 'level', label: 'Level', title: 'Rec mix level', build: () => [
            GR.level ('Level', r.level, (v) => { r.level = v; GR.sendValue ('reclevel', { op: 'rec.set', key: 'level', value: v }); }) ] },
    ]);
}

GR.screens.record = (root) =>
{
    const m = GR.state.mixer, r = m.rec, live0 = (GR.live || {}).rec || {};
    const page = GR.ui.recPage;
    const go = (to) => { GR.ui.recPage = to; GR.render(); };
    const listed = m.channels.map ((ch, c) => ({ ch, c })).filter ((x) => x.ch.on && ! x.ch.linked);
    const unused = m.channels.map ((ch, c) => ({ ch, c })).filter ((x) => ! x.ch.on && ! x.ch.linked);     // the duo channels, on a solo night

    // ---- one channel: where it is recorded ----
    if (page && page.kind === 'ch' && m.channels[page.i] && ! live0.on)
    {
        const ch = m.channels[page.i], at = listed.findIndex ((x) => x.c === page.i);
        tint (root, channelColour (ch));
        root.append (
            pageHead (() => go ({ kind: 'channels' }), 'Channels', ch.name, 'Rec point',
                stepButtons (at, listed.length, (to) => go ({ kind: 'ch', i: listed[to].c }), 'channel')),
            h ('div', { class: 'choices' }, r.points.map ((p) =>
                h ('button', { class: 'choice' + (p.id === ch.rec ? ' on' : ''), 'aria-pressed': p.id === ch.rec ? 'true' : 'false',
                               onclick: () => { ch.rec = p.id; GR.send ({ op: 'mix.ch', c: page.i, key: 'rec', value: p.id }, true).then (() => GR.render()); GR.render(); } },
                    h ('b', { text: p.label }), h ('small', { text: p.more })))),
            ! m.on ? h ('p', { class: 'hint', text: 'Mixer off: the post-looper points record post FX.' }) : null,
            h ('section', { class: 'card' }, h ('h2', { text: 'Channel' }),
                h ('div', { class: 'choices' },
                    h ('button', { class: 'choice', onclick: () => GR.ask ('Switch ' + ch.name + ' off? It leaves the mix as well as the recording.', 'Switch it off',
                            () => { ch.on = false; GR.send ({ op: 'mix.ch', c: page.i, key: 'on', value: false }, true).then (() => GR.render()); go ({ kind: 'channels' }); }) },
                        h ('b', { text: 'Switch channel off' }), h ('small', { text: 'Out of the mix and the recording.' })))));
        return;
    }

    // ---- the list of channels ----
    if (page && page.kind === 'channels' && ! live0.on)
    {
        root.append (
            pageHead (() => go (null), 'Record', 'Channels', 'Rec points'),
            h ('div', { class: 'choices' }, listed.map ((x) =>
                tint (h ('button', { class: 'choice go', onclick: () => go ({ kind: 'ch', i: x.c }) },
                    h ('b', { text: x.ch.name }), h ('small', { text: recPointOf (m, x.ch.rec).label })), channelColour (x.ch)))),
            unused.length ? h ('section', { class: 'card' }, h ('h2', { text: 'Off (tap to switch on)' }),
                h ('div', { class: 'choices' }, unused.map ((x) =>
                    tint (h ('button', { class: 'choice', onclick: () => GR.ask ('Switch ' + x.ch.name + ' on? It joins the mix as well as the recording.', 'Switch it on',
                            () => { x.ch.on = true; GR.send ({ op: 'mix.ch', c: x.c, key: 'on', value: true }, true).then (() => GR.render()); GR.render(); }) },
                        h ('b', { text: x.ch.name }), h ('small', { text: 'Off' })), channelColour (x.ch))))) : null);
        return;
    }

    // ---- the recording mix ----
    if (page && page.kind === 'mix')
    {
        const items = recMixItems();
        const picked = GR.clamp (GR.ui.recRail || 0, 0, items.length - 1);
        root.append (
            pageHead (() => go (null), 'Record', 'Rec mix', 'File only'),
            chainPage (items, picked, (n) => { GR.ui.recRail = n; GR.render(); }));
        return;
    }

    // ---- the recordings on the drive: soundcheck, keep, copy to USB, clear ----
    const recs = GR.state.recordings || { list: [], drives: [], free: 0 };
    const liveCheck = (GR.live && GR.live.check) || {}, liveCopy = (GR.live && GR.live.copy) || {};
    const facts = (x) => [recLength (x.min), x.files + ' files', Number (x.gb).toFixed (x.gb < 1 ? 2 : 1) + ' GB', x.kept ? 'kept' : null, x.copied ? 'on USB' : 'not copied',
                          x.check > 0 ? null : 'no raw files'].filter (Boolean).join (', ');
    const order = (c) => GR.send (c).then (() => GR.render());
    const copyTo = (names) =>
    {
        const drive = recs.drives[0];
        if (! drive) { GR.toast ('No USB drive.', true); return; }
        GR.ask ('Copy to "' + drive.name + '"? USB copying can click the audio: not during a set.', 'Copy', () => order ({ op: 'recs.copy', names, drive: drive.name }));
    };
    const watch = (line) =>
    {
        let wasCheck = !! liveCheck.on, wasCopy = !! liveCopy.on;
        GR.onLive = (l) =>
        {
            const c = l.check || {}, p = l.copy || {};
            if (!! c.on !== wasCheck || !! p.on !== wasCopy) { wasCheck = !! c.on; wasCopy = !! p.on; GR.render(); return; }
            line.textContent = c.on ? 'Soundcheck: ' + c.name + ', ' + recClock (c.pos || 0) + ' of ' + recClock (c.len || 0) + (c.missed > 0 ? '. Drive dropouts: ' + c.missed : '') + (c.loops ? '. ' + c.loops : '')
                : p.on ? p.now + ' (' + Math.round ((p.done || 0) * 100) + '%). Leave the drive in.'
                : p.result ? p.result : Number (recs.free || 0).toFixed (0) + ' GB free';
            line.className = 'hint' + ((p.failed && ! p.on && ! c.on) || c.missed > 0 ? ' bad' : '');
        };
        GR.onLive ({ check: liveCheck, copy: liveCopy });
    };

    if (page && page.kind === 'rec1' && recs.list.some ((x) => x.name === page.name))
    {
        const x = recs.list.find ((y) => y.name === page.name);
        const playing = liveCheck.on && liveCheck.name === x.name;
        const line = h ('p', { class: 'hint' });
        const withLoops = GR.ui.recLoops !== false;     // remembered on this phone only
        root.append (
            pageHead (() => go ({ kind: 'recs' }), 'Recordings', x.name, facts (x)),
            line,
            h ('div', { class: 'choices' },
                playing
                    ? h ('button', { class: 'choice on', onclick: () => order ({ op: 'check.stop' }) }, h ('b', { text: 'Stop soundcheck' }), h ('small', { text: 'Back to live inputs.' }))
                    : h ('button', { class: 'choice', disabled: ! (x.check > 0), onclick: () => order ({ op: 'check.start', name: x.name, loop: true, loops: withLoops }) }, h ('b', { text: 'Virtual soundcheck' }),
                        h ('small', { text: x.check > 0 ? 'Raw files into their inputs, looped.' : 'No raw files.' })),
                h ('button', { class: 'choice' + (withLoops ? ' on' : ''), 'aria-pressed': withLoops ? 'true' : 'false', disabled: playing || ! (x.check > 0), onclick: () => { GR.ui.recLoops = ! withLoops; GR.render(); } },
                    h ('b', { text: withLoops ? 'Replay footwork: on' : 'Replay footwork: off' }), h ('small', { text: 'Looper presses, kicks, FX switches, pedal moves.' })),
                h ('button', { class: 'choice' + (x.kept ? ' on' : ''), 'aria-pressed': x.kept ? 'true' : 'false', onclick: () => order ({ op: 'recs.keep', name: x.name, on: ! x.kept }) },
                    h ('b', { text: x.kept ? 'Kept' : 'Keep' }), h ('small', { text: 'Safe from Clear.' })),
                h ('button', { class: 'choice', disabled: ! recs.drives.length || !! liveCopy.on, onclick: () => copyTo ([x.name]) }, h ('b', { text: 'Copy to USB' }),
                    h ('small', { text: recs.drives.length ? 'To "' + recs.drives[0].name + '" (' + Number (recs.drives[0].free).toFixed (0) + ' GB free), verified.' : 'No USB drive.' })),
                h ('button', { class: 'choice danger', disabled: x.kept || playing, onclick: () => GR.ask ('Delete ' + x.name + ' for good?' + (x.copied ? '' : ' It has NOT been copied to USB.'), 'Delete',
                        () => GR.send ({ op: 'recs.delete', name: x.name }).then (() => go ({ kind: 'recs' }))) },
                    h ('b', { text: 'Delete' }), h ('small', { text: x.kept ? 'Kept: unkeep first.' : 'Permanent.' }))));
        watch (line);
        return;
    }

    if (page && (page.kind === 'recs' || page.kind === 'rec1'))
    {
        const line = h ('p', { class: 'hint' });
        root.append (
            pageHead (() => go (null), 'Record', 'Recordings', 'Newest first'),
            line,
            liveCheck.on ? h ('button', { class: 'choice on', onclick: () => order ({ op: 'check.stop' }) }, h ('b', { text: 'Stop soundcheck' }), h ('small', { text: liveCheck.name })) : null,
            recs.list.length
                ? h ('div', { class: 'choices' }, recs.list.map ((x) => h ('button', { class: 'choice go', onclick: () => go ({ kind: 'rec1', name: x.name }) }, h ('b', { text: x.name }), h ('small', { text: facts (x) }))))
                : h ('p', { class: 'hint', text: 'No recordings.' }),
            h ('section', { class: 'card' },
                h ('h2', { text: 'All' }),
                h ('div', { class: 'choices' },
                    liveCopy.on
                        ? h ('button', { class: 'choice', onclick: () => order ({ op: 'recs.cancel' }) }, h ('b', { text: 'Stop copy' }), h ('small', { text: 'Files already verified stay on the drive.' }))
                        : h ('button', { class: 'choice', disabled: ! recs.drives.length, onclick: () => copyTo ([]) }, h ('b', { text: 'Copy all to USB' }),
                            h ('small', { text: recs.drives.length ? 'Uncopied ones, to "' + recs.drives[0].name + '".' : 'No USB drive.' })),
                    h ('button', { class: 'choice', disabled: !! liveCopy.on, onclick: () => GR.ask ('Delete every recording that has been copied to USB and checked? Kept ones stay.', 'Clear', () => order ({ op: 'recs.clear', copied: true })) },
                        h ('b', { text: 'Clear copied' }), h ('small', { text: 'Kept ones stay.' })),
                    h ('button', { class: 'choice danger', disabled: !! liveCopy.on, onclick: () => GR.ask ('Delete EVERY recording, copied or not? Only kept ones stay.', 'Clear all', () => order ({ op: 'recs.clear', copied: false })) },
                        h ('b', { text: 'Clear all' }), h ('small', { text: 'Copied or not. Kept ones stay.' })))));
        watch (line);
        return;
    }

    GR.ui.recPage = null;

    // ---- the front page: the button, and what it will write ----
    const clock = h ('div', { class: 'rec-clock', text: recClock (live0.sec || 0) });
    const line = h ('p', { class: 'hint' });
    const room = h ('p', { class: 'hint' });
    const meters = [h ('i'), h ('i')];
    const button = h ('button', { class: 'rec-button' + (live0.on ? ' on' : ''), 'aria-pressed': live0.on ? 'true' : 'false',
        onclick: () => live0.on
            ? GR.ask ('Stop recording? The files are saved as they are.', 'Stop recording', () => GR.send ({ op: 'rec.stop' }).then (() => GR.render()))
            : GR.send ({ op: 'rec.start' }).then (() => GR.render()) },
        h ('span', { class: 'rec-dot' }), h ('b', { text: live0.on ? 'Stop' : 'Record' }));
    const set = (key, value) => { r[key] = value; GR.send ({ op: 'rec.set', key, value }, true).then (() => GR.render()); GR.render(); };
    const tick = (label, key, more) => h ('label', { class: 'inline between rec-tick' }, h ('span', {}, h ('b', { text: label }), h ('small', { text: more })),
        GR.toggle (!! r[key], (on) => set (key, on), label));

    root.append (
        h ('section', { class: 'card rec-card' + (live0.on ? ' now' : '') },
            h ('div', { class: 'rec-top' }, button, h ('div', { class: 'rec-read' }, clock, h ('div', { class: 'rec-meters', 'aria-hidden': 'true' }, meters.map ((i) => h ('div', { class: 'meter flat' }, i))), line, room))),
        h ('div', { class: 'choices' },
            h ('button', { class: 'choice go', disabled: !! live0.on, onclick: () => go ({ kind: 'channels' }) }, h ('b', { text: 'Channels' }),
                h ('small', { text: listed.filter ((x) => x.ch.rec !== 'none').length + '/' + listed.length + ' recorded' + (unused.length ? ', ' + unused.length + ' off' : '') })),
            h ('button', { class: 'choice go', onclick: () => go ({ kind: 'mix' }) }, h ('b', { text: 'Rec mix' }),
                h ('small', { text: 'Balance, pan, EQ, comp, limiter. File only.' })),
            h ('button', { class: 'choice go' + ((GR.live && GR.live.check && GR.live.check.on) ? ' on' : ''), onclick: () => go ({ kind: 'recs' }) }, h ('b', { text: 'Recordings' }),
                h ('small', { text: (GR.live && GR.live.check && GR.live.check.on) ? 'Soundcheck: ' + GR.live.check.name : ((GR.state.recordings || { list: [] }).list.length) + ' on the rig' }))),
        h ('section', { class: 'card' },
            h ('h2', { text: 'Mix files' }),
            ! m.on ? h ('p', { class: 'hint', text: 'Mixer off: raw inputs only.' }) : null,
            h ('fieldset', { class: 'plain', disabled: !! live0.on },
                tick ('Rec mix', 'mix', 'Stereo'),
                tick ('Main mix', 'main', 'Stereo, as sent to the PA'),
                tick ('FX returns', 'fx', 'One file each, wet only'),
                tick ('Raw inputs', 'check', 'For virtual soundcheck'))),
        h ('section', { class: 'card' },
            h ('h2', { text: (live0.on ? 'Recording ' : '') + r.files.length + (r.files.length === 1 ? ' file' : ' files') }),
            r.files.length ? h ('ol', { class: 'rec-files' }, r.files.map ((f) => h ('li', { text: f }))) : h ('p', { class: 'hint', text: 'Nothing selected.' })));

    let wasOn = !! live0.on;

    GR.onLive = (l) =>
    {
        const x = l.rec || {};
        if (!! x.on !== wasOn) { wasOn = !! x.on; GR.render(); return; }
        clock.textContent = recClock (x.sec || 0);
        clock.className = 'rec-clock' + (x.on ? ' on' : '');
        line.textContent = x.note ? x.note
            : x.on ? (x.lost > 0 ? 'Drive too slow: ' + x.lost + ' samples dropped from the files (live sound unaffected).' : 'Recording ' + x.files + ' files, 0 dropped.')
            : x.files > 0 ? 'Last: ' + x.files + ' files in ' + x.folder : 'Stopped';
        line.className = 'hint' + (x.note || x.lost > 0 ? ' bad' : '');
        room.textContent = Number (x.free || 0).toFixed (0) + ' GB free, about ' + Number (x.hours || 0).toFixed (1) + ' h';
        (x.pk || []).forEach ((p, i) => { if (meters[i]) meters[i].style.width = Math.round (GR.meterPos (p) * 100) + '%'; });
    };

    GR.onLive ({ rec: live0 });
};

// ---------------------------------------------------------------- Rig

GR.screens.rig = (root) =>
{
    const s = GR.state, r = s.rig;
    const load = h ('b'), late = h ('b');
    const reportBox = h ('textarea', { class: 'report', readonly: true, hidden: true, 'aria-label': 'The report' });

    const copyReport = async () =>
    {
        try
        {
            const text = await GR.report();
            reportBox.hidden = false;
            reportBox.value = text;

            try { await navigator.clipboard.writeText (text); GR.toast ('Report copied.'); }
            catch (e) { reportBox.focus(); reportBox.select(); GR.toast ('Report below: hold, Select all, Copy.'); }
        }
        catch (e) { GR.toast ('The report did not arrive. Check the Wi-Fi.', true); }
    };

    root.append (
        GR.demo ? h ('section', { class: 'card demo-card' },
            h ('h2', { text: 'This is the demo' }),
            h ('p', { text: 'Nothing here is connected to the rig, so nothing can break. The songs, sounds and mixer are a copy of Tom\'s laptop set-up. The looper is a simple stand-in that follows the switches, so the loops are pretend and the meters are made up.' }),
            h ('p', { class: 'hint', text: 'Loading a preset or a mixer set-up only says what would happen, because the demo does not have those files. Everything else can be changed, built, tried and saved, and is forgotten when the demo starts again.' }),
            h ('button', { class: 'btn', onclick: () => GR.ask ('Start the demo again from the beginning?', 'Start again', () => { GR.demoRig.reset(); GR.toast ('The demo is back at the start.'); }) }, 'Start the demo again')) : null,
        h ('section', { class: 'card' },
            h ('h2', { text: 'Rig' }),
            h ('dl', { class: 'facts' },
                h ('dt', { text: 'Audio' }), h ('dd', { text: r.open ? r.device : 'No audio device is open' }),
                h ('dt', { text: 'Rate / buffer' }), h ('dd', { text: r.open ? (r.rate / 1000) + ' kHz, ' + r.buffer + ' samples' : 'not running' }),
                h ('dt', { text: 'RC-600' }), h ('dd', { text: r.pedal ? 'Connected' : 'Not connected' }),
                h ('dt', { text: 'DSP peak' }), h ('dd', {}, load),
                h ('dt', { text: 'Late blocks' }), h ('dd', {}, late),
                h ('dt', { text: 'Build' }), h ('dd', { text: s.build }))),
        r.warnings.map ((w) => h ('div', { class: 'note warn', text: w })),
        h ('section', { class: 'card' },
            h ('h2', { text: 'Report' }),
            h ('button', { class: 'btn primary', onclick: copyReport }, 'Copy report'),
            reportBox));

    GR.onLive = (l) =>
    {
        load.textContent = Math.round ((l.load || 0) * 100) + '%';
        late.textContent = String (l.late || 0);
    };
};

})();


// ---------------------------------------------------------------- the strobe tuner, filling the screen

/* While the tuner is on it lies over everything (docs/tuner.md): a big note and four rows of
 * purple bands that stand still when the string is in tune, slide right when it is sharp and
 * left when it is flat. The rig's own screen draws its bands from the sound itself; a phone
 * hears about the note eight times a second, so here the bands are moved along by how far out
 * the note is (the same speed, a moment behind). */
GR.strobe = (() =>
{
    let box = null, canvas, note, octave, line, foot, flat, sharp, shown = false;
    let turn = 0, slip = 0, target = 0, glow = 0.16, heard = false, last = 0, frame = 0;

    const silence = (tu) => tu.silence || (tu.mutes === false ? 'nothing' : 'guitars');

    const words = (s) =>
    {
        const tu = s.tuner, how = silence (tu), chans = tu.channels || [];
        const quiet = chans.filter ((c) => c.silent).map ((c) => c.name), live = chans.filter ((c) => ! c.silent).map ((c) => c.name);
        if (! (s.mixer && s.mixer.on)) return 'Mixer off: nothing is muted.';
        if (how === 'main') return 'Mutes the main mix. Monitors and break music stay.';
        if (how === 'nothing') return 'Nothing is muted.';
        if (! quiet.length) return 'No guitar channels set: nothing is muted.';
        return (tu.monitors !== false ? 'Muted in PA (live in monitors): ' : 'Muted: ') + quiet.join (', ') + '.' + (live.length ? ' Live: ' + live.join (', ') + '.' : '');
    };

    const build = () =>
    {
        canvas = h ('canvas', { class: 'strobe-bands' });
        note = h ('span', { class: 'strobe-letter', text: '-' });
        octave = h ('sub');
        line = h ('div', { class: 'strobe-line' });
        foot = h ('div', { class: 'strobe-foot' });
        flat = h ('div', { class: 'strobe-side', text: '\u25C0 FLAT' });
        sharp = h ('div', { class: 'strobe-side', text: 'SHARP \u25B6' });
        box = h ('div', { class: 'strobe', role: 'dialog', 'aria-label': 'Tuner' },
            h ('div', { class: 'strobe-head' },
                h ('b', { text: 'TUNER' }),
                h ('span', { class: 'grow' }),
                h ('button', { class: 'strobe-btn', onclick: () => { GR.ui.strobeHidden = true; GR.go ('tools'); follow (GR.live || {}); } }, 'Options'),
                h ('button', { class: 'strobe-btn', onclick: () => { GR.send ({ op: 'tuner.on', on: false }, true).then (() => GR.render()); if (GR.live && GR.live.tuner) GR.live.tuner.on = false; follow (GR.live || {}); GR.render(); } }, 'Done')),
            h ('div', { class: 'strobe-note' }, flat, h ('div', { class: 'strobe-name' }, note, octave), sharp),
            line, canvas, foot);
        document.body.append (box);
    };

    const draw = (time) =>
    {
        if (! shown) return;
        frame = requestAnimationFrame (draw);
        const dt = Math.min (0.1, (time - (last || time)) / 1000);
        last = time;
        slip += (target - slip) * Math.min (1, dt * 10);      // eases between the rig's readings
        if (heard) turn += slip * dt;

        const scale = window.devicePixelRatio || 1, w = canvas.clientWidth, hgt = canvas.clientHeight;
        if (canvas.width !== Math.round (w * scale) || canvas.height !== Math.round (hgt * scale)) { canvas.width = Math.round (w * scale); canvas.height = Math.round (hgt * scale); }
        const g = canvas.getContext ('2d');
        g.setTransform (scale, 0, 0, scale, 0, 0);
        g.clearRect (0, 0, w, hgt);

        const rows = 4, gap = Math.max (4, hgt * 0.035), rowH = (hgt - gap * (rows - 1)) / rows;

        for (let row = 0; row < rows; ++row)
        {
            const times = 1 << row, period = w / ((w < 520 ? 2 : 3) * times), y = hgt - rowH * (row + 1) - gap * row;
            const t = turn * times, offset = (t - Math.floor (t)) * period;
            const blur = GR.clamp ((Math.abs (slip) * times - 7) / 9, 0, 1);
            g.fillStyle = 'rgba(42, 22, 66, .55)';
            g.beginPath(); g.roundRect (0, y, w, rowH, 5); g.fill();
            g.save(); g.clip();
            if (blur > 0) { g.fillStyle = 'rgba(177, 77, 255, ' + (glow * 0.42 * blur) + ')'; g.fillRect (0, y, w, rowH); }
            if (blur < 1)
            {
                g.fillStyle = 'rgba(177, 77, 255, ' + (glow * (1 - blur)) + ')';
                for (let x = offset - period; x < w; x += period) { g.beginPath(); g.roundRect (x, y, period * 0.5, rowH, Math.min (5, period * 0.12)); g.fill(); }
            }
            g.restore();
        }
    };

    const follow = (l) =>
    {
        const t = l.tuner || {}, tu = (GR.state && GR.state.tuner) || {};
        if (! t.on) GR.ui.strobeHidden = false;
        const want = !! t.on && tu.fullScreen !== false && ! GR.ui.strobeHidden;

        if (want !== shown)
        {
            if (want && ! box) build();
            shown = want;
            if (box) box.hidden = ! want;
            cancelAnimationFrame (frame);
            if (want) { last = 0; turn = 0; frame = requestAnimationFrame (draw); }
        }

        if (! shown) return;

        heard = !! t.heard;
        const cents = Number (t.cents) || 0, hz = Number (t.hz) || 0, c = Math.abs (cents), inTune = heard && c < 1;
        target = heard ? hz - hz / Math.pow (2, cents / 1200) : 0;            // how many cycles a second the string slips against the note
        glow = ! heard ? 0.16 : 0.45 + 0.55 * (t.strength === undefined ? 0.8 : t.strength);
        const name = heard ? String (t.name) : '-';
        note.textContent = name.replace (/[-0-9]/g, '');
        octave.textContent = heard ? name.replace (/[^-0-9]/g, '') : '';
        box.className = 'strobe' + (! heard ? ' rest' : inTune ? ' good' : '');
        flat.className = 'strobe-side' + (heard && ! inTune && cents < 0 ? ' on' : '');
        sharp.className = 'strobe-side' + (heard && ! inTune && cents > 0 ? ' on' : '');
        line.textContent = ! heard ? 'Play one string' : inTune ? 'In tune' : (c < 10 ? c.toFixed (1) : Math.round (c)) + (c < 1.05 ? ' cent ' : ' cents ') + (cents < 0 ? 'flat' : 'sharp');
        foot.textContent = GR.state ? words (GR.state) + (t.socket >= 0 ? ' Listening to input ' + (t.socket + 1) + '.' : '') : '';
    };

    return { follow, words, silence };
})();
