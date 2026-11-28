#include "Mixer.h"

#include "effects/Registry.h"

namespace gigrig
{

namespace
{
    float gainOf (float db) noexcept        { return db <= mixOffDb + 0.01f ? 0.0f : juce::Decibels::decibelsToGain (db); }

    float peakOf (const float* x, int n) noexcept
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return juce::jmax (-range.getStart(), range.getEnd());
    }

    void raise (std::atomic<float>& peak, float value) noexcept
    {
        if (value > peak.load (std::memory_order_relaxed))
            peak.store (value, std::memory_order_relaxed);
    }

    /** dst += src * (a gain gliding from 'start' by 'step' each sample). */
    void addGliding (float* dst, const float* src, float start, float step, int n) noexcept
    {
        if (start == 0.0f && step == 0.0f)
            return;

        if (step == 0.0f)
        {
            juce::FloatVectorOperations::addWithMultiply (dst, src, start, n);
            return;
        }

        for (int i = 0; i < n; ++i)
            dst[i] += src[i] * (start + step * (float) i);
    }

    fx::SlotModel slot (const char* type, bool on, std::vector<std::pair<std::string, float>> params = {})
    {
        return { type, on, std::move (params), {} };
    }

    /** The same blocks in the same order as 'shape', taking each block's settings from 'from'
        where it has that block. So a hand-edited or older preset can never change what a
        channel or an output is made of. */
    fx::ChainModel conform (const fx::ChainModel& from, const fx::ChainModel& shape)
    {
        fx::ChainModel m = shape;

        for (auto& want : m.slots)
            for (const auto& have : from.slots)
                if (have.type == want.type)
                {
                    want = have;
                    break;
                }

        return m;
    }

    fx::ChainModel conformFx (const fx::ChainModel& from)
    {
        fx::ChainModel m;

        for (const auto& s : from.slots)
            if (std::find (mixerFxChoices().begin(), mixerFxChoices().end(), s.type) != mixerFxChoices().end())
            {
                m.slots.push_back (s);
                break;
            }

        if (m.slots.empty())
            m.slots.push_back (slot ("plate", true, { { "reverb", 5.0f } }));

        return m;
    }

    /** The EQ on what goes into a mixer effect: always exactly one mixeq, switched on (flat until moved). */
    fx::ChainModel conformFxEq (const fx::ChainModel& from)
    {
        fx::ChainModel m;

        for (const auto& s : from.slots)
            if (s.type == "mixeq")
            {
                m.slots.push_back (s);
                break;
            }

        if (m.slots.empty())
            m.slots.push_back (slot ("mixeq", true));

        m.slots[0].on = true;
        return m;
    }

    const char* const outputKeys[numMixOutputs] { "main", "monitor 1", "monitor 2" };
    const char* const recordWords[] { "none", "raw", "effects", "looper", "processed" };
    const char* const tapWords[] { "pre", "eq", "all" };

    juce::String num (float v)              { return juce::String (v, 2); }

    //==============================================================================
    // The text files. A file is a list of sections; a section starts with a line in square
    // brackets. Plain sections hold "key=value" lines; "processing" and "effect" sections hold
    // a row of blocks in the same form as an effects preset (slot=..., then its settings).
    // THE WORDS HERE ARE SAVED ON TOM'S DISK: never rename a section or a key, only add.

    void writeChannel (juce::String& t, int c, const MixChannelSetup& s)
    {
        t << "[channel " << (c + 1) << "]\n";
        t << "name=" << s.name << "\n";
        t << "on=" << (s.on ? 1 : 0) << "\n";
        t << "socket=" << (s.socket + 1) << "\n";
        t << "trim=" << num (s.trimDb) << "\n";
        t << "fader=" << num (s.faderDb) << "\n";
        t << "mute=" << (s.mute ? 1 : 0) << "\n";
        t << "pan=" << num (s.pan) << "\n";
        for (int m = 0; m < numMixMonitors; ++m)
            t << "monitor" << (m + 1) << "=" << num (s.monDb[(size_t) m]) << "\n";
        for (int f = 0; f < numMixFx; ++f)
            t << "fx" << (f + 1) << "=" << num (s.fxDb[(size_t) f]) << "\n";
        t << "record=" << recordWords[(int) s.record] << "\n";
        t << "direct=" << (s.directReturn + 1) << "\n";
        t << "link=" << (s.link ? 1 : 0) << "\n";
        t << "reclevel=" << num (s.recDb) << "\n";
        t << "recpan=" << num (s.recPan) << "\n";
        t << "[channel " << (c + 1) << " processing]\n" << juce::String (s.strip.toText());
    }

    void writeFx (juce::String& t, int f, const MixFxSetup& s)
    {
        t << "[fx " << (f + 1) << "]\n";
        t << "name=" << s.name << "\n";
        t << "on=" << (s.on ? 1 : 0) << "\n";
        t << "lowcut=" << num (s.lowCutHz) << "\n";
        t << "highcut=" << num (s.highCutHz) << "\n";
        t << "return=" << num (s.returnDb) << "\n";
        for (int m = 0; m < numMixMonitors; ++m)
            t << "monitor" << (m + 1) << "=" << num (s.monDb[(size_t) m]) << "\n";
        t << "reclevel=" << num (s.recDb) << "\n";
        t << "[fx " << (f + 1) << " effect]\n" << juce::String (s.effect.toText());
        t << "[fx " << (f + 1) << " eq]\n" << juce::String (s.eq.toText());
    }

    void writeOutput (juce::String& t, int o, const MixOutputSetup& s, bool withSockets)
    {
        t << "[output " << outputKeys[o] << "]\n";
        t << "name=" << s.name << "\n";
        t << "level=" << num (s.levelDb) << "\n";
        t << "mute=" << (s.mute ? 1 : 0) << "\n";
        t << "mono=" << (s.mono ? 1 : 0) << "\n";
        if (o > 0)
        {
            t << "prefader=" << (s.preFader ? 1 : 0) << "\n";
            t << "tap=" << tapWords[(int) s.tap] << "\n";
        }
        if (withSockets)
        {
            t << "return=" << (s.firstReturn + 1) << "\n";
            t << "pair=" << (s.pair ? 1 : 0) << "\n";
        }
        t << "[output " << outputKeys[o] << " processing]\n" << juce::String (s.chain.toText());
    }

    /** The Recording tab's own settings: which mixes are written, and the recording mix's level, EQ and limiter. */
    void writeRecord (juce::String& t, const MixRecordSetup& s)
    {
        t << "[recording]\n";
        t << "mainmix=" << (s.mainMix ? 1 : 0) << "\n";
        t << "mix=" << (s.mix ? 1 : 0) << "\n";
        t << "effects=" << (s.fxTracks ? 1 : 0) << "\n";
        t << "soundcheck=" << (s.checkCopy ? 1 : 0) << "\n";
        t << "level=" << num (s.levelDb) << "\n";
        t << "[recording processing]\n" << juce::String (s.chain.toText());
    }

    struct Section
    {
        juce::String name;
        juce::StringPairArray values;
        juce::String body;
    };

    std::vector<Section> parseSections (const juce::String& text)
    {
        std::vector<Section> sections;

        for (const auto& raw : juce::StringArray::fromLines (text))
        {
            const auto line = raw.trim();

            if (line.startsWithChar ('[') && line.endsWithChar (']'))
            {
                sections.push_back ({ line.substring (1, line.length() - 1).trim().toLowerCase(), {}, {} });
                continue;
            }

            if (sections.empty() || line.isEmpty() || line.startsWithChar ('#'))
                continue;

            auto& s = sections.back();
            s.body << line << "\n";

            if (line.containsChar ('='))
                s.values.set (line.upToFirstOccurrenceOf ("=", false, false).trim(), line.fromFirstOccurrenceOf ("=", false, false).trim());
        }

        return sections;
    }

    enum Parts { channelParts = 1, fxParts = 2, mainPart = 4, monitorParts = 8, socketParts = 16, recordParts = 32 };

    /** Reads the sections a file has into 's' (what a file leaves out stays as it was). Returns the parts it found. */
    int readInto (MixerSetup& s, const juce::String& text, int wanted)
    {
        int found = 0;

        for (const auto& sec : parseSections (text))
        {
            const auto words = juce::StringArray::fromTokens (sec.name, " ", {});
            const auto has = [&sec] (const char* key) { return sec.values.containsKey (key); };
            const auto f = [&sec] (const char* key) { return sec.values[key].getFloatValue(); };
            const auto b = [&sec] (const char* key) { return sec.values[key].getIntValue() != 0; };

            if (words[0] == "channel" && (wanted & channelParts) != 0)
            {
                const int c = words[1].getIntValue() - 1;
                if (c < 0 || c >= numMixChannels)
                    continue;

                auto& ch = s.channel[(size_t) c];
                found |= channelParts;

                if (words.size() > 2 && words[2] == "processing")
                {
                    ch.strip = conform (fx::ChainModel::fromText (sec.body.toStdString()), defaultStripModel());
                    continue;
                }

                if (has ("name"))    ch.name = sec.values["name"];
                if (has ("on"))      ch.on = b ("on");
                if (has ("socket"))  ch.socket = juce::jlimit (-1, numMixSockets - 1, sec.values["socket"].getIntValue() - 1);
                if (has ("trim"))    ch.trimDb = juce::jlimit (-24.0f, 24.0f, f ("trim"));
                if (has ("fader"))   ch.faderDb = juce::jlimit (mixOffDb, 10.0f, f ("fader"));
                if (has ("mute"))    ch.mute = b ("mute");
                if (has ("pan"))     ch.pan = juce::jlimit (-1.0f, 1.0f, f ("pan"));
                for (int m = 0; m < numMixMonitors; ++m)
                    if (const auto key = "monitor" + juce::String (m + 1); sec.values.containsKey (key))
                        ch.monDb[(size_t) m] = juce::jlimit (mixOffDb, 10.0f, sec.values[key].getFloatValue());
                for (int k = 0; k < numMixFx; ++k)
                    if (const auto key = "fx" + juce::String (k + 1); sec.values.containsKey (key))
                        ch.fxDb[(size_t) k] = juce::jlimit (mixOffDb, 10.0f, sec.values[key].getFloatValue());
                if (has ("link"))    ch.link = b ("link") && c + 1 < numMixChannels;
                if (has ("direct"))  ch.directReturn = juce::jlimit (-1, 7, sec.values["direct"].getIntValue() - 1);
                if (has ("record"))
                    for (int r = 0; r < numRecordPoints; ++r)
                        if (sec.values["record"] == recordWords[r])
                            ch.record = (RecordPoint) r;
                if (has ("reclevel")) ch.recDb = juce::jlimit (mixOffDb, 12.0f, f ("reclevel"));
                if (has ("recpan"))   ch.recPan = juce::jlimit (-1.0f, 1.0f, f ("recpan"));
            }
            else if (words[0] == "recording" && (wanted & recordParts) != 0)
            {
                found |= recordParts;

                if (words.size() > 1 && words[1] == "processing")
                {
                    s.rec.chain = conform (fx::ChainModel::fromText (sec.body.toStdString()), defaultRecordModel());
                    continue;
                }

                if (has ("mainmix")) s.rec.mainMix = b ("mainmix");
                if (has ("mix"))     s.rec.mix = b ("mix");
                if (has ("effects")) s.rec.fxTracks = b ("effects");
                if (has ("soundcheck")) s.rec.checkCopy = b ("soundcheck");
                if (has ("level"))   s.rec.levelDb = juce::jlimit (mixOffDb, 10.0f, f ("level"));
            }
            else if (words[0] == "fx" && (wanted & fxParts) != 0)
            {
                const int k = words[1].getIntValue() - 1;
                if (k < 0 || k >= numMixFx)
                    continue;

                auto& fxs = s.fx[(size_t) k];
                found |= fxParts;

                if (words.size() > 2 && words[2] == "effect")
                {
                    fxs.effect = conformFx (fx::ChainModel::fromText (sec.body.toStdString()));
                    continue;
                }

                if (words.size() > 2 && words[2] == "eq")
                {
                    fxs.eq = conformFxEq (fx::ChainModel::fromText (sec.body.toStdString()));
                    continue;
                }

                if (has ("name"))    fxs.name = sec.values["name"];
                if (has ("on"))      fxs.on = b ("on");
                if (has ("lowcut"))  fxs.lowCutHz = juce::jlimit (20.0f, 1000.0f, f ("lowcut"));
                if (has ("highcut")) fxs.highCutHz = juce::jlimit (1000.0f, 20000.0f, f ("highcut"));
                if (has ("return"))  fxs.returnDb = juce::jlimit (mixOffDb, 10.0f, f ("return"));
                if (has ("reclevel")) fxs.recDb = juce::jlimit (mixOffDb, 12.0f, f ("reclevel"));
                for (int m = 0; m < numMixMonitors; ++m)
                    if (const auto key = "monitor" + juce::String (m + 1); sec.values.containsKey (key))
                        fxs.monDb[(size_t) m] = juce::jlimit (mixOffDb, 10.0f, sec.values[key].getFloatValue());
            }
            else if (words[0] == "output")
            {
                const bool processing = words[words.size() - 1] == "processing";
                const auto key = juce::StringArray (words.begin() + 1, words.size() - 1 - (processing ? 1 : 0)).joinIntoString (" ");
                int o = -1;
                for (int k = 0; k < numMixOutputs; ++k)
                    if (key == outputKeys[k])
                        o = k;

                const int part = o == 0 ? mainPart : monitorParts;
                if (o < 0 || (wanted & part) == 0)
                    continue;

                auto& out = s.output[(size_t) o];
                found |= part;

                if (processing)
                {
                    out.chain = conform (fx::ChainModel::fromText (sec.body.toStdString()), defaultOutputModel());
                    continue;
                }

                if (has ("name") && sec.values["name"].isNotEmpty()) out.name = sec.values["name"];
                if (has ("level"))    out.levelDb = juce::jlimit (mixOffDb, 10.0f, f ("level"));
                if (has ("mute"))     out.mute = b ("mute");
                if (has ("mono"))     out.mono = b ("mono");
                if (has ("prefader")) out.preFader = b ("prefader");
                if (has ("tap"))
                    for (int k = 0; k < 3; ++k)
                        if (sec.values["tap"] == tapWords[k])
                            out.tap = (TapPoint) k;

                if ((wanted & socketParts) != 0 || o > 0)
                {
                    if (has ("return")) out.firstReturn = juce::jlimit (-1, 7, sec.values["return"].getIntValue() - 1);
                    if (has ("pair"))   out.pair = b ("pair");
                }
            }
        }

        return found;
    }

