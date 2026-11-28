#include "RemoteApi.h"
#include "LooperLayout.h"
#include "effects/Registry.h"

#include <map>

namespace gigrig
{

namespace
{
    juce::DynamicObject* obj()                  { return new juce::DynamicObject(); }

    /** An effect type's settings, made once (making an amp loads a model, so not every time). */
    struct TypeFacts
    {
        std::vector<fx::ParamInfo> params;
        std::vector<std::string> textKeys;
    };

    const TypeFacts& factsFor (const std::string& type)
    {
        static std::map<std::string, TypeFacts> known;

        if (auto found = known.find (type); found != known.end())
            return found->second;

        TypeFacts f;

        if (auto e = fx::makeEffect (type))
        {
            f.params = e->params();
            f.textKeys = e->textKeys();
        }

        return known[type] = std::move (f);
    }

    double tidy (double v)                      { return std::round (v * 1000.0) / 1000.0; }

    juce::var floats (const float* values, int count)
    {
        juce::Array<juce::var> a;
        for (int i = 0; i < count; ++i)
            a.add (tidy ((double) values[i]));
        return a;
    }

    juce::var strings (const juce::StringArray& s)
    {
        juce::Array<juce::var> a;
        for (const auto& x : s)
            a.add (x);
        return a;
    }

    juce::var ok (const juce::String& note = {})
    {
        auto* o = obj();
        o->setProperty ("ok", true);
        o->setProperty ("note", note);
        return juce::var (o);
    }

    juce::var failed (const juce::String& error)
    {
        auto* o = obj();
        o->setProperty ("ok", false);
        o->setProperty ("error", error);
        return juce::var (o);
    }

    /** Sets one setting of one block in a chain, by the setting's id. Returns an error or nothing. */
    juce::String setInModel (fx::ChainModel& m, int slot, const juce::String& id, float value)
    {
        if (slot < 0 || slot >= (int) m.slots.size())
            return "That effect is not there any more.";

        auto& s = m.slots[(size_t) slot];

        if (id == "on")
        {
            s.on = value >= 0.5f;
            return {};
        }

        for (const auto& p : factsFor (s.type).params)
            if (id == juce::String (p.id))
            {
                s.setParam (p.id, juce::jlimit (p.min, p.max, value));
                return {};
            }

        return "That effect has no setting called \"" + id + "\".";
    }
}

RemoteApi::RemoteApi (AudioEngine& e, PedalInput& p, SongBook& b) : engine (e), pedalInput (p), book (b) {}

void RemoteApi::pedal (int sw, bool isDown)
{
    if (sw >= 0 && sw < numPedalSwitches)
        pedalInput.screenSwitch (sw, isDown);
}

void RemoteApi::readLists()
{
    // Looking in folders is slow next to everything else here, so once every few seconds is enough.
    const auto now = juce::Time::getMillisecondCounter();

    if (listsRead && now - listsAt < 3000)
        return;

    listsRead = true;
    listsAt = now;
    presetList.clear();

    for (const auto& f : book.presetsFolder().findChildFiles (juce::File::findFiles, false, "*.txt"))
        presetList.add (f.getFileNameWithoutExtension());

    presetList.sortNatural();
    actList = engine.mixer().presetNames (Mixer::Preset::act);
    venueList = engine.mixer().presetNames (Mixer::Preset::venue);
    monitorList = engine.mixer().presetNames (Mixer::Preset::monitors);
    recordingList = engine.recordings().list();
    driveList = engine.recordings().drives();
    recordingsFreeGb = engine.recordings().freeGb();
}

//==============================================================================
juce::var RemoteApi::chainJson (const fx::ChainModel& m)
{
    juce::Array<juce::var> slots;

    for (const auto& s : m.slots)
    {
        const auto& facts = factsFor (s.type);
        typesSeen.addIfNotAlreadyThere (s.type);

        auto* o = obj();
        o->setProperty ("type", juce::String (s.type));
        o->setProperty ("on", s.on);

        auto* values = obj();
        for (const auto& p : facts.params)
            values->setProperty (juce::Identifier (juce::String (p.id)), tidy ((double) s.param (p.id, p.def)));
        o->setProperty ("v", juce::var (values));

        if (! facts.textKeys.empty())
        {
            auto* texts = obj();
            for (const auto& k : facts.textKeys)
                texts->setProperty (juce::Identifier (juce::String (k)), juce::String (s.text (k)));
            o->setProperty ("t", juce::var (texts));
        }

        slots.add (juce::var (o));
    }

    return slots;
}

juce::var RemoteApi::typesJson()
{
    auto* all = obj();

    for (const auto& id : typesSeen)
    {
        const auto type = id.toStdString();
        const auto* info = fx::findEffectType (type);
        auto* t = obj();
        t->setProperty ("name", info != nullptr ? juce::String (info->name) : id);
        t->setProperty ("group", info != nullptr ? juce::String (info->group) : juce::String());

        juce::Array<juce::var> params;

        for (const auto& p : factsFor (type).params)
        {
            auto* o = obj();
            o->setProperty ("id", juce::String (p.id));
            o->setProperty ("label", juce::String (p.label));
            o->setProperty ("unit", juce::String (p.unit));
            o->setProperty ("min", (double) p.min);
            o->setProperty ("max", (double) p.max);
            o->setProperty ("def", (double) p.def);
            o->setProperty ("kind", p.kind == fx::ParamInfo::Kind::toggle ? "toggle" : p.kind == fx::ParamInfo::Kind::choice ? "choice" : "knob");

            if (! p.choices.empty())
            {
                juce::Array<juce::var> choices;
                for (const auto& c : p.choices)
                    choices.add (juce::String (c));
                o->setProperty ("choices", choices);
            }

            params.add (juce::var (o));
        }

        t->setProperty ("params", params);
        all->setProperty (juce::Identifier (id), juce::var (t));
    }

    return juce::var (all);
}

juce::var RemoteApi::effectsJson()
{
    auto& rack = engine.effects();
    auto* fxo = obj();
    fxo->setProperty ("enabled", rack.isEnabled());
    fxo->setProperty ("tempo", rack.tempoText());

    juce::Array<juce::var> chains;

    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        const auto route = rack.routing (c);
        auto* o = obj();
        o->setProperty ("on", route.on);
        o->setProperty ("name", route.name.isNotEmpty() ? route.name : "Chain " + juce::String (c + 1));
        o->setProperty ("socket", route.source + 1);
        o->setProperty ("recipe", strings (rack.recipe (c)));
        o->setProperty ("slots", chainJson (rack.chain (c).model()));
        chains.add (juce::var (o));
    }

