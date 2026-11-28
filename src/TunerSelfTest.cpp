#include "AudioEngine.h"

#include <cmath>
#include <functional>
#include <random>

// Checks for the tuner (27 Nov 2026), run by the automatic builds (GigRig --self-test): that it
// reads notes from a low bass string to a high voice within a cent, that a string whose first
// overtone is louder than the note itself is not read an octave out, that hiss and silence
// show no note, that it finds the input being played, that with the mixer on the guitars (and
// only the guitars) go silent while it is on, or the whole main mix if that is chosen, and that
// with the mixer off it changes no sound at all.
//
// The strobe (28 Nov 2026): that its picture stands still for a string in tune and slips by
// exactly the difference in frequency when it is not, that the cents it shows are right to a
// tenth of a cent, on plain notes, on a plucked string with overtones that are not quite in
// tune with it, and on a low string whose own note is almost missing.

namespace gigrig
{

namespace
{
    constexpr double twoPi = 6.283185307179586;
    constexpr int block = 64;

    std::vector<float> tone (double hz, double rate, int n, std::initializer_list<double> partials = { 1.0 })
    {
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            int k = 1;

            for (double a : partials)
                v += a * std::sin (twoPi * hz * (double) k++ * (double) i / rate + 0.3 * (double) k);

            x[(size_t) i] = (float) (0.2 * v);
        }

        return x;
    }

    double centsBetween (double a, double b)    { return 1200.0 * std::log2 (a / b); }

    struct Bench
    {
        AudioEngine engine;
        double rate;
        std::vector<std::vector<float>> ins, outs;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        std::array<std::vector<float>, numReturns> heard;
        juce::int64 t = 0;
        std::function<float (int socket, juce::int64 time)> source = [] (int, juce::int64) { return 0.0f; };

        explicit Bench (double sampleRate) : rate (sampleRate)
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
            const int perTick = juce::jmax (1, (int) (0.05 * rate / block));

            for (int b = 0; b < blocks; ++b)
            {
                for (int s = 0; s < 16; ++s)
                    for (int i = 0; i < block; ++i)
                        ins[(size_t) s][(size_t) i] = source (s, t + i);

                t += block;
                engine.breakMusic().fillNow();
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);

                for (int r = 0; r < numReturns; ++r)
                {
                    const auto& o = outs[(size_t) returnToOutput (r, 16)];
                    heard[(size_t) r].insert (heard[(size_t) r].end(), o.begin(), o.end());
                }

                if (b % perTick == perTick - 1)
                {
                    engine.tuner().tick();
                    engine.mixer().tick();
                }
            }
        }

        float peakOfLast (int ret, double seconds) const
        {
            const auto& h = heard[(size_t) ret];
            const auto n = juce::jmin (h.size(), (size_t) (seconds * rate));
            float p = 0.0f;

            for (auto i = h.size() - n; i < h.size(); ++i)
                p = juce::jmax (p, std::abs (h[i]));

            return p;
        }
    };
}