    juce::String cleanName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).trim();
    }

    juce::String dbText (float db)
    {
        return db <= mixOffDb + 0.01f ? juce::String ("off") : juce::String (db, 1) + " dB";
    }

    juce::String peakText (float gain)
    {
        return gain <= 1.0e-5f ? juce::String ("silent") : juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB";
    }
}

//==============================================================================
fx::ChainModel defaultStripModel()
{
    fx::ChainModel m;
    // Gate, warmth (valve preamp) and EQ are the tone; then the clean compressor and two
    // analog-style ones (1176 style, LA-2A style). Only the EQ starts switched on.
    m.slots.push_back (slot ("gate", false));
    m.slots.push_back (slot ("enhancer", false));       // the gentle gate for a voice (Tom, 27 Nov 2026)
    m.slots.push_back (slot ("warmth", false));
    m.slots.push_back (slot ("mixeq", true));
    m.slots.push_back (slot ("mixcomp", false));
    m.slots.push_back (slot ("comp", false));
    m.slots.push_back (slot ("la2a", false));
    return m;
}

fx::ChainModel defaultOutputModel()
{
    fx::ChainModel m;
    m.slots.push_back (slot ("mixeq", true));
    m.slots.push_back (slot ("notches", true));
    m.slots.push_back (slot ("mixcomp", false));
    m.slots.push_back (slot ("buscomp", false));
    m.slots.push_back (slot ("tape", false));
    m.slots.push_back (slot ("limiter", true));
    m.slots.push_back (slot ("linedelay", true));
    return m;
}

fx::ChainModel defaultRecordModel()
{
    // The recording mix: an EQ curve of its own, the bus compressor for those who want it, and
    // a limiter so nothing in the file goes over full level.
    fx::ChainModel m;
    m.slots.push_back (slot ("mixeq", true));
    m.slots.push_back (slot ("buscomp", false));
    m.slots.push_back (slot ("limiter", true));
    return m;
}

const char* recordWord (RecordPoint p)
{
    return recordWords[juce::jlimit (0, numRecordPoints - 1, (int) p)];
}

juce::String recordPointName (RecordPoint p)
{
    // "raw", "after effects" and "after looper" are in the names of files Tom already has: leave them.
    static const char* const names[] { "nothing", "raw", "after effects", "after looper", "after channel processing" };
    return names[juce::jlimit (0, numRecordPoints - 1, (int) p)];
}

const std::vector<std::string>& mixerFxChoices()
{
    static const std::vector<std::string> list { "plate", "room", "spring", "digitaldelay", "tapedelay", "analogdelay" };
    return list;
}

juce::String mixOutputName (int output)
{
    return output == 0 ? "Main" : "Monitor " + juce::String (output);
}

MixerSetup defaultMixerSetup()
{
    MixerSetup s;

    // The design doc's channel order, on the sockets Tom's CQ12T test wiring uses (the looper's
    // own defaults: vocal 6, acoustic 7, electric 8, bass 9), and the stomp box on socket 10 (Tom, 26 Nov 2026).
    static const struct { const char* name; int socket; bool on; float lowCut; } start[numMixChannels] {
        { "Vocal",           5, true,  90.0f },
        { "Acoustic",        6, true,  70.0f },
        { "Stomp",           9, true,  20.0f },
        { "Electric",        7, true,  70.0f },
        { "Bass",            8, true,  20.0f },
        { "Duo vocal",       0, false, 90.0f },
        { "Duo instrument",  1, false, 60.0f },
        { "Vocal (in-ears)", 5, false, 90.0f } };

    for (int c = 0; c < numMixChannels; ++c)
    {
        auto& ch = s.channel[(size_t) c];
        ch.name = start[c].name;
        ch.socket = start[c].socket;
        ch.on = start[c].on;
        ch.strip = defaultStripModel();
        ch.strip.slots[stripEq].setParam ("lowcut", start[c].lowCut);
        ch.monDb = { 0.0f, 0.0f };
    }

    // The stomp has no effects chain: its EQ and compressor are here (Tom, 25 Nov 2026).
    s.channel[2].strip.slots[stripComp].on = true;
    s.channel[2].strip.slots[stripComp].setParam ("ratio", 4.0f);
    // The in-ear copy of the vocal feeds no speakers, only Tom's monitor.
    s.channel[7].faderDb = mixOffDb;
    s.channel[7].monDb = { 0.0f, mixOffDb };
    s.channel[7].record = RecordPoint::none;

    s.fx[0] = { "Vocal plate", true, {}, 180.0f, 9000.0f, 0.0f, { mixOffDb, mixOffDb } };
    s.fx[0].effect.slots.push_back (slot ("plate", true, { { "reverb", 5.0f }, { "decay", 5.0f }, { "predelay", 30.0f }, { "tone", 6.0f }, { "lowcut", 20.0f } }));
    s.fx[1] = { "Vocal delay", true, {}, 200.0f, 6000.0f, 0.0f, { mixOffDb, mixOffDb } };
    s.fx[1].effect.slots.push_back (slot ("digitaldelay", true, { { "follow", 1.0f }, { "time", 375.0f }, { "feedback", 30.0f }, { "mix", 100.0f }, { "tone", 8.0f }, { "mod", 2.0f }, { "throw", 0.0f } }));
    s.fx[2] = { "Instrument room", true, {}, 120.0f, 10000.0f, 0.0f, { mixOffDb, mixOffDb } };
    s.fx[2].effect.slots.push_back (slot ("room", true, { { "reverb", 5.0f }, { "size", 1.0f }, { "decay", 5.0f }, { "tone", 5.0f }, { "lowcut", 20.0f } }));
    s.fx[3] = { "Spare (tape echo)", false, {}, 150.0f, 5000.0f, 0.0f, { mixOffDb, mixOffDb } };
    s.fx[3].effect.slots.push_back (slot ("tapedelay", true, { { "follow", 0.0f }, { "time", 300.0f }, { "feedback", 35.0f }, { "mix", 100.0f } }));

    // On the CQ12T: the main mix on the USB stereo channel (returns 5 and 6), the two
    // monitors on ST1 left and right (returns 3 and 4).
    for (int o = 0; o < numMixOutputs; ++o)
    {
        auto& out = s.output[(size_t) o];
        out.name = o == 0 ? juce::String ("Main") : (o == 1 ? juce::String ("Tom") : juce::String ("Duo partner"));
        out.chain = defaultOutputModel();
        out.mono = true;
        out.firstReturn = o == 0 ? 4 : o + 1;
        out.pair = o == 0;
    }

    s.rec.chain = defaultRecordModel();
    return s;
}

//==============================================================================
Mixer::Mixer()
{
    root = presetRoot();
    now = defaultMixerSetup();
    pushRecord (true);

    for (int c = 0; c < numMixChannels; ++c)   pushChannel (c, true);
    for (int f = 0; f < numMixFx; ++f)         pushFx (f, true);
    for (int o = 0; o < numMixOutputs; ++o)    pushOutput (o, true);

    dirty = false;
}

Mixer::~Mixer()
{
    rec.stop();
}

void Mixer::pushChannel (int c, bool first)
{
    auto& a = chans[(size_t) c];
    auto& s = now.channel[(size_t) c];
    s.strip = conform (s.strip, defaultStripModel());

    a.socket.store (s.socket);
    a.record.store ((int) s.record);
    a.direct.store (s.directReturn);
    a.trim.store (juce::Decibels::decibelsToGain (s.trimDb));
    a.fader.store (gainOf (s.faderDb));
    a.pan.store (s.pan);
    a.recGain.store (gainOf (s.recDb));
    a.recPan.store (s.recPan);
    a.mute.store (s.mute);
    for (int m = 0; m < numMixMonitors; ++m)   a.mon[(size_t) m].store (gainOf (s.monDb[(size_t) m]));
    for (int f = 0; f < numMixFx; ++f)         a.fx[(size_t) f].store (gainOf (sendDb (c, f)));
    a.on.store (s.on);

    fx::ChainModel tone, squeeze;
    tone.slots.assign (s.strip.slots.begin(), s.strip.slots.begin() + stripSplit);
    squeeze.slots.assign (s.strip.slots.begin() + stripSplit, s.strip.slots.end());

    if (first)  { a.strip.setModel (tone);        a.strip2.setModel (squeeze); }
    else        { a.strip.setModelGently (tone);  a.strip2.setModelGently (squeeze); }

    int mask = 0;
    for (const auto& ch : now.channel)
        if (ch.on && ch.socket >= 0 && ch.socket < numMixSockets)
            mask |= 1 << ch.socket;
    socketMask.store (mask);
}