    fxo->setProperty ("chains", chains);

    readLists();
    fxo->setProperty ("presets", strings (presetList));

    return juce::var (fxo);
}

juce::var RemoteApi::mixerJson()
{
    auto& mixer = engine.mixer();
    const auto& now = mixer.setup();
    auto* m = obj();
    m->setProperty ("on", mixer.isOn());
    m->setProperty ("act", mixer.actName());
    m->setProperty ("venue", mixer.venueName());
    m->setProperty ("monitors", mixer.monitorsName());
    m->setProperty ("actChanged", mixer.actChanged());
    m->setProperty ("venueChanged", mixer.venueChanged());
    m->setProperty ("monitorsChanged", mixer.monitorsChanged());
    readLists();
    m->setProperty ("acts", strings (actList));
    m->setProperty ("venues", strings (venueList));
    m->setProperty ("monitorSets", strings (monitorList));
    m->setProperty ("offDb", (double) mixOffDb);
    m->setProperty ("warnings", strings (mixer.warnings()));

    juce::Array<juce::var> channels, effects, outputs;

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];
        auto* o = obj();
        o->setProperty ("name", ch.name.isNotEmpty() ? ch.name : "Channel " + juce::String (c + 1));
        o->setProperty ("on", ch.on);
        o->setProperty ("socket", ch.socket + 1);
        o->setProperty ("trim", tidy (ch.trimDb));
        o->setProperty ("mute", ch.mute);
        o->setProperty ("fader", tidy (ch.faderDb));
        o->setProperty ("pan", tidy (ch.pan));
        o->setProperty ("link", ch.link);
        o->setProperty ("linked", c > 0 && now.channel[(size_t) c - 1].link);      // the right side of the channel before it
        o->setProperty ("mon", floats (ch.monDb.data(), numMixMonitors));

        juce::Array<juce::var> sends, own;
        for (int f = 0; f < numMixFx; ++f)
        {
            sends.add (tidy (mixer.sendDb (c, f)));
            own.add (mixer.hasSongSend (c, f));
        }
        o->setProperty ("fx", sends);
        o->setProperty ("fxSong", own);
        o->setProperty ("strip", chainJson (ch.strip));
        o->setProperty ("rec", recordWord (ch.record));
        o->setProperty ("recLevel", tidy (ch.recDb));
        o->setProperty ("recPan", tidy (ch.recPan));
        channels.add (juce::var (o));
    }

    for (int f = 0; f < numMixFx; ++f)
    {
        const auto& e = now.fx[(size_t) f];
        auto* o = obj();
        o->setProperty ("name", e.name.isNotEmpty() ? e.name : "Effect " + juce::String (f + 1));
        o->setProperty ("on", e.on);
        o->setProperty ("ret", tidy (e.returnDb));
        o->setProperty ("mon", floats (e.monDb.data(), numMixMonitors));
        o->setProperty ("lowCut", tidy (e.lowCutHz));
        o->setProperty ("highCut", tidy (e.highCutHz));
        o->setProperty ("song", mixer.hasSongFx (f));
        o->setProperty ("effect", chainJson (mixer.effectNow (f)));
        o->setProperty ("eq", chainJson (e.eq));
        o->setProperty ("recLevel", tidy (e.recDb));
        effects.add (juce::var (o));
    }

    for (int i = 0; i < numMixOutputs; ++i)
    {
        const auto& out = now.output[(size_t) i];
        auto* o = obj();
        o->setProperty ("name", out.name.isNotEmpty() ? out.name : mixOutputName (i));
        o->setProperty ("kind", mixOutputName (i));
        o->setProperty ("level", tidy (out.levelDb));
        o->setProperty ("mute", out.mute);
        o->setProperty ("mono", out.mono);
        o->setProperty ("connected", out.firstReturn >= 0);
        o->setProperty ("chain", chainJson (out.chain));
        outputs.add (juce::var (o));
    }

    m->setProperty ("channels", channels);
    m->setProperty ("fx", effects);
    m->setProperty ("outputs", outputs);

    // The Record screen (docs/recording.md): which mixes are written, the recording mix, and the files Record would make now.
    {
        auto* r = obj();
        r->setProperty ("main", now.rec.mainMix);
        r->setProperty ("mix", now.rec.mix);
        r->setProperty ("fx", now.rec.fxTracks);
        r->setProperty ("check", now.rec.checkCopy);
        r->setProperty ("level", tidy (now.rec.levelDb));
        r->setProperty ("chain", chainJson (now.rec.chain));

        juce::Array<juce::var> points, files;
        static const char* const labels[numRecordPoints] { "Nothing", "The plain socket", "After its effects", "After the looper", "After the looper, with its EQ and compressors" };
        static const char* const more[numRecordPoints] {
            "This channel gets no file of its own.",
            "Straight from the input, before any effect. The safest thing to keep: anything can be redone from it.",
            "After the guitar or vocal effects, before the looper. No loops in it.",
            "The live sound and its loops together, before the channel's own gate, EQ and compressors.",
            "The live sound and its loops together, after the channel's gate, EQ and compressors, before its fader." };

        for (int p = 0; p < numRecordPoints; ++p)
        {
            auto* o = obj();
            o->setProperty ("id", recordWord ((RecordPoint) p));
            o->setProperty ("label", labels[p]);
            o->setProperty ("more", more[p]);
            points.add (juce::var (o));
        }

        for (const auto& t : mixer.recordTracks())
            files.add (t.name + (t.channels == 2 ? " (stereo)" : ""));

        r->setProperty ("points", points);
        r->setProperty ("files", files);
        m->setProperty ("rec", juce::var (r));
    }

    return juce::var (m);
}

