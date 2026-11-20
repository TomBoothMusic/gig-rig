#include "AudioEngine.h"
#include "BassMachine.h"

#include <cmath>
#include <functional>
#include <random>

// Checks for the bass (the app's copy of the Bass9 PRECISION sound), run by the automatic builds.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr double rate = 96000.0;
    constexpr double twoPi = 6.283185307179586;

    /** Strength of one frequency in a signal (Goertzel). */
    double strength (const std::vector<float>& x, size_t from, double hz)
    {
        const double w = twoPi * hz / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = from; i < x.size(); ++i)
        {
            const double s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double n = (double) (x.size() - from);
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
    }

    double rms (const std::vector<float>& x, size_t from)
    {
        double sum = 0.0;
        for (size_t i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) (x.size() - from));
    }

    std::vector<float> run (const BassSettings& s, const std::vector<float>& in)
    {
        BassMachine m;
        m.prepare (rate);
        m.setSettings (s);
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); i += 64)
            m.process (in.data() + i, out.data() + i, (int) std::min<size_t> (64, in.size() - i));
        return out;
    }

    /** A plucked note: harmonics that die away, higher ones faster. */
    std::vector<float> pluck (double hz, double seconds, double level)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        for (int h = 1; h <= 20 && h * hz < 8000.0; ++h)
        {
            const double a = std::abs (std::sin (3.14159265 * h * 0.18)) / h;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / rate;
                x[i] += (float) (level * a * std::exp (-t * (1.0 + 0.6 * h)) * std::sin (twoPi * h * hz * t));
            }
        }
        return x;
    }

    /** Plain settings, so a check measures the octave itself and not the voicing. */
    BassSettings plain()
    {
        BassSettings s;
        s.compress = 0.0f;
        s.drive = 0.0f;
        s.gateDb = -90.0f;
        s.lowDb = s.lowMidDb = s.midDb = 0.0f;
        s.tone = 1.0f;
        return s;
    }
}

