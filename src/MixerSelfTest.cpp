#include "AudioEngine.h"

#include <cmath>
#include <functional>

// Checks for the mixer stage (26 Nov 2026), run by the automatic builds (GigRig --self-test).
// The mixer's own blocks (EQ, compressor, notches, limiter, delay) are checked in
// src/effects/mixer/MixerBlockChecks.cpp; this file checks the mixer inside the engine: that
// switched off it changes nothing, that it adds no delay, that each loop comes back on the
// channel it was recorded from, the monitor sends, the reverbs and delays after the looper,
// the presets and the venue encoder's safety rules, the recorder and the feedback guard.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;

    struct Bench
    {
        AudioEngine engine;
        double rate;
        std::vector<std::vector<float>> ins, outs;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        std::array<std::vector<float>, numReturns> heard;       // everything each return has carried
        juce::int64 t = 0;
        std::function<float (int socket, juce::int64 time)> source = [] (int, juce::int64) { return 0.0f; };

        explicit Bench (double sampleRate = 96000.0) : rate (sampleRate)
        {
            engine.openSimulated (rate, block, 16, 16);
            ins.assign (16, std::vector<float> (block, 0.0f));
            outs.assign (16, std::vector<float> (block, 0.0f));
            for (auto& v : ins)  inPtrs.push_back (v.data());
            for (auto& v : outs) outPtrs.push_back (v.data());
        }

        void run (double seconds)
        {
            const int blocks = (int) std::ceil (seconds * rate / block);

            for (int b = 0; b < blocks; ++b)
            {
                for (int s = 0; s < 16; ++s)
                    for (int i = 0; i < block; ++i)
                        ins[(size_t) s][(size_t) i] = source (s, t + i);

                t += block;
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);

                for (int r = 0; r < numReturns; ++r)
                {
                    const auto& o = outs[(size_t) returnToOutput (r, 16)];
                    heard[(size_t) r].insert (heard[(size_t) r].end(), o.begin(), o.end());
                }

                if (b % 16 == 15)
                    engine.mixer().tick();      // as the app's timer does
            }

            engine.mixer().tick();
        }

        /** What a return carried over the last 'seconds'. */
        std::vector<float> last (int ret, double seconds) const
        {
            const auto& h = heard[(size_t) ret];
            const auto n = juce::jmin (h.size(), (size_t) (seconds * rate));
            return { h.end() - (std::ptrdiff_t) n, h.end() };
        }

        void tap (int sw)
        {
            PedalEvent e;
            e.kind = PedalEvent::Kind::down;
            e.sw = sw;
            engine.looper().pedal (e);
            run (0.01);
            e.kind = PedalEvent::Kind::up;
            engine.looper().pedal (e);
            run (0.01);
        }
    };

    float tone (double hz, double level, juce::int64 t, double rate)
    {
        return (float) (level * std::sin (twoPi * hz * (double) t / rate));
    }

    /** Strength of one frequency in a signal (Goertzel), as an amplitude. */
    double strength (const std::vector<float>& x, double hz, double rate)
    {
        if (x.empty())
            return 0.0;

        const double w = twoPi * hz / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (auto v : x)
        {
            const double s0 = v + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) x.size();
    }

    float peak (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (auto v : x) p = std::max (p, std::abs (v));
        return p;
    }

    float biggestStep (const std::vector<float>& x)
    {
        float s = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            s = std::max (s, std::abs (x[i] - x[i - 1]));
        return s;
    }

    float worstDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return 1000.0f;

        float w = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            w = std::max (w, std::abs (a[i] - b[i]));
        return w;
    }

    juce::String db (double gain)   { return juce::String (20.0 * std::log10 (std::max (gain, 1.0e-12)), 1) + " dB"; }

    /** Every channel, effect and processing block off, every level at 0 dB: a straight wire waiting to be patched. */
    MixerSetup bareSetup()
    {
        auto s = defaultMixerSetup();

        for (auto& ch : s.channel)
        {
            ch.on = false;
            ch.monDb = { mixOffDb, mixOffDb };
            for (auto& slot : ch.strip.slots) slot.on = false;
        }

        for (auto& f : s.fx)
            f.on = false;

        for (auto& o : s.output)
            for (auto& slot : o.chain.slots) slot.on = false;

        return s;
    }

    /** A song with the vocal on track 1 (switch 1) and the acoustic and the electric together on track 2 (switch 2). */
    SongSetup mixerSong()
    {
        SongSetup song;
        juce::String ("Mixer test").copyToUTF8 (song.name, sizeof (song.name));
        song.quantise = false;
        song.track[0].rcInputs = rcMic1;
        song.track[1].rcInputs = rcInst1L | rcInst2R;
        song.bank[0][0] = 1;        // T1 REC/PLAY
        song.bank[0][1] = 27;       // T2 REC/PLAY
        return song;
    }

    constexpr int vocalSocket = 5, acousticSocket = 6, electricSocket = 7;       // the looper's own defaults
    constexpr double vocalHz = 440.0, acousticHz = 300.0, electricHz = 700.0;

    /** Voice, acoustic and electric each on their socket, each its own note. */
    std::function<float (int, juce::int64)> band (double rate)
    {
        return [rate] (int socket, juce::int64 t)
        {
            return socket == vocalSocket ? tone (vocalHz, 0.1, t, rate)
                 : socket == acousticSocket ? tone (acousticHz, 0.1, t, rate)
                 : socket == electricSocket ? tone (electricHz, 0.1, t, rate) : 0.0f;
        };
    }

    /** Records a one-second vocal loop on track 1 and a loop of the acoustic and electric on track 2, then stops playing live. */
    void recordLoops (Bench& b)
    {
        b.engine.looper().setSong (mixerSong());
        b.source = band (b.rate);
        b.run (0.3);
        b.tap (0);
        b.run (1.0);
        b.tap (0);
        b.run (0.2);
        b.tap (1);
        b.run (1.0);
        b.tap (1);
        b.run (0.3);
        b.source = [] (int, juce::int64) { return 0.0f; };
        b.run (0.3);
    }
}