juce::var RemoteApi::breakJson()
{
    auto& music = engine.breakMusic();
    const auto set = music.settings();
    auto* b = obj();
    auto list = music.mixes();

    if (BreakMusic::phoneSupported())
        list.add (BreakMusic::phoneName());

    b->setProperty ("mixes", strings (list));
    b->setProperty ("mix", music.mixName());
    b->setProperty ("level", tidy ((double) music.levelDb()));
    b->setProperty ("minLevel", (double) BreakMusic::minLevelDb);
    b->setProperty ("maxLevel", (double) BreakMusic::maxLevelDb);
    b->setProperty ("fadeDown", tidy (set.fadeDownSeconds));
    b->setProperty ("fadeUp", tidy (set.fadeUpSeconds));
    b->setProperty ("onSwitch", set.onSwitch);
    b->setProperty ("onSong", set.onSong);
    b->setProperty ("onSound", set.onSound);
    b->setProperty ("soundDb", tidy ((double) set.soundDb));
    return juce::var (b);
}

juce::var RemoteApi::state()
{
    typesSeen.clear();
    auto* s = obj();
    s->setProperty ("build", juce::String (GIGRIG_BUILD_LABEL));
    s->setProperty ("demo", false);

    // ---- the rig itself ----
    {
        auto* r = obj();
        r->setProperty ("open", engine.isOpen());
        r->setProperty ("device", engine.isOpen() ? engine.currentChoice().label() : juce::String ("No audio device is open"));
        r->setProperty ("rate", engine.sampleRate());
        r->setProperty ("buffer", engine.bufferSize());
        r->setProperty ("pedal", pedalInput.rc600Connected());
        juce::StringArray warnings;
        if (const auto slow = engine.slowBufferWarning(); slow.isNotEmpty())
            warnings.add (slow);
        if (! engine.isOpen())
            warnings.add ("No audio device is open, so nothing can be heard or looped. Open one on the rig's Latency and audio test tab.");
        if (engine.feedbackGuardTripped())
            warnings.add ("The feedback guard stopped the play-through.");
        r->setProperty ("warnings", strings (warnings));
        s->setProperty ("rig", juce::var (r));
    }

    // ---- songs ----
    {
        juce::Array<juce::var> songs;

        for (int i = 0; i < book.numEntries(); ++i)
        {
            const auto& e = book.entry (i);
            auto* o = obj();
            o->setProperty ("name", e.name);
            o->setProperty ("program", e.program);
            o->setProperty ("sounds", e.sounds.saysAnything());
            o->setProperty ("ownLayout", ! e.sounds.layoutLines.isEmpty());
            o->setProperty ("builtIn", e.builtIn);
            songs.add (juce::var (o));
        }

        s->setProperty ("songs", songs);
        s->setProperty ("current", book.currentIndex());
        s->setProperty ("songNote", book.lastNote());
        s->setProperty ("songStatus", book.statusLine());
        s->setProperty ("songProblems", strings (book.lastProblems()));
        s->setProperty ("hasDefault", book.hasDefault());
    }

    // ---- the looper's layout for the loaded song, and what the builder may choose from ----
    {
        s->setProperty ("layout", layout::toJson (book.song()));
        s->setProperty ("layoutTried", book.layoutIsTried());
        s->setProperty ("layoutOwn", ! book.currentEntry().sounds.layoutLines.isEmpty());
        s->setProperty ("layoutWords", book.song().title().isEmpty() ? juce::String() : describeSong (book.song()));

        juce::Array<juce::var> functions, sources, switches;

        for (const auto& c : layout::functionChoices())
        {
            auto* o = obj();
            o->setProperty ("words", c.words);
            o->setProperty ("label", c.label);
            o->setProperty ("index", c.index);
            o->setProperty ("code", c.code);
            functions.add (juce::var (o));
        }

        static const int bits[numLoopSources] = { rcMic1, rcInst1L, rcInst1R, rcInst2L, rcInst2R };
        const auto options = engine.looper().options();

        for (int i = 0; i < numLoopSources; ++i)
        {
            auto* o = obj();
            o->setProperty ("bit", bits[i]);
            o->setProperty ("name", loopSourceName (i));
            o->setProperty ("socket", options.inputFor[(size_t) i] + 1);
            sources.add (juce::var (o));
        }

        for (int sw = 0; sw < numPedalSwitches; ++sw)
        {
            auto* o = obj();
            o->setProperty ("name", pedalSwitchName (sw));
            o->setProperty ("effects", engine.effects().pedalUse (sw));
            switches.add (juce::var (o));
        }

        s->setProperty ("functions", functions);
        s->setProperty ("sources", sources);
        s->setProperty ("switches", switches);
        s->setProperty ("maxLanes", maxLoopLanes);
        s->setProperty ("maxLinks", maxLoopAssigns);
    }

    s->setProperty ("effects", effectsJson());
    s->setProperty ("mixer", mixerJson());
    s->setProperty ("breakMusic", breakJson());

    // The recordings on the drive (the Record screen's Recordings page). Names only: never a path from the disk.
    {
        juce::Array<juce::var> list, drives;

        for (const auto& i : recordingList)
        {
            auto* o = obj();
            o->setProperty ("name", i.name);
            o->setProperty ("files", i.files);
            o->setProperty ("min", tidy (i.seconds / 60.0));
            o->setProperty ("gb", tidy ((double) i.bytes / 1.0e9));
            o->setProperty ("kept", i.kept);
            o->setProperty ("copied", i.copied);
            o->setProperty ("check", i.soundcheckSockets);
            list.add (juce::var (o));
        }

        for (const auto& d : driveList)
        {
            auto* o = obj();
            o->setProperty ("name", d.name);
            o->setProperty ("free", tidy (d.freeGb));
            drives.add (juce::var (o));
        }

        auto* r = obj();
        r->setProperty ("list", list);
        r->setProperty ("drives", drives);
        r->setProperty ("free", tidy (recordingsFreeGb));
        s->setProperty ("recordings", juce::var (r));
    }

    {
        // Top-level, not inside "effects": a song's sounds replace "effects" whole (in the demo too), and these never change.
        // What "Add an effect" offers, in the Effects tab's order.
        juce::Array<juce::var> catalogue;

        for (const auto& t : fx::effectTypes())
        {
            auto* o = obj();
            o->setProperty ("id", juce::String (t.id));
            o->setProperty ("name", juce::String (t.name));
            o->setProperty ("group", juce::String (t.group));
            catalogue.add (juce::var (o));
        }

        s->setProperty ("fxCatalogue", catalogue);
        s->setProperty ("fxMaxSlots", fx::Chain::maxSlots);
    }

    {
        auto& tuner = engine.tuner();
        auto* t = obj();
        t->setProperty ("socket", tuner.socket());              // 0-based, -1 = whichever input is being played
        t->setProperty ("reference", tidy (tuner.reference()));
        t->setProperty ("mutes", tuner.mutes());
        t->setProperty ("silence", Tuner::silenceWord (tuner.silence()));     // "guitars", "main" or "nothing"
        t->setProperty ("fullScreen", tuner.fillsScreen());
        t->setProperty ("monitors", tuner.monitorsKeepGuitars());       // under "guitars": the monitors still carry them

        // Every mixer channel that is switched on, and whether it goes silent while tuning.
        juce::Array<juce::var> channels;
        const auto silent = engine.tunerChannels (true);

        for (const auto& ch : engine.mixer().setup().channel)
            if (ch.on && ch.name.isNotEmpty())
            {
                auto* o = obj();
                o->setProperty ("name", ch.name);
                o->setProperty ("silent", silent.contains (ch.name));
                channels.add (juce::var (o));
            }

        t->setProperty ("channels", channels);
        t->setProperty ("minReference", Tuner::minReference);
        t->setProperty ("maxReference", Tuner::maxReference);
        s->setProperty ("tuner", juce::var (t));
    }
    s->setProperty ("types", typesJson());      // last: it describes every effect type the parts above used
    return juce::var (s);
}