void runBassSelfTest (const Check& check)
{
    // ---- presets as text ----
    {
        BassSettings s;
        s.subMix = 0.3f;
        s.tone = 0.25f;
        s.levelDb = -4.5f;
        const auto back = BassSettings::fromText (s.toText());
        check (back == s, "bass: a preset written as text reads back the same");

        const auto odd = BassSettings::fromText ("# a comment\ntone = 0.4\nsubMix=7\nnonsense=3\nlevelDb=abc\n");
        check (std::abs (odd.tone - 0.4f) < 1.0e-6f && odd.subMix == 1.0f && odd.levelDb == BassSettings().levelDb,
               "bass: preset text tolerates spaces, comments, unknown names and bad numbers, and keeps values in range");
    }

    // ---- one octave down ----
    {
        std::vector<float> in ((size_t) rate);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = (float) (0.3 * std::sin (twoPi * 110.0 * (double) i / rate));

        const auto out = run (plain(), in);
        const auto from = (size_t) (0.4 * rate);
        const double sub = strength (out, from, 55.0), same = strength (out, from, 110.0), lower = strength (out, from, 27.5);
        check (sub > 10.0 * same && sub > 10.0 * lower, "bass: 110 Hz in gives 55 Hz out (55: " + juce::String (sub, 4)
                                                        + ", 110: " + juce::String (same, 4) + ", 27.5: " + juce::String (lower, 4) + ")");

        auto two = plain();
        two.subMix = 1.0f;
        const auto out2 = run (two, in);
        const double s2 = strength (out2, from, 27.5), s1 = strength (out2, from, 55.0);
        check (s2 > 5.0 * s1, "bass: sub mix at 10 gives two octaves down (27.5: " + juce::String (s2, 4) + ", 55: " + juce::String (s1, 4) + ")");

        const double ratio = rms (out, from) / rms (in, from);
        check (ratio > 0.7 && ratio < 1.4, "bass: comes out about as loud as the guitar went in (ratio " + juce::String (ratio, 2) + ")");
    }

    // ---- same level every time, whatever came before the note ----
    // Without the bands being lined up, a note's level depends on chance: up to 4 times quieter.
    for (double hz : { 82.4, 196.0, 440.0 })
    {
        std::mt19937 random (7);
        std::normal_distribution<float> hiss (0.0f, 0.001f);
        double low = 1.0e9, high = 0.0;

        for (int trial = 0; trial < 6; ++trial)
        {
            std::vector<float> in ((size_t) (0.9 * rate));
            const size_t start = (size_t) ((0.2 + 0.013 * trial) * rate);
            for (size_t i = 0; i < in.size(); ++i)
            {
                in[i] = hiss (random);
                if (i >= start)
                {
                    const double t = (double) (i - start) / rate;
                    in[i] += (float) (0.3 * std::sin (twoPi * hz * t + trial) * std::min (1.0, t / 0.005));
                }
            }

            const auto level = rms (run (plain(), in), (size_t) (0.65 * rate));
            low = std::min (low, level);
            high = std::max (high, level);
        }

        const double spreadDb = 20.0 * std::log10 (high / std::max (low, 1.0e-12));
        check (spreadDb < 1.5, "bass: a " + juce::String (hz, 1) + " Hz note comes out at the same level every time (spread "
                               + juce::String (spreadDb, 2) + " dB)");
    }

    // ---- tone knob ----
    {
        const auto in = pluck (110.0, 1.0, 0.3);
        auto bright = plain(), dark = plain();
        bright.tone = 1.0f;
        dark.tone = 0.0f;
        const auto a = run (bright, in), b = run (dark, in);
        const double highA = strength (a, 4800, 1650.0) + strength (a, 4800, 2200.0);
        const double highB = strength (b, 4800, 1650.0) + strength (b, 4800, 2200.0);
        check (highB < 0.3 * highA, "bass: tone knob down takes the top off");
    }

    // ---- gate, and nothing silly with silly input ----
    {
        std::vector<float> quiet ((size_t) (0.5 * rate));
        std::mt19937 random (3);
        std::normal_distribution<float> hiss (0.0f, 0.0002f);
        for (auto& v : quiet)
            v = hiss (random);

        const auto out = run (BassSettings(), quiet);
        check (rms (out, (size_t) (0.2 * rate)) < 1.0e-5, "bass: the gate keeps hiss from coming out as rumble");

        std::vector<float> loud ((size_t) (0.5 * rate), 0.0f);
        for (size_t i = 0; i < loud.size(); ++i)
            loud[i] = i % 200 < 100 ? 1.0f : -1.0f;
        loud[1000] = 1.0e6f;

        auto hot = BassSettings();
        hot.levelDb = 24.0f;
        hot.drive = 1.0f;
        bool finite = true;
        for (auto v : run (hot, loud))
            finite = finite && std::isfinite (v);
        check (finite, "bass: a huge input spike gives no broken numbers");
    }

    // ---- through the engine: A, B and off on the bass returns ----
    {
        AudioEngine engine;
        engine.openSimulated (rate, 64, 16, 16);
        auto& rig = engine.bass();
        rig.setSettings (plain());
        rig.setDryInput (9);
        rig.setRealInput (8);
        rig.setOutputPair (2);

        std::vector<std::vector<float>> ins (16, std::vector<float> (64, 0.0f)), outs (16, std::vector<float> (64, 0.0f));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : ins)  inPtrs.push_back (v.data());
        for (auto& v : outs) outPtrs.push_back (v.data());

        const auto outIndex = returnToOutput (2, 16);
        juce::int64 t = 0;

        const auto play = [&] (int blocks)
        {
            std::vector<float> heard, real;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 64; ++i, ++t)
                {
                    ins[9][(size_t) i] = (float) (0.3 * std::sin (twoPi * 147.0 * (double) t / rate));
                    ins[8][(size_t) i] = (float) (0.2 * std::sin (twoPi * 73.5 * (double) t / rate));
                }
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, 64);
                heard.insert (heard.end(), outs[(size_t) outIndex].begin(), outs[(size_t) outIndex].end());
                real.insert (real.end(), ins[8].begin(), ins[8].end());
            }
            return std::make_pair (heard, real);
        };

        rig.setListen (BassRig::Listen::off);
        auto [offHeard, unused] = play (200);
        check (rms (offHeard, 0) == 0.0, "bass: off means nothing on the bass returns");

        rig.setListen (BassRig::Listen::real);
        auto [realHeard, realIn] = play (200);
        double worst = 0.0;
        for (size_t i = 0; i < realHeard.size(); ++i)
            worst = std::max (worst, (double) std::abs (realHeard[i] - realIn[i]));
        check (worst < 1.0e-6, "bass: A passes the real Bass9 straight through to return 3");

        rig.setListen (BassRig::Listen::app);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        auto [appHeard, unused2] = play (1500);     // one second
        const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
        const auto from = appHeard.size() / 2;
        check (strength (appHeard, from, 73.5) > 5.0 * strength (appHeard, from, 147.0),
               "bass: B plays the app's bass, an octave below the dry guitar");
        check (ms < 600.0, "bass: one second of audio at 96 kHz took " + juce::String (ms, 0) + " ms to work out (well under a second)");

        BassRig::Levels levels = rig.takeLevels();
        check (levels.playedSeconds > 0.5 && levels.appDb > -40.0 && levels.realDb > -40.0, "bass: the level match hears both basses");
    }
}

} // namespace gigrig
