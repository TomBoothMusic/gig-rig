/* The remote screen: the looper arrangement builder (docs/remote.md, "Building a looper layout").
 *
 * The builder works on a draft: a copy of the loaded song's layout (state.layout, the numbers
 * described in src/LooperLayout.h). Nothing reaches the looper until Try it or Save, and the
 * rig checks the layout again when it arrives (layout::fromJson).
 *
 * Left out on purpose (Tom, 27 Nov 2026): reverse, one-shot, fades, replace overdub, half speed.
 * A layout that already has a fade keeps it; the builder just does not offer to add one.
 */
'use strict';

(() =>
{
const h = GR.h;

const kinds = [
    { id: 0, label: 'Record, play, overdub' },
    { id: 1, label: 'Play or stop' },
    { id: 2, label: 'Stop' },
    { id: 3, label: 'Clear' },
    { id: 5, label: 'Undo, then redo' },
];

/** "Track 2: Play or stop", "All start or stop" for a link's target number. */
GR.linkTarget = (tgt) =>
{
    if (tgt === 80) return 'All start or stop';
    const current = tgt >= 66 && tgt < 77;
    const track = current ? -1 : Math.floor (tgt / 11), kind = current ? tgt - 66 : tgt % 11;
    const k = kinds.find ((x) => x.id === kind);
    if (! k || (! current && track > 5)) return 'An RC-600 action this looper does not copy (number ' + tgt + ')';
    return (current ? 'Current track' : 'Track ' + (track + 1)) + ': ' + k.label;
};

/** False for a target the looper does not copy (kept in the layout so nothing from the RC-600 is lost). */
GR.linkKnown = (tgt) => ! GR.linkTarget (tgt).startsWith ('An RC-600');

const linkSource = (src) =>
{
    if (src >= 0 && src <= 5)       return 'When track ' + (src + 1) + ' starts to record';
    if (src >= 6 && src <= 11)      return 'When track ' + (src - 5) + ' starts to play';
    if (src === 42 || src === 43)   return 'When ' + (src === 42 ? 'CTL3' : 'CTL4') + ' is pressed';
    if (src >= 13 && src <= 30)     return 'When switch ' + ((src - 13) % 9 + 1) + ' in bank ' + (Math.floor ((src - 13) / 9) + 1) + ' is pressed';
    return 'RC-600 source ' + src;
};

const sameLayout = (a, b) => JSON.stringify (a) === JSON.stringify (b);

/** The draft, started again from the loaded layout whenever that changes and the draft has no changes of its own. */
function draft()
{
    const s = GR.state, ui = GR.ui;
    const loadedKey = s.current + '|' + JSON.stringify (s.layout);

    if (! ui.draft || (! ui.draftDirty && ui.draftKey !== loadedKey))
    {
        ui.draft = GR.copy (s.layout);
        ui.draftKey = loadedKey;
        ui.draftDirty = false;
        ui.draftFrom = (s.songs[s.current] || {}).name || '';
        ui.draftTempo = s.layout.tempo >= 30;
    }

    return ui.draft;
}

const touch = () => { GR.ui.draftDirty = ! sameLayout (GR.ui.draft, GR.state.layout); };
const change = (fn) => { fn (GR.ui.draft); touch(); GR.render(); };

// ---------------------------------------------------------------- tracks

function trackCard (d, i)
{
    const t = d.tracks[i], s = GR.state;
    const lanes = GR.lanesFor (t.inputs);
    const lengths = [0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32];
    if (! lengths.includes (t.bars)) lengths.push (t.bars);

    const stops = [{ id: 0, label: 'At once' }, { id: 2, label: 'At the end of its loop' }];
    if (t.stop === 1) stops.splice (1, 0, { id: 1, label: 'Fades out (kept from the old layout)' });

    return h ('section', { class: 'card track-card' },
        h ('div', { class: 'row between' },
            h ('h3', {}, 'Track ' + (i + 1)),
            h ('span', { class: 'tag' + (lanes > 1 ? '' : ' dim'), text: lanes === 0 ? 'records nothing' : lanes === 1 ? '1 lane' : Math.min (lanes, s.maxLanes) + ' lanes' })),
        h ('div', { class: 'label', text: 'What it records' }),
        h ('div', { class: 'chips' }, s.sources.map ((src) =>
            h ('button', { class: 'chip' + (t.inputs & src.bit ? ' on' : ''), onclick: () => change ((x) => { x.tracks[i].inputs ^= src.bit; }) }, src.name))),
        lanes > 1 ? h ('p', { class: 'hint', text: 'Each source on its own socket gets its own lane: it loops side by side with the others and comes back on its own mixer channel.' }) : null,
        lanes > s.maxLanes ? h ('div', { class: 'note', text: 'This track listens to ' + lanes + ' sockets and a track keeps ' + s.maxLanes + ' lanes, so the last sources share the last lane: they loop together and come back on one mixer channel.' }) : null,
        h ('div', { class: 'grid2' },
            h ('label', { class: 'field' }, 'Length',
                GR.select (lengths.map ((b) => ({ id: b, label: b === 0 ? 'Free (as long as you play)' : b + (b === 1 ? ' bar' : ' bars') })), t.bars, (v) => change ((x) => { x.tracks[i].bars = Number (v); }), 'Length')),
            h ('label', { class: 'field' }, 'When started again it comes back',
                GR.select ([{ id: 0, label: 'In place (as if it never stopped)' }, { id: 1, label: 'From its first bar' }, { id: 2, label: 'From its first note' }], t.ret, (v) => change ((x) => { x.tracks[i].ret = Number (v); }), 'Comes back')),
            h ('label', { class: 'field' }, 'When stopped it stops',
                GR.select (stops, t.stop, (v) => change ((x) => { x.tracks[i].stop = Number (v); }), 'Stops')),
            h ('label', { class: 'field' }, 'Playback level (100 is as recorded)',
                h ('input', { type: 'number', min: 0, max: 200, step: 1, value: String (t.level),
                    onchange: (ev) => change ((x) => { x.tracks[i].level = GR.clamp (Math.round (Number (ev.target.value) || 0), 0, 200); }) }))));
}

// ---------------------------------------------------------------- switches

const switchSource = (bank, sw) => sw >= 9 ? 42 + (sw - 9) : 13 + bank * 9 + sw;

function targetPicker (tgt, set)
{
    const all = tgt === 80;
    const current = tgt >= 66 && tgt < 77;
    const track = all ? -2 : current ? -1 : Math.floor (tgt / 11);
    const kind = all ? 0 : current ? tgt - 66 : tgt % 11;
    const make = (tr, k) => tr === -2 ? 80 : tr === -1 ? 66 + k : tr * 11 + k;
    const who = [0, 1, 2, 3, 4, 5].map ((n) => ({ id: n, label: 'Track ' + (n + 1) })).concat ([{ id: -1, label: 'Current track' }, { id: -2, label: 'All tracks: start or stop' }]);

    return [
        GR.select (who, track, (v) => set (make (Number (v), kind)), 'Which track'),
        all ? null : GR.select (kinds, kind, (v) => set (make (track, Number (v))), 'What it does'),
    ];
}

function switchSheet (bank, sw)
{
    const name = sw < 9 ? 'Switch ' + (sw + 1) + ', bank ' + (bank + 1) : sw === 9 ? 'CTL3' : 'CTL4';

    GR.openSheet ({ title: name, build: () =>
    {
        const d = draft(), s = GR.state;
        const src = switchSource (bank, sw);
        const code = sw < 9 ? d.banks[bank][sw] : 0;
        const now = GR.describe (code);
        const extras = d.links.map ((l, i) => ({ l, i })).filter ((x) => x.l.src === src);
        const fnId = now.fn ? s.functions.indexOf (now.fn) : -1;
        const track = now.track === null ? 0 : now.track;
        const setFn = (id, tr) => change ((x) => { const fn = s.functions[id]; x.banks[bank][sw] = fn.words === 'nothing' ? 0 : GR.codeFor (fn, tr); });
        const use = (s.switches[sw] || {}).effects;

        return [
            sw < 9 ? [
                h ('div', { class: 'label', text: 'What this switch does' }),
                h ('label', { class: 'field' }, 'Action',
                    GR.select ((fnId < 0 ? [{ id: -1, label: now.long }] : []).concat (s.functions.map ((f, i) => ({ id: i, label: f.label }))), fnId, (v) => { if (Number (v) >= 0) setFn (Number (v), track); }, 'Action')),
                now.fn && now.fn.index >= 0 ? h ('label', { class: 'field' }, 'On which track',
                    GR.select ([0, 1, 2, 3, 4, 5].map ((n) => ({ id: n, label: 'Track ' + (n + 1) + ' (' + GR.sourceNames (d.tracks[n].inputs) + ')' })).concat ([{ id: -1, label: 'The current track (the one last used)' }]),
                        track, (v) => setFn (fnId, Number (v)), 'Track')) : null,
                now.fn && now.fn.index === 26 && now.track === -1 ? h ('p', { class: 'hint', text: 'For the current track the nearest the RC-600 has is play or stop with undo on a hold, so that is what is saved.' }) : null,
            ] : h ('p', { class: 'hint', text: 'The CTL switches have no action of their own in a bank. Give them one below; it works in both banks.' }),
            h ('div', { class: 'label', text: sw < 9 ? 'Also, on the same press' : 'What this switch does' }),
            extras.map (({ l, i }) => h ('div', { class: 'row link-row' },
                targetPicker (l.tgt, (tgt) => change ((x) => { x.links[i].tgt = tgt; })),
                h ('button', { class: 'btn tiny danger-ghost', onclick: () => change ((x) => { x.links.splice (i, 1); }) }, 'Remove'))),
            d.links.length < s.maxLinks
                ? h ('button', { class: 'btn', onclick: () => change ((x) => { x.links.push ({ src, tgt: 1 }); }) }, sw < 9 ? 'Add another action to this press' : 'Add an action')
                : h ('p', { class: 'hint', text: 'A layout can hold ' + s.maxLinks + ' extra actions and this one is full.' }),
            use ? h ('div', { class: 'note', text: 'The effects also use this switch: ' + use + '. That is set in the song\'s sounds, not here.' }) : null,
        ];
    } });
}

function switchesTab (d)
{
    const bank = GR.ui.buildBank;

    const tile = (sw) =>
    {
        const code = sw < 9 ? d.banks[bank][sw] : 0;
        const what = GR.describe (code);
        const extras = d.links.filter ((l) => l.src === switchSource (bank, sw) && GR.linkKnown (l.tgt));
        const idle = ! what.short && ! extras.length;

        return h ('button', { class: 'pad build' + (idle ? ' idle' : ''), onclick: () => switchSheet (bank, sw) },
            h ('span', { class: 'pad-n', text: sw < 9 ? String (sw + 1) : sw === 9 ? 'CTL3' : 'CTL4' }),
            h ('span', { class: 'pad-who', text: what.who || '' }),
            h ('span', { class: 'pad-fn', text: what.short || (extras.length ? '' : 'Nothing') }),
            extras.map ((l) => h ('span', { class: 'pad-fx', text: '+ ' + GR.linkTarget (l.tgt) })));
    };

    return [
        h ('div', { class: 'row between' },
            GR.chips ([{ id: 0, label: 'Bank 1' }, { id: 1, label: 'Bank 2' }], bank, (b) => { GR.ui.buildBank = b; GR.render(); }),
            h ('button', { class: 'btn tiny ghost', onclick: () => GR.ask ('Copy every switch of bank ' + (bank + 1) + ' onto bank ' + (2 - bank) + '?', 'Copy the bank', () => change ((x) => { x.banks[1 - bank] = x.banks[bank].slice(); })) }, 'Copy to bank ' + (2 - bank))),
        h ('p', { class: 'hint', text: 'Tap a switch to choose what it does. The nine are laid out as on the RC-600.' }),
        h ('div', { class: 'pads' }, [0, 1, 2, 3, 4, 5, 6, 7, 8].map (tile)),
        h ('div', { class: 'pads ctl' }, tile (9), tile (10)),
    ];
}

// ---------------------------------------------------------------- track links

function linksTab (d)
{
    const s = GR.state;
    const mine = d.links.map ((l, i) => ({ l, i })).filter ((x) => x.l.src <= 11);
    const others = d.links.map ((l, i) => ({ l, i })).filter ((x) => x.l.src > 11);
    const whens = [0, 1, 2, 3, 4, 5].map ((n) => ({ id: n, label: 'When track ' + (n + 1) + ' starts to record' }))
        .concat ([0, 1, 2, 3, 4, 5].map ((n) => ({ id: 6 + n, label: 'When track ' + (n + 1) + ' starts to play' })));

    return [
        h ('p', { class: 'hint', text: 'A link makes one track do something when another starts. For example: when track 1 starts to play, track 3 starts to record.' }),
        mine.length === 0 ? h ('p', { class: 'hint pad', text: 'This layout has no links between tracks.' }) : null,
        mine.map (({ l, i }) => h ('div', { class: 'card link-card' },
            GR.select (whens, l.src, (v) => change ((x) => { x.links[i].src = Number (v); }), 'When'),
            h ('div', { class: 'row link-row' },
                targetPicker (l.tgt, (tgt) => change ((x) => { x.links[i].tgt = tgt; })),
                h ('button', { class: 'btn tiny danger-ghost', onclick: () => change ((x) => { x.links.splice (i, 1); }) }, 'Remove')))),
        d.links.length < s.maxLinks
            ? h ('button', { class: 'btn', onclick: () => change ((x) => { x.links.push ({ src: 6, tgt: 22 }); }) }, 'Add a link between tracks')
            : h ('p', { class: 'hint', text: 'A layout can hold ' + s.maxLinks + ' links and extra switch actions together, and this one is full.' }),
        others.length ? h ('div', { class: 'card' },
            h ('div', { class: 'label', text: 'Extra actions on switches (changed on the Switches page)' }),
            others.map (({ l }) => h ('p', { class: 'hint', text: linkSource (l.src) + ': ' + GR.linkTarget (l.tgt) }))) : null,
        h ('p', { class: 'hint', text: d.links.length + ' of ' + s.maxLinks + ' used.' }),
    ];
}

// ---------------------------------------------------------------- the song's options

function optionsTab (d)
{
    return [
        h ('section', { class: 'card' },
            h ('label', { class: 'field' }, 'Beats in a bar',
                GR.select ([2, 3, 4, 5, 6, 7, 8, 9, 12].concat ([2, 3, 4, 5, 6, 7, 8, 9, 12].includes (d.beats) ? [] : [d.beats]).map ((b) => ({ id: b, label: String (b) })), d.beats, (v) => change ((x) => { x.beats = Number (v); }), 'Beats')),
            h ('label', { class: 'field' }, 'After recording, a track goes to',
                GR.select ([{ id: 1, label: 'Play, then overdub on the next press' }, { id: 0, label: 'Overdub, then play on the next press' }], d.recThenPlay ? 1 : 0, (v) => change ((x) => { x.recThenPlay = v === '1'; }), 'Order')),
            h ('label', { class: 'field' }, 'A new track, once the first loop is going',
                GR.select ([{ id: 1, label: 'Waits for the next bar to start recording' }, { id: 0, label: 'Starts recording at once' }], d.quantise ? 1 : 0, (v) => change ((x) => { x.quantise = v === '1'; }), 'New track'))),
        h ('section', { class: 'card' },
            h ('label', { class: 'inline between' }, 'Save a tempo with the song',
                GR.toggle (!! GR.ui.draftTempo, (on) => { GR.ui.draftTempo = on; if (on && ! (d.tempo >= 30)) d.tempo = 120; touch(); GR.render(); }, 'Save a tempo')),
            GR.ui.draftTempo ? h ('label', { class: 'field' }, 'Tempo (beats a minute)',
                h ('input', { type: 'number', min: 30, max: 300, step: 0.5, value: String (d.tempo),
                    onchange: (ev) => change ((x) => { x.tempo = GR.clamp (Number (ev.target.value) || 120, 30, 300); }) })) : null,
            h ('p', { class: 'hint', text: 'The tempo does not make a click. It helps the looper count how many bars the first loop is, so a loop of two bars is not taken for one.' })),
    ];
}

// ---------------------------------------------------------------- the screen

function saveAsSheet()
{
    let name = '', program = 0;

    GR.openSheet ({ small: true, title: 'Save as a new song', build: (close) => [
        h ('p', { class: 'hint', text: 'The new song gets this layout and the sounds that are loaded now. Give it the program number Songbook Pro sends for it, or leave 0 for none.' }),
        h ('label', { class: 'field' }, 'Name', h ('input', { type: 'text', value: name, oninput: (ev) => { name = ev.target.value; } })),
        h ('label', { class: 'field' }, 'Program number', h ('input', { type: 'number', min: 0, max: 128, value: String (program), oninput: (ev) => { program = Number (ev.target.value) || 0; } })),
        h ('div', { class: 'row end' }, h ('button', { class: 'btn', onclick: close }, 'Cancel'),
            h ('button', { class: 'btn primary', onclick: async () =>
            {
                if (! name.trim()) return GR.toast ('Give the song a name first.', true);
                const r = await GR.send ({ op: 'layout.saveAs', name: name.trim(), program, layout: GR.ui.draft, withTempo: !! GR.ui.draftTempo });
                if (r.ok) { GR.ui.draftDirty = false; close(); }
            } }, 'Save the new song')),
    ] });
}

function copySheet()
{
    GR.openSheet ({ title: 'Start from another song\'s layout', build: (close) =>
        [h ('p', { class: 'hint', text: 'This copies that song\'s looper layout into the draft. The other song is not changed, and nothing is saved until you save.' }),
         h ('div', { class: 'songs' }, GR.state.songs.map ((song, i) => h ('button', { class: 'song', onclick: async () =>
         {
             const r = await GR.send ({ op: 'layout.of', index: i }, true);
             if (! r.ok || ! r.layout) return;
             const keep = GR.ui.draft.name;
             GR.ui.draft = GR.copy (r.layout);
             GR.ui.draft.name = keep;
             GR.ui.draftTempo = r.layout.tempo >= 30;
             touch();
             close();
             GR.render();
             GR.toast ('The draft now has the layout of "' + song.name + '".');
         } }, h ('span', { class: 'song-pc', text: song.program > 0 ? String (song.program) : '' }), h ('span', { class: 'song-title', text: song.name }))))] });
}

GR.screens.build = (root) =>
{
    const s = GR.state, d = draft(), ui = GR.ui;
    const cur = s.songs[s.current] || { name: 'No song' };
    const busy = () => GR.live && GR.live.looper.tracks.some ((t) => t.a || t.s !== 0);
    const guard = (text, yes, go) => busy() ? GR.ask (text + ' The loops you have now are cleared.', yes, go, { danger: true }) : go();
    const payload = () => ({ layout: ui.draft, withTempo: !! ui.draftTempo });

    const tryIt = () => guard ('Try this layout on the looper now?', 'Try it', async () => { const r = await GR.send (Object.assign ({ op: 'layout.try' }, payload())); if (r.ok) GR.toast ('The looper has the draft. Nothing is saved yet.'); });
    const save = () => GR.ask ('Save this layout into "' + cur.name + '"?' + (cur.builtIn && ! cur.ownLayout ? ' The song then uses it in place of the layout copied from the RC-600, which stays in the rig and can be put back.' : ''), 'Save it',
        async () => { const r = await GR.send (Object.assign ({ op: 'layout.save' }, payload())); if (r.ok) ui.draftDirty = false; });

    const pages = [{ id: 'tracks', label: 'Tracks' }, { id: 'switches', label: 'Switches' }, { id: 'links', label: 'Links' }, { id: 'options', label: 'Song options' }];
    if (! pages.some ((p) => p.id === ui.buildTab)) ui.buildTab = 'tracks';

    root.append (
        h ('section', { class: 'card now' },
            h ('div', { class: 'card-title' }, h ('small', { text: 'Looper layout for' }), h ('h2', { text: cur.name })),
            h ('p', { class: 'hint', text: ui.draftDirty ? 'The draft has changes that are not saved' + (ui.draftFrom !== cur.name ? ' (it was started on "' + ui.draftFrom + '")' : '') + '.'
                                                   : s.layoutTried ? 'The looper is trying a layout that is not saved yet.'
                                                   : s.layoutOwn ? 'This song has its own layout, saved in its song file.' : 'This song uses the layout "' + s.layout.name + '" that is built into the rig.' }),
            h ('div', { class: 'row wrap' },
                h ('button', { class: 'btn primary', onclick: tryIt }, 'Try it'),
                h ('button', { class: 'btn', onclick: save }, 'Save to this song'),
                h ('button', { class: 'btn', onclick: saveAsSheet }, 'Save as a new song'),
                h ('button', { class: 'btn ghost', onclick: copySheet }, 'Start from another song'),
                ui.draftDirty ? h ('button', { class: 'btn ghost', onclick: () => GR.ask ('Throw away the changes in the draft?', 'Throw them away', () => { ui.draft = null; ui.draftDirty = false; GR.render(); }, { danger: true }) }, 'Undo my changes') : null,
                s.layoutOwn || s.layoutTried ? h ('button', { class: 'btn danger-ghost', onclick: () => guard (s.layoutOwn ? 'Take this song\'s own layout out of its file, so it goes back to the one it had before?' : 'Stop trying and go back to the saved layout?', 'Go back',
                    async () => { const r = await GR.send ({ op: 'layout.forget' }); if (r.ok) { ui.draft = null; ui.draftDirty = false; } }) }, s.layoutOwn ? 'Go back to the built-in layout' : 'Stop trying') : null)),
        GR.chips (pages, ui.buildTab, (id) => { ui.buildTab = id; GR.render(); }, 'scroll sub'),
        h ('div', { class: 'build-page build-' + ui.buildTab },
            ui.buildTab === 'tracks' ? h ('div', { class: 'track-cards' }, d.tracks.map ((t, i) => trackCard (d, i)))
            : ui.buildTab === 'switches' ? switchesTab (d)
            : ui.buildTab === 'links' ? linksTab (d)
            : optionsTab (d)),
        h ('details', { class: 'card' }, h ('summary', { text: 'How this layout is written in the song file' }),
            h ('pre', { class: 'words', text: ui.draftDirty ? 'Save or try the draft to see its lines.' : (s.layoutWords || '') })));
};

})();