juce::var RemoteApi::live()
{
    auto* l = obj();

    const auto snap = engine.looper().snapshot();
    {
        auto* lo = obj();
        lo->setProperty ("running", snap.running);
        lo->setProperty ("bpm", snap.tempoKnown ? tidy (snap.bpm) : 0.0);
        lo->setProperty ("bank", snap.bank);
        lo->setProperty ("cur", snap.currentTrack);
        lo->setProperty ("beat", snap.beatInBar);
        lo->setProperty ("bar", (double) snap.bar);
        lo->setProperty ("ms", snap.msSinceBeat > 1.0e8 ? -1.0 : std::round (snap.msSinceBeat));

        juce::Array<juce::var> tracks;

        for (const auto& t : snap.track)
        {
            auto* o = obj();
            o->setProperty ("s", (int) t.state);
            o->setProperty ("a", t.hasAudio);
            o->setProperty ("d", t.hasOverdub);
            o->setProperty ("u", t.undone || t.overdubUndone);
            o->setProperty ("l", t.lanes);
            o->setProperty ("sec", tidy (t.seconds));
            o->setProperty ("bars", tidy (t.bars));
            o->setProperty ("pos", tidy ((double) t.position));
            o->setProperty ("pk", tidy ((double) t.peak));
            tracks.add (juce::var (o));
        }

        lo->setProperty ("tracks", tracks);
        l->setProperty ("looper", juce::var (lo));
    }

    auto& mixer = engine.mixer();

    if (mixer.isOn())
    {
        auto* mo = obj();
        juce::Array<juce::var> ch, gr, out, fxp;

        for (int c = 0; c < numMixChannels; ++c)
        {
            ch.add (tidy ((double) mixer.takeChannelPeak (c)));
            gr.add (tidy ((double) mixer.channelCompDb (c)));
        }

        for (int o = 0; o < numMixOutputs; ++o)
        {
            juce::Array<juce::var> pair;
            pair.add (tidy ((double) mixer.takeOutputPeak (o, 0)));
            pair.add (tidy ((double) mixer.takeOutputPeak (o, 1)));
            out.add (pair);
        }

        for (int f = 0; f < numMixFx; ++f)
            fxp.add (tidy ((double) mixer.takeFxPeak (f)));

        mo->setProperty ("ch", ch);
        mo->setProperty ("gr", gr);
        mo->setProperty ("out", out);
        mo->setProperty ("fx", fxp);
        mo->setProperty ("limit", tidy ((double) mixer.outputLimiterDb (0)));
        mo->setProperty ("guard", mixer.guardTripped());
        l->setProperty ("mix", juce::var (mo));
    }

    // ---- the live picture behind the EQ curve (only while a screen is asking for it) ----
    if (mixer.lookingAt() != Mixer::Look::nothing)
    {
        auto* ro = obj();
        ro->setProperty ("where", mixer.lookingAt() == Mixer::Look::channel ? "ch" : "out");
        ro->setProperty ("i", mixer.lookingAtIndex());
        ro->setProperty ("before", mixer.lookingBefore());
        ro->setProperty ("lowHz", spectrumLowHz);
        ro->setProperty ("perOctave", spectrumBandsPerOctave);

        juce::Array<juce::var> db;
        for (const float v : mixer.picture())
            db.add ((int) std::lround (v));
        ro->setProperty ("db", db);
        l->setProperty ("rta", juce::var (ro));
    }

    // ---- the recorder ----
    {
        auto& rec = mixer.recorder();
        auto* ro = obj();
        const auto room = mixer.recordRoom (Mixer::recordingFolder());
        juce::Array<juce::var> pk;
        pk.add (tidy ((double) mixer.takeRecordPeak (0)));
        pk.add (tidy ((double) mixer.takeRecordPeak (1)));
        ro->setProperty ("on", rec.isRecording());
        ro->setProperty ("sec", std::round (rec.seconds()));
        ro->setProperty ("lost", (double) rec.lostSamples());
        ro->setProperty ("files", rec.fileNames().size());
        ro->setProperty ("folder", rec.folder().getFileName());        // its name only: never a path from the disk
        ro->setProperty ("free", tidy (room.freeGb));
        ro->setProperty ("hours", tidy (room.hours));
        ro->setProperty ("pk", pk);
        ro->setProperty ("note", mixer.recordNote());
        l->setProperty ("rec", juce::var (ro));
    }

    // ---- the virtual soundcheck, and a copy to USB ----
    {
        auto& sc = engine.soundcheck();
        auto* co = obj();
        co->setProperty ("on", sc.isPlaying());
        co->setProperty ("name", sc.name());
        co->setProperty ("pos", std::round (sc.positionSeconds()));
        co->setProperty ("len", std::round (sc.lengthSeconds()));
        co->setProperty ("missed", sc.missedBlocks());
        co->setProperty ("loops", engine.loopReplay().isArmed() ? engine.loopReplay().statusLine() : juce::String());
        l->setProperty ("check", juce::var (co));

        const auto p = engine.recordings().progress();
        auto* po = obj();
        po->setProperty ("on", p.running);
        po->setProperty ("done", tidy (p.done));
        po->setProperty ("now", p.now);
        po->setProperty ("result", p.result);
        po->setProperty ("failed", p.failed);
        l->setProperty ("copy", juce::var (po));
    }

    // ---- the break music, and the knob that works it and the venue presets ----
    {
        auto& music = engine.breakMusic();
        auto* bo = obj();
        bo->setProperty ("playing", music.isPlaying());
        bo->setProperty ("up", music.isUp());
        bo->setProperty ("fade", tidy ((double) music.fadeNow()));
        bo->setProperty ("pos", std::round (music.positionSeconds()));
        bo->setProperty ("len", std::round (music.lengthSeconds()));
        bo->setProperty ("mix", music.mixName());
        bo->setProperty ("level", tidy ((double) music.levelDb()));
        bo->setProperty ("line", music.statusLine());
        bo->setProperty ("knobOnBreak", engine.venueKnob().onBreakMusic());
        bo->setProperty ("knob", engine.venueKnob().screen (juce::Time::getMillisecondCounterHiRes()));
        l->setProperty ("breakMusic", juce::var (bo));

        auto& tuner = engine.tuner();
        const auto r = tuner.reading();
        auto* to = obj();
        to->setProperty ("on", tuner.isOn());
        to->setProperty ("heard", r.heard);
        to->setProperty ("name", r.name);
        to->setProperty ("cents", tidy (r.cents));
        to->setProperty ("hz", tidy (r.hz));
        to->setProperty ("socket", r.socket);
        to->setProperty ("strobing", r.strobing);
        to->setProperty ("strength", tidy ((double) r.strength));
        l->setProperty ("tuner", juce::var (to));
    }

    const auto drops = engine.dropoutStats();
    l->setProperty ("load", tidy (drops.peakLoad));
    l->setProperty ("late", drops.lateCallbacks);
    l->setProperty ("open", engine.isOpen());
    return juce::var (l);
}