void Mixer::pushFx (int f, bool first)
{
    auto& a = fxs[(size_t) f];
    auto& s = now.fx[(size_t) f];
    s.effect = conformFx (s.effect);
    s.eq = conformFxEq (s.eq);

    if (first)  a.pre.setModel (s.eq);
    else        a.pre.setModelGently (s.eq);

    a.ret.store (gainOf (s.returnDb));
    a.recGain.store (gainOf (s.recDb));
    for (int m = 0; m < numMixMonitors; ++m)   a.mon[(size_t) m].store (gainOf (s.monDb[(size_t) m]));
    a.filter.setParam ("lowcut", s.lowCutHz);
    a.filter.setParam ("highcut", s.highCutHz);
    a.on.store (s.on);

    if (songFxHas[(size_t) f])
        songFxModel[(size_t) f] = conformFx (songFxModel[(size_t) f]);

    if (first)  a.chain.setModel (effectNow (f));
    else        a.chain.setModelGently (effectNow (f));
}

void Mixer::pushOutput (int o, bool first)
{
    auto& a = outs[(size_t) o];
    auto& s = now.output[(size_t) o];
    s.chain = conform (s.chain, defaultOutputModel());

    a.level.store (gainOf (s.levelDb));
    a.mute.store (s.mute);
    a.mono.store (s.mono);
    a.preFader.store (s.preFader);
    a.tap.store ((int) s.tap);
    a.pair.store (s.pair);
    a.firstReturn.store (s.firstReturn);

    const auto set = (size_t) (o == 0 ? mainSet.load() : 0);

    for (auto& side : a.chain[set])
    {
        if (first)  side.setModel (s.chain);
        else        side.setModelGently (s.chain);
    }

    if (first && o == 0)
        for (auto& side : a.chain[1 - set])
            side.setModel (s.chain);
}

void Mixer::pushRecord (bool first)
{
    auto& s = now.rec;
    s.chain = conform (s.chain, defaultRecordModel());
    recOut.level.store (gainOf (s.levelDb));

    for (auto& side : recOut.chain)
    {
        if (first)  side.setModel (s.chain);
        else        side.setModelGently (s.chain);
    }
}

void Mixer::setRecord (const MixRecordSetup& s)
{
    now.rec = s;
    pushRecord();
    actDirty = true;
    markChanged();
}

void Mixer::followLinks()
{
    // A linked pair is one stereo channel: the right side takes every setting from the left
    // except its name and socket; the left pans left and the right pans right.
    for (int c = 0; c + 1 < numMixChannels; ++c)
    {
        auto& left = now.channel[(size_t) c];

        if (c > 0 && now.channel[(size_t) c - 1].link)
            left.link = false;                              // a right side cannot start a pair of its own

        if (! left.link)
            continue;

        auto& right = now.channel[(size_t) c + 1];
        const auto name = right.name;
        const int socket = right.socket;
        left.pan = -1.0f;
        left.recPan = -1.0f;
        right = left;
        right.name = name;
        right.socket = socket;
        right.link = false;
        right.pan = 1.0f;
        right.recPan = 1.0f;
    }

    now.channel[(size_t) numMixChannels - 1].link = false;
}

void Mixer::setChannel (int c, const MixChannelSetup& s)
{
    if (c < 0 || c >= numMixChannels)
        return;

    const bool wasPaired = now.channel[(size_t) c].link || (c > 0 && now.channel[(size_t) c - 1].link);

    if (c > 0 && now.channel[(size_t) c - 1].link)
    {
        // A change made on the right side of a stereo pair is a change to the pair.
        auto& left = now.channel[(size_t) c - 1];
        const auto name = left.name;
        const int socket = left.socket;
        left = s;
        left.name = name;
        left.socket = socket;
        left.link = true;
        now.channel[(size_t) c].name = s.name;
        now.channel[(size_t) c].socket = s.socket;
    }
    else
    {
        // Unlinking leaves both sides in the middle again, not hard left and right.
        if (now.channel[(size_t) c].link && ! s.link && c + 1 < numMixChannels)
            now.channel[(size_t) c + 1].pan = now.channel[(size_t) c + 1].recPan = 0.0f;

        now.channel[(size_t) c] = s;

        if (wasPaired && ! s.link)
            now.channel[(size_t) c].pan = now.channel[(size_t) c].recPan = 0.0f;
    }

    followLinks();

    if (c > 0)                      pushChannel (c - 1);
    if (c + 1 < numMixChannels)     pushChannel (c + 1);
    pushChannel (c);
    actDirty = true;
    markChanged();
}

float Mixer::sendDb (int c, int f) const noexcept
{
    // The right side of a stereo pair follows the left in this too.
    const int from = c > 0 && now.channel[(size_t) c - 1].link ? c - 1 : c;
    return songHas[(size_t) from][(size_t) f] ? songDb[(size_t) from][(size_t) f] : now.channel[(size_t) c].fxDb[(size_t) f];
}

void Mixer::setSongSends (const std::vector<SongSend>& sends)
{
    for (auto& row : songHas)
        row.fill (false);

    for (const auto& s : sends)
        if (s.channel >= 0 && s.channel < numMixChannels && s.fx >= 0 && s.fx < numMixFx)
        {
            songHas[(size_t) s.channel][(size_t) s.fx] = true;
            songDb[(size_t) s.channel][(size_t) s.fx] = juce::jlimit (mixOffDb, 10.0f, s.db);
        }

    for (int c = 0; c < numMixChannels; ++c)
        for (int f = 0; f < numMixFx; ++f)
            chans[(size_t) c].fx[(size_t) f].store (gainOf (sendDb (c, f)));

    markChanged();
}

std::vector<Mixer::SongSend> Mixer::songSends() const
{
    std::vector<SongSend> sends;

    for (int c = 0; c < numMixChannels; ++c)
        for (int f = 0; f < numMixFx; ++f)
            if (songHas[(size_t) c][(size_t) f])
                sends.push_back ({ c, f, songDb[(size_t) c][(size_t) f] });

    return sends;
}

void Mixer::setSongSend (int c, int f, float db)
{
    auto sends = songSends();
    sends.erase (std::remove_if (sends.begin(), sends.end(), [c, f] (const SongSend& s) { return s.channel == c && s.fx == f; }), sends.end());
    sends.push_back ({ c, f, db });
    setSongSends (sends);
}

void Mixer::clearSongSend (int c, int f)
{
    auto sends = songSends();
    sends.erase (std::remove_if (sends.begin(), sends.end(), [c, f] (const SongSend& s) { return s.channel == c && s.fx == f; }), sends.end());
    setSongSends (sends);
}

void Mixer::setSongFx (const std::vector<SongFx>& list)
{
    std::array<bool, numMixFx> had = songFxHas;
    songFxHas.fill (false);

    for (const auto& s : list)
        if (s.fx >= 0 && s.fx < numMixFx)
        {
            songFxHas[(size_t) s.fx] = true;
            songFxModel[(size_t) s.fx] = conformFx (s.effect);
        }

    for (int f = 0; f < numMixFx; ++f)
        if (had[(size_t) f] || songFxHas[(size_t) f])
            pushFx (f);

    markChanged();
}

std::vector<Mixer::SongFx> Mixer::songFx() const
{
    std::vector<SongFx> list;

    for (int f = 0; f < numMixFx; ++f)
        if (songFxHas[(size_t) f])
            list.push_back ({ f, songFxModel[(size_t) f] });

    return list;
}

void Mixer::setSongFx (int f, const fx::ChainModel& m)
{
    if (f < 0 || f >= numMixFx)
        return;

    songFxHas[(size_t) f] = true;
    songFxModel[(size_t) f] = conformFx (m);
    pushFx (f);
    markChanged();
}

void Mixer::clearSongFx (int f)
{
    if (! hasSongFx (f))
        return;

    songFxHas[(size_t) f] = false;
    pushFx (f);
    markChanged();
}

void Mixer::setFx (int f, const MixFxSetup& s)
{
    if (f < 0 || f >= numMixFx)
        return;

    now.fx[(size_t) f] = s;
    pushFx (f);
    venueDirty = true;
    markChanged();
}

void Mixer::setOutput (int o, const MixOutputSetup& s)
{
    if (o < 0 || o >= numMixOutputs)
        return;

    // Which sockets the main mix leaves on belongs to the rig, not to the venue.
    const auto& old = now.output[(size_t) o];
    auto before = old, after = s;
    before.firstReturn = after.firstReturn = 0;
    before.pair = after.pair = true;
    const bool sameSound = before.levelDb == after.levelDb && before.mute == after.mute && before.mono == after.mono
                           && before.preFader == after.preFader && before.name == after.name && before.chain.toText() == after.chain.toText();

    now.output[(size_t) o] = s;
    pushOutput (o);

    if (o == 0)     venueDirty = venueDirty || ! sameSound;
    else            monitorsDirty = true;

    markChanged();
}

void Mixer::setAll (const MixerSetup& s)
{
    now = s;
    followLinks();
    for (int c = 0; c < numMixChannels; ++c)   pushChannel (c);
    for (int f = 0; f < numMixFx; ++f)         pushFx (f);
    for (int o = 0; o < numMixOutputs; ++o)    pushOutput (o);
    pushRecord();
    markChanged();
}

//==============================================================================
juce::File Mixer::presetRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Mixer");
}

juce::File Mixer::presetFolder (Preset kind, const juce::File& rootFolder)
{
    static const char* const names[] { "Acts", "Venues", "Monitors", "Outputs" };
    return rootFolder.getChildFile (names[(int) kind]);
}

juce::File Mixer::recordingFolder()
{
    if (recordingFolderOverride != juce::File())
        return recordingFolderOverride;

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Recordings");
}

