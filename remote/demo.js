/* The remote screen's demo: a pretend rig kept inside the page (docs/remote.md, "The demo").
 *
 * It answers the same questions as the real rig (state, live, command) from a copy of a real
 * state captured from the app (remote/demo-data.js, made by tools/remote_demo.py), so the demo
 * shows Tom's real songs, sounds and mixer and cannot fall out of step with the real page.
 *
 * The looper here is a simple stand-in, NOT the rig's looper: it follows the switches closely
 * enough to try a layout (first loop sets the bar, a new track waits for the bar, one recording
 * at a time, holds and double presses, links, banks), with pretend loops and made-up meters.
 * Timing questions are answered by the real looper and its self-test, never by this.
 */
'use strict';

GR.makeDemoRig = (data) =>
{
    const copy = GR.copy;
    const now = () => performance.now();
    const rig = { changed: true, state: null };
    let lo, perSong, commands = 0, bm, tuner;

    // Track states, as in the rig: 0 empty, 1 armed, 2 recording, 3 closing, 4 playing, 5 overdubbing, 6 stopping, 7 stopped
    const freshLooper = () => ({ bank: 0, cur: 0, bar: 0, t0: 0, tracks: Array.from ({ length: 6 }, () => ({ s: 0, a: false, d: false, u: false, len: 0, start: 0, at: 0, next: 4, last: 0, downAt: 0, hold: 0 })) });

    rig.reset = () =>
    {
        rig.state = copy (data.state);
        rig.state.demo = true;
        rig.state.build = 'the demo (a copy of ' + data.state.build + ')';
        rig.state.rig = { open: true, device: 'Pretend audio (the demo)', rate: 96000, buffer: 64, pedal: false, warnings: [] };
        perSong = copy (data.songs);
        lo = freshLooper();
        // The demo has no files, so it pretends three mixes are in the break music folder.
        rig.state.breakMusic.mixes = ['Dinner jazz', 'Sunset chill', 'Dance floor warm-up'];
        rig.state.breakMusic.mix = '';
        bm = { playing: false, up: false, fade: 0, at: now(), pos: 0, knobOnBreak: false, venueAt: -1, faded: 0 };
        tuner = { on: false, since: 0 };
        rig.changed = true;
    };

    const st = () => rig.state;
    const lay = () => st().layout;
    const touch = () => { rig.changed = true; };
    const ok = (note) => ({ ok: true, note: note || '' });
    const fail = (error) => ({ ok: false, error });

    // ------------------------------------------------------------ the pretend looper

    const running = () => lo.tracks.some ((t) => t.s === 2 || t.s === 3 || t.s === 4 || t.s === 5 || t.s === 6);
    const nextBar = (t) => lo.bar > 0 ? lo.t0 + Math.ceil ((t - lo.t0) / lo.bar - 0.0001) * lo.bar : t;

    const fire = (src, depth) =>
    {
        if (depth > 3) return;
        lay().links.filter ((l) => l.src === src).forEach ((l) => target (l.tgt, depth + 1));
    };

    const setState = (i, s, depth) =>
    {
        const t = lo.tracks[i], was = t.s;
        t.s = s;
        if ((s === 2 || s === 5) && was !== 2 && was !== 5) fire (i, depth || 0);
        if (s === 4 && was !== 4) fire (6 + i, depth || 0);
    };

    const startRecording = (i, at, depth) =>
    {
        const t = lo.tracks[i];
        t.start = at; t.a = false; t.d = false; t.u = false;
        setState (i, 2, depth);
    };

    const closeRecording = (i, at, depth) =>
    {
        const t = lo.tracks[i];
        let len = at - t.start;

        if (lo.bar <= 0)
        {
            // The first loop sets the bar: guess how many bars it was from the song's tempo, or from a tempo people play at.
            const beats = lay().beats || 4, bpm = lay().tempo >= 30 ? lay().tempo : 0;
            let bars = 1;
            if (bpm > 0) bars = Math.max (1, Math.round (len * bpm / (beats * 60000)));
            else while (beats * 60000 * bars / len < 70 && bars < 64) bars *= 2;
            lo.bar = len / bars;
            lo.t0 = t.start;
        }
        else
            len = Math.max (1, Math.round (len / lo.bar)) * lo.bar;

        t.len = len; t.a = true;
        setState (i, lay().recThenPlay ? 4 : 5, depth);
        if (t.s === 5) t.d = true;
    };

    const finishOthers = (i, at, depth) => lo.tracks.forEach ((t, k) => { if (k !== i && (t.s === 2 || t.s === 1)) { if (t.s === 1) t.s = 0; else closeRecording (k, at, depth); } if (k !== i && t.s === 5) setState (k, 4, depth); });

    const act = (i, kind, depth) =>
    {
        const t = lo.tracks[i], at = now(), L = lay().tracks[i];
        lo.cur = i;

        if (kind === 'recPlay')
        {
            if (t.s === 0)
            {
                if (! L.inputs) return;
                finishOthers (i, at, depth);
                if (lo.bar > 0 && lay().quantise && running()) { t.s = 1; t.at = nextBar (at); }
                else startRecording (i, at, depth);
            }
            else if (t.s === 1)     t.s = 0;
            else if (t.s === 2)     { if (lo.bar > 0) { t.s = 3; t.at = t.start + Math.max (1, Math.round ((at - t.start) / lo.bar)) * lo.bar; if (t.at <= at) closeRecording (i, t.at, depth); } else closeRecording (i, at, depth); }
            else if (t.s === 4)     { finishOthers (i, at, depth); t.d = true; t.u = false; setState (i, 5, depth); }
            else if (t.s === 5)     setState (i, 4, depth);
            else if (t.s === 7 || t.s === 6) { if (t.s === 7) t.start = L.ret === 0 && lo.bar > 0 ? t.start : nextBar (at) - (lo.bar > 0 ? lo.bar : 0); setState (i, 4, depth); }
        }
        else if (kind === 'playStop')
        {
            if (t.s === 4 || t.s === 5)             stop (i, at);
            else if (t.s === 2)                     { closeRecording (i, at, depth); }
            else if (t.s === 7 || t.s === 6)        { if (L.ret !== 0) t.start = at; setState (i, 4, depth); }
        }
        else if (kind === 'stop')       { if (t.s === 2) closeRecording (i, at, depth); if (t.s === 1) t.s = 0; else if (t.s !== 0) stop (i, at); }
        else if (kind === 'clear')      { Object.assign (t, { s: 0, a: false, d: false, u: false, len: 0 }); if (! lo.tracks.some ((x) => x.a || x.s !== 0)) { lo.bar = 0; } }
        else if (kind === 'undo')       { if (! t.a) return; if (t.u) { t.u = false; } else { t.u = true; } if (t.s === 5) t.s = 4; }
        else if (kind === 'undoOrClear') { if (t.s === 7) act (i, 'clear', depth); else act (i, 'undo', depth); }
    };

    const stop = (i, at) =>
    {
        const t = lo.tracks[i];
        if (! t.a) { t.s = 0; return; }
        if (lay().tracks[i].stop === 2 && t.len > 0) { t.s = 6; t.at = t.start + Math.ceil ((at - t.start) / t.len) * t.len; }
        else t.s = 7;
    };

    const allStartStop = (depth) =>
    {
        const at = now();

        if (running())
            lo.tracks.forEach ((t, i) => { if (t.s === 2) closeRecording (i, at, depth); if (t.s === 1) t.s = 0; if (t.s === 4 || t.s === 5 || t.s === 6) t.s = 7; });
        else
            lo.tracks.forEach ((t, i) => { if (t.s === 7) { t.start = at; setState (i, 4, depth); } });

        if (lo.bar > 0 && running()) lo.t0 = at;
    };

    const clearAll = () => { const bank = lo.bank; lo = freshLooper(); lo.bank = bank; };

    const kindNames = { 0: 'recPlay', 1: 'playStop', 2: 'stop', 3: 'clear', 5: 'undo' };

    function target (tgt, depth)
    {
        if (tgt === 80) return allStartStop (depth);
        const current = tgt >= 66 && tgt < 77;
        const track = current ? lo.cur : Math.floor (tgt / 11), kind = kindNames[current ? tgt - 66 : tgt % 11];
        if (kind && track >= 0 && track < 6) act (track, kind, depth);
    }

    // What each function does on a press, a hold (2 seconds) and a second press soon after the first.
    const plans = {
        'record play': ['recPlay'], 'record play, hold undo': ['recPlay', 'undo'], 'record play, hold undo, double stop': ['recPlay', 'undo', 'stop'],
        'record play, hold undo or clear, double stop': ['recPlay', 'undoOrClear', 'stop'],
        'play stop': ['playStop'], 'play stop, hold undo': ['playStop', 'undo'], 'play stop, hold undo or clear': ['playStop', 'undoOrClear'],
        'stop': ['stop'], 'stop, hold clear': ['stop', 'clear'], 'stop, double clear': ['stop', null, 'clear'], 'clear': ['clear'], 'undo redo': ['undo'],
        'all start stop': ['all'], 'all start stop, hold clear all': ['all', 'clearAll'], 'all start stop, double clear all': ['all', null, 'clearAll'], 'clear all': ['clearAll'], 'bank': ['bank'],
    };

    const press = { timers: {}, last: {} };

    const run = (what, track) =>
    {
        if (what === 'all')             allStartStop (0);
        else if (what === 'clearAll')   clearAll();
        else if (what === 'bank')       lo.bank = 1 - lo.bank;
        else if (what)                  act (track, what, 0);
    };

    const startedPlaying = (kind) =>
    {
        if (st().breakMusic[kind] && bm.playing && bm.up) { breakStep(); bm.up = false; bm.faded++; bm.knobOnBreak = false; }
    };

    const pedal = (sw, down) =>
    {
        clearTimeout (press.timers[sw]);
        if (! down) return;
        startedPlaying ('onSwitch');

        const bank = lo.bank;
        fire (sw >= 9 ? 42 + (sw - 9) : 13 + bank * 9 + sw, 0);
        if (sw >= 9) return;

        const d = GR.describe (lay().banks[bank][sw]);
        if (! d.fn) return;
        const plan = plans[d.fn.words] || [];
        const track = d.track === null ? 0 : d.track < 0 ? lo.cur : d.track;
        const t = now(), again = press.last[sw] && t - press.last[sw] < 400;
        press.last[sw] = t;

        if (again && plan[2]) { run (plan[2], track); press.last[sw] = 0; }
        else run (plan[0], track);

        if (plan[1]) press.timers[sw] = setTimeout (() => run (plan[1], track), 2000);
    };

    const tick = (t) =>
    {
        lo.tracks.forEach ((tr, i) =>
        {
            const L = lay().tracks[i];
            if (tr.s === 1 && t >= tr.at)               startRecording (i, tr.at, 0);
            if (tr.s === 3 && t >= tr.at)               closeRecording (i, tr.at, 0);
            if (tr.s === 6 && t >= tr.at)               tr.s = 7;
            if (tr.s === 2 && L.bars > 0 && lo.bar > 0 && t >= tr.start + L.bars * lo.bar) closeRecording (i, tr.start + L.bars * lo.bar, 0);
        });
    };

    // ------------------------------------------------------------ the pretend break music, knob and tuner

    function breakStep()
    {
        const t = now(), b = st().breakMusic, dt = (t - bm.at) / 1000;
        bm.at = t;
        if (! bm.playing) { bm.fade = 0; return; }
        bm.pos = (bm.pos + dt) % 3483;
        bm.fade = bm.up ? Math.min (1, bm.fade + dt / Math.max (0.2, b.fadeUp)) : Math.max (0, bm.fade - dt / Math.max (0.2, b.fadeDown));
    }

    const clock = (sec) => Math.floor (sec / 60) + ':' + String (Math.floor (sec % 60)).padStart (2, '0');

    const knobScreen = () =>
    {
        const m = st().mixer, b = st().breakMusic;
        if (bm.knobOnBreak) return 'Break music: ' + (bm.playing ? (bm.up ? 'up' : 'faded down') : 'stopped') + ', level ' + b.level + ' dB. Turn = level, push = up or down, hold = back to the venue.';
        return bm.venueAt >= 0 && m.venues[bm.venueAt] ? 'Venue: push to load "' + m.venues[bm.venueAt] + '".' : 'Venue in use: ' + (m.venue || 'none loaded') + '.';
    };

    const knob = (what) =>
    {
        const m = st().mixer, b = st().breakMusic;

        if (what === 'hold')    { bm.knobOnBreak = ! bm.knobOnBreak; bm.venueAt = -1; return ok (knobScreen()); }
        if (what !== 'left' && what !== 'right' && what !== 'push') return fail ('The knob can go left or right, be pushed or be held.');

        if (bm.knobOnBreak)
        {
            breakStep();
            if (what === 'push')
            {
                if (! bm.playing) { if (! b.mix) return fail ('Choose a mix and press Play first.'); bm.playing = true; bm.up = true; }
                else bm.up = ! bm.up;
            }
            else { b.level = Math.min (b.maxLevel, Math.max (b.minLevel, b.level + (what === 'right' ? 1 : -1))); if (what === 'right' && bm.playing) bm.up = true; }
        }
        else if (what === 'push')   { if (bm.venueAt >= 0 && m.venues[bm.venueAt]) { m.venue = m.venues[bm.venueAt]; m.venueChanged = false; bm.venueAt = -1; } }
        else if (m.venues.length)   bm.venueAt = (Math.max (0, bm.venueAt) + (what === 'right' ? 1 : -1) + (bm.venueAt < 0 ? (what === 'right' ? 0 : 1) : 0) + m.venues.length) % m.venues.length;

        return ok (knobScreen());
    };

    rig.live = () =>
    {
        const t = now();
        tick (t);
        breakStep();
        const beats = lay().beats || 4, beatMs = lo.bar > 0 ? lo.bar / beats : 0;
        const since = beatMs > 0 ? t - lo.t0 : 0;
        const wobble = (seed) => 0.55 + 0.4 * Math.abs (Math.sin (t / 130 + seed * 1.7)) * Math.abs (Math.sin (t / 471 + seed));
        const chPeak = st().mixer.channels.map (() => 0);

        const tracks = lo.tracks.map ((tr, i) =>
        {
            const sounding = (tr.s === 4 || tr.s === 5 || tr.s === 6) && tr.a && ! tr.u, taking = tr.s === 2 || tr.s === 3 || tr.s === 5;
            const pk = sounding || taking ? wobble (i) * 0.5 * (lay().tracks[i].level / 100) : 0;

            if (pk > 0)
                st().sources.filter ((src) => lay().tracks[i].inputs & src.bit).forEach ((src) =>
                    st().mixer.channels.forEach ((ch, c) => { if (ch.on && ch.socket === src.socket) chPeak[c] = Math.max (chPeak[c], pk); }));

            return { s: tr.s, a: tr.a, d: tr.d, u: tr.u, l: Math.max (1, Math.min (st().maxLanes, GR.lanesFor (lay().tracks[i].inputs))),
                     sec: tr.len / 1000, bars: lo.bar > 0 && tr.len > 0 ? Math.round (tr.len / lo.bar * 100) / 100 : 0,
                     pos: tr.s === 2 || tr.s === 3 ? (t - tr.start) / 1000 : tr.a && tr.len > 0 && tr.s !== 7 ? ((t - tr.start) % tr.len) / tr.len : 0, pk };
        });

        const live = {
            looper: { running: running(), bpm: beatMs > 0 ? Math.round (600000 / beatMs) / 10 : 0, bank: lo.bank, cur: lo.cur,
                      beat: beatMs > 0 ? Math.floor (since / beatMs) % beats : 0, bar: lo.bar > 0 ? Math.floor (since / lo.bar) : 0, ms: beatMs > 0 ? Math.round (since % beatMs) : -1, tracks },
            load: 0.21, late: 0, open: true,
        };

        const b = st().breakMusic;
        live.breakMusic = { playing: bm.playing, up: bm.up, fade: Math.round (bm.fade * bm.fade * 1000) / 1000, pos: bm.pos, len: 3483, mix: b.mix, level: b.level, knobOnBreak: bm.knobOnBreak, knob: knobScreen(),
            line: ! bm.playing ? (b.mix ? 'Stopped. "' + b.mix + '" will carry on from ' + clock (bm.pos) + '.' : 'Nothing chosen yet.')
                : 'Playing "' + b.mix + '": ' + clock (bm.pos) + ' of 58:03, ' + (bm.up ? 'up' : 'faded down (it comes back by hand only)') + ', level ' + b.level + ' dB' };

        // A pretend string being tuned: it starts flat and is brought up to pitch, again and again.
        const tuning = (t - tuner.since) / 1000, turn = tuning % 9;
        const cents = turn < 1 ? 0 : turn < 6 ? -32 * Math.exp (-(turn - 1) * 0.9) + 1.2 * Math.sin (t / 300) : 0.6 * Math.sin (t / 400);
        const string = [['E2', 82.41], ['A2', 110], ['D3', 146.83], ['G3', 196], ['B3', 246.94], ['E4', 329.63]][Math.floor (tuning / 9) % 6];
        const ref = st().tuner.reference / 440;
        live.tuner = { on: tuner.on, heard: tuner.on && turn >= 1, name: tuner.on && turn >= 1 ? string[0] : '', cents: tuner.on && turn >= 1 ? Math.round (cents * 10) / 10 : 0,
                       hz: tuner.on && turn >= 1 ? Math.round (string[1] * ref * Math.pow (2, cents / 1200) * 10) / 10 : 0, socket: tuner.on ? (st().tuner.socket >= 0 ? st().tuner.socket : st().effects.chains[0].socket - 1) : -1 };

        const m = st().mixer;

        if (m.on)
        {
            const gain = (db) => db <= m.offDb + 0.5 ? 0 : Math.pow (10, db / 20);
            const ch = m.channels.map ((c, i) => c.on ? chPeak[i] * gain (c.trim) : 0);
            const sum = (pick) => Math.min (1.2, m.channels.reduce ((a, c, i) => a + (c.on && ! c.mute ? ch[i] * gain (pick (c)) : 0), 0));
            const fx = m.fx.map ((e, f) => e.on ? sum ((c) => c.fx[f]) * 0.7 : 0);
            const out = m.outputs.map ((o, k) =>
            {
                const dry = k === 0 ? sum ((c) => c.fader) : sum ((c) => c.mon[k - 1]);
                const wet = m.fx.reduce ((a, e, f) => a + fx[f] * gain (k === 0 ? e.ret : e.mon[k - 1]), 0);
                const v = o.mute || (k === 0 && tuner.on && st().tuner.mutes) ? 0 : Math.min (1, (dry + wet) * gain (o.level));
                return [v, o.mono ? v : v * 0.93];
            });

            live.mix = { ch, gr: m.channels.map ((c, i) => c.on && c.strip.some ((b) => b.on && /comp|la2a/.test (b.type)) ? -Math.min (9, ch[i] * 12) : 0), out, fx, limit: out[0][0] > 0.9 ? -1.5 : 0, guard: false };
        }

        return live;
    };

    // ------------------------------------------------------------ commands, as RemoteApi::command answers them

    const blocksAt = (c) =>
    {
        const m = st().mixer;
        if (c.where === 'ch')   return (m.channels[c.i] || {}).strip;
        if (c.where === 'out')  return (m.outputs[c.i] || {}).chain;
        if (c.where === 'fx')   return (m.fx[c.i] || {}).effect;
        if (c.where === 'fxeq') return (m.fx[c.i] || {}).eq;
        return null;
    };

    const setBlock = (block, id, value) =>
    {
        if (! block) return fail ('That part is not there.');
        if (id === 'on') block.on = !! value; else block.v[id] = Number (value);
        return ok();
    };

    const loadSong = (index, why) =>
    {
        const s = st(), p = perSong[index];
        if (! p) return fail ('There is no song with that number.');
        s.current = index;
        s.layout = copy (p.layout);
        s.layoutTried = false;
        s.layoutOwn = !! p.layoutOwn;
        s.layoutWords = p.layoutWords || '';
        s.songStatus = p.songStatus || '';
        s.songNote = 'Loaded "' + s.songs[index].name + '" (' + why + ').';
        s.songProblems = p.songProblems || [];
        if (p.effects) s.effects = copy (p.effects);
        if (p.switches) s.switches = copy (p.switches);
        clearAll();
        startedPlaying ('onSong');
        return ok();
    };

    const goodLayout = (l) =>
    {
        if (! l || ! Array.isArray (l.tracks) || l.tracks.length !== 6 || ! Array.isArray (l.banks) || l.banks.length !== 2) return 'That is not a looper layout.';
        if ((l.links || []).length > st().maxLinks) return 'A layout can hold ' + st().maxLinks + ' links.';
        return '';
    };

    const addSong = (name, program) =>
    {
        const s = st();
        if (s.songs.some ((x) => x.name.toLowerCase() === name.toLowerCase())) return 'There is already a song called "' + name + '".';
        s.songs.push ({ name, program: program || 0, sounds: true, ownLayout: false, builtIn: false });
        const from = perSong[s.current];
        perSong.push (Object.assign (copy (from), { layout: copy (s.layout), layoutOwn: false, songStatus: name + (program ? ' (program ' + program + ')' : ' (no program number)') + ': a new song in the demo.' }));
        return '';
    };

    const saveLayout = (l, withTempo) =>
    {
        const s = st(), p = perSong[s.current];
        const keepLoops = s.layoutTried && JSON.stringify (s.layout) === JSON.stringify (l);
        p.layout = copy (l);
        if (! withTempo) p.layout.tempo = s.layout.tempo;
        p.layoutOwn = true;
        p.layoutWords = '(The demo does not write song files. On the rig the layout\'s lines are shown here.)';
        s.songs[s.current].ownLayout = true;
        s.layout = copy (p.layout);
        s.layoutOwn = true; s.layoutTried = false; s.layoutWords = p.layoutWords;
        if (! keepLoops) clearAll();
        return ok ('"' + s.songs[s.current].name + '" has its own looper layout now. (In the demo it lasts until the demo starts again.)');
    };

    rig.command = (c) =>
    {
        const s = st(), m = s.mixer, fx = s.effects, op = c.op;
        const num = (v, lo2, hi) => Math.min (hi, Math.max (lo2, Number (v) || 0));
        const level = (v) => num (v, m.offDb, 10);

        if (op === 'pedal')             { pedal (c.sw, !! c.down); return ok(); }

        commands++;
        touch();

        switch (op)
        {
            case 'looper.clearAll':     clearAll(); return ok();
            case 'report':              return ok (rig.report());
            case 'song.load':           return loadSong (c.index, 'chosen on the remote screen');
            case 'song.program':        { const i = s.songs.findIndex ((x) => x.program === c.program); return i < 0 ? fail ('No song has program number ' + c.program + '.') : loadSong (i, 'program ' + c.program); }
            case 'song.keep':           s.songs[s.current].sounds = true; perSong[s.current].effects = copy (s.effects); return ok ('The sounds are kept for "' + s.songs[s.current].name + '". (In the demo they last until the demo starts again.)');
            case 'song.again':          if (perSong[s.current].effects) s.effects = copy (perSong[s.current].effects); return ok ('The song\'s sounds are back.');
            case 'song.new':            { const e = addSong (String (c.name || '').trim(), c.program); if (e) return fail (e); return loadSong (s.songs.length - 1, 'just added'); }
            case 'layout.of':           return perSong[c.index] ? { ok: true, note: '', layout: copy (perSong[c.index].layout) } : fail ('There is no song with that number.');
            case 'layout.try':          { const e = goodLayout (c.layout); if (e) return fail (e); s.layout = copy (c.layout); if (! c.withTempo) s.layout.tempo = perSong[s.current].layout.tempo; s.layoutTried = true; clearAll(); return ok(); }
            case 'layout.save':         { const e = goodLayout (c.layout); return e ? fail (e) : saveLayout (c.layout, c.withTempo); }
            case 'layout.saveAs':       { const e = goodLayout (c.layout) || addSong (String (c.name || '').trim(), c.program); if (e) return fail (e); s.current = s.songs.length - 1; s.songStatus = perSong[s.current].songStatus; return saveLayout (c.layout, c.withTempo); }
            case 'layout.forget':
            {
                const p = perSong[s.current], first = data.songs[s.current];
                if (p.layoutOwn && first) { p.layout = copy (first.layout); p.layoutOwn = !! first.layoutOwn; s.songs[s.current].ownLayout = p.layoutOwn; }
                return loadSong (s.current, 'back on its saved layout');
            }
            case 'fx.enabled':          fx.enabled = !! c.on; return ok();
            case 'fx.chainOn':          if (! fx.chains[c.chain]) return fail ('There is no chain with that number.'); fx.chains[c.chain].on = !! c.on; return ok();
            case 'fx.preset':           return ok ('On the rig this loads the preset "' + c.name + '". The demo does not have the preset files, so nothing changed.');
            case 'fx.slotOn':           return setBlock ((fx.chains[c.chain] || { slots: [] }).slots[c.slot], 'on', c.on);
            case 'fx.param':            return setBlock ((fx.chains[c.chain] || { slots: [] }).slots[c.slot], c.id, c.value);
            case 'fx.add':
            {
                const ch = fx.chains[c.chain], type = s.fxCatalogue.find ((e) => e.id === c.type);
                if (! ch) return fail ('There is no chain with that number.');
                if (! type) return fail ('There is no effect called "' + c.type + '" to add.');
                if (ch.slots.length >= s.fxMaxSlots) return fail ('This chain is full: it holds ' + s.fxMaxSlots + ' effects. Remove one first.');
                const at = Math.min (ch.slots.length, Math.max (0, c.at === undefined ? ch.slots.length : Number (c.at)));
                const v = {};
                ((s.types[c.type] || {}).params || []).forEach ((p) => { v[p.id] = p.def; });
                ch.slots.splice (at, 0, { type: c.type, on: true, v });
                return ok ('Added at place ' + (at + 1) + '.');
            }
            case 'fx.remove':
            case 'fx.move':
            {
                const ch = fx.chains[c.chain];
                if (! ch) return fail ('There is no chain with that number.');
                if (! ch.slots[c.slot]) return fail ('That effect is not there any more.');
                if (op === 'fx.remove') { ch.slots.splice (c.slot, 1); return ok(); }
                if (! (c.to >= 0 && c.to < ch.slots.length)) return fail ('It is already at that end of the chain.');
                ch.slots.splice (c.to, 0, ch.slots.splice (c.slot, 1)[0]);
                return ok();
            }
            case 'fx.save':
            {
                const ch = fx.chains[c.chain], name = String (c.name || '').trim();
                if (! ch) return fail ('There is no chain with that number.');
                if (! name) return fail ('Type a name for the preset first.');
                if (! fx.presets.includes (name)) { fx.presets.push (name); fx.presets.sort(); }
                ch.recipe = [name];
                return ok ('Saved the preset "' + name + '". (The demo does not write files, so loading it back only says what would happen.)');
            }
            case 'fx.update':
            {
                const ch = fx.chains[c.chain];
                if (! ch) return fail ('There is no chain with that number.');
                return ok (ch.recipe.length ? 'On the rig this saves the changes back into ' + ch.recipe.join (', ') + '. The demo does not have the preset files, so nothing was saved.'
                                            : 'Nothing was saved: this chain was not built from presets.');
            }
            case 'mix.save':
            {
                const name = String (c.name || '').trim(), list = { act: m.acts, venue: m.venues, monitors: m.monitorSets }[c.kind];
                if (! name) return fail ('Type a name to save it under first.');
                if (! list) return fail ('There is no kind of preset called "' + c.kind + '".');
                if (! list.includes (name)) { list.push (name); list.sort(); }
                if (c.kind === 'act') { m.act = name; m.actChanged = false; } else if (c.kind === 'venue') { m.venue = name; m.venueChanged = false; } else { m.monitors = name; m.monitorsChanged = false; }
                return ok ('Saved the ' + (c.kind === 'monitors' ? 'monitor set-up' : c.kind) + ' "' + name + '". (In the demo it lasts until the demo starts again.)');
            }
            case 'break.play':
            {
                const b = s.breakMusic, name = c.name || b.mix;
                if (! b.mixes.includes (name)) return fail ('There is no mix called "' + name + '" in the break music folder.');
                breakStep();
                if (name !== b.mix || c.fromStart) bm.pos = 0;
                b.mix = name; bm.playing = true; bm.up = true;
                return ok();
            }
            case 'break.stop':          breakStep(); bm.playing = false; bm.up = false; return ok();
            case 'break.up':            breakStep(); if (bm.playing) bm.up = true; return ok();
            case 'break.down':          breakStep(); bm.up = false; return ok();
            case 'break.level':         s.breakMusic.level = num (c.value, s.breakMusic.minLevel, s.breakMusic.maxLevel); return ok();
            case 'break.set':
            {
                const b = s.breakMusic, k = c.key;
                if (k === 'fadeDown' || k === 'fadeUp') b[k] = num (c.value, 0.2, 15);
                else if (k === 'onSwitch' || k === 'onSong' || k === 'onSound') b[k] = !! c.value;
                else if (k === 'soundDb') b.soundDb = num (c.value, -60, 0);
                else return fail ('The break music has no "' + k + '".');
                return ok();
            }
            case 'knob':                return knob (c.do);
            case 'tuner.on':            tuner.on = !! c.on; tuner.since = now(); return ok();
            case 'tuner.set':
            {
                const t = s.tuner, k = c.key;
                if (k === 'socket') t.socket = Math.round (num (c.value, -1, 31)); else if (k === 'reference') t.reference = num (c.value, t.minReference, t.maxReference);
                else if (k === 'mutes') t.mutes = !! c.value; else return fail ('The tuner has no "' + k + '".');
                return ok();
            }
            case 'mix.on':              m.on = !! c.on; return ok();
            case 'mix.ch':
            {
                const ch = m.channels[c.c], k = c.key;
                if (! ch) return fail ('There is no channel with that number.');
                if (k === 'fader') ch.fader = level (c.value); else if (k === 'mute') ch.mute = !! c.value; else if (k === 'pan') ch.pan = num (c.value, -1, 1);
                else if (k === 'trim') ch.trim = num (c.value, -24, 24); else if (k === 'on') ch.on = !! c.value;
                else if (/^mon[01]$/.test (k)) ch.mon[Number (k[3])] = level (c.value); else if (/^fx[0-3]$/.test (k)) ch.fx[Number (k[2])] = level (c.value);
                else return fail ('A channel has no "' + k + '".');
                m.actChanged = true;
                return ok();
            }
            case 'mix.out':
            {
                const o = m.outputs[c.o];
                if (! o) return fail ('There is no output with that number.');
                if (c.key === 'level') o.level = level (c.value); else if (c.key === 'mute') o.mute = !! c.value; else return fail ('An output has no "' + c.key + '".');
                m.venueChanged = true;
                return ok();
            }
            case 'mix.fx':
            {
                const e = m.fx[c.f], k = c.key;
                if (! e) return fail ('There is no mixer effect with that number.');
                if (k === 'ret') e.ret = level (c.value); else if (k === 'on') e.on = !! c.value; else if (/^mon[01]$/.test (k)) e.mon[Number (k[3])] = level (c.value);
                else if (k === 'lowCut') e.lowCut = num (c.value, 20, 2000); else if (k === 'highCut') e.highCut = num (c.value, 1000, 20000);
                else return fail ('A mixer effect has no "' + k + '".');
                m.venueChanged = true;
                return ok();
            }
            case 'mix.param':           return setBlock ((blocksAt (c) || [])[c.slot], c.id, c.value);
            case 'mix.preset':          return ok ('On the rig this loads the ' + c.kind + ' set-up "' + c.name + '". The demo does not have the files, so nothing changed.');
            case 'mix.keepFx':          if (m.fx[c.f]) m.fx[c.f].song = true; return ok ('Kept for this song. (In the demo it lasts until the demo starts again.)');
            default:                    commands--; return fail ('This build of the rig does not know "' + op + '". The page may be newer than the rig.');
        }
    };

    rig.report = () => 'GigRig remote screen: the demo\n\nThis report comes from the demo, which is not connected to a rig.\n'
        + 'Song loaded: ' + (st().songs[st().current] || {}).name + '\nCommands carried out in the demo: ' + commands + '\n\n'
        + 'On the rig, this button gives the same text as the Copy report button on the rig\'s own screen.\n';

    rig.reset();
    return rig;
};