//==============================================================================
juce::String RemoteApi::doSongs (const juce::String& op, const juce::var& c, juce::var& answer)
{
    if (op == "song.load")
    {
        const int index = (int) c.getProperty ("index", -1);
        if (index < 0 || index >= book.numEntries())
            return "That song is not in the list any more.";
        book.load (index, "the remote screen");
        return {};
    }

    if (op == "song.program")
    {
        const int program = (int) c.getProperty ("program", 0);
        if (program < 1 || program > 128)
            return "A program number is 1 to 128.";
        book.programChange (program, "the remote screen");
        return {};
    }

    if (op == "song.keep")          { answer = book.keepForCurrent(); return {}; }
    if (op == "song.again")         { book.applySounds ("the remote screen asked"); return {}; }

    if (op == "song.new")
        return book.addEntry (c.getProperty ("name", {}).toString(), (int) c.getProperty ("program", 0));

    if (op == "layout.of")
    {
        const int index = (int) c.getProperty ("index", -1);
        if (index < 0 || index >= book.numEntries())
            return "That song is not in the list any more.";
        answer = layout::toJson (book.layoutOf (index));
        return {};
    }

    if (op == "layout.try" || op == "layout.save" || op == "layout.saveAs")
    {
        auto song = book.song();
        const auto wanted = c.getProperty ("layout", {});

        if (const auto error = layout::fromJson (wanted, song); error.isNotEmpty())
            return error;

        const double tempo = (double) wanted.getProperty ("tempo", 0.0);
        const bool withTempo = (bool) c.getProperty ("withTempo", false) && tempo >= 30.0 && tempo <= 300.0;

        if (withTempo)
        {
            song.savedBpm = tempo;
            song.tempoGuide = true;
        }

        if (op == "layout.try")     { book.tryLayout (song); return {}; }
        const auto error = op == "layout.save" ? book.saveLayout (song, withTempo)
                                               : book.addEntryWithLayout (c.getProperty ("name", {}).toString(), (int) c.getProperty ("program", 0), song, withTempo);
        if (error.isEmpty())
            answer = book.lastNote();

        return error;
    }

    if (op == "layout.forget")
    {
        // Only trying a layout (nothing of its own in the file): dropping the try is the whole job, and not an error.
        const bool onlyTrying = book.layoutIsTried() && book.currentEntry().sounds.layoutLines.isEmpty();
        const auto result = book.forgetLayout();
        answer = onlyTrying ? juce::String ("The looper is back on the song's saved layout.") : book.lastNote();
        return onlyTrying ? juce::String() : result;
    }

    return "unknown";
}