juce::StringArray Mixer::presetNames (Preset kind) const
{
    juce::StringArray names;

    for (const auto& f : presetFolder (kind, root).findChildFiles (juce::File::findFiles, false, "*.txt"))
        names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

juce::StringArray Mixer::venueList() const   { return presetNames (Preset::venue); }

juce::String Mixer::actText() const
{
    juce::String t ("# GigRig mixer: an ACT. Every channel for one line-up: what is plugged in where, levels, EQ, compressor and sends.\n"
                    "# Loading it leaves the venue and the monitors alone.\n");
    for (int c = 0; c < numMixChannels; ++c)
        writeChannel (t, c, now.channel[(size_t) c]);
    writeRecord (t, now.rec);       // the recording mix is a balance of these channels, so it travels with them
    return t;
}

juce::String Mixer::venueText() const
{
    juce::String t ("# GigRig mixer: a VENUE (or PA). The main output's whole chain (level, EQ, notches, compressor, limiter, delay)\n"
                    "# and the mixer's reverbs and delays. Loading it leaves the act and the monitors alone.\n");
    writeOutput (t, 0, now.output[0], false);
    for (int f = 0; f < numMixFx; ++f)
        writeFx (t, f, now.fx[(size_t) f]);
    return t;
}

juce::String Mixer::monitorsText() const
{
    juce::String t ("# GigRig mixer: MONITORS. How the two monitor mixes go out: sockets, mono or stereo, level and processing.\n");
    for (int o = 1; o < numMixOutputs; ++o)
        writeOutput (t, o, now.output[(size_t) o], true);
    return t;
}

juce::String Mixer::outputText (int output) const
{
    juce::String t ("# GigRig mixer: ONE OUTPUT's processing (EQ, notches, compressor, limiter, delay). It can be loaded into any output.\n");
    t << "[processing]\n" << juce::String (now.output[(size_t) juce::jlimit (0, numMixOutputs - 1, output)].chain.toText());
    return t;
}

namespace
{
    juce::String writePreset (const juce::File& folder, const juce::String& name, const juce::String& text)
    {
        const auto clean = cleanName (name);

        if (clean.isEmpty())
            return "Give it a name first.";

        if (! folder.createDirectory())
            return "Could not make the folder " + folder.getFullPathName();

        return folder.getChildFile (clean + ".txt").replaceWithText (text) ? juce::String() : "Could not write " + clean + ".txt";
    }

    juce::String readPreset (const juce::File& folder, const juce::String& name, juce::String& text)
    {
        const auto file = folder.getChildFile (cleanName (name) + ".txt");

        if (! file.existsAsFile())
            return "There is no preset called '" + name + "'.";

        text = file.loadFileAsString();
        return {};
    }
}

juce::String Mixer::saveAct (const juce::String& name)
{
    if (const auto error = writePreset (presetFolder (Preset::act, root), name, actText()); error.isNotEmpty())
        return error;

    act = cleanName (name);
    actDirty = false;
    markChanged();
    return {};
}

juce::String Mixer::loadAct (const juce::String& name)
{
    juce::String text;
    if (const auto error = readPreset (presetFolder (Preset::act, root), name, text); error.isNotEmpty())
        return error;

    auto next = now;
    if ((readInto (next, text, channelParts | recordParts) & channelParts) == 0)
        return "'" + name + "' holds no channels.";

    now.channel = next.channel;
    now.rec = next.rec;             // an act saved before the Recording tab has none, and leaves it as it is
    for (int c = 0; c < numMixChannels; ++c)
        pushChannel (c);
    pushRecord();

    act = cleanName (name);
    actDirty = false;
    markChanged();
    return {};
}

juce::String Mixer::saveVenue (const juce::String& name)
{
    if (const auto error = writePreset (presetFolder (Preset::venue, root), name, venueText()); error.isNotEmpty())
        return error;

    venue = cleanName (name);
    venueDirty = false;
    markChanged();
    return {};
}

juce::String Mixer::saveMonitors (const juce::String& name)
{
    if (const auto error = writePreset (presetFolder (Preset::monitors, root), name, monitorsText()); error.isNotEmpty())
        return error;

    monitors = cleanName (name);
    monitorsDirty = false;
    markChanged();
    return {};
}

juce::String Mixer::loadMonitors (const juce::String& name)
{
    juce::String text;
    if (const auto error = readPreset (presetFolder (Preset::monitors, root), name, text); error.isNotEmpty())
        return error;

    auto next = now;
    if ((readInto (next, text, monitorParts) & monitorParts) == 0)
        return "'" + name + "' holds no monitor outputs.";

    for (int o = 1; o < numMixOutputs; ++o)
    {
        now.output[(size_t) o] = next.output[(size_t) o];
        pushOutput (o);
    }

    monitors = cleanName (name);
    monitorsDirty = false;
    markChanged();
    return {};
}

juce::String Mixer::saveOutput (int output, const juce::String& name)
{
    return writePreset (presetFolder (Preset::output, root), name, outputText (output));
}

juce::String Mixer::loadOutput (int output, const juce::String& name)
{
    if (output < 0 || output >= numMixOutputs)
        return "No such output.";

    juce::String text;
    if (const auto error = readPreset (presetFolder (Preset::output, root), name, text); error.isNotEmpty())
        return error;

    for (const auto& sec : parseSections (text))
        if (sec.name == "processing" || sec.name.endsWith (" processing"))
        {
            auto s = now.output[(size_t) output];
            s.chain = conform (fx::ChainModel::fromText (sec.body.toStdString()), defaultOutputModel());
            setOutput (output, s);
            return {};
        }

    return "'" + name + "' holds no output processing.";
}

juce::String Mixer::loadVenue (const juce::String& name)
{
    juce::String text;
    if (const auto error = readPreset (presetFolder (Preset::venue, root), name, text); error.isNotEmpty())
        return error;

    auto next = now;
    if ((readInto (next, text, mainPart | fxParts) & mainPart) == 0)
        return "'" + name + "' holds no main output.";

    return applyVenue (next, cleanName (name));
}

juce::String Mixer::applyVenue (const MixerSetup& from, const juce::String& name)
{
    const auto old = now.output[0];
    auto main = from.output[0];
    main.firstReturn = old.firstReturn;         // the sockets belong to the rig
    main.pair = old.pair;
    main.chain = conform (main.chain, defaultOutputModel());

    // Never louder in a jump: a venue saved louder than the main is now keeps the level as it
    // is, and says by how much it differs so it can be raised by hand.
    juce::String note = "Venue '" + name + "' loaded.";
    const float savedDb = main.levelDb;

    if (savedDb > old.levelDb + 0.05f)
    {
        main.levelDb = old.levelDb;
        note << " It was saved " << juce::String (savedDb - old.levelDb, 1) << " dB louder than the main is now: the level has been left at "
             << dbText (old.levelDb) << ", raise it by hand.";
    }
    else if (savedDb < old.levelDb - 0.05f)
        note << " The main level fades down to its " << dbText (savedDb) << ".";

    // The new chain is built on the set that is not in use, and the audio thread fades across.
    const int idle = 1 - mainSet.load();
    for (auto& side : outs[0].chain[(size_t) idle])
        side.setModel (main.chain);

    now.output[0] = main;
    outs[0].level.store (gainOf (main.levelDb));
    outs[0].mute.store (main.mute);
    outs[0].mono.store (main.mono);
    slowLevel.store (true);
    mainSet.store (idle);

    now.fx = from.fx;
    for (int f = 0; f < numMixFx; ++f)
        pushFx (f);

    venue = name;
    venueDirty = false;
    lastVenueNote = note;
    markChanged();
    return note;
}

//==============================================================================
juce::String Mixer::turnVenue (int steps, double nowMs)
{
    const auto list = venueList();

    if (list.isEmpty())
        return "No venue presets are saved yet.";

    if (browseIndex < 0 || nowMs - browseAtMs > browseTimeoutMs)
        browseIndex = juce::jmax (0, list.indexOf (venue));     // browsing starts from the venue in use

    browseIndex = juce::jlimit (0, list.size() - 1, browseIndex + steps);
    browseAtMs = nowMs;
    armedFor.clear();
    return venueScreen (nowMs);
}

juce::String Mixer::pushVenue (double nowMs)
{
    const auto list = venueList();
    const bool browsing = browseIndex >= 0 && browseIndex < list.size() && nowMs - browseAtMs <= browseTimeoutMs;

    if (! browsing)
    {
        browseIndex = -1;
        return "Turn to choose a venue first. " + venueScreen (nowMs);
    }

    const auto name = list[browseIndex];

    if (venueDirty && ! (armedFor == name && nowMs - armedAtMs <= browseTimeoutMs))
    {
        armedFor = name;
        armedAtMs = browseAtMs = nowMs;
        return "Tonight's changes to '" + (venue.isEmpty() ? juce::String ("the venue settings") : venue) + "' are not saved. Push again to load '" + name + "' over them.";
    }

    browseIndex = -1;
    armedFor.clear();
    return loadVenue (name);
}

juce::String Mixer::venueScreen (double nowMs) const
{
    const auto inUse = (venue.isEmpty() ? juce::String ("none loaded") : "'" + venue + "'") + (venueDirty ? " (changed, not saved)" : "");
    const auto list = venueList();

    if (browseIndex >= 0 && browseIndex < list.size() && nowMs - browseAtMs <= browseTimeoutMs)
        return "Browsing: '" + list[browseIndex] + "'. Push to load it. In use: " + inUse + ".";

    return "Venue in use: " + inUse + ".";
}

//==============================================================================
std::vector<Mixer::RecordTrack> Mixer::recordTracks() const
{
    std::vector<RecordTrack> tracks;
    const bool mixing = wanted.load();

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];

        if (! ch.on || ch.record == RecordPoint::none || ch.socket < 0)
            continue;

        // With the mixer off there is no looper point to tap: the channel's socket after its effects is the nearest thing.
        const bool fallsBack = ! mixing && (ch.record == RecordPoint::afterLooper || ch.record == RecordPoint::afterProcessing);
        tracks.push_back ({ c, ch.name + " (" + recordPointName (fallsBack ? RecordPoint::afterEffects : ch.record) + ")", 1, ch.record == RecordPoint::raw ? ch.socket : -1 });
    }

    if (mixing)
    {
        if (now.rec.fxTracks)
            for (int f = 0; f < numMixFx; ++f)
                if (now.fx[(size_t) f].on)
                    tracks.push_back ({ recSlotFx + f, "Effect " + juce::String (f + 1) + " " + now.fx[(size_t) f].name, 1 });

        if (now.rec.mainMix)    tracks.push_back ({ recSlotMain, "Main mix", 2 });
        if (now.rec.mix)        tracks.push_back ({ recSlotMix, "Recording mix", 2 });
    }

    // The soundcheck copy: every socket a channel uses, raw, once (a socket already recorded raw needs no second file).
    if (now.rec.checkCopy)
    {
        unsigned covered = 0;

        for (const auto& t : tracks)
            if (t.socket >= 0 && t.socket < 32)
                covered |= 1u << t.socket;

        for (int c = 0; c < numMixChannels; ++c)
        {
            const auto& ch = now.channel[(size_t) c];

            if (! ch.on || ch.socket < 0 || ch.socket >= 32 || (covered & (1u << ch.socket)) != 0)
                continue;

            covered |= 1u << ch.socket;
            tracks.push_back ({ recSlotRaw + c, ch.name + " (raw for soundcheck)", 1, ch.socket });
        }
    }

    return tracks;
}

Mixer::RecordRoom Mixer::recordRoom (const juce::File& parentFolder) const
{
    RecordRoom r;
    auto folder = parentFolder;

    while (! folder.isDirectory() && folder.getParentDirectory() != folder)
        folder = folder.getParentDirectory();

    r.freeGb = (double) folder.getBytesFreeOnVolume() / 1.0e9;
    const double fileRate = rate >= 88000.0 ? rate * 0.5 : rate;

    for (const auto& t : recordTracks())
        r.gbPerHour += (double) t.channels * fileRate * 3.0 * 3600.0 / 1.0e9;      // 24-bit: three bytes a sample

    r.hours = r.gbPerHour > 0.0 ? juce::jmax (0.0, r.freeGb - recordStopGb) / r.gbPerHour : 0.0;
    return r;
}

