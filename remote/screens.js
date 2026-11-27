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
        s.layoutTried ? h ('div', { class: 'note', text: 'This layout is being tried and is not saved yet. Save it on the Build screen.' }) : null,
        h ('div', { class: 'looper-grid' },
            h ('section', { class: 'looper-tracks' }, trackGrid),
            h ('section', { class: 'looper-pads' },
                h ('div', { class: 'pad-head' },
                    h ('span', {}, 'Switches, ', bankLabel),
                    h ('button', { class: 'btn tiny danger-ghost', onclick: () => GR.ask ('Clear every loop on every track? This cannot be undone.', 'Clear all loops',
                        () => GR.send ({ op: 'looper.clearAll' }), { danger: true }) }, 'Clear all loops')),
                h ('div', { class: 'pads' }, [0, 1, 2, 3, 4, 5, 6, 7, 8].map (pad)),
                h ('div', { class: 'pads ctl' }, pad (9), pad (10)),
                h ('p', { class: 'hint', text: 'Touching a switch is the same as a foot on the real one, with a short delay from the Wi-Fi. Hold and double-press work too.' }))));

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
            h ('p', { class: 'hint', text: 'A new song starts with the looper layout and sounds that are loaded now. Give it the program number Songbook Pro sends for it, or leave 0 for none.' }),
            h ('label', { class: 'field' }, 'Name', h ('input', { type: 'text', value: name, oninput: (ev) => { name = ev.target.value; } })),
            h ('label', { class: 'field' }, 'Program number', h ('input', { type: 'number', min: 0, max: 128, value: String (program), oninput: (ev) => { program = Number (ev.target.value) || 0; } })),
            h ('div', { class: 'row end' }, h ('button', { class: 'btn', onclick: close }, 'Cancel'),
                h ('button', { class: 'btn primary', onclick: async () => { if (! name.trim()) return GR.toast ('Give the song a name first.', true); const r = await GR.send ({ op: 'song.new', name: name.trim(), program }); if (r.ok) close(); } }, 'Add the song')),
        ] });
    };

    root.append (
        h ('section', { class: 'card now' },
            h ('div', { class: 'card-title' }, h ('small', { text: 'Loaded now' }), h ('h2', { text: cur ? cur.name : 'No song' })),
            h ('p', { class: 'hint', text: s.songStatus }),
            s.songNote ? h ('p', { class: 'hint', text: s.songNote }) : null,
            s.songProblems.length ? h ('div', { class: 'note warn' }, h ('b', { text: 'The song\'s file has lines the rig could not use:' }), s.songProblems.map ((p) => h ('div', { text: p }))) : null,
            h ('div', { class: 'row wrap' },
                h ('button', { class: 'btn', onclick: () => GR.ask ('Save the sounds that are set now (every chain\'s presets and changes) as this song\'s sounds?', 'Keep them for this song', () => GR.send ({ op: 'song.keep' })) }, 'Keep these sounds for this song'),
                h ('button', { class: 'btn', onclick: () => GR.send ({ op: 'song.again' }) }, 'Put the song\'s sounds back'),
                h ('button', { class: 'btn', onclick: () => GR.go ('build') }, 'Change its looper layout'))),
        h ('div', { class: 'row' },
            h ('input', { type: 'search', class: 'grow', placeholder: 'Find a song', value: GR.ui.songFilter, 'aria-label': 'Find a song',
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
        return out.length ? out : [h ('p', { class: 'hint pad', text: 'No song matches that.' })];
    };

    const list = h ('div', { class: 'songs' }, rows());
    void filter;
    root.append (list, h ('p', { class: 'hint', text: 'The number on the left is the program number Songbook Pro sends to load the song.' }));
};

// ---------------------------------------------------------------- Effects

GR.screens.effects = (root) =>
{
    const fx = GR.state.effects;
    const c = GR.clamp (GR.ui.fxChain, 0, fx.chains.length - 1);
    const chain = fx.chains[c];
    let preset = GR.ui.fxPreset && fx.presets.includes (GR.ui.fxPreset) ? GR.ui.fxPreset : (fx.presets[0] || '');

    root.append (
        h ('div', { class: 'row between' },
            h ('h2', { text: 'Effects' }),
            h ('label', { class: 'inline' }, fx.enabled ? 'The effects are on' : 'The effects are off',
                GR.toggle (fx.enabled, (on) =>
                {
                    const go = () => { fx.enabled = on; GR.send ({ op: 'fx.enabled', on }); GR.render(); };
                    if (on) go(); else GR.ask ('Switch every effects chain off? The instruments go quiet or dry at once.', 'Switch the effects off', go, { danger: true });
                }, 'All effects'))),
        GR.chips (fx.chains.map ((ch, i) => ({ id: i, label: ch.name, cls: ch.on ? '' : 'dim' })), c, (i) => { GR.ui.fxChain = i; GR.render(); }, 'scroll'),
        h ('section', { class: 'card' },
            h ('div', { class: 'row between' },
                h ('div', {}, h ('b', { text: chain.name }), h ('div', { class: 'hint', text: 'Input socket ' + chain.socket + (chain.recipe.length ? '. Made from: ' + chain.recipe.join (', then ') : '') })),
                h ('label', { class: 'inline' }, chain.on ? 'On' : 'Off', GR.toggle (chain.on, (on) => { chain.on = on; GR.send ({ op: 'fx.chainOn', chain: c, on }); GR.render(); }, chain.name))),
            fx.presets.length ? h ('div', { class: 'row' },
                GR.select (fx.presets.map ((p) => ({ id: p, label: p })), preset, (v) => { preset = v; GR.ui.fxPreset = v; }, 'Preset'),
                h ('button', { class: 'btn', onclick: () => GR.ask ('Load the preset "' + preset + '" onto ' + chain.name + '? It replaces what the chain has now.', 'Load it',
                    () => GR.send ({ op: 'fx.preset', chain: c, name: preset })) }, 'Load preset')) : null),
        chain.slots.length === 0 ? h ('p', { class: 'hint pad', text: 'This chain has no effects in it. Effects are added and put in order on the rig\'s own Effects tab.' }) : null,
        h ('div', { class: 'blocks' }, chain.slots.map ((slot, i) => GR.block ('fx' + c + '.' + i + slot.type, slot, {
            setOn: (on) => GR.send ({ op: 'fx.slotOn', chain: c, slot: i, on }, true),
            setParam: (id, value) => GR.sendValue ('fx' + c + '.' + i + id, { op: 'fx.param', chain: c, slot: i, id, value }),
        }))),
        h ('p', { class: 'hint', text: 'Tempo for synced delays: ' + fx.tempo + '. Adding, removing and reordering effects is done on the rig\'s own Effects tab for now.' }));
};

// ---------------------------------------------------------------- Mixer

const mixViews = () =>
{
    const m = GR.state.mixer;
    return [{ id: 'main', label: 'Main mix', cls: 'v-main' }]
        .concat (m.outputs.slice (1).map ((o, i) => ({ id: 'mon' + i, label: o.name, cls: 'v-mon' })))
        .concat (m.fx.map ((f, i) => ({ id: 'fx' + i, label: f.name, cls: 'v-fx' })));
};

function channelSheet (c)
{
    GR.openSheet ({ title: GR.state.mixer.channels[c].name, build: () =>
    {
        const m = GR.state.mixer, ch = m.channels[c];
        const set = (key, value) => { GR.sendValue ('ch' + c + key, { op: 'mix.ch', c, key, value }); };

        return [
            h ('div', { class: 'row between' },
                h ('span', { class: 'hint', text: 'Input socket ' + ch.socket + (ch.linked ? '. The right side of the channel before it.' : ch.link ? '. Linked with the next channel as a stereo pair.' : '') }),
                h ('label', { class: 'inline' }, ch.on ? 'Channel on' : 'Channel off', GR.toggle (ch.on, (on) => { ch.on = on; GR.send ({ op: 'mix.ch', c, key: 'on', value: on }); GR.render(); }))),
            GR.param ({ id: 'trim', label: 'Trim', unit: 'dB', min: -24, max: 24, def: 0, kind: 'knob' }, ch.trim, (v) => { ch.trim = v; set ('trim', v); }),
            GR.param ({ id: 'pan', label: 'Pan (-1 left, 1 right)', unit: '', min: -1, max: 1, def: 0, kind: 'knob' }, ch.pan, (v) => { ch.pan = v; set ('pan', v); }),
            h ('h3', { text: 'Where it goes' }),
            GR.level ('Main mix', ch.fader, (v) => { ch.fader = v; set ('fader', v); }),
            m.outputs.slice (1).map ((o, i) => GR.level (o.name, ch.mon[i], (v) => { ch.mon[i] = v; set ('mon' + i, v); })),
            m.fx.map ((f, i) => GR.level (f.name + (ch.fxSong[i] ? ' (set by the song)' : ''), ch.fx[i], (v) => { ch.fx[i] = v; set ('fx' + i, v); })),
            h ('h3', { text: 'The channel\'s own processing' }),
            h ('div', { class: 'blocks' }, ch.strip.map ((b, i) => GR.block ('ch' + c + '.' + i, b, {
                setOn: (on) => GR.send ({ op: 'mix.param', where: 'ch', i: c, slot: i, id: 'on', value: on }, true),
                setParam: (id, value) => GR.sendValue ('chp' + c + '.' + i + id, { op: 'mix.param', where: 'ch', i: c, slot: i, id, value }),
            }))),
        ];
    } });
}

function fxSheet (f)
{
    GR.openSheet ({ title: GR.state.mixer.fx[f].name, build: () =>
    {
        const m = GR.state.mixer, e = m.fx[f];
        const set = (key, value) => GR.sendValue ('fx' + f + key, { op: 'mix.fx', f, key, value });

        return [
            h ('div', { class: 'row between' },
                h ('span', { class: 'hint', text: e.song ? 'This song has its own settings for this effect.' : 'These settings come from the venue.' }),
                h ('label', { class: 'inline' }, e.on ? 'On' : 'Off', GR.toggle (e.on, (on) => { e.on = on; GR.send ({ op: 'mix.fx', f, key: 'on', value: on }); GR.render(); }))),
            GR.level ('Level back into the main mix', e.ret, (v) => { e.ret = v; set ('ret', v); }),
            m.outputs.slice (1).map ((o, i) => GR.level ('Level to ' + o.name, e.mon[i], (v) => { e.mon[i] = v; set ('mon' + i, v); })),
            h ('h3', { text: 'The effect' }),
            h ('div', { class: 'blocks' }, e.effect.map ((b, i) => GR.block ('mfx' + f + '.' + i, b, {
                setOn: (on) => GR.send ({ op: 'mix.param', where: 'fx', i: f, slot: i, id: 'on', value: on }, true),
                setParam: (id, value) => GR.sendValue ('mfxp' + f + '.' + i + id, { op: 'mix.param', where: 'fx', i: f, slot: i, id, value }),
            }))),
            h ('button', { class: 'btn', onclick: () => GR.ask ('Keep this effect and its settings for the song that is loaded? The venue is not changed.', 'Keep for this song', () => GR.send ({ op: 'mix.keepFx', f })) }, 'Keep this effect for this song'),
            h ('h3', { text: 'Its tone (the venue\'s choice)' }),
            GR.param ({ id: 'lowCut', label: 'Low cut', unit: 'Hz', min: 20, max: 2000, def: 180, kind: 'knob' }, e.lowCut, (v) => { e.lowCut = v; set ('lowCut', v); }),
            GR.param ({ id: 'highCut', label: 'High cut', unit: 'Hz', min: 1000, max: 20000, def: 9000, kind: 'knob' }, e.highCut, (v) => { e.highCut = v; set ('highCut', v); }),
            h ('div', { class: 'blocks' }, e.eq.map ((b, i) => GR.block ('mfxeq' + f + '.' + i, b, {
                setOn: (on) => GR.send ({ op: 'mix.param', where: 'fxeq', i: f, slot: i, id: 'on', value: on }, true),
                setParam: (id, value) => GR.sendValue ('mfxeqp' + f + '.' + i + id, { op: 'mix.param', where: 'fxeq', i: f, slot: i, id, value }),
            }))),
        ];
    } });
}

function outputSheet (o)
{
    GR.openSheet ({ title: GR.state.mixer.outputs[o].name + ' output', build: () =>
    {
        const out = GR.state.mixer.outputs[o];

        return [
            h ('p', { class: 'hint', text: (out.connected ? '' : 'This output has no socket chosen on the rig, so nothing is heard from it. ') + (out.mono ? 'Mono.' : 'Stereo.') + ' These blocks belong to the venue, never to a song.' }),
            GR.level ('Level', out.level, (v) => { out.level = v; GR.sendValue ('out' + o, { op: 'mix.out', o, key: 'level', value: v }); }),
            h ('div', { class: 'blocks' }, out.chain.map ((b, i) => GR.block ('out' + o + '.' + i, b, {
                setOn: (on) => GR.send ({ op: 'mix.param', where: 'out', i: o, slot: i, id: 'on', value: on }, true),
                setParam: (id, value) => GR.sendValue ('outp' + o + '.' + i + id, { op: 'mix.param', where: 'out', i: o, slot: i, id, value }),
            }))),
        ];
    } });
}

function presetSheet()
{
    GR.openSheet ({ title: 'Mixer set-ups', build: () =>
    {
        const m = GR.state.mixer;

        const row = (kind, title, now, changed, list, what) =>
        {
            let pick = list.includes (now) ? now : (list[0] || '');
            return h ('div', { class: 'card' },
                h ('div', { class: 'row between' }, h ('b', { text: title }), h ('span', { class: 'hint', text: (now || 'none loaded') + (changed ? ' (changed since it was loaded)' : '') })),
                h ('p', { class: 'hint', text: what }),
                list.length ? h ('div', { class: 'row' },
                    GR.select (list.map ((n) => ({ id: n, label: n })), pick, (v) => { pick = v; }, title),
                    h ('button', { class: 'btn', onclick: () => GR.ask ('Load the ' + title.toLowerCase() + ' "' + pick + '"?' + (changed ? ' The changes made since the last one was loaded are lost unless they were saved on the rig.' : ''), 'Load it',
                        () => GR.send ({ op: 'mix.preset', kind, name: pick })) }, 'Load'))
                    : h ('p', { class: 'hint', text: 'None saved yet. They are saved on the rig\'s own Mixer tab.' }));
        };

        return [
            row ('act', 'Act', m.act, m.actChanged, m.acts, 'Who is playing: the channels, their processing and the main mix.'),
            row ('venue', 'Venue', m.venue, m.venueChanged, m.venues, 'The room and the PA: the output chain and the four effects. A venue fades in and never jumps louder.'),
            row ('monitors', 'Monitors', m.monitors, m.monitorsChanged, m.monitorSets, 'What each monitor hears.'),
            h ('p', { class: 'hint', text: 'Saving a set-up is done on the rig\'s own Mixer tab for now.' }),
        ];
    } });
}

GR.screens.mixer = (root) =>
{
    const m = GR.state.mixer;

    if (! m.on)
    {
        root.append (h ('section', { class: 'card' },
            h ('h2', { text: 'The mixer is off' }),
            h ('p', { text: 'With the mixer off the rig sounds exactly as it did before the mixer existed, and the real mixer does the mixing.' }),
            h ('button', { class: 'btn primary', onclick: () => GR.ask ('Switch the mixer on? What you hear changes at once: the rig starts mixing the channels to its outputs.', 'Switch the mixer on',
                () => GR.send ({ op: 'mix.on', on: true })) }, 'Switch the mixer on')));
        return;
    }

    const views = mixViews();
    if (! views.some ((v) => v.id === GR.ui.mixView)) GR.ui.mixView = 'main';
    const view = GR.ui.mixView, kind = view.replace (/\d+$/, ''), n = Number (view.replace (/\D+/g, '')) || 0;
    const meters = { ch: [], fx: [], out: null, gr: [] };

    const strip = (o) =>
    {
        const f = GR.fader (o.db, o.change, { cls: o.cls });
        if (o.meter) o.meter (f.meter);
        const gr = h ('div', { class: 'gr', title: 'How much the compressors are turning it down' }, h ('i'));
        if (o.gr) o.gr (gr.firstChild);

        return h ('div', { class: 'strip' + (o.dim ? ' dim' : '') + (o.cls ? ' ' + o.cls : '') },
            h ('button', { class: 'strip-name', onclick: o.open, title: 'Open ' + o.name }, o.name, o.sub ? h ('small', { text: o.sub }) : null),
            o.gr ? gr : h ('div', { class: 'gr none' }),
            f,
            o.mute ? h ('button', { class: 'mute' + (o.muted ? ' on' : ''), onclick: o.mute }, 'Mute') : h ('div', { class: 'mute none' }));
    };

    const strips = [];

    m.channels.forEach ((ch, c) =>
    {
        if (! ch.on && ! GR.ui.mixAll) return;
        const key = kind === 'main' ? 'fader' : kind === 'mon' ? 'mon' + n : 'fx' + n;
        const db = kind === 'main' ? ch.fader : kind === 'mon' ? ch.mon[n] : ch.fx[n];

        strips.push (strip ({
            name: ch.name, db, dim: ! ch.on || ch.mute,
            sub: ! ch.on ? 'off' : kind === 'fx' && ch.fxSong[n] ? 'song' : ch.linked ? 'right side' : '',
            change: (v) => { if (kind === 'main') ch.fader = v; else if (kind === 'mon') ch.mon[n] = v; else ch.fx[n] = v; GR.sendValue ('ch' + c + key, { op: 'mix.ch', c, key, value: v }); },
            open: () => channelSheet (c),
            meter: (el) => { meters.ch[c] = el; },
            gr: (el) => { meters.gr[c] = el; },
            mute: () => { ch.mute = ! ch.mute; GR.send ({ op: 'mix.ch', c, key: 'mute', value: ch.mute }, true); GR.render(); },
            muted: ch.mute,
        }));
    });

    if (kind !== 'fx')
        m.fx.forEach ((e, f) =>
        {
            const key = kind === 'main' ? 'ret' : 'mon' + n;
            strips.push (strip ({
                name: e.name, cls: 'fxret', db: kind === 'main' ? e.ret : e.mon[n], dim: ! e.on, sub: e.on ? 'effect' : 'off',
                change: (v) => { if (kind === 'main') e.ret = v; else e.mon[n] = v; GR.sendValue ('fx' + f + key, { op: 'mix.fx', f, key, value: v }); },
                open: () => fxSheet (f),
                meter: (el) => { meters.fx[f] = el; },
            }));
        });
    else
    {
        const e = m.fx[n];
        strips.push (strip ({
            name: e.name, cls: 'fxret', db: e.ret, dim: ! e.on, sub: 'level back',
            change: (v) => { e.ret = v; GR.sendValue ('fx' + n + 'ret', { op: 'mix.fx', f: n, key: 'ret', value: v }); },
            open: () => fxSheet (n),
            meter: (el) => { meters.fx[n] = el; },
        }));
    }

    if (kind !== 'fx')
    {
        const o = kind === 'main' ? 0 : n + 1, out = m.outputs[o];
        strips.push (strip ({
            name: out.name, cls: 'master', db: out.level, dim: out.mute, sub: out.connected ? 'output' : 'no socket',
            change: (v) => { out.level = v; GR.sendValue ('out' + o, { op: 'mix.out', o, key: 'level', value: v }); },
            open: () => outputSheet (o),
            meter: (el) => { meters.out = { el, o }; },
            mute: () => { out.mute = ! out.mute; GR.send ({ op: 'mix.out', o, key: 'mute', value: out.mute }, true); GR.render(); },
            muted: out.mute,
        }));
    }

    const limit = h ('span', { class: 'tag' });
    const hidden = m.channels.filter ((ch) => ! ch.on).length;
    const viewNow = views.find ((v) => v.id === view);

    root.append (
        h ('div', { class: 'row between mix-top' },
            h ('button', { class: 'btn', onclick: presetSheet }, 'Set-ups: ' + (m.act || 'no act') + ' / ' + (m.venue || 'no venue')),
            h ('div', { class: 'row' }, limit,
                h ('button', { class: 'btn tiny ghost', onclick: () => GR.ask ('Switch the mixer off? The rig goes back to sounding exactly as it did without a mixer, at once.', 'Switch the mixer off',
                    () => GR.send ({ op: 'mix.on', on: false }), { danger: true }) }, 'Mixer off'))),
        m.warnings.map ((w) => h ('div', { class: 'note warn', text: w })),
        h ('div', { class: 'sends' }, h ('span', { class: 'hint', text: 'The faders are sending to' }), GR.chips (views, view, (id) => { GR.ui.mixView = id; GR.render(); }, 'scroll')),
        h ('div', { class: 'strips ' + viewNow.cls, 'data-keep-scroll': 'strips' }, strips),
        h ('div', { class: 'row between' },
            h ('p', { class: 'hint', text: 'Drag a fader up or down from anywhere on it. Tap a name to open the channel.' }),
            hidden ? h ('button', { class: 'btn tiny ghost', onclick: () => { GR.ui.mixAll = ! GR.ui.mixAll; GR.render(); } }, GR.ui.mixAll ? 'Hide channels that are off' : 'Show ' + hidden + ' channels that are off') : null));

    GR.onLive = (l) =>
    {
        if (! l.mix) return;
        const set = (el, peak) => { if (el) { el.style.height = (GR.meterPos (peak) * 100) + '%'; el.className = peak >= 0.98 ? 'clip' : peak > 0.5 ? 'hot' : ''; } };
        meters.ch.forEach ((el, c) => set (el, l.mix.ch[c]));
        meters.fx.forEach ((el, f) => set (el, l.mix.fx[f]));
        meters.gr.forEach ((el, c) => { if (el) el.style.width = GR.clamp (-(l.mix.gr[c] || 0) / 18, 0, 1) * 100 + '%'; });
        if (meters.out) set (meters.out.el, Math.max (l.mix.out[meters.out.o][0], l.mix.out[meters.out.o][1]));
        const lim = l.mix.limit || 0;
        limit.textContent = l.mix.guard ? 'The loudness guard cut in' : lim < -0.5 ? 'Limiter ' + lim.toFixed (1) + ' dB' : 'Limiter resting';
        limit.className = 'tag' + (l.mix.guard ? ' bad' : lim < -0.5 ? ' warn' : ' dim');
    };
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

            try { await navigator.clipboard.writeText (text); GR.toast ('The report is copied. Paste it into the chat.'); }
            catch (e) { reportBox.focus(); reportBox.select(); GR.toast ('The report is below. Press and hold on it, choose Select all, then Copy.'); }
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
            h ('h2', { text: 'The rig' }),
            h ('dl', { class: 'facts' },
                h ('dt', { text: 'Audio' }), h ('dd', { text: r.open ? r.device : 'No audio device is open' }),
                h ('dt', { text: 'Rate and buffer' }), h ('dd', { text: r.open ? (r.rate / 1000) + ' kHz, ' + r.buffer + ' samples' : 'not running' }),
                h ('dt', { text: 'Foot switches (RC-600)' }), h ('dd', { text: r.pedal ? 'Connected' : 'Not connected' }),
                h ('dt', { text: 'Busiest moment' }), h ('dd', {}, load),
                h ('dt', { text: 'Late blocks' }), h ('dd', {}, late),
                h ('dt', { text: 'Build' }), h ('dd', { text: s.build }))),
        r.warnings.map ((w) => h ('div', { class: 'note warn', text: w })),
        h ('section', { class: 'card' },
            h ('h2', { text: 'Report' }),
            h ('p', { class: 'hint', text: 'The same report as the Copy report button on the rig. Paste it into the chat when something needs looking at.' }),
            h ('button', { class: 'btn primary', onclick: copyReport }, 'Copy report'),
            reportBox),
        h ('p', { class: 'hint', text: 'This screen is a page served by the rig itself. Anyone on the same Wi-Fi who knows the address can open it, so use it on the rig\'s own network.' }));

    GR.onLive = (l) =>
    {
        load.textContent = Math.round ((l.load || 0) * 100) + '% of the time the rig has for each block';
        late.textContent = String (l.late || 0);
    };
};

})();