juce::String RemoteApi::doEffects (const juce::String& op, const juce::var& c, juce::var& answer)
{
    auto& rack = engine.effects();

    if (op == "fx.enabled")
    {
        rack.setEnabled ((bool) c.getProperty ("on", false));
        rack.markChanged();
        return {};
    }

    const int chain = (int) c.getProperty ("chain", -1);

    if (chain < 0 || chain >= EffectsRack::numChains)
        return "There is no chain with that number.";

    if (op == "fx.chainOn")
    {
        auto r = rack.routing (chain);
        r.on = (bool) c.getProperty ("on", false);
        rack.setRouting (chain, r);
        rack.markChanged();
        return {};
    }

    if (op == "fx.preset")
    {
        const auto name = c.getProperty ("name", {}).toString();
        const auto file = book.presetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".txt");

        if (name.isEmpty() || ! file.existsAsFile())
            return "There is no preset called \"" + name + "\".";

        const auto m = fx::ChainModel::fromText (file.loadFileAsString().toStdString());
        rack.chain (chain).setModel (m);
        rack.setRecipe (chain, juce::StringArray (name));
        rack.setBaseline (chain, m);
        rack.markChanged();
        return {};
    }

    // ---- the row itself: add, save. (Remove and move are further down, with the other orders that name a slot.) ----
    if (op == "fx.add")
    {
        const auto type = c.getProperty ("type", {}).toString().toStdString();
        auto m = rack.chain (chain).model();
        bool offered = false;

        for (const auto& t : fx::effectTypes())
            offered = offered || t.id == type;

        if (! offered)
            return "There is no effect called \"" + juce::String (type) + "\" to add.";

        if ((int) m.slots.size() >= fx::Chain::maxSlots)
            return "This chain is full: it holds " + juce::String (fx::Chain::maxSlots) + " effects. Remove one first.";

        // After the effect that is open on the page, or at the end. A new effect starts from its own defaults, switched on.
        const int at = juce::jlimit (0, (int) m.slots.size(), (int) c.getProperty ("at", (int) m.slots.size()));
        m.slots.insert (m.slots.begin() + at, fx::SlotModel { type, true, {}, {} });
        rack.chain (chain).setModel (m);
        rack.markChanged();
        answer = "Added at place " + juce::String (at + 1) + ".";
        return {};
    }

    if (op == "fx.save")
    {
        bool saved = false;
        const auto line = saveChainAsPreset (rack, chain, book.presetsFolder(), c.getProperty ("name", {}).toString(), saved);

        if (! saved)
            return line;

        answer = line;
        return {};
    }

    if (op == "fx.update")
    {
        answer = saveBackToPresets (rack, chain, book.presetsFolder());
        return {};
    }

    const int slot = (int) c.getProperty ("slot", -1);
    const auto& model = rack.chain (chain).model();

    if (slot < 0 || slot >= (int) model.slots.size())
        return "That effect is not there any more.";

    if (op == "fx.slotOn")
    {
        rack.chain (chain).setSlotOn (slot, (bool) c.getProperty ("on", false));
        rack.markChanged();
        return {};
    }

    if (op == "fx.remove")
    {
        auto m = model;
        m.slots.erase (m.slots.begin() + slot);
        rack.chain (chain).setModel (m);
        rack.markChanged();
        return {};
    }

    if (op == "fx.move")
    {
        auto m = model;
        const int to = (int) c.getProperty ("to", -1);

        if (to < 0 || to >= (int) m.slots.size())
            return "It is already at that end of the chain.";

        // Its settings travel with it (the model always holds the live values).
        auto moving = m.slots[(size_t) slot];
        m.slots.erase (m.slots.begin() + slot);
        m.slots.insert (m.slots.begin() + to, std::move (moving));
        rack.chain (chain).setModel (m);
        rack.markChanged();
        return {};
    }

    if (op == "fx.param")
    {
        const auto id = c.getProperty ("id", {}).toString().toStdString();
        const auto& params = factsFor (model.slots[(size_t) slot].type).params;

        for (size_t p = 0; p < params.size(); ++p)
            if (params[p].id == id)
            {
                rack.chain (chain).setParam (slot, (int) p, juce::jlimit (params[p].min, params[p].max, (float) (double) c.getProperty ("value", 0.0)));
                rack.markChanged();
                return {};
            }

        return "That effect has no setting called \"" + juce::String (id) + "\".";
    }

    return "unknown";
}

juce::String RemoteApi::doRecord (const juce::String& op, const juce::var& c, juce::var& answer)
{
    auto& mixer = engine.mixer();

    if (op == "rec.start")
    {
        if (mixer.recorder().isRecording())
            return "It is already recording.";

        if (const auto error = mixer.startRecording (Mixer::recordingFolder()); error.isNotEmpty())
            return error;

        answer = "Recording " + juce::String (mixer.recorder().fileNames().size()) + " files.";
        return {};
    }

    if (op == "rec.stop")
    {
        if (! mixer.recorder().isRecording())
            return "It is not recording.";

        mixer.stopRecording();
        answer = "Saved " + juce::String (mixer.recorder().fileNames().size()) + " files in the folder " + mixer.recorder().folder().getFileName() + ".";
        return {};
    }

    if (op == "rec.set")
    {
        const auto key = c.getProperty ("key", {}).toString();
        const auto value = c.getProperty ("value", 0.0);
        auto r = mixer.setup().rec;

        if (key == "main")          r.mainMix = (bool) value;
        else if (key == "mix")      r.mix = (bool) value;
        else if (key == "fx")       r.fxTracks = (bool) value;
        else if (key == "check")    r.checkCopy = (bool) value;
        else if (key == "level")    r.levelDb = juce::jlimit (mixOffDb, 10.0f, (float) (double) value);
        else                        return "The recorder has no \"" + key + "\".";

        mixer.setRecord (r);
        return {};
    }

    return "unknown";
}

juce::String RemoteApi::doRecordings (const juce::String& op, const juce::var& c, juce::var& answer)
{
    auto& lib = engine.recordings();
    const auto name = c.getProperty ("name", {}).toString();

    if (op == "check.start")
    {
        if (const auto error = engine.startSoundcheck (name, (bool) c.getProperty ("loop", true), (bool) c.getProperty ("loops", true)); error.isNotEmpty())
            return error;

        answer = "Playing " + name + " through the rig. Its sockets hear the recording until Stop. " + ((bool) c.getProperty ("loops", true) ? engine.loopReplay().statusLine() : juce::String ("Without its loops, kicks and switches."));
        return {};
    }

    if (op == "check.stop")
    {
        if (! engine.soundcheck().isPlaying())
            return "No soundcheck is playing.";

        engine.soundcheck().stop();
        answer = "Stopped. The sockets hear what is plugged in again.";
        return {};
    }

    if (op == "recs.keep")      return lib.setKept (name, (bool) c.getProperty ("on", true));

    if (op == "recs.delete")
    {
        if (const auto error = lib.remove (name); error.isNotEmpty())
            return error;

        answer = "Deleted " + name + ".";
        return {};
    }

    if (op == "recs.clear")
    {
        answer = lib.clear ((bool) c.getProperty ("copied", true));
        return {};
    }

    if (op == "recs.copy")
    {
        juce::StringArray names;

        if (const auto* list = c.getProperty ("names", {}).getArray())
            for (const auto& n : *list)
                names.add (n.toString());

        if (const auto error = lib.copy (names, c.getProperty ("drive", {}).toString()); error.isNotEmpty())
            return error;

        answer = "Copying. Leave the drive in until it says it has finished.";
        return {};
    }

    if (op == "recs.cancel")
    {
        lib.cancelCopy();
        return {};
    }

    return "unknown";
}