juce::String Mixer::startRecording (const juce::File& parentFolder)
{
    std::vector<MixRecorder::Track> tracks;
    const auto list = recordTracks();

    for (const auto& t : list)
        tracks.push_back ({ t.slot, t.name, t.channels });

    if (tracks.empty())
        return "Nothing is set to be recorded. On the Recording tab, give at least one channel a record point.";

    if (const auto room = recordRoom (parentFolder); room.freeGb < recordStopGb + 0.5)
        return "The recordings drive has only " + juce::String (room.freeGb, 1) + " GB left. Clear some old recordings first.";

    const auto stamp = juce::Time::getCurrentTime();
    const auto folder = parentFolder.getChildFile (stamp.formatted ("%Y-%m-%d %H-%M-%S"));
    const auto error = rec.start (folder, tracks, rate, capacity);

    if (error.isEmpty())
    {
        recNote.clear();
        recParent = parentFolder;
        recTicks = 0;

        // A note in the folder, so the files explain themselves a year from now.
        juce::String about;
        about << "Recorded by GigRig on " << stamp.formatted ("%d %b %Y at %H:%M") << ".\n"
              << "Every file starts at the same moment and is " << juce::String (rec.fileRate() / 1000.0, 1) << " kHz, 24-bit.\n"
              << "The mixer was " << (wanted.load() ? "on" : "OFF, so channels are recorded at their sockets and there are no mixes") << ".\n\n";

        for (const auto& name : rec.fileNames())
            about << name << "\n";

        // The lines a virtual soundcheck reads: which input socket each raw file came from. Never reword them.
        const auto names = rec.fileNames();
        bool any = false;

        for (size_t i = 0; i < list.size() && (int) i < names.size(); ++i)
            if (list[i].socket >= 0)
            {
                about << (any ? "" : "\nFor a virtual soundcheck (these lines are read by the app):\n") << "soundcheck: " << names[(int) i] << " = socket " << (list[i].socket + 1) << "\n";
                any = true;
            }

        about << "\nWhat the words in the names mean:\n"
                 "  raw: the plain input socket, before any effect.\n"
                 "  raw for soundcheck: the same, kept so this recording can be played back through the rig as a virtual soundcheck.\n"
                 "  after effects: after the guitar or vocal effects chain, before the looper.\n"
                 "  after looper: the live sound and its loops together, before the channel's own gate, EQ and compressors (loops carry the processing they were recorded with).\n"
                 "  after channel processing: the live sound and its loops together, after the channel's gate, EQ and compressors, before its fader.\n"
                 "  Effect: one of the mixer's reverbs or delays alone (only its wet sound), at the level it has in the main mix.\n"
                 "  Main mix: what the PA was sent.\n"
                 "  Recording mix: the separate balance set on the Recording tab.\n";
        folder.getChildFile ("About this recording.txt").replaceWithText (about);
    }

    return error;
}

void Mixer::stopRecording()
{
    rec.stop();
}

juce::String Mixer::recordingReportText() const
{
    juce::String t;
    t << "RECORDING\n";
    t << rec.statusText() << "\n";

    if (recNote.isNotEmpty())
        t << "RECORDING WARNING: " << recNote << "\n";

    juce::StringArray names;
    for (const auto& track : recordTracks())
        names.add (track.name + (track.channels == 2 ? " [stereo]" : ""));

    t << "Pressing Record now would write " << names.size() << " files: " << (names.isEmpty() ? juce::String ("none") : names.joinIntoString ("; ")) << ".\n";

    if (! wanted.load())
        t << "The mixer is off, so only the channels' sockets can be recorded (plain, or after their effects): no loops, no main mix and no recording mix.\n";

    const auto room = recordRoom (recordingFolder());
    t << "Recordings drive: " << juce::String (room.freeGb, 1) << " GB free; these files take " << juce::String (room.gbPerHour, 2) << " GB an hour, so about "
      << juce::String (room.hours, 1) << " hours fit. A recording stops by itself with " << juce::String (recordStopGb, 0) << " GB left.\n";

    t << "Recording mix: " << (now.rec.mix ? "written" : "not written") << ", level " << dbText (now.rec.levelDb)
      << ", EQ " << (now.rec.chain.slots[recEq].on ? "on" : "off") << ", bus compressor " << (now.rec.chain.slots[recBus].on ? "on" : "off")
      << ", limiter " << (now.rec.chain.slots[recLimiter].on ? "on at " + juce::String (now.rec.chain.slots[recLimiter].param ("ceiling", -1.0f), 1) + " dB" : juce::String ("off"))
      << ". Main mix file: " << (now.rec.mainMix ? "written" : "not written") << ". Reverbs and delays as files of their own: " << (now.rec.fxTracks ? "yes" : "no")
      << ". Raw copy for a virtual soundcheck: " << (now.rec.checkCopy ? "yes" : "no") << ".\n";

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];
        if (ch.on)
            t << "  " << ch.name << ": records " << recordPointName (ch.record) << "; in the recording mix "
              << (ch.recDb <= mixOffDb + 0.01f ? juce::String ("not at all") : (ch.recDb > 0.0f ? "+" : "") + juce::String (ch.recDb, 1) + " dB against the main mix")
              << ", pan " << (std::abs (ch.recPan) < 0.01f ? juce::String ("centre") : juce::String (ch.recPan, 2)) << "\n";
    }

    for (int f = 0; f < numMixFx; ++f)
        if (now.fx[(size_t) f].on && std::abs (now.fx[(size_t) f].recDb) > 0.01f)
            t << "  " << now.fx[(size_t) f].name << ": in the recording mix " << (now.fx[(size_t) f].recDb <= mixOffDb + 0.01f ? juce::String ("not at all") : juce::String (now.fx[(size_t) f].recDb, 1) + " dB against the main mix") << "\n";

    return t;
}

//==============================================================================
float Mixer::channelCompDb (int c) const
{
    // The compressor and the 1176-style insert together.
    float down = 0.0f;

    for (int k : { stripComp, stripInsert, stripOpto })
        if (const auto* e = chans[(size_t) c].strip2.effect (k - stripSplit); e != nullptr && now.channel[(size_t) c].strip.slots[(size_t) k].on)
            down += std::abs (e->meterValue());

    return down;
}

float Mixer::outputCompDb (int o) const
{
    // The compressor and the bus compressor together.
    float down = 0.0f;

    for (int k : { outComp, outBus })
        if (const auto* e = outs[(size_t) o].chain[(size_t) (o == 0 ? mainSet.load() : 0)][0].effect (k); e != nullptr && now.output[(size_t) o].chain.slots[(size_t) k].on)
            down += std::abs (e->meterValue());

    return down;
}

float Mixer::outputLimiterDb (int o) const
{
    const auto* e = outs[(size_t) o].chain[(size_t) (o == 0 ? mainSet.load() : 0)][0].effect (outLimiter);
    return e != nullptr && now.output[(size_t) o].chain.slots[outLimiter].on ? e->meterValue() : 0.0f;
}

void Mixer::tick()
{
    for (auto& c : chans)   { c.strip.collectGarbage(); c.strip2.collectGarbage(); }
    for (auto& f : fxs)     f.chain.collectGarbage();
    for (auto& side : recOut.chain) side.collectGarbage();

    // A recording never fills the drive: every few seconds the room left is looked at, away from the sound.
    if (rec.isRecording() && ++recTicks >= 50)
    {
        recTicks = 0;

        if (const auto room = recordRoom (recParent); room.freeGb < recordStopGb)
        {
            rec.stop();
            recNote = "The recording stopped by itself after " + juce::String (rec.seconds() / 60.0, 1) + " minutes: the drive had less than " + juce::String (recordStopGb, 0) + " GB left.";
        }
    }

    for (auto& o : outs)
        for (auto& set : o.chain)
            for (auto& side : set)
                side.collectGarbage();

    if (dirty)
    {
        dirty = false;
        ticksSinceChange = 1;
    }
    else if (ticksSinceChange > 0 && ++ticksSinceChange > 15)
    {
        ticksSinceChange = 0;
        saveState();
    }
}

juce::File Mixer::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("mixer.txt");
}

juce::String Mixer::stateText() const
{
    juce::String t ("# GigRig mixer: the app's own memory of the Mixer tab, so it comes back as it was left. Safe to delete (it starts afresh).\n");
    t << "[mixer]\n";
    t << "on=" << (wanted.load() ? 1 : 0) << "\n";
    t << "act=" << act << "\n";
    t << "venue=" << venue << "\n";
    t << "monitors=" << monitors << "\n";
    t << "actchanged=" << (actDirty ? 1 : 0) << "\n";
    t << "venuechanged=" << (venueDirty ? 1 : 0) << "\n";
    t << "monitorschanged=" << (monitorsDirty ? 1 : 0) << "\n";

    for (int c = 0; c < numMixChannels; ++c)   writeChannel (t, c, now.channel[(size_t) c]);
    for (int f = 0; f < numMixFx; ++f)         writeFx (t, f, now.fx[(size_t) f]);
    for (int o = 0; o < numMixOutputs; ++o)    writeOutput (t, o, now.output[(size_t) o], true);
    writeRecord (t, now.rec);

    t << "[song sends]\n";
    for (const auto& s : songSends())
        t << "channel " << (s.channel + 1) << " fx " << (s.fx + 1) << "=" << num (s.db) << "\n";

    for (const auto& s : songFx())
        t << "[song fx " << (s.fx + 1) << "]\n" << juce::String (s.effect.toText());

    return t;
}

void Mixer::loadStateText (const juce::String& text)
{
    auto next = now;
    readInto (next, text, channelParts | fxParts | mainPart | monitorParts | socketParts | recordParts);
    setAll (next);

    std::vector<SongSend> sends;

    for (const auto& sec : parseSections (text))
        if (sec.name == "song sends")
            for (const auto& key : sec.values.getAllKeys())
            {
                const auto words = juce::StringArray::fromTokens (key, " ", {});
                if (words.size() == 4 && words[0] == "channel" && words[2] == "fx")
                    sends.push_back ({ words[1].getIntValue() - 1, words[3].getIntValue() - 1, sec.values[key].getFloatValue() });
            }

    setSongSends (sends);

    std::vector<SongFx> songEffects;

    for (const auto& sec : parseSections (text))
        if (sec.name.startsWith ("song fx "))
            songEffects.push_back ({ sec.name.fromLastOccurrenceOf (" ", false, false).getIntValue() - 1, fx::ChainModel::fromText (sec.body.toStdString()) });

    setSongFx (songEffects);

    for (const auto& sec : parseSections (text))
        if (sec.name == "mixer")
        {
            wanted.store (sec.values["on"].getIntValue() != 0);
            act = sec.values["act"];
            venue = sec.values["venue"];
            monitors = sec.values["monitors"];
            actDirty = sec.values["actchanged"].getIntValue() != 0;
            venueDirty = sec.values["venuechanged"].getIntValue() != 0;
            monitorsDirty = sec.values["monitorschanged"].getIntValue() != 0;
        }

    dirty = false;
}

void Mixer::saveState()
{
    if (! memoryOnDisk.load())
        return;

    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (stateText());
}

int Mixer::writeStarterPresets()
{
    // Each is written only if no file of that name is there, so Tom's own changes to one are never overwritten.
    int wrote = 0;
    const auto keep = now;
    const auto keepAct = act, keepVenue = venue, keepMonitors = monitors;
    const bool keepA = actDirty, keepV = venueDirty, keepM = monitorsDirty;
    const auto missing = [this] (Preset kind, const char* name) { return ! presetFolder (kind, root).getChildFile (juce::String (name) + ".txt").existsAsFile(); };
    const auto write = [&] (Preset kind, const char* name, const juce::String& text)
    {
        if (missing (kind, name) && writePreset (presetFolder (kind, root), name, text).isEmpty())
            ++wrote;
    };

    now = defaultMixerSetup();
    write (Preset::act, "Solo", actText());
    write (Preset::venue, "Starting point (flat)", venueText());
    write (Preset::monitors, "Two wedges (mono)", monitorsText());
    write (Preset::output, "Flat", outputText (0));

    // A duo partner who sings and plays one instrument: the two duo channels on, into both monitors.
    now.channel[5].on = now.channel[6].on = true;
    write (Preset::act, "Duo (partner sings and plays)", actText());

    // In-ears: each monitor in stereo on its own pair of sockets.
    now = defaultMixerSetup();
    now.output[1].mono = now.output[2].mono = false;
    now.output[1].firstReturn = 0;  now.output[1].pair = true;
    now.output[2].firstReturn = 2;  now.output[2].pair = true;
    write (Preset::monitors, "Two in-ears (stereo)", monitorsText());

    // A small powered PA whose own limiter acts early and sounds bad: this one catches the peaks first.
    now = defaultMixerSetup();
    now.output[0].chain.slots[outEq].setParam ("lowcut", 50.0f);
    now.output[0].chain.slots[outLimiter].setParam ("ceiling", -6.0f);
    now.output[0].levelDb = -6.0f;
    write (Preset::venue, "Own PA (limiter catches peaks first)", venueText());

    now = keep;
    act = keepAct; venue = keepVenue; monitors = keepMonitors;
    actDirty = keepA; venueDirty = keepV; monitorsDirty = keepM;
    return wrote;
}