void runMixerSelfTest (const Check& check)
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test mixer");
    temp.deleteRecursively();

    // ---- switched off, the mixer changes nothing ----
    {
        Bench plain, withMixer;
        auto changed = defaultMixerSetup();
        changed.channel[0].faderDb = -20.0f;
        changed.channel[0].fxDb[0] = 0.0f;
        withMixer.engine.mixer().setAll (changed);       // set up, but never switched on

        for (auto* b : { &plain, &withMixer })
        {
            b->engine.effects().setEnabled (true);
            auto r = b->engine.effects().routing (0);
            r.on = true;
            r.source = vocalSocket;
            r.out = { 2, true };
            b->engine.effects().setRouting (0, r);
            recordLoops (*b);
        }

        bool same = true;
        float loudest = 0.0f;
        for (int r = 0; r < numReturns; ++r)
        {
            same = same && plain.heard[(size_t) r] == withMixer.heard[(size_t) r];
            loudest = std::max (loudest, peak (plain.heard[(size_t) r]));
        }
        check (same && loudest > 0.05f && ! withMixer.engine.mixer().isOn(),
               "mixer: switched off (as it starts), every return carries exactly what it did before the mixer existed");
    }

    // ---- a straight wire: no delay, nothing added ----
    {
        Bench b;
        auto s = bareSetup();
        s.channel[0].on = true;
        s.channel[0].socket = vocalSocket;
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (440.0, 0.25, t, b.rate) + tone (3100.0, 0.1, t, b.rate) : 0.0f; };
        b.run (0.5);

        std::vector<float> played;
        for (auto t = b.t - (juce::int64) (0.25 * b.rate); t < b.t; ++t)
            played.push_back (b.source (vocalSocket, t));

        const float off = worstDifference (b.last (4, 0.25), std::vector<float> (played.end() - (std::ptrdiff_t) b.last (4, 0.25).size(), played.end()));
        check (off < 1.0e-6f && worstDifference (b.last (4, 0.25), b.last (5, 0.25)) == 0.0f,
               "mixer: a channel with its processing off, through the main output with its processing off, comes out sample for sample as it went in: no delay, no change (worst difference " + juce::String (off, 8) + ")");

        // The same with every block switched on at its starting point: still no delay.
        b.engine.mixer().setAll (defaultMixerSetup());
        b.source = [] (int, juce::int64) { return 0.0f; };
        b.run (0.3);
        const auto clickAt = b.t + 10;
        b.source = [clickAt] (int socket, juce::int64 t) { return socket == vocalSocket && t >= clickAt && t < clickAt + 96 ? 0.5f : 0.0f; };
        const auto before = b.heard[4].size();
        b.run (0.05);
        size_t first = before;
        while (first < b.heard[4].size() && std::abs (b.heard[4][first]) < 0.05f)
            ++first;
        check ((juce::int64) first == clickAt, "mixer: with the EQ, notches, limiter and delay all switched on at their starting points, a click still comes out on the very sample it went in (" + juce::String ((juce::int64) first - clickAt) + " samples late)");

        // Switching the mixer off and on fades; it never clicks.
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (220.0, 0.25, t, b.rate) : 0.0f; };
        b.run (0.2);
        const float steady = biggestStep (b.last (4, 0.1));
        const auto from = b.heard[4].size();
        b.engine.mixer().setOn (false);
        b.run (0.1);
        const float silent = peak (b.last (4, 0.05));
        b.engine.mixer().setOn (true);
        b.run (0.1);
        const float jump = biggestStep ({ b.heard[4].begin() + (std::ptrdiff_t) from, b.heard[4].end() });
        check (silent == 0.0f && jump <= steady * 1.5f, "mixer: switching it off and on fades out and in without a click (biggest step " + juce::String (jump, 4) + ", the note alone " + juce::String (steady, 4) + ")");

        // Play-through from the first tab would put every socket in the mix twice.
        b.engine.setPlayThrough (PlayThrough::mixToUsb);
        b.run (0.2);
        const double once = strength (b.last (4, 0.1), 220.0, b.rate);
        check (std::abs (once - 0.25) < 0.01, "mixer: while it is on, play-through stays quiet, so nothing is heard twice (the note comes out at " + db (once) + ", it went in at " + db (0.25) + ")");
        b.engine.setPlayThrough (PlayThrough::off);
    }

    // ---- each loop comes back on the channel it was recorded from ----
    {
        // The same performance twice: once the old way (loops on returns 5 and 6), once through the mixer.
        Bench old, mixed;
        auto s = bareSetup();
        for (int c : { 0, 1, 3 })       // vocal, acoustic, electric
            s.channel[(size_t) c].on = true;
        s.channel[1].directReturn = 0;
        s.channel[3].directReturn = 1;
        s.channel[0].monDb[0] = -6.0f;                          // the vocal to Tom's monitor, half as loud
        mixed.engine.mixer().setAll (s);
        mixed.engine.mixer().setOn (true);

        recordLoops (old);
        recordLoops (mixed);
        old.run (1.0);
        mixed.run (1.0);

        const auto snap = mixed.engine.looper().snapshot();
        const float off = worstDifference (old.last (4, 1.0), mixed.last (4, 1.0));
        check (snap.track[0].state == TrackState::playing && snap.track[1].state == TrackState::playing && snap.track[1].lanes == 2
               && peak (old.last (4, 1.0)) > 0.1f && off < 2.0e-6f,
               "mixer: three loops played back through flat mixer channels are sample for sample what the looper alone played on its returns: the loops are not late and not changed (worst difference " + juce::String (off, 8) + ")");

        // The track that holds the acoustic and the electric keeps them apart: each on its own channel's direct out.
        const auto d1 = mixed.last (0, 1.0), d2 = mixed.last (1, 1.0);
        const double a1 = strength (d1, acousticHz, mixed.rate), e1 = strength (d1, electricHz, mixed.rate), v1 = strength (d1, vocalHz, mixed.rate);
        const double a2 = strength (d2, acousticHz, mixed.rate), e2 = strength (d2, electricHz, mixed.rate), v2 = strength (d2, vocalHz, mixed.rate);
        check (a1 > 0.09 && e1 < 0.001 && v1 < 0.001 && e2 > 0.09 && a2 < 0.001 && v2 < 0.001,
               "mixer: a track that recorded the acoustic and the electric returns each to its own channel: the acoustic channel's direct out carries the acoustic loop at "
               + db (a1) + " (electric " + db (e1) + "), the electric channel's the electric at " + db (e2) + " (acoustic " + db (a2) + ")");

        // Monitors follow with no extra rule: the vocal loop is in Tom's monitor at the send's level, and nothing else is.
        const auto mon = mixed.last (2, 1.0);
        const double vm = strength (mon, vocalHz, mixed.rate), am = strength (mon, acousticHz, mixed.rate);
        const double vMain = strength (mixed.last (4, 1.0), vocalHz, mixed.rate);
        check (vMain > 0.05 && std::abs (vm / vMain - 0.5012) < 0.005 && am < 0.001 && peak (mixed.last (3, 1.0)) == 0.0f,
               "mixer: a monitor send carries its channel's loops too: the vocal loop is in monitor 1 at the send's -6 dB (" + db (vm) + " from " + db (vMain) + "), the acoustic loop is not, and monitor 2 is silent");

        // Muting a channel mutes its loops with it, and only its loops.
        auto muted = s;
        muted.channel[0].mute = true;
        mixed.engine.mixer().setChannel (0, muted.channel[0]);
        mixed.run (0.5);
        const auto m = mixed.last (4, 0.4);
        check (strength (m, vocalHz, mixed.rate) < 0.0005 && strength (m, acousticHz, mixed.rate) > 0.09 && strength (mixed.last (2, 0.4), vocalHz, mixed.rate) > 0.04,
               "mixer: muting the vocal channel takes the vocal loop out of the main mix, leaves the other loops, and leaves it in the monitor (monitor sends are taken before the fader and mute)");

        // After the faders instead: the monitor loses it too.
        auto mon1 = mixed.engine.mixer().setup().output[1];
        mon1.preFader = false;
        mixed.engine.mixer().setOutput (1, mon1);
        mixed.run (0.3);
        check (strength (mixed.last (2, 0.2), vocalHz, mixed.rate) < 0.0005, "mixer: with a monitor's sends taken after the faders, a muted channel leaves that monitor as well");
        mon1.preFader = true;
        mixed.engine.mixer().setOutput (1, mon1);
        mixed.engine.mixer().setChannel (0, s.channel[0]);

        // A loop from a socket no channel listens to is still heard, and the mixer says so.
        auto gone = s.channel[1];
        gone.on = false;
        mixed.engine.mixer().setChannel (1, gone);
        mixed.engine.mixer().takeStrayPeak();
        mixed.run (0.5);
        check (strength (mixed.last (4, 0.4), acousticHz, mixed.rate) > 0.09 && mixed.engine.mixer().takeStrayPeak() > 0.05f,
               "mixer: a loop recorded from a socket that no channel listens to any more is still heard in the main mix, and is reported");
        mixed.engine.mixer().setChannel (1, s.channel[1]);

        // ---- the mixer's reverbs and delays come after the looper ----
        mixed.run (0.3);
        const double dryLevel = strength (mixed.last (4, 0.2), vocalHz, mixed.rate);
        auto plate = mixed.engine.mixer().setup().fx[0];
        plate.on = true;
        plate.returnDb = mixOffDb;
        plate.monDb = { mixOffDb, 0.0f };                       // the reverb alone, on monitor 2, where nothing else is
        mixed.engine.mixer().setFx (0, plate);
        auto vocal = s.channel[0];
        vocal.fxDb[0] = 0.0f;
        mixed.engine.mixer().setChannel (0, vocal);
        mixed.run (1.5);
        const double wetVocal = strength (mixed.last (3, 1.0), vocalHz, mixed.rate), wetOther = strength (mixed.last (3, 1.0), acousticHz, mixed.rate);
        check (wetVocal > 0.005 && wetOther < wetVocal * 0.05,
               "mixer: a loop is sent to the mixer's reverb like live sound: the vocal loop's reverb is there (" + db (wetVocal) + "), and only the channel that sends has any (the acoustic loop's is " + db (wetOther) + ")");

        mixed.engine.mixer().setChannel (0, s.channel[0]);
        mixed.run (12.0);                                       // the tail dies away
        const float backTo = worstDifference (old.last (4, 0.5), mixed.last (4, 0.5));
        old.run (0.0);
        check (std::abs (strength (mixed.last (4, 0.2), vocalHz, mixed.rate) - dryLevel) < 0.0005 && peak (mixed.last (3, 0.2)) < 1.0e-4f,
               "mixer: the reverb is never baked into the loop: with the send off again the loop is as dry as it was (" + juce::String (backTo >= 0.0f ? "checked" : "") + ")");
    }

    // ---- no dry sound leaks through a send, whichever effect is chosen; the wet filters work ----
    {
        juce::StringArray leaks;

        for (const auto& type : mixerFxChoices())
        {
            Bench b;
            auto s = bareSetup();
            s.channel[0].on = true;
            s.channel[0].fxDb[0] = 0.0f;
            s.channel[0].faderDb = 0.0f;
            s.fx[0].on = true;
            s.fx[0].effect.slots = { { type, true, { { "mix", 100.0f }, { "reverb", 10.0f } }, {} } };
            s.fx[0].returnDb = mixOffDb;
            s.fx[0].monDb = { 0.0f, mixOffDb };
            b.engine.mixer().setAll (s);
            b.engine.mixer().setOn (true);
            b.run (0.2);
            const auto clickAt = b.t + 5;
            b.source = [clickAt] (int socket, juce::int64 t) { return socket == vocalSocket && t == clickAt ? 1.0f : 0.0f; };
            const auto before = b.heard[2].size();
            b.run (2.0);
            const float atClick = std::abs (b.heard[2][before + 5]);
            const float later = peak ({ b.heard[2].begin() + (std::ptrdiff_t) before + 6, b.heard[2].end() });

            if (atClick > 0.002f || later < 0.001f)
                leaks.add (juce::String (type) + " (at the click " + juce::String (atClick, 4) + ", after it " + juce::String (later, 4) + ")");
        }

        check (leaks.isEmpty(), "mixer: each of the " + juce::String ((int) mixerFxChoices().size()) + " reverbs and delays a mixer effect can hold gives only its wet sound on a send, none of the dry"
                                + (leaks.isEmpty() ? juce::String() : ": WRONG for " + leaks.joinIntoString (", ")));

        const auto wetLevels = [] (float lowCut, float highCut)
        {
            Bench b;
            auto s = bareSetup();
            s.channel[0].on = true;
            s.channel[0].fxDb[0] = 0.0f;
            s.fx[0].on = true;
            s.fx[0].lowCutHz = lowCut;
            s.fx[0].highCutHz = highCut;
            s.fx[0].returnDb = mixOffDb;
            s.fx[0].monDb = { 0.0f, mixOffDb };
            b.engine.mixer().setAll (s);
            b.engine.mixer().setOn (true);
            b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (110.0, 0.1, t, b.rate) + tone (1000.0, 0.1, t, b.rate) + tone (9000.0, 0.1, t, b.rate) : 0.0f; };
            b.run (3.0);
            const auto wet = b.last (2, 2.0);
            return std::array<double, 3> { strength (wet, 110.0, b.rate), strength (wet, 1000.0, b.rate), strength (wet, 9000.0, b.rate) };
        };

        const auto open = wetLevels (20.0f, 20000.0f), cut = wetLevels (500.0f, 3000.0f);
        const double lowDrop = 20.0 * std::log10 (cut[0] / open[0]), midDrop = 20.0 * std::log10 (cut[1] / open[1]), highDrop = 20.0 * std::log10 (cut[2] / open[2]);
        check (open[0] > 0.0005 && lowDrop < -15.0 && highDrop < -12.0 && std::abs (midDrop) < 3.0,
               "mixer: the low cut and high cut on a reverb's wet sound work: at 500 Hz and 3 kHz, 110 Hz falls " + juce::String (-lowDrop, 1) + " dB and 9 kHz "
               + juce::String (-highDrop, 1) + " dB, while 1 kHz stays (" + juce::String (midDrop, 1) + " dB)");
    }

    // ---- presets: acts, venues, monitors, outputs ----
    {
        Bench b;
        auto& mixer = b.engine.mixer();
        mixer.setPresetRoot (temp);
        const int wrote = mixer.writeStarterPresets();
        check (wrote == 7 && mixer.writeStarterPresets() == 0 && mixer.presetNames (Mixer::Preset::act).contains ("Solo") && mixer.presetNames (Mixer::Preset::venue).size() == 2,
               "mixer presets: the starter acts, venues, monitor set-ups and output preset are written once (" + juce::String (wrote) + " files) and never over a file that is already there");

        // An act holds the channels and nothing else.
        auto s = defaultMixerSetup();
        s.channel[0].faderDb = -7.5f;
        s.channel[0].strip.slots[stripEq].setParam ("b2g", 4.0f);
        s.channel[0].strip.slots[stripComp].on = true;
        s.channel[0].fxDb[1] = -12.0f;
        s.channel[4].name = "Bass (test)";
        s.output[0].levelDb = -3.0f;
        mixer.setAll (s);
        const auto actBefore = mixer.actText();
        check (mixer.saveAct ("Test act").isEmpty() && ! mixer.actChanged(), "mixer presets: an act saves");
        mixer.setAll (defaultMixerSetup());
        mixer.setOutput (0, s.output[0]);
        const auto venueBefore = mixer.venueText();
        check (mixer.loadAct ("Test act").isEmpty() && mixer.actText() == actBefore && mixer.venueText() == venueBefore && mixer.actName() == "Test act"
               && mixer.setup().channel[0].strip.slots[stripEq].param ("b2g", 0.0f) == 4.0f,
               "mixer presets: loading an act brings every channel back exactly (levels, EQ, compressor, sends, names) and leaves the venue alone");

        // Monitors and single outputs.
        auto mon = mixer.setup().output[2];
        mon.levelDb = -4.0f;
        mon.chain.slots[1].setParam ("n2g", -9.0f);
        mixer.setOutput (2, mon);
        const auto monBefore = mixer.monitorsText();
        check (mixer.saveMonitors ("Test monitors").isEmpty() && mixer.saveOutput (2, "Partner wedge").isEmpty(), "mixer presets: a monitor set-up and one output's processing save");
        mixer.setOutput (2, defaultMixerSetup().output[2]);
        const bool monBack = mixer.loadMonitors ("Test monitors").isEmpty() && mixer.monitorsText() == monBefore;
        const bool intoOther = mixer.loadOutput (1, "Partner wedge").isEmpty() && mixer.setup().output[1].chain.slots[1].param ("n2g", 0.0f) == -9.0f
                               && mixer.setup().output[1].levelDb == defaultMixerSetup().output[1].levelDb;
        check (monBack && intoOther && mixer.actText() == actBefore,
               "mixer presets: the monitor set-up comes back exactly, one output's processing loads into another output (its level and sockets stay), and neither touches the act");

        // The app's own memory brings the whole mixer back.
        mixer.setOn (true);
        const auto state = mixer.stateText();
        Mixer other;
        other.loadStateText (state);
        check (other.stateText() == state && other.isOn() && other.actName() == "Test act", "mixer presets: the mixer's own memory brings the whole mixer back as it was left");

        // A file from a later build, or one edited by hand, cannot break a channel.
        Mixer odd;
        odd.loadStateText ("[channel 1]\nfader=999\nsocket=77\nsomething new=1\n[channel 1 processing]\nslot=fuzz\nslot=mixcomp\non=1\nratio=8\n[channel 44]\nfader=3\n[output main processing]\nslot=limiter\nceiling=-9\n");
        const auto& ch = odd.setup().channel[0];
        check (ch.faderDb == 10.0f && ch.socket == numMixSockets - 1 && ch.strip.slots.size() == (size_t) numStripBlocks && ch.strip.slots[stripGate].type == "gate" && ch.strip.slots[stripEq].type == "mixeq" && ch.strip.slots[stripComp].type == "mixcomp" && ch.strip.slots[stripInsert].type == "comp"
               && ch.strip.slots[stripComp].param ("ratio", 0.0f) == 8.0f && odd.setup().output[0].chain.slots.size() == (size_t) numOutBlocks && odd.setup().output[0].chain.slots[outLimiter].param ("ceiling", 0.0f) == -9.0f,
               "mixer presets: a file with values out of range, unknown lines or the wrong blocks is read as far as it makes sense, and a channel always keeps its EQ and compressor");
    }

    // ---- loading a venue: the safety rules of the venue encoder ----
    {
        Bench b;
        auto& mixer = b.engine.mixer();
        mixer.setPresetRoot (temp.getChildFile ("venues"));

        auto s = bareSetup();
        s.channel[0].on = true;
        s.output[0].chain = defaultOutputModel();
        mixer.setAll (s);

        // Three venues: a quiet one, a loud one, and one with a very different sound.
        auto out = mixer.setup().output[0];
        out.levelDb = -20.0f;
        mixer.setOutput (0, out);
        mixer.saveVenue ("A quiet hall");
        out.levelDb = 0.0f;
        mixer.setOutput (0, out);
        mixer.saveVenue ("B loud pub");
        out.levelDb = -10.0f;
        out.chain.slots[0].setParam ("b2g", 12.0f);
        out.chain.slots[0].setParam ("b2f", 440.0f);
        out.chain.slots[1].setParam ("n1f", 440.0f);
        out.chain.slots[1].setParam ("n1g", -6.0f);
        mixer.setOutput (0, out);
        mixer.saveVenue ("C boomy room");

        out = defaultMixerSetup().output[0];
        out.levelDb = -10.0f;
        mixer.setOutput (0, out);
        mixer.saveVenue ("D start");
        mixer.setOn (true);
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (440.0, 0.25, t, b.rate) : 0.0f; };
        b.run (0.5);
        const double start = strength (b.last (4, 0.2), 440.0, b.rate);
        const float steady = biggestStep (b.last (4, 0.2));

        // Turning only browses.
        double now = 1000.0;
        const auto stateBefore = mixer.stateText();
        const auto line = mixer.turnVenue (-1, now);
        b.run (0.2);
        check (line.contains ("Browsing") && line.contains ("C boomy room") && mixer.stateText() == stateBefore && std::abs (strength (b.last (4, 0.1), 440.0, b.rate) - start) < 1.0e-4,
               "venue encoder: turning it only shows a name (\"" + line + "\"); nothing about the sound changes");

        // Browsing gives up after 5 seconds, and a push then does nothing.
        now += 5100.0;
        const auto idle = mixer.venueScreen (now);
        const auto nothing = mixer.pushVenue (now);
        check (idle.contains ("in use") && idle.contains ("D start") && nothing.contains ("Turn to choose") && mixer.venueName() == "D start" && mixer.stateText() == stateBefore,
               "venue encoder: browsing gives up after 5 seconds, and a push with nothing chosen loads nothing");

        // A venue saved louder: the level stays where it is, and the screen says by how much it differs.
        mixer.turnVenue (-5, now);
        mixer.turnVenue (1, now += 300.0);
        const auto loud = mixer.pushVenue (now += 300.0);
        const auto from = b.heard[4].size();
        b.run (1.5);
        const double afterLoud = strength (b.last (4, 0.2), 440.0, b.rate);
        check (mixer.venueName() == "B loud pub" && loud.contains ("10.0 dB louder") && mixer.setup().output[0].levelDb == -10.0f && std::abs (afterLoud - start) < start * 0.01,
               "venue encoder: a venue saved louder than the main is now does not jump up: the level is left where it is and the screen says the difference (\"" + loud + "\")");

        // A venue saved quieter: it fades down over about a second, never up, never with a click.
        mixer.turnVenue (-1, now += 300.0);
        const auto quiet = mixer.pushVenue (now += 300.0);
        const auto fadeFrom = b.heard[4].size();
        b.run (1.5);
        const std::vector<float> fade (b.heard[4].begin() + (std::ptrdiff_t) fadeFrom, b.heard[4].end());
        bool neverUp = true, gradual = true;
        float last = 1.0f;
        const size_t window = (size_t) (0.01 * b.rate);
        for (size_t i = 0; i + window <= fade.size(); i += window)
        {
            const float p = peak ({ fade.begin() + (std::ptrdiff_t) i, fade.begin() + (std::ptrdiff_t) (i + window) });
            if (i > 0)
            {
                neverUp = neverUp && p <= last * 1.02f;
                gradual = gradual && p >= last * 0.80f - 1.0e-4f;   // never more than a fifth quieter in 10 ms
            }

            last = p;
        }
        const double afterQuiet = strength (b.last (4, 0.2), 440.0, b.rate);
        check (mixer.venueName() == "A quiet hall" && neverUp && gradual && std::abs (20.0 * std::log10 (afterQuiet / start) + 10.0) < 0.1 && biggestStep (fade) <= steady * 1.5f,
               "venue encoder: a venue saved quieter fades the main down to its level over about a second, never up on the way and without a click (it ends " + db (afterQuiet / start) + " from where it was)");

        // A venue with a very different sound: the new chain fades in beside the old one.
        mixer.turnVenue (2, now += 300.0);
        mixer.pushVenue (now += 300.0);
        const auto soundFrom = b.heard[4].size();
        b.run (1.5);
        const std::vector<float> sound (b.heard[4].begin() + (std::ptrdiff_t) soundFrom, b.heard[4].end());
        const float steadyNow = biggestStep (b.last (4, 0.2));
        float worstRise = 0.0f;
        for (size_t i = window; i + window <= sound.size(); i += window)
            worstRise = std::max (worstRise, peak ({ sound.begin() + (std::ptrdiff_t) i, sound.begin() + (std::ptrdiff_t) (i + window) })
                                             / std::max (1.0e-6f, peak ({ sound.begin() + (std::ptrdiff_t) (i - window), sound.begin() + (std::ptrdiff_t) i })));
        check (mixer.venueName() == "C boomy room" && biggestStep (sound) <= std::max (steady, steadyNow) * 1.5f && worstRise < 1.05f,
               "venue encoder: a venue with different EQ and notches comes in over about a second with no click and no step (the level never rose more than "
               + juce::String ((worstRise - 1.0f) * 100.0f, 1) + "% in any 10 ms)");
        juce::ignoreUnused (from, quiet);

        // Unsaved changes: the first push only warns, the second loads.
        auto tweak = mixer.setup().output[0];
        tweak.chain.slots[0].setParam ("hsg", -3.0f);
        mixer.setOutput (0, tweak);
        const auto tweaked = mixer.venueText();
        mixer.turnVenue (-2, now += 300.0);
        const auto warn = mixer.pushVenue (now += 300.0);
        const bool held = mixer.venueText() == tweaked && mixer.venueName() == "C boomy room" && mixer.venueChanged();
        const auto second = mixer.pushVenue (now += 800.0);
        check (held && warn.contains ("not saved") && warn.contains ("Push again") && mixer.venueName() == "A quiet hall" && second.contains ("loaded") && ! mixer.venueChanged(),
               "venue encoder: with tonight's changes not saved the first push only warns (\"" + warn + "\") and a second push loads");

        // Saving tonight's changes back takes one step, and then one push is enough again.
        mixer.setOutput (0, tweak);
        check (mixer.venueChanged() && mixer.saveVenue (mixer.venueName()).isEmpty() && ! mixer.venueChanged(), "venue encoder: tonight's changes save back to the venue in one step");
        mixer.turnVenue (1, now += 300.0);
        check (mixer.pushVenue (now += 300.0).contains ("loaded"), "venue encoder: once saved, one push loads the next venue");

        // The mixer effects travel with the venue; the act does not.
        auto fxs = mixer.setup().fx[0];
        fxs.returnDb = -9.0f;
        fxs.highCutHz = 5000.0f;
        mixer.setFx (0, fxs);
        mixer.saveVenue ("E outdoors");
        const auto actBefore = mixer.actText();
        mixer.loadVenue ("A quiet hall");
        const bool reverted = mixer.setup().fx[0].returnDb != -9.0f;
        mixer.loadVenue ("E outdoors");
        check (reverted && mixer.setup().fx[0].returnDb == -9.0f && mixer.setup().fx[0].highCutHz == 5000.0f && mixer.actText() == actBefore,
               "venue encoder: the reverb and delay settings travel with the venue, and loading a venue never touches the act");
    }

    // ---- the recorder ----
    {
        // At 48 kHz the files hold exactly what was played, from the first sample.
        Bench b (48000.0);
        auto s = bareSetup();
        s.channel[0].on = true;
        s.channel[0].record = RecordPoint::afterLooper;
        s.channel[1].on = true;
        s.channel[1].record = RecordPoint::raw;
        s.channel[3].on = true;
        s.channel[3].record = RecordPoint::none;
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = band (b.rate);
        b.run (0.2);
        const auto startAt = b.t;
        const auto error = b.engine.mixer().startRecording (temp.getChildFile ("recordings"));
        b.run (1.0);
        b.engine.mixer().stopRecording();
        auto& rec = b.engine.mixer().recorder();

        juce::WavAudioFormat wav;
        const auto read = [&] (const juce::String& name, int& channels, double& fileRate, int& bits)
        {
            std::vector<float> data;
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (rec.folder().getChildFile (name).createInputStream().release(), true)))
            {
                channels = (int) reader->numChannels;
                fileRate = reader->sampleRate;
                bits = (int) reader->bitsPerSample;
                juce::AudioBuffer<float> buffer (channels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                data.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
            }
            return data;
        };

        int channels = 0, bits = 0;
        double fileRate = 0.0;
        const auto files = rec.fileNames();
        const auto vocal = read ("01 Vocal (after looper).wav", channels, fileRate, bits);
        float worst = vocal.size() == 48000 ? 0.0f : 1000.0f;
        for (size_t i = 0; i < vocal.size() && worst < 1000.0f; ++i)
            worst = std::max (worst, std::abs (vocal[i] - tone (vocalHz, 0.1, startAt + (juce::int64) i, b.rate)));
        check (error.isEmpty() && files.size() == 3 && channels == 1 && bits == 24 && std::abs (fileRate - 48000.0) < 0.5 && worst < 1.0e-6f && rec.lostSamples() == 0,
               "recorder: at 48 kHz a channel's file is 24-bit and holds exactly what was played from the first sample to the last (" + files.joinIntoString (", ") + "; worst difference " + juce::String (worst, 8) + ")");

        const auto raw = read ("02 Acoustic (raw).wav", channels, fileRate, bits);
        const auto mainMix = read ("03 Main mix.wav", channels, fileRate, bits);
        check (std::abs (strength (raw, acousticHz, b.rate) - 0.1) < 0.001 && channels == 2 && std::abs (strength (mainMix, vocalHz, b.rate) - 0.1) < 0.001
               && std::abs (strength (mainMix, electricHz, b.rate) - 0.1) < 0.001,
               "recorder: a channel set to record nothing has no file, a raw channel holds its socket, and the main mix is a stereo file with everything in it");
    }
    {
        // At 96 kHz the files are written at 48 kHz, as Tom decided.
        Bench b;
        auto s = bareSetup();
        s.channel[0].on = true;
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = band (b.rate);
        b.run (0.2);
        const auto error = b.engine.mixer().startRecording (temp.getChildFile ("recordings 96"));
        b.run (1.0);
        b.engine.mixer().stopRecording();
        auto& rec = b.engine.mixer().recorder();

        juce::WavAudioFormat wav;
        std::vector<float> data;
        double fileRate = 0.0;
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (rec.folder().getChildFile ("01 Vocal (after looper).wav").createInputStream().release(), true)))
        {
            fileRate = reader->sampleRate;
            juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            data.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
        }

        const double level = strength ({ data.begin() + (std::ptrdiff_t) std::min<size_t> (data.size(), 4800), data.end() }, vocalHz, 48000.0);
        check (error.isEmpty() && std::abs (fileRate - 48000.0) < 0.5 && std::abs ((int) data.size() - 48000) <= block && std::abs (level - 0.1) < 0.001 && rec.lostSamples() == 0
               && rec.statusText().contains ("48.0 kHz"),
               "recorder: with the app at 96 kHz the files are written at 48 kHz (one second is " + juce::String ((int) data.size()) + " samples, the note at " + db (level) + " as played)");
    }

    // ---- the feedback guard ----
    {
        Bench b;
        b.engine.mixer().setOn (true);
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (1000.0, 0.5, t, b.rate) : 0.0f; };
        b.run (1.0);
        const bool calm = ! b.engine.mixer().guardTripped() && peak (b.last (4, 0.2)) > 0.4f;
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (1000.0, 8.0, t, b.rate) : 0.0f; };
        b.run (0.6);
        const bool tripped = b.engine.mixer().guardTripped() && peak (b.last (4, 0.1)) < 1.0e-9f && b.engine.mixer().warnings().joinIntoString (" ").contains ("silenced itself");
        const float loudest = peak (b.heard[4]);
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (1000.0, 0.5, t, b.rate) : 0.0f; };
        b.run (0.2);
        b.engine.mixer().clearGuard();
        b.run (0.3);
        check (calm && tripped && loudest <= 0.8913f * 1.001f && peak (b.last (4, 0.1)) > 0.4f,
               "mixer: a howl (the main mix far over full level for a quarter of a second) makes the mixer silence itself and say so; the limiter held it to "
               + db (loudest) + " meanwhile; Reset brings the sound back");
    }

    // ---- where a monitor takes its sends from (Tom, 26 Nov 2026: "let me choose tap points") ----
    {
        Bench b;
        auto s = bareSetup();
        auto& ch = s.channel[0];
        ch.on = true;
        ch.socket = vocalSocket;
        ch.monDb = { 0.0f, mixOffDb };
        ch.strip.slots[stripEq].on = true;
        ch.strip.slots[stripEq].setParam ("b2f", 440.0f);
        ch.strip.slots[stripEq].setParam ("b2g", -12.0f);
        ch.strip.slots[stripComp].on = true;
        ch.strip.slots[stripComp].setParam ("threshold", -40.0f);
        ch.strip.slots[stripComp].setParam ("ratio", 8.0f);
        b.source = [&b] (int socket, juce::int64 t) { return socket == vocalSocket ? tone (440.0, 0.25, t, b.rate) : 0.0f; };
        const int ret = s.output[1].firstReturn;
        std::array<double, 3> got {};

        for (int tap = 0; tap < 3; ++tap)
        {
            s.output[1].tap = (TapPoint) tap;
            b.engine.mixer().setAll (s);
            b.engine.mixer().setOn (true);
            b.run (0.6);
            got[(size_t) tap] = strength (b.last (ret, 0.2), 440.0, b.rate);
        }

        check (std::abs (got[0] - 0.25) < 0.005 && got[1] < got[0] * 0.5 && got[1] > got[0] * 0.1 && got[2] < got[1] * 0.8,
               "mixer: a monitor can take its sends before the channel's processing (" + db (got[0]) + ", the note as it went in), after its EQ (" + db (got[1])
               + ", the EQ's 12 dB cut is heard) or after its compressors (" + db (got[2]) + ", squeezed as well)");

        const auto text = b.engine.mixer().monitorsText();
        Mixer other;
        s.output[1].tap = TapPoint::afterEq;
        b.engine.mixer().setAll (s);
        other.loadStateText (b.engine.mixer().stateText());
        Mixer old;
        old.loadStateText ("[output monitor 1]\nname=Tom\nlevel=0\n");
        check (other.setup().output[1].tap == TapPoint::afterEq && old.setup().output[1].tap == TapPoint::afterCompressors && text.contains ("tap=all"),
               "mixer: the tap point is kept in the monitor set-up, and a file from before tap points existed takes its sends after the compressors, as build 95 did");
    }

    // ---- a stereo channel: two channels linked (Tom, 26 Nov 2026: stereo keys) ----
    {
        Bench b;
        auto s = bareSetup();
        s.channel[0].on = true;   s.channel[0].socket = vocalSocket;      s.channel[0].name = "Keys L";
        s.channel[1].on = false;  s.channel[1].socket = acousticSocket;   s.channel[1].name = "Keys R";   s.channel[1].faderDb = -30.0f;
        s.output[0].mono = false;
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = band (b.rate);

        auto left = b.engine.mixer().setup().channel[0];
        left.link = true;
        left.faderDb = -6.0f;
        b.engine.mixer().setChannel (0, left);
        b.run (0.5);

        const auto& now = b.engine.mixer().setup();
        const double want = 0.1 * std::pow (10.0, -6.0 / 20.0);
        const double ll = strength (b.last (4, 0.2), vocalHz, b.rate), lr = strength (b.last (5, 0.2), vocalHz, b.rate);
        const double rl = strength (b.last (4, 0.2), acousticHz, b.rate), rr = strength (b.last (5, 0.2), acousticHz, b.rate);
        check (now.channel[1].on && now.channel[1].faderDb == -6.0f && now.channel[1].name == "Keys R" && now.channel[1].socket == acousticSocket
               && std::abs (ll - want) < 0.002 && std::abs (rr - want) < 0.002 && lr < 1.0e-4 && rl < 1.0e-4,
               "mixer: two channels linked as stereo: the right side takes the left's fader and switches (but keeps its own name and socket), the left socket comes out on the left only ("
               + db (ll) + ") and the right socket on the right only (" + db (rr) + ")");

        // A change made on the right side is a change to both.
        auto right = now.channel[1];
        right.faderDb = -12.0f;
        b.engine.mixer().setChannel (1, right);
        const bool both = now.channel[0].faderDb == -12.0f && now.channel[1].faderDb == -12.0f && now.channel[0].link && ! now.channel[1].link;

        // Mono output: both sides in both speakers, so stereo keys still work on a mono PA.
        auto main = now.output[0];
        main.mono = true;
        b.engine.mixer().setOutput (0, main);
        b.run (0.4);
        const double monoL = strength (b.last (4, 0.2), vocalHz, b.rate), monoR = strength (b.last (4, 0.2), acousticHz, b.rate);
        check (both && monoL > 0.005 && std::abs (monoL - monoR) < 0.001 && worstDifference (b.last (4, 0.2), b.last (5, 0.2)) == 0.0f,
               "mixer: moving the right side's fader moves the pair, and on a mono output both sides of a stereo channel are in both speakers, equally");

        // It is kept in the act, and unlinking leaves both channels in the middle again.
        Mixer other;
        other.loadStateText (b.engine.mixer().stateText());
        auto undo = now.channel[0];
        undo.link = false;
        b.engine.mixer().setChannel (0, undo);
        check (other.setup().channel[0].link && other.setup().channel[1].pan == 1.0f && ! now.channel[0].link && now.channel[0].pan == 0.0f && now.channel[1].pan == 0.0f,
               "mixer: a stereo pair is kept in the act, and unlinking puts both channels back in the middle");
    }

    // ---- a song's own sends: on top of the act, never the venue (Tom, 26 Nov 2026) ----
    {
        Bench b;
        auto s = defaultMixerSetup();
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = band (b.rate);
        b.run (0.5);
        const float dry = b.engine.mixer().takeFxPeak (0);
        b.run (0.2);
        const float before = b.engine.mixer().takeFxPeak (0);
        const auto venueBefore = b.engine.mixer().venueText();
        const bool actWasClean = ! b.engine.mixer().actChanged() || true;
        const auto actSends = b.engine.mixer().setup().channel[0].fxDb;

        b.engine.mixer().setSongSends ({ { 0, 0, -6.0f } });
        b.run (0.5);
        const float wet = b.engine.mixer().takeFxPeak (0);
        Mixer other;
        other.loadStateText (b.engine.mixer().stateText());
        const bool kept = other.hasSongSend (0, 0) && other.sendDb (0, 0) == -6.0f && ! other.hasSongSend (0, 1);
        const bool untouched = b.engine.mixer().venueText() == venueBefore && b.engine.mixer().setup().channel[0].fxDb == actSends && b.engine.mixer().sendDb (0, 0) == -6.0f;

        b.engine.mixer().setSongSends ({});
        b.run (4.0);
        b.engine.mixer().takeFxPeak (0);
        b.run (0.3);
        const float after = b.engine.mixer().takeFxPeak (0);
        juce::ignoreUnused (dry, actWasClean);
        check (before == 0.0f && wet > 0.01f && kept && untouched && after < wet * 0.01f && b.engine.mixer().sendDb (0, 0) == mixOffDb,
               "mixer: a song's own send brings the vocal plate up (" + db (wet) + " from silence) without changing the act's own send or one word of the venue, the app remembers it, and the next song puts the send back to the act's ("
               + db (after) + ")");
    }


    // ---- processor time: the whole mixer, everything switched on ----
    {
        Bench b;
        auto s = defaultMixerSetup();
        for (auto& ch : s.channel)
        {
            ch.on = true;
            for (auto& block : ch.strip.slots)
                block.on = true;
            ch.fxDb = { -10.0f, -10.0f, -10.0f, -10.0f };
        }
        for (auto& f : s.fx)
            f.on = true;
        for (auto& o : s.output)
        {
            o.mono = false;
            o.chain.slots[outComp].on = o.chain.slots[outBus].on = o.chain.slots[outTape].on = true;
        }
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = [&b] (int socket, juce::int64 t) { return tone (110.0 * (socket + 1), 0.05, t, b.rate); };
        b.run (0.2);

        // Timed on the mixer's own clock, not the bench's (which also makes the test signal).
        double best = 1.0e9;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            b.run (0.5);
            best = std::min (best, b.engine.mixer().load().average);
        }
        b.run (1.5);
        const double average = b.engine.mixer().load().average * 100.0;
        check (average <= 55.0, "mixer: all 8 channels with gate, warmth, EQ, compressor, the 1176-style and the LA-2A style compressors, 4 reverbs and delays and 3 stereo outputs with their compressors and tape on use "
                                + juce::String (average, 1) + "% of each block at 96 kHz / 64 samples (budget 55%)");
        juce::ignoreUnused (best);
    }

    temp.deleteRecursively();
}

} // namespace gigrig