juce::String RemoteApi::doMixer (const juce::String& op, const juce::var& c, juce::var& answer)
{
    auto& mixer = engine.mixer();
    const auto& now = mixer.setup();
    const auto key = c.getProperty ("key", {}).toString();
    const auto value = c.getProperty ("value", 0.0);
    const auto db = [&value] (float low, float high) { return juce::jlimit (low, high, (float) (double) value); };

    if (op == "mix.on")
    {
        mixer.setOn ((bool) c.getProperty ("on", false));
        return {};
    }

    if (op == "mix.ch")
    {
        const int i = (int) c.getProperty ("c", -1);
        if (i < 0 || i >= numMixChannels)
            return "There is no channel with that number.";

        auto ch = now.channel[(size_t) i];

        if (key == "fader")             ch.faderDb = db (mixOffDb, 10.0f);
        else if (key == "mute")         ch.mute = (bool) value;
        else if (key == "pan")          ch.pan = db (-1.0f, 1.0f);
        else if (key == "trim")         ch.trimDb = db (-24.0f, 24.0f);
        else if (key == "on")           ch.on = (bool) value;
        else if (key == "mon0")         ch.monDb[0] = db (mixOffDb, 10.0f);
        else if (key == "mon1")         ch.monDb[1] = db (mixOffDb, 10.0f);
        else if (key == "recLevel")     ch.recDb = db (mixOffDb, 12.0f);
        else if (key == "recPan")       ch.recPan = db (-1.0f, 1.0f);
        else if (key == "rec")
        {
            int point = -1;
            for (int p = 0; p < numRecordPoints; ++p)
                if (value.toString() == recordWord ((RecordPoint) p))
                    point = p;

            if (point < 0)
                return "A channel can record \"none\", \"raw\", \"effects\", \"looper\" or \"processed\".";

            ch.record = (RecordPoint) point;
        }
        else if (key.startsWith ("fx") && key.length() == 3)
        {
            const int f = key.getLastCharacter() - '0';
            if (f < 0 || f >= numMixFx)
                return "There is no mixer effect with that number.";

            // A send that belongs to the loaded song is changed in the song's copy, as on the Mixer tab.
            if (mixer.hasSongSend (i, f))
            {
                mixer.setSongSend (i, f, db (mixOffDb, 10.0f));
                return {};
            }

            ch.fxDb[(size_t) f] = db (mixOffDb, 10.0f);
        }
        else
            return "A channel has no \"" + key + "\".";

        mixer.setChannel (i, ch);
        return {};
    }

    if (op == "mix.out")
    {
        const int i = (int) c.getProperty ("o", -1);
        if (i < 0 || i >= numMixOutputs)
            return "There is no output with that number.";

        auto out = now.output[(size_t) i];

        if (key == "level")             out.levelDb = db (mixOffDb, 10.0f);
        else if (key == "mute")         out.mute = (bool) value;
        else                            return "An output has no \"" + key + "\".";

        mixer.setOutput (i, out);
        return {};
    }

    if (op == "mix.fx")
    {
        const int i = (int) c.getProperty ("f", -1);
        if (i < 0 || i >= numMixFx)
            return "There is no mixer effect with that number.";

        auto e = now.fx[(size_t) i];

        if (key == "ret")               e.returnDb = db (mixOffDb, 10.0f);
        else if (key == "on")           e.on = (bool) value;
        else if (key == "mon0")         e.monDb[0] = db (mixOffDb, 10.0f);
        else if (key == "mon1")         e.monDb[1] = db (mixOffDb, 10.0f);
        else if (key == "lowCut")       e.lowCutHz = db (20.0f, 2000.0f);
        else if (key == "highCut")      e.highCutHz = db (1000.0f, 20000.0f);
        else if (key == "recLevel")     e.recDb = db (mixOffDb, 12.0f);
        else                            return "A mixer effect has no \"" + key + "\".";

        mixer.setFx (i, e);
        return {};
    }

    // One setting of one block: a channel's strip, an output's chain, a mixer effect or the EQ in front of it.
    if (op == "mix.param")
    {
        const auto where = c.getProperty ("where", {}).toString();
        const int i = (int) c.getProperty ("i", -1), slot = (int) c.getProperty ("slot", -1);
        const auto id = c.getProperty ("id", {}).toString();
        const float v = (float) (double) value;

        if (where == "ch" && i >= 0 && i < numMixChannels)
        {
            auto ch = now.channel[(size_t) i];
            if (const auto error = setInModel (ch.strip, slot, id, v); error.isNotEmpty())
                return error;
            mixer.setChannel (i, ch);
            return {};
        }

        if (where == "rec")
        {
            auto r = now.rec;
            if (const auto error = setInModel (r.chain, slot, id, v); error.isNotEmpty())
                return error;
            mixer.setRecord (r);
            return {};
        }

        if (where == "out" && i >= 0 && i < numMixOutputs)
        {
            auto out = now.output[(size_t) i];
            if (const auto error = setInModel (out.chain, slot, id, v); error.isNotEmpty())
                return error;
            mixer.setOutput (i, out);
            return {};
        }

        if ((where == "fx" || where == "fxeq") && i >= 0 && i < numMixFx)
        {
            if (where == "fx")
            {
                // A song's own reverb or delay is edited in the song's copy; the venue's is left alone.
                auto m = mixer.effectNow (i);
                if (const auto error = setInModel (m, slot, id, v); error.isNotEmpty())
                    return error;

                if (mixer.hasSongFx (i))
                {
                    mixer.setSongFx (i, m);
                    return {};
                }

                auto e = now.fx[(size_t) i];
                e.effect = m;
                mixer.setFx (i, e);
                return {};
            }

            auto e = now.fx[(size_t) i];
            if (const auto error = setInModel (e.eq, slot, id, v); error.isNotEmpty())
                return error;
            mixer.setFx (i, e);
            return {};
        }

        return "That part of the mixer is not there.";
    }

    // The EQ curve's live picture: look at one channel or output (asked again every couple of seconds while the curve is on a screen).
    if (op == "mix.rta")
    {
        const auto where = c.getProperty ("where", {}).toString();
        const int i = (int) c.getProperty ("i", -1);
        const bool before = (bool) c.getProperty ("before", false);

        if (where == "off")
            mixer.lookAt (Mixer::Look::nothing, 0, false);
        else if (where == "ch" && i >= 0 && i < numMixChannels)
            mixer.lookAt (Mixer::Look::channel, i, before);
        else if (where == "out" && i >= 0 && i < numMixOutputs)
            mixer.lookAt (Mixer::Look::output, i, before);
        else
            return "The live picture can look at a channel (\"ch\") or an output (\"out\"), or be switched \"off\".";

        return {};
    }

    if (op == "mix.preset")
    {
        const auto kind = c.getProperty ("kind", {}).toString();
        const auto name = c.getProperty ("name", {}).toString();

        if (kind == "act")          return mixer.loadAct (name);
        if (kind == "monitors")     return mixer.loadMonitors (name);
        if (kind == "venue")        { answer = mixer.loadVenue (name); return {}; }     // the safe way: fades in, never jumps up

        return "There is no kind of preset called \"" + kind + "\".";
    }

    if (op == "mix.save")
    {
        const auto kind = c.getProperty ("kind", {}).toString();
        const auto name = juce::File::createLegalFileName (c.getProperty ("name", {}).toString().trim());

        if (name.isEmpty())
            return "Type a name to save it under first.";

        if (kind != "act" && kind != "venue" && kind != "monitors")
            return "There is no kind of preset called \"" + kind + "\".";

        const auto error = kind == "act" ? mixer.saveAct (name) : kind == "venue" ? mixer.saveVenue (name) : mixer.saveMonitors (name);

        if (error.isEmpty())
            answer = "Saved the " + juce::String (kind == "act" ? "act" : kind == "venue" ? "venue" : "monitor set-up") + " \"" + name + "\".";

        return error;
    }

    if (op == "mix.keepFx")
    {
        answer = book.keepMixerEffect ((int) c.getProperty ("f", -1), false);
        return {};
    }

    return "unknown";
}