//==============================================================================
juce::StringArray Mixer::warnings (const std::function<juce::String (int)>& socketNote) const
{
    juce::StringArray w;

    if (guard.load())
        w.add ("The mixer silenced itself: its main mix was far too loud for a quarter of a second, which is a howl round the CQ, not music. Find the loop (a CQ channel that sends the app's own output back to it), then press Reset.");

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];

        if (! ch.on)
            continue;

        if (ch.socket < 0)
            w.add ("Channel " + juce::String (c + 1) + " (" + ch.name + ") is on but has no socket.");
        else if (socketNote != nullptr)
            if (const auto note = socketNote (ch.socket); note.isNotEmpty())
                w.add ("Channel " + juce::String (c + 1) + " (" + ch.name + "): " + note);

        for (int d = c + 1; d < numMixChannels; ++d)
        {
            const auto& other = now.channel[(size_t) d];
            if (other.on && other.socket == ch.socket && ch.socket >= 0 && ! ch.mute && ! other.mute
                && ch.faderDb > mixOffDb && other.faderDb > mixOffDb)
                w.add ("Channels " + juce::String (c + 1) + " and " + juce::String (d + 1) + " both listen to socket " + juce::String (ch.socket + 1)
                       + " and both feed the main mix, so that sound is in it twice.");
        }
    }

    for (int o = 0; o < numMixOutputs; ++o)
    {
        const auto& out = now.output[(size_t) o];

        if (out.firstReturn < 0)
        {
            if (o == 0)
                w.add ("The main mix is not connected to any return, so nothing will be heard.");
            continue;
        }

        for (int p = o + 1; p < numMixOutputs; ++p)
        {
            const auto& other = now.output[(size_t) p];
            if (other.firstReturn < 0)
                continue;

            const int a0 = out.firstReturn, a1 = out.firstReturn + (out.pair ? 1 : 0);
            const int b0 = other.firstReturn, b1 = other.firstReturn + (other.pair ? 1 : 0);
            if (a0 <= b1 && b0 <= a1)
                w.add (mixOutputName (o) + " and " + mixOutputName (p) + " share a return, so they will be mixed together there.");
        }
    }

    return w;
}

juce::String Mixer::reportText() const
{
    juce::String t;
    t << "MIXER\n";
    t << "Mixer: " << (wanted.load() ? "ON (the app mixes; chains and loops go through it)" : "off (chains and loops go straight to their returns, as before)") << "\n";
    t << "Act: " << (act.isEmpty() ? juce::String ("none loaded") : "'" + act + "'") << (actDirty ? " (changed, not saved)" : "")
      << ".  Venue: " << (venue.isEmpty() ? juce::String ("none loaded") : "'" + venue + "'") << (venueDirty ? " (changed, not saved)" : "")
      << ".  Monitors: " << (monitors.isEmpty() ? juce::String ("none loaded") : "'" + monitors + "'") << (monitorsDirty ? " (changed, not saved)" : "") << ".\n";

    if (lastVenueNote.isNotEmpty())
        t << "Last venue load: " << lastVenueNote << "\n";

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];
        t << "Channel " << (c + 1) << " " << ch.name << ": ";

        if (! ch.on)
        {
            t << "off\n";
            continue;
        }

        t << "socket " << (ch.socket >= 0 ? juce::String (ch.socket + 1) : juce::String ("none")) << ", trim " << juce::String (ch.trimDb, 1) << " dB, fader "
          << (ch.mute ? juce::String ("MUTED") : dbText (ch.faderDb)) << ", pan " << (std::abs (ch.pan) < 0.01f ? juce::String ("centre") : juce::String (ch.pan, 2));
        for (int m = 0; m < numMixMonitors; ++m)
            t << ", monitor " << (m + 1) << " " << dbText (ch.monDb[(size_t) m]);
        for (int f = 0; f < numMixFx; ++f)
            if (sendDb (c, f) > mixOffDb || hasSongSend (c, f))
                t << ", to " << now.fx[(size_t) f].name << " " << dbText (sendDb (c, f)) << (hasSongSend (c, f) ? " (this song's own; the act says " + dbText (ch.fxDb[(size_t) f]) + ")" : juce::String());
        t << ", gate " << (ch.strip.slots[stripGate].on ? "on" : "off") << ", source enhancer " << (ch.strip.slots[stripEnhancer].on ? "on" : "off")
          << ", EQ " << (ch.strip.slots[stripEq].on ? "on" : "off") << " (low cut " << juce::String (ch.strip.slots[stripEq].param ("lowcut", 20.0f), 0) << " Hz)"
          << ", warmth " << (ch.strip.slots[stripWarmth].on ? "on" : "off")
          << ", compressor " << (ch.strip.slots[stripComp].on ? "on" : "off") << ", 1176 style " << (ch.strip.slots[stripInsert].on ? "on" : "off")
          << ", LA-2A style " << (ch.strip.slots[stripOpto].on ? "on" : "off") << (ch.link ? ", STEREO with the next channel" : "")
          << ", records " << recordPointName (ch.record);
        if (ch.directReturn >= 0)
            t << ", direct out on return " << (ch.directReturn + 1);
        t << "\n";
    }

    for (int f = 0; f < numMixFx; ++f)
    {
        const auto& s = now.fx[(size_t) f];
        const auto& effect = effectNow (f);
        const auto* type = fx::findEffectType (effect.slots.empty() ? std::string() : effect.slots[0].type);
        juce::String settings;
        if (! effect.slots.empty())
            for (const auto& p : effect.slots[0].params)
                settings << " " << juce::String (p.first) << "=" << juce::String (p.second, 2);
        t << "Mixer effect " << (f + 1) << " " << s.name << ": " << (s.on ? "on" : "off") << ", " << (type != nullptr ? juce::String (type->name) : juce::String ("?"))
          << (hasSongFx (f) ? " (this song's own; the venue's comes back with the next song)" : "") << (settings.isNotEmpty() ? " [" + settings.trim() + "]" : juce::String())
          << ", wet low cut " << juce::String (s.lowCutHz, 0) << " Hz, wet high cut " << juce::String (s.highCutHz, 0) << " Hz, return " << dbText (s.returnDb);
        for (int m = 0; m < numMixMonitors; ++m)
            if (s.monDb[(size_t) m] > mixOffDb)
                t << ", to monitor " << (m + 1) << " " << dbText (s.monDb[(size_t) m]);
        t << "\n";
    }

    for (int o = 0; o < numMixOutputs; ++o)
    {
        const auto& s = now.output[(size_t) o];
        t << mixOutputName (o) << " output" << (o > 0 ? " (" + s.name + ")" : juce::String()) << ": ";
        if (s.firstReturn < 0)  t << "not connected";
        else                    t << "return " << (s.firstReturn + 1) << (s.pair ? " and " + juce::String (s.firstReturn + 2) : juce::String());
        t << ", " << (s.mono ? "mono" : "stereo") << ", level " << (s.mute ? juce::String ("MUTED") : dbText (s.levelDb));
        if (o > 0)
            t << ", sends taken " << (s.preFader ? "before" : "after") << " the faders";
        if (o > 0)
            t << " and " << (s.tap == TapPoint::beforeProcessing ? "before each channel's processing" : s.tap == TapPoint::afterEq ? "after each channel's EQ (before its compressors)" : "after each channel's compressors");
        t << ", EQ " << (s.chain.slots[outEq].on ? "on" : "off") << ", notches " << (s.chain.slots[outNotches].on ? "on" : "off")
          << ", compressor " << (s.chain.slots[outComp].on ? "on" : "off") << ", bus compressor " << (s.chain.slots[outBus].on ? "on" : "off")
          << ", tape " << (s.chain.slots[outTape].on ? "on" : "off")
          << ", limiter " << (s.chain.slots[outLimiter].on ? "on at " + juce::String (s.chain.slots[outLimiter].param ("ceiling", -1.0f), 1) + " dB" : juce::String ("off"))
          << ", delay " << juce::String (s.chain.slots[outDelay].on ? s.chain.slots[outDelay].param ("ms", 0.0f) : 0.0f, 1) << " ms\n";
    }

    const auto l = load();
    t << "The mixer's share of the processor: " << juce::String (l.average * 100.0, 1) << "% of each block on average, " << juce::String (l.busiest * 100.0, 1) << "% at its busiest in the last 10 seconds.\n";
    t << recordingReportText();

    for (const auto& w : warnings())
        t << "MIXER WARNING: " << w << "\n";

    return t;
}

//==============================================================================
void Mixer::prepare (double sampleRate, int maxBlock)
{
    rec.stop();
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    capacity = juce::jmax (16, maxBlock);

    laneData.assign ((size_t) numMixSockets * (size_t) capacity, 0.0f);
    stray.assign ((size_t) capacity, 0.0f);
    chanPre.assign ((size_t) numMixChannels * (size_t) capacity, 0.0f);
    chanMid.assign ((size_t) numMixChannels * (size_t) capacity, 0.0f);
    chanPost.assign ((size_t) numMixChannels * (size_t) capacity, 0.0f);
    tuneFeed.assign ((size_t) capacity, 0.0f);
    stripped.fill (false);
    silence.assign ((size_t) capacity, 0.0f);
    directData.assign ((size_t) 8 * (size_t) capacity, 0.0f);
    directUsed.fill (false);
    recScratch.assign ((size_t) capacity, 0.0f);
    recMixWas = false;

    for (int side = 0; side < 2; ++side)
    {
        recOut.chain[(size_t) side].prepare (rate, capacity);
        recOut.bus[(size_t) side].assign ((size_t) capacity, 0.0f);
    }

    recOut.levelG = {};

    for (auto& c : chans)
    {
        c.strip.prepare (rate, capacity);
        c.strip2.prepare (rate, capacity);
        c.wasOn = false;
        c.trimG = c.mainL = c.mainR = c.directG = c.recL = c.recR = {};
        c.monL = {}; c.monR = {}; c.fxG = {};
    }

    for (auto& f : fxs)
    {
        f.chain.prepare (rate, capacity);
        f.pre.prepare (rate, capacity);
        f.filter.prepare (rate, capacity);
        f.in.assign ((size_t) capacity, 0.0f);
        f.wet.assign ((size_t) capacity, 0.0f);
        f.retG = {}; f.recG = {}; f.monG = {};
        f.live = false;
    }

    for (auto& o : outs)
    {
        for (auto& set : o.chain)
            for (auto& side : set)
                side.prepare (rate, capacity);

        for (int side = 0; side < 2; ++side)
        {
            o.bus[(size_t) side].assign ((size_t) capacity, 0.0f);
            o.buf[(size_t) side].assign ((size_t) capacity, 0.0f);
            o.other[(size_t) side].assign ((size_t) capacity, 0.0f);
        }

        o.levelG = {};
    }

    fastStep = (float) (1.0 / juce::jmax (1.0, 0.010 * rate));              // a fader thrown end to end glides for 10 ms
    slowStep = (float) (1.0 / juce::jmax (1.0, venueFadeSeconds * rate));
    masterStep = (float) (1.0 / juce::jmax (1.0, 0.020 * rate));
    master = 0.0f;
    mainMix = (float) mainSet.load();
    guardWindowPeak = 0.0f;
    guardWindowLeft = (int) (0.010 * rate);
    guardHotWindows = 0;
}