void runTunerSelfTest (const std::function<void (bool, const juce::String&)>& check)
{
    // ---- the note from a stretch of sound ----
    {
        double worst = 0.0, clarity = 0.0;
        juce::String worstAt;

        for (double rate : { 24000.0, 22050.0 })
            for (double hz : { 30.87, 41.2, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.26, 880.0, 1046.5 })
            {
                const auto x = tone (hz, rate, 4096);
                const double got = Tuner::pitchOf (x.data(), 4096, rate, clarity);
                const double off = got > 0.0 ? std::abs (centsBetween (got, hz)) : 1200.0;

                if (off > worst)
                {
                    worst = off;
                    worstAt = juce::String (hz, 2) + " Hz";
                }
            }

        check (worst < 1.0, "tuner: plain notes from a five-string bass's low B to a high C read within a cent (worst " + juce::String (worst, 2) + " cents, at " + worstAt + ")");

        // A wound string: the first overtone louder than the note itself, and more above it.
        double worstString = 0.0;

        for (double hz : { 41.2, 82.41, 110.0, 196.0, 329.63 })
        {
            const auto x = tone (hz, 24000.0, 4096, { 0.3, 1.0, 0.6, 0.35, 0.2, 0.1 });
            const double got = Tuner::pitchOf (x.data(), 4096, 24000.0, clarity);
            worstString = juce::jmax (worstString, got > 0.0 ? std::abs (centsBetween (got, hz)) : 1200.0);
        }

        check (worstString < 1.5, "tuner: a string whose overtones are louder than the note is not read an octave out (worst " + juce::String (worstString, 2) + " cents)");

        const auto sharp = tone (110.0 * std::pow (2.0, 7.0 / 1200.0), 24000.0, 4096, { 1.0, 0.5, 0.25 });
        const auto flat = tone (196.0 * std::pow (2.0, -12.0 / 1200.0), 24000.0, 4096, { 1.0, 0.5, 0.25 });
        int midi = 0;
        double cents = 0.0, cents2 = 0.0;
        Tuner::nearestNote (Tuner::pitchOf (sharp.data(), 4096, 24000.0, clarity), 440.0, midi, cents);
        const bool a2 = Tuner::noteName (midi) == "A2";
        Tuner::nearestNote (Tuner::pitchOf (flat.data(), 4096, 24000.0, clarity), 440.0, midi, cents2);
        check (a2 && std::abs (cents - 7.0) < 0.7 && Tuner::noteName (midi) == "G3" && std::abs (cents2 + 12.0) < 0.7,
               "tuner: 7 cents sharp reads 7 sharp and 12 cents flat reads 12 flat (" + juce::String (cents, 2) + ", " + juce::String (cents2, 2) + ")");

        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
        std::vector<float> hiss (4096);
        for (auto& v : hiss) v = dist (rng);
        const double fromHiss = Tuner::pitchOf (hiss.data(), 4096, 24000.0, clarity);
        const double hissClarity = clarity;
        const std::vector<float> nothing (4096, 0.0f);
        check ((fromHiss == 0.0 || hissClarity < 0.9) && Tuner::pitchOf (nothing.data(), 4096, 24000.0, clarity) == 0.0,
               "tuner: hiss and silence are not taken for a note (hiss is " + juce::String (hissClarity, 2) + " sure, a note needs 0.9)");

        Tuner::nearestNote (440.0, 440.0, midi, cents);
        const bool a4 = midi == 69 && std::abs (cents) < 1.0e-6 && Tuner::noteName (69) == "A4" && Tuner::noteName (40) == "E2" && Tuner::noteName (61) == "C#4";
        Tuner::nearestNote (432.0, 432.0, midi, cents);
        const bool other = midi == 69 && std::abs (cents) < 1.0e-6;
        Tuner::nearestNote (440.0, 432.0, midi, cents);
        check (a4 && other && midi == 69 && cents > 31.0 && cents < 32.5, "tuner: note names are right, and tuning to another A moves every note with it");
    }

    // ---- in the engine: it finds the input, at every sample rate ----
    for (double rate : { 96000.0, 48000.0, 44100.0 })
    {
        Bench b (rate);
        auto& tuner = b.engine.tuner();
        const double hz = 110.0 * std::pow (2.0, 5.0 / 1200.0);
        bool playing = true;
        b.source = [&] (int socket, juce::int64 t)
        {
            const double x = (double) t / b.rate;
            return socket == 2 && playing ? (float) (0.1 * std::sin (twoPi * hz * x) + 0.05 * std::sin (twoPi * 2.0 * hz * x)) : 0.0f;
        };

        b.run (0.5);
        tuner.tick();
        const bool offSilent = ! tuner.reading().heard && ! tuner.isOn();

        tuner.setOn (true);
        b.run (0.6);
        const auto r = tuner.reading();
        const auto at = juce::String (rate / 1000.0, 1) + " kHz";
        check (offSilent && r.heard && r.name == "A2" && std::abs (r.cents - 5.0) < 1.0 && r.socket == 2,
               "tuner at " + at + ": off it reads nothing; on, it finds the input being played and reads A2, 5 cents sharp (" + (r.heard ? r.name + " " + juce::String (r.cents, 2) : juce::String ("nothing")) + ", input " + juce::String (r.socket + 1) + ")");

        if (rate == 96000.0)
        {
            check (tuner.statusLine().contains ("A2") && tuner.statusLine().contains ("5 cents sharp") && tuner.reportText().contains ("TUNER"), "tuner: the status line and the report say the note (" + tuner.statusLine() + ")");

            playing = false;
            b.run (0.8);
            check (! tuner.reading().heard, "tuner: when the note stops the needle rests");

            playing = true;
            tuner.setSocket (5);
            b.run (0.6);
            const bool deaf = ! tuner.reading().heard;
            tuner.setSocket (2);
            b.run (0.6);
            check (deaf && tuner.reading().heard && tuner.reading().name == "A2", "tuner: told to listen to one input, it hears that input and no other");

            tuner.setOn (false);
            tuner.tick();
            check (! tuner.reading().heard, "tuner: switched off, it shows nothing");
        }
    }

    // ---- the sound: mixer on, the main mix alone goes silent; mixer off, nothing changes ----
    {
        juce::File temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gigrig-tuner-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        temp.createDirectory();

        {
            Bench b (48000.0);
            auto s = defaultMixerSetup();

            for (auto& ch : s.channel)
            {
                ch.on = false;
                for (auto& slot : ch.strip.slots) slot.on = false;
            }
            for (auto& f : s.fx)
                f.on = false;
            for (auto& o : s.output)
                for (auto& slot : o.chain.slots) slot.on = false;

            s.channel[0].on = true;
            s.channel[0].monDb = { 0.0f, 0.0f };
            s.output[0].mono = false;
            s.output[0].firstReturn = 4;
            s.output[1].firstReturn = 0;
            s.output[2].firstReturn = 2;
            b.engine.mixer().setAll (s);
            b.engine.mixer().setOn (true);
            const int socket = s.channel[0].socket;
            b.source = [&] (int sk, juce::int64 t) { return sk == socket ? (float) (0.1 * std::sin (twoPi * 220.0 * (double) t / b.rate)) : 0.0f; };

            auto& tuner = b.engine.tuner();
            tuner.setSilence (Tuner::Silence::mainMix);
            b.run (0.5);
            const float before = b.peakOfLast (4, 0.1), monBefore = b.peakOfLast (0, 0.1);

            tuner.setOn (true);
            b.run (0.5);
            const float during = b.peakOfLast (4, 0.2), monDuring = b.peakOfLast (0, 0.2);
            const bool reads = tuner.reading().heard && tuner.reading().name == "A3";

            // No click on the way down: the biggest step between samples is no bigger than the note's own.
            float step = 0.0f;
            const auto& h = b.heard[4];
            for (size_t i = h.size() - (size_t) (0.5 * b.rate); i + 1 < h.size(); ++i)
                step = juce::jmax (step, std::abs (h[i + 1] - h[i]));

            tuner.setOn (false);
            b.run (0.3);
            const float after = b.peakOfLast (4, 0.1);

            check (before > 0.09f && during < 1.0e-6f && std::abs (after - before) < 1.0e-5f && step < 0.1f * (float) (twoPi * 220.0 / b.rate) * 1.5f,
                   "tuner: set to silence the whole main mix (mixer on), it fades silent while the tuner is on and comes back exactly, with no click (" + juce::String (before, 4) + ", " + juce::String (during, 6) + ", " + juce::String (after, 4) + ")");
            check (reads && monBefore > 0.09f && std::abs (monDuring - monBefore) < 1.0e-5f, "tuner: with the whole main mix silent, the monitors stay live and the note is still read");

            tuner.setSilence (Tuner::Silence::nothing);
            tuner.setOn (true);
            b.run (0.3);
            check (std::abs (b.peakOfLast (4, 0.1) - before) < 1.0e-5f, "tuner: set to silence nothing, the main mix carries on");
            tuner.setOn (false);
            tuner.setSilence (Tuner::Silence::mainMix);

            // The break music is not the main mix: it plays on while the tuner is on.
            auto folder = temp.getChildFile ("Break music");
            folder.createDirectory();
            {
                juce::AudioBuffer<float> buf (2, 48000);
                for (int i = 0; i < 48000; ++i)
                {
                    buf.setSample (0, i, (float) (0.5 * std::sin (twoPi * 1000.0 * i / 48000.0)));
                    buf.setSample (1, i, (float) (0.5 * std::sin (twoPi * 1000.0 * i / 48000.0)));
                }
                juce::WavAudioFormat wav;
                auto out = std::unique_ptr<juce::OutputStream> (folder.getChildFile ("Tone.wav").createOutputStream());
                if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (buf, 0, 48000);
            }
            auto& music = b.engine.breakMusic();
            music.setFolder (folder);
            auto set = music.settings();
            set.onSwitch = false;
            music.setSettings (set);
            music.setLevelDb (0.0f);
            music.play ("Tone", true);
            tuner.setOn (true);
            b.run (6.0);
            const float withMusic = b.peakOfLast (4, 0.2);
            check (withMusic > 0.4f && withMusic < 0.52f, "tuner: the break music plays on while the tuner is on (" + juce::String (withMusic, 3) + ", and the instrument is not in it)");
            music.stop();
            tuner.setOn (false);
        }

        temp.deleteRecursively();

        // ---- the guitars go silent, the voice and the stomp do not (Tom, 28 Nov 2026) ----
        {
            // Two rigs fed the same sound: one never tunes, the other does. The voice must come out
            // of both exactly alike, sample for sample, all the way through.
            const auto setUp = [] (Bench& b, bool guitarToo)
            {
                auto s = defaultMixerSetup();

                for (auto& ch : s.channel)
                {
                    for (auto& slot : ch.strip.slots) slot.on = false;
                    ch.monDb = { 0.0f, 0.0f };
                }
                for (auto& f : s.fx)
                    f.on = false;
                for (auto& o : s.output)
                    for (auto& slot : o.chain.slots) slot.on = false;

                s.output[0].mono = false;
                s.output[0].firstReturn = 4;
                s.output[1].firstReturn = 0;
                s.output[2].firstReturn = 2;
                b.engine.mixer().setAll (s);
                b.engine.mixer().setOn (true);

                // Vocal on socket 6 (a loud 300 Hz), electric on socket 8 (a quieter A string, 4 cents flat).
                b.source = [&b, guitarToo] (int sk, juce::int64 t)
                {
                    const double x = (double) t / b.rate;
                    return sk == 5 ? (float) (0.3 * std::sin (twoPi * 300.0 * x))
                         : sk == 7 && guitarToo ? (float) (0.05 * std::sin (twoPi * 110.0 * std::pow (2.0, -4.0 / 1200.0) * x)) : 0.0f;
                };
            };

            Bench voiceOnly (48000.0), both (48000.0);
            setUp (voiceOnly, false);
            setUp (both, true);
            auto& tuner = both.engine.tuner();
            const bool byDefault = tuner.silence() == Tuner::Silence::guitars;

            // The guitar alone in an output: the rig with the guitar less the rig without it.
            const auto guitarIn = [&] (int ret, double seconds)
            {
                const auto& a = voiceOnly.heard[(size_t) ret];
                const auto& c = both.heard[(size_t) ret];
                float p = 0.0f;

                for (size_t i = a.size() - (size_t) (seconds * 48000.0); i < a.size(); ++i)
                    p = juce::jmax (p, std::abs (c[i] - a[i]));

                return p;
            };

            voiceOnly.run (0.5);
            both.run (0.5);
            const float before = guitarIn (4, 0.1), monBefore = guitarIn (0, 0.1);

            // As it comes: the guitar leaves the main mix and stays in the monitors (Tom's in-ears).
            const bool keepsByDefault = tuner.monitorsKeepGuitars();
            tuner.setOn (true);
            voiceOnly.run (0.7);
            both.run (0.7);
            const auto r = tuner.reading();
            const float mainDuring = juce::jmax (guitarIn (4, 0.3), guitarIn (5, 0.3));
            const float monDuring = guitarIn (0, 0.1), mon2During = guitarIn (2, 0.1);

            // No click on the way down: the guitar's part of the main mix never steps more than the note itself does.
            const auto biggestStep = [&] (int ret, double seconds)
            {
                const auto& a = voiceOnly.heard[(size_t) ret];
                const auto& c = both.heard[(size_t) ret];
                float step = 0.0f;

                for (size_t i = a.size() - (size_t) (seconds * 48000.0); i + 1 < a.size(); ++i)
                    step = juce::jmax (step, std::abs ((c[i + 1] - a[i + 1]) - (c[i] - a[i])));

                return step;
            };

            const float limit = 0.05f * (float) (twoPi * 110.0 / 48000.0) * 1.5f + 2.0e-4f;
            float step = biggestStep (4, 0.7);

            tuner.setOn (false);
            voiceOnly.run (0.3);
            both.run (0.3);
            step = juce::jmax (step, biggestStep (4, 0.3));
            const float after = guitarIn (4, 0.1), monAfter = guitarIn (0, 0.1);
            const auto silentNames = both.engine.tunerChannels (true).joinIntoString (", "), liveNames = both.engine.tunerChannels (false).joinIntoString (", ");

            check (byDefault && keepsByDefault && before > 0.045f && monBefore > 0.045f && mainDuring < 1.0e-6f && std::abs (after - before) < 1.0e-5f && step < limit,
                   "tuner: with the mixer on, the guitar fades out of the main mix while the tuner is on, the voice there is untouched to the sample, and the guitar comes back exactly, with no click (left over "
                       + juce::String (mainDuring, 8) + ", biggest step " + juce::String (step, 6) + ")");
            check (std::abs (monDuring - monBefore) < 1.0e-5f && std::abs (mon2During - monBefore) < 1.0e-5f && std::abs (monAfter - monBefore) < 1.0e-5f,
                   "tuner: the monitors keep the guitar while tuning, exactly as loud (" + juce::String (monBefore, 4) + ", " + juce::String (monDuring, 4) + ")");
            check (r.heard && r.name == "A2" && std::abs (r.cents + 4.0) < 0.3 && r.socket == 7,
                   "tuner: it reads the guitar, not the louder voice, while the voice is live (" + (r.heard ? r.name + " " + juce::String (r.cents, 2) : juce::String ("nothing")) + ", input " + juce::String (r.socket + 1) + ")");
            check (silentNames == "Acoustic, Electric, Bass" && liveNames == "Vocal, Stomp",
                   "tuner: guitars are known by their names: silent " + silentNames + "; live " + liveNames);

            // The other way: silent everywhere, as a tuner pedal at the front of a pedalboard does it.
            tuner.setMonitorsKeepGuitars (false);
            tuner.setOn (true);
            voiceOnly.run (0.7);
            both.run (0.7);
            float left = 0.0f;
            for (int ret : { 4, 5, 0, 2 })
                left = juce::jmax (left, guitarIn (ret, 0.3));
            float step2 = juce::jmax (biggestStep (4, 0.7), biggestStep (0, 0.7));
            tuner.setOn (false);
            voiceOnly.run (0.3);
            both.run (0.3);
            step2 = juce::jmax (step2, juce::jmax (biggestStep (4, 0.3), biggestStep (0, 0.3)));
            Tuner remembers;
            remembers.loadStateText (tuner.stateText());
            check (left < 1.0e-6f && std::abs (guitarIn (4, 0.1) - before) < 1.0e-5f && std::abs (guitarIn (0, 0.1) - monBefore) < 1.0e-5f && step2 < limit && ! remembers.monitorsKeepGuitars(),
                   "tuner: with 'keep the guitars in the monitors' unticked the guitar is silent in the main mix and both monitors, comes back exactly with no click, and the choice is remembered (left over "
                       + juce::String (left, 8) + ", biggest step " + juce::String (step2, 6) + ")");
            tuner.setMonitorsKeepGuitars (true);

            // A choice made by hand wins over the name, both ways, and is remembered.
            tuner.setGoesSilent ("Stomp", true);
            tuner.setGoesSilent ("Bass", false);
            Tuner other;
            other.loadStateText (tuner.stateText());
            const bool byHand = tuner.goesSilent ("stomp") && ! tuner.goesSilent ("BASS") && other.goesSilent ("Stomp") && ! other.goesSilent ("Bass") && other.goesSilent ("Electric") && ! other.goesSilent ("Vocal");
            tuner.setGoesSilent ("Stomp", false);
            tuner.setGoesSilent ("Bass", true);
            check (byHand && ! tuner.stateText().contains ("channels=")
                       && Tuner::looksLikeGuitar ("Guitar 2") && Tuner::looksLikeGuitar ("Duo bass") && ! Tuner::looksLikeGuitar ("Duo vocal") && ! Tuner::looksLikeGuitar ("Bass drum") && ! Tuner::looksLikeGuitar ("Keys"),
                   "tuner: a channel ticked or unticked by hand wins over its name and is remembered; with none, nothing extra is written");
        }

        Bench plain (96000.0), tuning (96000.0);
        const auto src = [] (int socket, juce::int64 t) { return socket < 6 ? (float) (0.1 * std::sin (twoPi * (110.0 + 40.0 * socket) * (double) t / 96000.0)) : 0.0f; };
        plain.source = src;
        tuning.source = src;
        tuning.engine.tuner().setOn (true);
        plain.run (0.5);
        tuning.run (0.5);
        bool same = true;
        for (int r = 0; r < numReturns; ++r)
            same = same && plain.heard[(size_t) r] == tuning.heard[(size_t) r];
        check (same && tuning.engine.tuner().reading().heard, "tuner: with the mixer off, every output is sample for sample the same with the tuner on as with it off");
    }

    // ---- what it remembers ----
    {
        Tuner a, b, old, oldOff;
        a.setSocket (3);
        a.setReference (442.0);
        a.setSilence (Tuner::Silence::mainMix);
        a.setFillsScreen (false);
        b.loadStateText (a.stateText());
        a.setReference (900.0);

        // A memory file written by a build before the silence choice: yes now means the guitars.
        old.loadStateText ("[tuner]\nsocket=-1\nreference=440.0\nmute main=1\n");
        oldOff.loadStateText ("[tuner]\nmute main=0\n");
        check (b.socket() == 3 && std::abs (b.reference() - 442.0) < 0.01 && b.silence() == Tuner::Silence::mainMix && ! b.fillsScreen() && std::abs (a.reference() - Tuner::maxReference) < 0.01
                   && old.silence() == Tuner::Silence::guitars && old.fillsScreen() && oldOff.silence() == Tuner::Silence::nothing && ! oldOff.mutes()
                   && a.stateText().contains ("mute main=1"),
               "tuner: the input, the A, what goes silent and the full screen choice are remembered, an older build's memory file still reads, and a silly A is held to 430 to 450");
    }

    // ---- the strobe ----
    {
        // The measurement itself: a steady wave against a note 0.05 Hz below it slips 0.05 cycles a second.
        {
            const double rate = 24000.0, hz = 110.05;
            std::vector<float> x (48000);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (0.2 * std::sin (twoPi * hz * (double) i / rate) + 0.1 * std::sin (twoPi * 2.0 * hz * (double) i / rate));

            double size = 0.0, size2 = 0.0;
            const double p1 = Tuner::phaseAgainst (x.data() + 12000 - 2048, 2048, rate, 110.0, 12000, size);
            const double p2 = Tuner::phaseAgainst (x.data() + 36000 - 2048, 2048, rate, 110.0, 36000, size2);
            double moved = p2 - p1;
            moved -= std::floor (moved + 0.5);
            check (std::abs (moved - 0.05) < 0.0005 && std::abs (size - 0.2) < 0.005 && std::abs (size2 - 0.2) < 0.005,
                   "strobe: a wave 0.05 Hz above the note slips 0.05 cycles in a second (" + juce::String (moved, 5) + "), and its size is read right (" + juce::String (size, 4) + ")");
        }

        // In the engine. A plucked string: it dies away, and its overtones are a little sharp of
        // whole multiples, as a real string's are (the stiffer the string, the more).
        struct Case { const char* what; double note; double cents; double stiff; double first; };
        const Case cases[] {
            { "bass E, in tune",              41.203,  0.0,  0.00005, 1.0 },
            { "low E, in tune",               82.407,  0.0,  0.0001,  1.0 },
            { "low E, 0.3 sharp",             82.407,  0.3,  0.0001,  1.0 },
            { "A, 2 flat",                    110.0,  -2.0,  0.0001,  1.0 },
            { "D, 0.5 flat",                  146.832, -0.5, 0.0001,  1.0 },
            { "G, 10 sharp",                  195.998, 10.0, 0.0003,  1.0 },
            { "B, 1 sharp",                   246.942, 1.0,  0.0003,  1.0 },
            { "high E, 25 flat",              329.628, -25.0, 0.0002, 1.0 },
            { "low E, its own note nearly gone", 82.407, 0.7, 0.0,    0.01 },
            { "bass B, 3 sharp",              30.868,  3.0,  0.00005, 0.5 } };

        double worstCents = 0.0, worstSlip = 0.0;
        juce::String worstAt, slipAt;
        bool allLocked = true;

        for (const auto& c : cases)
        {
            Bench b (96000.0);
            auto& tuner = b.engine.tuner();
            const double hz = c.note * std::pow (2.0, c.cents / 1200.0);
            static const double loud[] { 1.0, 0.8, 0.5, 0.35, 0.2, 0.12 };

            b.source = [&] (int socket, juce::int64 t)
            {
                if (socket != 7)
                    return 0.0f;

                const double x = (double) t / b.rate;
                double v = 0.0;

                for (int k = 1; k <= 6; ++k)
                    v += (k == 1 ? c.first : 1.0) * loud[k - 1] * std::exp (-x * 0.35 * (double) k)
                            * std::sin (twoPi * hz * (double) k * std::sqrt (1.0 + c.stiff * (double) (k * k)) / std::sqrt (1.0 + c.stiff) * x + 0.7 * (double) k);

                return (float) (0.08 * v);
            };

            tuner.setOn (true);
            b.run (1.0);
            const auto r1 = tuner.reading();
            const auto t1 = b.t;
            b.run (1.0);
            const auto r2 = tuner.reading();
            const double seconds = (double) (b.t - t1) / b.rate;

            // The picture's slip, in cycles a second, against the true difference in frequency.
            const double slip = (r2.strobe - r1.strobe) / seconds, truth = hz - c.note;
            const double slipCents = 1200.0 * std::log2 ((c.note + slip) / c.note) - c.cents;
            const double off = std::abs (r2.cents - c.cents);
            allLocked = allLocked && r1.strobing && r2.strobing && r2.heard && std::abs (r2.hz - hz) < 0.01 * hz;

            if (off > worstCents)                   { worstCents = off; worstAt = c.what; }
            if (std::abs (slipCents) > worstSlip)   { worstSlip = std::abs (slipCents); slipAt = juce::String (c.what) + ": " + juce::String (slip, 4) + " against " + juce::String (truth, 4) + " cycles a second"; }
        }

        check (allLocked && worstCents < 0.1, "strobe: on a plucked string, from a bass's low B to a guitar's high E, in tune and out, the cents shown are right to a tenth of a cent (worst "
                                                  + juce::String (worstCents, 3) + ", " + worstAt + ")");
        check (allLocked && worstSlip < 0.05, "strobe: the picture stands still for a string in tune and slips by the difference in frequency when it is not (worst " + juce::String (worstSlip, 3) + " cents out, " + slipAt + ")");

        // A peg being turned: 20 cents flat up to pitch over two seconds, then left alone. The
        // figure follows it up and the picture comes to rest.
        {
            Bench b (48000.0);
            auto& tuner = b.engine.tuner();
            double phase = 0.0;
            juce::int64 lastT = -1;
            b.source = [&] (int socket, juce::int64 t)
            {
                if (socket != 7)
                    return 0.0f;

                if (t != lastT)
                {
                    const double x = (double) t / b.rate;
                    const double cents = x < 0.5 ? -20.0 : x < 2.5 ? -20.0 + 10.0 * (x - 0.5) : 0.0;
                    phase += 196.0 * std::pow (2.0, cents / 1200.0) / b.rate;
                    lastT = t;
                }

                return (float) (0.1 * std::sin (twoPi * phase) + 0.04 * std::sin (2.0 * twoPi * phase));
            };

            tuner.setOn (true);
            b.run (1.5);
            const auto mid = tuner.reading();          // at 1.5 s the string is 10 cents flat and rising 10 cents a second
            b.run (2.0);
            const auto r1 = tuner.reading();
            b.run (1.0);
            const auto r2 = tuner.reading();
            check (mid.heard && mid.name == "G3" && mid.cents < -9.0 && mid.cents > -14.0 && r2.strobing && std::abs (r2.cents) < 0.05 && std::abs (r2.strobe - r1.strobe) < 0.01,
                   "strobe: while a peg is turned the figure follows it (" + juce::String (mid.cents, 2) + " where the string was about 10 to 12 flat), and at pitch the picture rests ("
                       + juce::String (r2.cents, 3) + " cents, moved " + juce::String (r2.strobe - r1.strobe, 4) + " cycles in a second)");

            // Silence: the strobe lets go with the needle.
            b.source = [] (int, juce::int64) { return 0.0f; };
            b.run (0.8);
            check (! tuner.reading().heard && ! tuner.reading().strobing, "strobe: when the note stops the strobe lets go");
        }
    }

    // ---- a real take (not in the automatic builds: there is no recording in the repository) ----
    // GIGRIG_TUNER_TAKE=<a WAV of a guitar> plays it into the tuner and says what it read, twenty
    // times a second, in tuner-take.txt beside the WAV. For checking on a played
    // instrument what the made-up strings above cannot show.
    if (const auto takePath = juce::SystemStats::getEnvironmentVariable ("GIGRIG_TUNER_TAKE", {}); takePath.isNotEmpty())
    {
        juce::WavAudioFormat wav;
        juce::File file (takePath);
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));

        if (reader == nullptr)
        {
            check (false, "tuner take: could not read " + file.getFileName());
            return;
        }

        juce::AudioBuffer<float> take (1, (int) reader->lengthInSamples);
        reader->read (&take, 0, (int) reader->lengthInSamples, 0, true, false);
        Bench b (reader->sampleRate);
        auto& tuner = b.engine.tuner();
        const float* data = take.getReadPointer (0);
        const auto length = (juce::int64) take.getNumSamples();
        b.source = [&] (int socket, juce::int64 t) { return socket == 7 && t < length ? data[t] : 0.0f; };
        tuner.setOn (true);

        juce::String trace;
        int ticks = 0, heard = 0, locked = 0, jumps = 0;
        double lastCents = 0.0, lastStrobe = 0.0;
        int lastMidi = -1;
        bool lastLocked = false;

        while (b.t < length)
        {
            b.run (0.05);
            const auto r = tuner.reading();
            ++ticks;
            heard += r.heard ? 1 : 0;
            locked += r.strobing ? 1 : 0;

            // A jump: the same note, locked both times, and the figure moved more than 3 cents in a twentieth of a second.
            if (r.strobing && lastLocked && r.midi == lastMidi && std::abs (r.cents - lastCents) > 3.0)
                ++jumps;

            trace << juce::String ((double) b.t / b.rate, 2) << " s  " << (r.heard ? r.name.paddedRight (' ', 4) + juce::String (r.cents, 2).paddedLeft (' ', 7) + " cents  " + juce::String (r.hz, 2) + " Hz" : juce::String ("-"))
                  << (r.strobing ? "  strobe " + juce::String (r.strobe - lastStrobe, 3) + "  strength " + juce::String (r.strength, 2) : juce::String()) << "\n";
            lastCents = r.cents;
            lastMidi = r.midi;
            lastLocked = r.strobing;
            lastStrobe = r.strobe;
        }

        file.getSiblingFile ("tuner-take.txt").replaceWithText (trace);
        check (true, "tuner take: " + file.getFileName() + ", " + juce::String (ticks) + " looks, a note in " + juce::String (heard) + ", the strobe locked in " + juce::String (locked)
                         + ", " + juce::String (jumps) + " jumps of more than 3 cents");
    }
}

} // namespace gigrig