juce::String RemoteApi::doBreak (const juce::String& op, const juce::var& c, juce::var& answer)
{
    auto& music = engine.breakMusic();

    if (op == "break.play")
    {
        const auto name = c.getProperty ("name", {}).toString();
        return music.play (name.isNotEmpty() ? name : music.mixName(), (bool) c.getProperty ("fromStart", false));
    }

    if (op == "break.stop")     { music.stop(); return {}; }
    if (op == "break.up")       { music.bringUp(); return {}; }
    if (op == "break.down")     { music.fadeDown(); return {}; }

    if (op == "break.level")
    {
        music.setLevelDb ((float) (double) c.getProperty ("value", (double) music.levelDb()));
        return {};
    }

    if (op == "break.set")
    {
        const auto key = c.getProperty ("key", {}).toString();
        const auto value = c.getProperty ("value", 0.0);
        auto s = music.settings();

        if (key == "fadeDown")          s.fadeDownSeconds = (double) value;
        else if (key == "fadeUp")       s.fadeUpSeconds = (double) value;
        else if (key == "onSwitch")     s.onSwitch = (bool) value;
        else if (key == "onSong")       s.onSong = (bool) value;
        else if (key == "onSound")      s.onSound = (bool) value;
        else if (key == "soundDb")      s.soundDb = (float) (double) value;
        else                            return "The break music has no \"" + key + "\".";

        music.setSettings (s);      // which limits every figure
        return {};
    }

    // The knob (ENC 5), as the buttons on the Mixer tab work it.
    if (op == "knob")
    {
        const auto what = c.getProperty ("do", {}).toString();
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        auto& knob = engine.venueKnob();

        if (what == "left")         answer = knob.turn (-1, nowMs);
        else if (what == "right")   answer = knob.turn (1, nowMs);
        else if (what == "push")    answer = knob.push (nowMs);
        else if (what == "hold")    answer = knob.hold (nowMs);
        else                        return "The knob can go left or right, be pushed or be held.";

        return {};
    }

    if (op == "tuner.on")
    {
        engine.tuner().setOn ((bool) c.getProperty ("on", false));
        return {};
    }

    if (op == "tuner.set")
    {
        const auto key = c.getProperty ("key", {}).toString();
        const auto value = c.getProperty ("value", 0.0);
        auto& tuner = engine.tuner();

        if (key == "socket")            tuner.setSocket ((int) value);
        else if (key == "reference")    tuner.setReference ((double) value);
        else if (key == "mutes")        tuner.setMutes ((bool) value);
        else if (key == "fullScreen")   tuner.setFillsScreen ((bool) value);
        else if (key == "monitors")     tuner.setMonitorsKeepGuitars ((bool) value);
        else if (key == "silence")
        {
            Tuner::Silence how;

            if (! Tuner::silenceFromWord (value.toString(), how))
                return juce::String ("The tuner can silence \"guitars\", \"main\" or \"nothing\".");

            tuner.setSilence (how);
        }
        else if (key == "channel")
        {
            const auto name = c.getProperty ("name", {}).toString();

            if (name.trim().isEmpty())
                return juce::String ("Which channel?");

            tuner.setGoesSilent (name, (bool) value);
        }
        else                            return "The tuner has no \"" + key + "\".";

        return {};
    }

    return "unknown";
}

juce::var RemoteApi::command (const juce::var& c)
{
    const auto op = c.getProperty ("op", {}).toString();
    juce::var answer;
    juce::String error = "unknown";

    if (op == "pedal")
    {
        pedal ((int) c.getProperty ("sw", -1), (bool) c.getProperty ("down", false));
        return ok();
    }

    if (op == "looper.clearAll")
    {
        engine.looper().clearAll();
        error = {};
    }
    else if (op == "report")
    {
        answer = wholeReport != nullptr ? wholeReport() : juce::String();
        error = {};
    }
    else if (op.startsWith ("song.") || op.startsWith ("layout."))  error = doSongs (op, c, answer);
    else if (op.startsWith ("fx."))                                 error = doEffects (op, c, answer);
    else if (op.startsWith ("break.") || op.startsWith ("tuner.") || op == "knob")              error = doBreak (op, c, answer);
    else if (op.startsWith ("mix."))                                error = doMixer (op, c, answer);
    else if (op.startsWith ("rec."))                                error = doRecord (op, c, answer);
    else if (op.startsWith ("recs.") || op.startsWith ("check."))   error = doRecordings (op, c, answer);

    if (error == "unknown")
        return failed ("This build of the rig does not know \"" + op + "\". The page may be newer than the rig.");

    if (error.isNotEmpty())
        return failed (error);

    // The EQ curve's live picture asks again every couple of seconds and changes nothing a window shows.
    if (op == "mix.rta")
        return ok ({});

    ++done;
    listsRead = false;

    if (onRigChanged != nullptr && op != "report" && op != "layout.of")
        onRigChanged();

    auto result = ok (answer.isString() ? answer.toString() : juce::String());

    if (op == "layout.of")
        result.getDynamicObject()->setProperty ("layout", answer);

    return result;
}

} // namespace gigrig