void Mixer::setTempo (double bpm) noexcept
{
    for (auto& f : fxs)
        f.chain.setTempo (bpm);
}

bool Mixer::startBlock (int n) noexcept
{
    if (n <= 0 || n > capacity || (! wanted.load (std::memory_order_relaxed) && master <= 0.0f))
        return false;

    const int mask = socketMask.load (std::memory_order_relaxed);

    for (int s = 0; s < numMixSockets; ++s)
    {
        if ((mask & (1 << s)) != 0)
        {
            busPtrs[(size_t) s] = laneData.data() + (size_t) s * (size_t) capacity;
            juce::FloatVectorOperations::clear (busPtrs[(size_t) s], n);
        }
        else
            busPtrs[(size_t) s] = nullptr;
    }

    juce::FloatVectorOperations::clear (stray.data(), n);
    return true;
}

const float* const* Mixer::stripChannels (const float* const* feed, int numIns, int n) noexcept
{
    stripped.fill (false);

    if (n <= 0 || n > capacity || numIns > (int) loopFeed.size())
        return feed;

    for (int s = 0; s < numIns; ++s)
        loopFeed[(size_t) s] = feed[s];

    for (int c = 0; c < numMixChannels; ++c)
    {
        auto& ch = chans[(size_t) c];
        const bool on = ch.on.load (std::memory_order_relaxed);

        // A channel switched off fades out before it stops being worked on.
        if (! on && ! ch.wasOn)
            continue;

        const int socket = ch.socket.load (std::memory_order_relaxed);
        const bool plugged = socket >= 0 && socket < numIns && socket < numMixSockets;
        const float* live = plugged && feed[socket] != nullptr ? feed[socket] : silence.data();
        float* pre = chanPre.data() + (size_t) c * (size_t) capacity;
        float* mid = chanMid.data() + (size_t) c * (size_t) capacity;
        float* post = chanPost.data() + (size_t) c * (size_t) capacity;

        float start, step;
        // The tuner: a guitar's live sound fades out here, before anything hears it (20 ms, no click).
        const bool tuning = tuningGuitars && (tunerChannels.load (std::memory_order_relaxed) & (1u << c)) != 0;
        ch.trimG.advance (tuning ? 0.0f : ch.trim.load (std::memory_order_relaxed), tuning || ch.tunedOut ? fastStep * 0.5f : fastStep * 4.0f, n, start, step);
        ch.tunedOut = tuning || (ch.tunedOut && ch.trimG.now < ch.trim.load (std::memory_order_relaxed));
        for (int i = 0; i < n; ++i)
            pre[i] = live[i] * (start + step * (float) i);

        juce::FloatVectorOperations::copy (post, pre, n);
        ch.strip.process (post, n);
        juce::FloatVectorOperations::copy (mid, post, n);
        ch.strip2.process (post, n);
        stripped[(size_t) c] = true;

        // The looper hears the first channel that is on and listens to this socket.
        if (on && plugged && loopFeed[(size_t) socket] == feed[socket])
            loopFeed[(size_t) socket] = post;
    }

    return loopFeed.data();
}

