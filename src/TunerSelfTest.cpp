#include "AudioEngine.h"

#include <cmath>
#include <functional>
#include <random>

// Checks for the tuner (27 Nov 2026), run by the automatic builds (GigRig --self-test): that it
// reads notes from a low bass string to a high voice within a cent, that a string whose first
// overtone is louder than the note itself is not read an octave out, that hiss and silence
// show no note, that it finds the input being played, that with the mixer on the main mix
// (and only the main mix) goes silent while it is on, and that with the mixer off it changes
// no sound at all.

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
                   "tuner: with the mixer on, the main mix fades silent while it is on and comes back exactly, with no click (" + juce::String (before, 4) + ", " + juce::String (during, 6) + ", " + juce::String (after, 4) + ")");
            check (reads && monBefore > 0.09f && std::abs (monDuring - monBefore) < 1.0e-5f, "tuner: the monitors stay live while tuning, and the note is still read");

            tuner.setMutes (false);
            tuner.setOn (true);
            b.run (0.3);
            check (std::abs (b.peakOfLast (4, 0.1) - before) < 1.0e-5f, "tuner: with 'silence the main mix' unticked, the main mix carries on");
            tuner.setOn (false);
            tuner.setMutes (true);

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
        Tuner a, b;
        a.setSocket (3);
        a.setReference (442.0);
        a.setMutes (false);
        b.loadStateText (a.stateText());
        a.setReference (900.0);
        check (b.socket() == 3 && std::abs (b.reference() - 442.0) < 0.01 && ! b.mutes() && std::abs (a.reference() - Tuner::maxReference) < 0.01,
               "tuner: the input, the A and the silence choice are remembered, and a silly A is held to 430 to 450");
    }
}

} // namespace gigrig