void Mixer::process (const float* const* raw, const float* const* feed, int numIns, int n) noexcept
{
    if (n <= 0 || n > capacity)
        return;

    const auto started = juce::Time::getHighResolutionTicks();

    for (auto& o : outs)
        for (auto& b : o.bus)
            juce::FloatVectorOperations::clear (b.data(), n);

    for (auto& f : fxs)
        juce::FloatVectorOperations::clear (f.in.data(), n);

    // The recording mix is only worked on while it is being recorded, so it costs nothing otherwise.
    // Its gains start from silence each time, which fades the first few milliseconds in.
    const bool recMixing = rec.wants (recSlotMix);

    if (recMixing)
    {
        juce::FloatVectorOperations::clear (recOut.bus[0].data(), n);
        juce::FloatVectorOperations::clear (recOut.bus[1].data(), n);
    }

    if (recMixing != recMixWas)
    {
        for (auto& c : chans)   c.recL = c.recR = {};
        for (auto& f : fxs)     f.recG = {};
        recOut.levelG = {};
        recMixWas = recMixing;
    }

    const bool tripped = guard.load (std::memory_order_relaxed);
    const float masterNow = wanted.load (std::memory_order_relaxed) && ! tripped ? 1.0f : 0.0f;
    directUsed.fill (false);

    // ---- the channels ----
    for (int c = 0; c < numMixChannels; ++c)
    {
        auto& ch = chans[(size_t) c];
        const bool on = ch.on.load (std::memory_order_relaxed);

        // A channel switched off fades out before it stops being worked on. (Its file, if it has
        // one, still gets this block, as silence, so every file of a recording stays the same length.)
        // The soundcheck copy: this channel's socket, raw, whatever else happens to the channel.
        if (rec.wants (recSlotRaw + c))
        {
            const int s = ch.socket.load (std::memory_order_relaxed);
            rec.write (recSlotRaw + c, on && s >= 0 && s < numIns && raw[s] != nullptr ? raw[s] : silence.data(), nullptr, n);
        }

        if ((! on && ! ch.wasOn) || ! stripped[(size_t) c])
        {
            if (rec.wants (c))
                rec.write (c, silence.data(), nullptr, n);
            continue;
        }

        const int socket = ch.socket.load (std::memory_order_relaxed);
        const bool plugged = socket >= 0 && socket < numIns && socket < numMixSockets;
        const float* live = plugged && feed[socket] != nullptr ? feed[socket] : silence.data();
        const float* loops = plugged && busPtrs[(size_t) socket] != nullptr ? busPtrs[(size_t) socket] : silence.data();

        // The channel's own sound: live, after its own processing (done in stripChannels, before the
        // looper heard it), and every loop recorded from it. A loop was recorded after the channel's
        // processing, so it rejoins after it: at every point a monitor or the sound crew can tap.

        float* tapPre = chanPre.data() + (size_t) c * (size_t) capacity;
        float* tapMid = chanMid.data() + (size_t) c * (size_t) capacity;
        float* work = chanPost.data() + (size_t) c * (size_t) capacity;
        juce::FloatVectorOperations::add (tapPre, loops, n);
        juce::FloatVectorOperations::add (tapMid, loops, n);

        // The tuner, with the monitors kept: the main mix and the reverbs get this channel's loops
        // without its live sound (20 ms down and back, no click); the monitors get both as always.
        const float* forMain = work;
        const bool tuningMain = tuningGuitarsMain && (tunerChannels.load (std::memory_order_relaxed) & (1u << c)) != 0;

        if (tuningMain || ch.tuneMain < 1.0f)
        {
            const float target = tuningMain ? 0.0f : 1.0f, pace = fastStep * 0.5f;
            float g = ch.tuneMain;

            for (int i = 0; i < n; ++i)
            {
                g = target > g ? juce::jmin (target, g + pace) : juce::jmax (target, g - pace);
                tuneFeed[(size_t) i] = work[i] * g + loops[i];
            }

            ch.tuneMain = g;
            forMain = tuneFeed.data();
        }

        juce::FloatVectorOperations::add (work, loops, n);
        float start, step;

        if (rec.wants (c))
        {
            const auto point = (RecordPoint) ch.record.load (std::memory_order_relaxed);
            const float* tap = point == RecordPoint::raw ? (plugged && raw[socket] != nullptr ? raw[socket] : silence.data())
                             : point == RecordPoint::afterEffects ? live
                             : point == RecordPoint::afterProcessing ? work : tapPre;
            rec.write (c, tap, nullptr, n);
        }

        // The sound crew's direct out: this source alone, live and looped, after its effects and
        // before the channel's EQ, so front of house processes it once, their way.
        float start2, step2;
        const int direct = ch.direct.load (std::memory_order_relaxed);
        ch.directG.advance (on && direct >= 0 && direct < 8 ? masterNow : 0.0f, fastStep, n, start2, step2);

        if (direct >= 0 && direct < 8 && (start2 != 0.0f || step2 != 0.0f))
        {
            auto* to = directData.data() + (size_t) direct * (size_t) capacity;
            if (! directUsed[(size_t) direct])
            {
                juce::FloatVectorOperations::clear (to, n);
                directUsed[(size_t) direct] = true;
            }
            addGliding (to, tapPre, start2, step2, n);
        }

        // A monitor can take its sends before the processing, after the tone blocks (gate,
        // warmth, EQ) or after the compressors, so a wedge need not hear the squeeze.
        raise (ch.peak, peakOf (work, n));

        const float fader = on && ! ch.mute.load (std::memory_order_relaxed) ? ch.fader.load (std::memory_order_relaxed) : 0.0f;
        const float pan = ch.pan.load (std::memory_order_relaxed);
        const float left = juce::jmin (1.0f, 1.0f - pan), right = juce::jmin (1.0f, 1.0f + pan);    // centre is full level on both sides
        bool sounding = false;

        ch.mainL.advance (fader * left, fastStep, n, start, step);
        addGliding (outs[0].bus[0].data(), forMain, start, step, n);
        sounding = sounding || ch.mainL.now != 0.0f;
        ch.mainR.advance (fader * right, fastStep, n, start, step);
        addGliding (outs[0].bus[1].data(), forMain, start, step, n);
        sounding = sounding || ch.mainR.now != 0.0f;

        // The recording mix: the same channel as the main mix hears it (so it follows the fader and
        // the mute), a little louder or quieter, and panned its own way.
        if (recMixing)
        {
            const float recPan = ch.recPan.load (std::memory_order_relaxed);
            const float recLevel = fader * ch.recGain.load (std::memory_order_relaxed);
            ch.recL.advance (recLevel * juce::jmin (1.0f, 1.0f - recPan), fastStep, n, start, step);
            addGliding (recOut.bus[0].data(), forMain, start, step, n);
            ch.recR.advance (recLevel * juce::jmin (1.0f, 1.0f + recPan), fastStep, n, start, step);
            addGliding (recOut.bus[1].data(), forMain, start, step, n);
        }

        for (int m = 0; m < numMixMonitors; ++m)
        {
            auto& out = outs[(size_t) (m + 1)];
            const float send = ! on ? 0.0f : ch.mon[(size_t) m].load (std::memory_order_relaxed) * (out.preFader.load (std::memory_order_relaxed) ? 1.0f : fader);
            const int tap = out.tap.load (std::memory_order_relaxed);
            const float* from = tap == 0 ? tapPre : tap == 1 ? tapMid : work;
            ch.monL[(size_t) m].advance (send * left, fastStep, n, start, step);
            addGliding (out.bus[0].data(), from, start, step, n);
            ch.monR[(size_t) m].advance (send * right, fastStep, n, start, step);
            addGliding (out.bus[1].data(), from, start, step, n);
            sounding = sounding || ch.monL[(size_t) m].now != 0.0f || ch.monR[(size_t) m].now != 0.0f;
        }

        for (int f = 0; f < numMixFx; ++f)
        {
            ch.fxG[(size_t) f].advance (fader * ch.fx[(size_t) f].load (std::memory_order_relaxed), fastStep, n, start, step);
            addGliding (fxs[(size_t) f].in.data(), forMain, start, step, n);
            sounding = sounding || ch.fxG[(size_t) f].now != 0.0f;
        }

        ch.wasOn = on || sounding || ch.directG.now != 0.0f;
    }

    // Loops recorded from a socket that no channel listens to still have to be heard somewhere.
    if (const float p = peakOf (stray.data(), n); p > 0.0f)
    {
        raise (strayPeak, p);
        juce::FloatVectorOperations::add (outs[0].bus[0].data(), stray.data(), n);
        juce::FloatVectorOperations::add (outs[0].bus[1].data(), stray.data(), n);

        if (recMixing)
        {
            juce::FloatVectorOperations::add (recOut.bus[0].data(), stray.data(), n);
            juce::FloatVectorOperations::add (recOut.bus[1].data(), stray.data(), n);
        }
    }

    // ---- the mixer's reverbs and delays: after the looper, so never in a loop ----
    for (int k = 0; k < numMixFx; ++k)
    {
        auto& f = fxs[(size_t) k];
        const bool on = f.on.load (std::memory_order_relaxed);

        if (! on && ! f.live)
        {
            if (rec.wants (recSlotFx + k))
                rec.write (recSlotFx + k, silence.data(), nullptr, n);
            continue;
        }

        f.pre.process (f.in.data(), n);      // the EQ on what goes into the effect
        juce::FloatVectorOperations::copy (f.wet.data(), f.in.data(), n);
        f.chain.process (f.wet.data(), n);
        juce::FloatVectorOperations::subtract (f.wet.data(), f.in.data(), n);      // every effect passes its input straight through: take it away, the wet sound is left
        float* wet = f.wet.data();
        f.filter.process (&wet, 1, n);
        raise (f.peak, peakOf (wet, n));

        float start, step;
        f.retG.advance (on ? f.ret.load (std::memory_order_relaxed) : 0.0f, fastStep, n, start, step);
        addGliding (outs[0].bus[0].data(), wet, start, step, n);
        addGliding (outs[0].bus[1].data(), wet, start, step, n);
        bool sounding = f.retG.now != 0.0f;

        // Recorded as a file of its own: the wet sound alone, at the level it has in the main mix.
        if (rec.wants (recSlotFx + k))
        {
            for (int i = 0; i < n; ++i)
                recScratch[(size_t) i] = wet[i] * (start + step * (float) i);

            rec.write (recSlotFx + k, recScratch.data(), nullptr, n);
        }

        if (recMixing)
        {
            f.recG.advance (on ? f.ret.load (std::memory_order_relaxed) * f.recGain.load (std::memory_order_relaxed) : 0.0f, fastStep, n, start, step);
            addGliding (recOut.bus[0].data(), wet, start, step, n);
            addGliding (recOut.bus[1].data(), wet, start, step, n);
        }

        for (int m = 0; m < numMixMonitors; ++m)
        {
            f.monG[(size_t) m].advance (on ? f.mon[(size_t) m].load (std::memory_order_relaxed) : 0.0f, fastStep, n, start, step);
            addGliding (outs[(size_t) (m + 1)].bus[0].data(), wet, start, step, n);
            addGliding (outs[(size_t) (m + 1)].bus[1].data(), wet, start, step, n);
            sounding = sounding || f.monG[(size_t) m].now != 0.0f;
        }

        f.live = on || sounding;
    }

    // ---- the feedback guard: the main mix far over full level for a quarter of a second ----
    guardWindowPeak = juce::jmax (guardWindowPeak, peakOf (outs[0].bus[0].data(), n), peakOf (outs[0].bus[1].data(), n));
    guardWindowLeft -= n;

    if (guardWindowLeft <= 0)
    {
        guardHotWindows = guardWindowPeak >= 4.0f ? guardHotWindows + 1 : 0;
        guardWindowPeak = 0.0f;
        guardWindowLeft = (int) (0.010 * rate);

        if (guardHotWindows >= 25)
        {
            guard.store (true);
            guardHotWindows = 0;
        }
    }

    // ---- the outputs: EQ, notches, compressor, limiter, delay, level ----
    const float masterTarget = masterNow;
    const float masterStart = master;
    master = masterTarget > master ? juce::jmin (masterTarget, master + masterStep * (float) n) : juce::jmax (masterTarget, master - masterStep * (float) n);
    const float masterInc = (master - masterStart) / (float) n;
    float mainStart = 0.0f, mainStep = 0.0f;

    for (int o = 0; o < numMixOutputs; ++o)
    {
        auto& out = outs[(size_t) o];
        const bool mono = out.mono.load (std::memory_order_relaxed);
        float* left = out.bus[0].data();
        float* right = out.bus[1].data();

        if (mono)
            for (int i = 0; i < n; ++i)
                left[i] = 0.5f * (left[i] + right[i]);

        const int sides = mono ? 1 : 2;
        float* const bus[2] { left, right };

        if (o == 0)
        {
            // A venue load builds its chain on the set not in use; fade across over a second.
            const float target = (float) mainSet.load (std::memory_order_relaxed);

            if (mainMix == target)
            {
                for (int s = 0; s < sides; ++s)
                    out.chain[(size_t) target][(size_t) s].process (bus[s], n);
            }
            else
            {
                const float from = mainMix;
                mainMix = target > mainMix ? juce::jmin (target, mainMix + slowStep * (float) n) : juce::jmax (target, mainMix - slowStep * (float) n);
                const float inc = (mainMix - from) / (float) n;

                for (int s = 0; s < sides; ++s)
                {
                    float* other = out.other[(size_t) s].data();
                    juce::FloatVectorOperations::copy (other, bus[s], n);
                    out.chain[0][(size_t) s].process (bus[s], n);
                    out.chain[1][(size_t) s].process (other, n);

                    for (int i = 0; i < n; ++i)
                        bus[s][i] += (other[i] - bus[s][i]) * (from + inc * (float) i);
                }
            }
        }
        else
        {
            for (int s = 0; s < sides; ++s)
                out.chain[0][(size_t) s].process (bus[s], n);
        }

        const bool slow = o == 0 && slowLevel.load (std::memory_order_relaxed);
        const float levelTarget = out.mute.load (std::memory_order_relaxed) ? 0.0f : out.level.load (std::memory_order_relaxed);
        float start, step;
        out.levelG.advance (levelTarget, slow ? slowStep : fastStep, n, start, step);

        if (slow && out.levelG.now == levelTarget)
            slowLevel.store (false, std::memory_order_relaxed);

        if (o == 0)
        {
            mainStart = start;
            mainStep = step;
        }

        for (int s = 0; s < 2; ++s)
        {
            const float* from = bus[mono ? 0 : s];
            float* to = out.buf[(size_t) s].data();

            for (int i = 0; i < n; ++i)
                to[i] = from[i] * (start + step * (float) i) * (masterStart + masterInc * (float) i);

            raise (out.peak[(size_t) s], peakOf (to, n));
        }
    }

    if (rec.wants (recSlotMain))
        rec.write (recSlotMain, outs[0].buf[0].data(), outs[0].buf[1].data(), n);

    // ---- the recording mix: its own EQ, bus compressor and limiter, its own level, always stereo ----
    if (recMixing)
    {
        float start, step;
        recOut.levelG.advance (recOut.level.load (std::memory_order_relaxed), fastStep, n, start, step);

        for (int s = 0; s < 2; ++s)
        {
            float* bus = recOut.bus[(size_t) s].data();
            recOut.chain[(size_t) s].process (bus, n);

            for (int i = 0; i < n; ++i)
                bus[i] *= (start + step * (float) i) * (masterStart + masterInc * (float) i);

            raise (recOut.peak[(size_t) s], peakOf (bus, n));
        }

        rec.write (recSlotMix, recOut.bus[0].data(), recOut.bus[1].data(), n);
    }

    // ---- the tuner: the main mix goes silent while it is on (after the recording, before the break music) ----
    if (tuningNow || tuneGain < 1.0f)
    {
        const float target = tuningNow ? 0.0f : 1.0f;
        const float step = (float) (1.0 / (0.02 * rate));       // 20 ms from one to the other
        auto& out = outs[0];
        float g = tuneGain;

        for (int i = 0; i < n; ++i)
        {
            g = target > g ? juce::jmin (target, g + step) : juce::jmax (target, g - step);
            out.buf[0][(size_t) i] *= g;
            out.buf[1][(size_t) i] *= g;
        }

        tuneGain = g;
        out.peak[0].store (peakOf (out.buf[0].data(), n), std::memory_order_relaxed);
        out.peak[1].store (peakOf (out.buf[1].data(), n), std::memory_order_relaxed);
    }

    // ---- the break music: main output only, and only after the main mix was recorded ----
    if (breakL != nullptr && breakR != nullptr)
    {
        auto& out = outs[0];
        const bool mono = out.mono.load (std::memory_order_relaxed);

        for (int s = 0; s < 2; ++s)
        {
            float* to = out.buf[(size_t) s].data();
            const float* from = s == 0 ? breakL : breakR;

            for (int i = 0; i < n; ++i)
                to[i] += (mono ? 0.5f * (breakL[i] + breakR[i]) : from[i]) * (mainStart + mainStep * (float) i) * (masterStart + masterInc * (float) i);

            raise (out.peak[(size_t) s], peakOf (to, n));
        }

        breakL = breakR = nullptr;
    }

    rec.endBlock (n);

    // ---- how much of the block that took ----
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started) / ((double) n / rate);
    loadAverage.store ((float) (loadAverage.load (std::memory_order_relaxed) * 0.999 + used * 0.001), std::memory_order_relaxed);
    loadPeakHold = juce::jmax (loadPeakHold, (float) used);
    loadHoldLeft -= n;

    if (loadHoldLeft <= 0)
    {
        loadBusiest.store (loadPeakHold, std::memory_order_relaxed);
        loadPeakHold = 0.0f;
        loadHoldLeft = (int) (10.0 * rate);
    }
}

void Mixer::recordWithoutMixing (const float* const* raw, const float* const* feed, int numIns, int n) noexcept
{
    if (! rec.isRecording() || n <= 0 || n > capacity)
        return;

    for (int c = 0; c < numMixChannels; ++c)
    {
        if (! rec.wants (c))
            continue;

        auto& ch = chans[(size_t) c];
        const int socket = ch.socket.load (std::memory_order_relaxed);
        const bool plugged = ch.on.load (std::memory_order_relaxed) && socket >= 0 && socket < numIns;
        const auto* from = (RecordPoint) ch.record.load (std::memory_order_relaxed) == RecordPoint::raw ? raw : feed;
        rec.write (c, plugged && from[socket] != nullptr ? from[socket] : silence.data(), nullptr, n);
    }

    for (int c = 0; c < numMixChannels; ++c)
        if (rec.wants (recSlotRaw + c))
        {
            auto& ch = chans[(size_t) c];
            const int socket = ch.socket.load (std::memory_order_relaxed);
            rec.write (recSlotRaw + c, ch.on.load (std::memory_order_relaxed) && socket >= 0 && socket < numIns && raw[socket] != nullptr ? raw[socket] : silence.data(), nullptr, n);
        }

    for (int slot = numMixChannels; slot < recSlotRaw; ++slot)
        if (rec.wants (slot))
            rec.write (slot, silence.data(), silence.data(), n);

    rec.endBlock (n);
}

} // namespace gigrig
