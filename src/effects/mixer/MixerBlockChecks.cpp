#include "MixerBlocks.h"

#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <random>

// The mixer blocks' own checks (the standard checks in EffectChecks.cpp run on them too).

namespace gigrig::fx
{

using namespace test;

void mixerBlockChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.3 * rate);

    /** How much louder or quieter a steady note comes out, in dB. */
    const auto gainAt = [&] (Effect& fx, double hz, double level = 0.1)
    {
        fx.reset();
        const auto x = sine (hz, 0.6, level, rate);
        return 20.0 * std::log10 (std::max (strength (run (fx, x), from, hz, rate), 1.0e-12) / strength (x, from, hz, rate));
    };

    // ---- the EQ ----
    {
        auto fx = makeEffect ("mixeq");
        fx->prepare (rate, 64);

        double worst = 0.0;
        for (double hz : { 40.0, 100.0, 1000.0, 5000.0, 15000.0 })
            worst = std::max (worst, std::abs (gainAt (*fx, hz)));
        check (worst < 0.1, "mixeq: with everything at its starting point it changes nothing (within " + num (worst, 2) + " dB from 40 Hz to 15 kHz)");

        fx->setParam ("b2g", 6.0f);
        const double at = gainAt (*fx, 800.0), low = gainAt (*fx, 80.0), high = gainAt (*fx, 8000.0);
        check (std::abs (at - 6.0) < 0.5 && std::abs (low) < 0.5 && std::abs (high) < 0.5,
               "mixeq: Band 2 up 6 dB lifts 800 Hz by " + num (at) + " dB and leaves 80 Hz (" + num (low) + ") and 8 kHz (" + num (high) + ") alone");

        fx->setParam ("b2q", 8.0f);
        const double narrow = gainAt (*fx, 500.0);
        fx->setParam ("b2q", 0.5f);
        const double wide = gainAt (*fx, 500.0);
        check (wide > narrow + 2.0, "mixeq: a band's width works (500 Hz is lifted " + num (narrow) + " dB narrow, " + num (wide) + " dB wide)");
        fx->setParam ("b2g", 0.0f);

        fx->setParam ("b2f", 2000.0f);
        fx->setParam ("b2g", -9.0f);
        fx->setParam ("b2q", 1.0f);
        const double moved = gainAt (*fx, 2000.0);
        check (std::abs (moved + 9.0) < 0.5, "mixeq: a band can be moved and cut (2 kHz down " + num (-moved) + " dB)");
        fx->setParam ("b2g", 0.0f);

        fx->setParam ("lowcut", 120.0f);
        const double rumble = gainAt (*fx, 40.0), voice = gainAt (*fx, 400.0);
        check (rumble < -12.0 && std::abs (voice) < 0.7, "mixeq: Low cut at 120 Hz takes 40 Hz down " + num (-rumble) + " dB and leaves 400 Hz alone (" + num (voice) + ")");
        fx->setParam ("lowcut", 20.0f);

        fx->setParam ("lsg", 6.0f);
        fx->setParam ("hsg", -6.0f);
        const double shelfLow = gainAt (*fx, 40.0), shelfHigh = gainAt (*fx, 15000.0), middle = gainAt (*fx, 900.0);
        check (std::abs (shelfLow - 6.0) < 1.0 && std::abs (shelfHigh + 6.0) < 1.0 && std::abs (middle) < 1.0,
               "mixeq: the shelves work (40 Hz " + num (shelfLow) + " dB, 15 kHz " + num (shelfHigh) + " dB, 900 Hz " + num (middle) + " dB)");

        // Two channels are filtered apart from each other.
        fx->setParam ("lsg", 0.0f);
        fx->setParam ("hsg", 0.0f);
        fx->setParam ("b1g", 9.0f);
        fx->reset();
        auto left = sine (250.0, 0.4, 0.1, rate);
        std::vector<float> right (left.size(), 0.0f);
        for (size_t i = 0; i + 64 <= left.size(); i += 64)
        {
            float* both[2] { left.data() + i, right.data() + i };
            fx->process (both, 2, 64);
        }
        check (rms (right) == 0.0 && rms (left, from) > 0.15, "mixeq: on two channels, a silent side stays silent while the other is boosted");
    }

    // ---- the compressor ----
    {
        auto fx = makeEffect ("mixcomp");
        fx->prepare (rate, 64);
        fx->setParam ("threshold", -20.0f);
        fx->setParam ("ratio", 4.0f);
        fx->setParam ("knee", 0.0f);

        const auto levelDb = [&] (double inDb)
        {
            fx->reset();
            const auto x = sine (196.0, 1.0, std::pow (10.0, inDb / 20.0), rate);
            return 20.0 * std::log10 (std::max (rms (run (*fx, x), (size_t) (0.5 * rate)), 1.0e-12)) - 20.0 * std::log10 (rms (x, (size_t) (0.5 * rate)));
        };

        const double quiet = levelDb (-40.0);
        check (std::abs (quiet) < 0.1 && fx->meterValue() > -0.1f, "mixcomp: a note under the threshold passes untouched (" + num (quiet, 2) + " dB)");

        const double a = levelDb (-10.0), b = levelDb (0.0);
        const double growth = 10.0 + b - a;
        check (std::abs (growth - 2.5) < 0.6 && fx->meterValue() < -12.0f,
               "mixcomp: at ratio 4, 10 dB more in gives 2.5 dB more out (" + num (growth) + " dB; the meter shows " + num (fx->meterValue()) + " dB)");

        fx->setParam ("makeup", 6.0f);
        const double made = levelDb (-40.0);
        check (std::abs (made - 6.0) < 0.1, "mixcomp: make-up gain of 6 dB lifts a quiet note by " + num (made) + " dB");
        fx->setParam ("makeup", 0.0f);

        fx->setParam ("ratio", 1.0f);
        const double one = levelDb (0.0);
        check (std::abs (one) < 0.1, "mixcomp: at ratio 1 nothing is turned down (" + num (one, 2) + " dB)");

        // Two channels are turned down together: a loud left side turns a quiet right side down too.
        fx->setParam ("ratio", 4.0f);
        fx->reset();
        auto left = sine (196.0, 0.6, 0.8, rate);
        auto right = sine (330.0, 0.6, 0.01, rate);
        const double before = rms (right, from);
        for (size_t i = 0; i + 64 <= left.size(); i += 64)
        {
            float* both[2] { left.data() + i, right.data() + i };
            fx->process (both, 2, 64);
        }
        const double linked = 20.0 * std::log10 (rms (right, from) / before);
        check (linked < -6.0, "mixcomp: on two channels both sides are turned down together (the quiet side fell " + num (-linked) + " dB)");
    }

    // ---- the notches ----
    {
        auto fx = makeEffect ("notches");
        fx->prepare (rate, 64);
        double flat = 0.0;
        for (double hz : { 125.0, 1000.0, 4000.0 })
            flat = std::max (flat, std::abs (gainAt (*fx, hz)));
        check (flat < 0.05, "notches: every notch at 0 dB changes nothing (within " + num (flat, 2) + " dB)");

        fx->setParam ("n4f", 1000.0f);
        fx->setParam ("n4g", -12.0f);
        const double at = gainAt (*fx, 1000.0), below = gainAt (*fx, 500.0), above = gainAt (*fx, 2000.0);
        check (std::abs (at + 12.0) < 0.6 && below > -1.0 && above > -1.0,
               "notches: a 12 dB notch at 1 kHz takes " + num (-at) + " dB out there and under 1 dB an octave either side (" + num (below) + ", " + num (above) + ")");

        fx->setParam ("n1f", 180.0f);
        fx->setParam ("n1g", -24.0f);
        const double boom = gainAt (*fx, 180.0);
        check (boom < -22.0 && std::abs (gainAt (*fx, 1000.0) + 12.0) < 0.8, "notches: two notches work side by side (180 Hz down " + num (-boom) + " dB)");
    }

    // ---- the limiter ----
    {
        auto fx = makeEffect ("limiter");
        fx->prepare (rate, 64);
        fx->setParam ("ceiling", -6.0f);
        fx->reset();
        const float ceiling = dbToGain (-6.0f);

        std::mt19937 random (5);
        std::uniform_real_distribution<float> u (-2.0f, 2.0f);
        std::vector<float> loud ((size_t) rate);
        for (auto& v : loud) v = u (random);
        float top = 0.0f;
        for (auto v : run (*fx, loud))
            top = std::max (top, std::abs (v));
        check (top <= ceiling * 1.0001f, "limiter: noise at twice full level never gets past a -6 dB ceiling (loudest sample " + num (gainToDb (top), 2) + " dB)");

        fx->reset();
        const auto quiet = sine (440.0, 0.3, 0.2, rate);
        const auto out = run (*fx, quiet);
        double diff = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
            diff = std::max (diff, (double) std::abs (out[i] - quiet[i]));
        check (diff < 1.0e-6 && fx->meterValue() > -0.01f, "limiter: a note under the ceiling passes exactly as it was");

        // A drum hit over the ceiling is held to it, then the limiter lets go within the release time.
        fx->setParam ("release", 100.0f);
        fx->reset();
        run (*fx, sine (100.0, 0.05, 1.0, rate));
        const float during = fx->meterValue();
        run (*fx, std::vector<float> ((size_t) (0.5 * rate), 0.0f));
        const auto after = run (*fx, quiet);
        double late = 0.0;
        for (size_t i = 0; i < after.size(); ++i)
            late = std::max (late, (double) std::abs (after[i] - quiet[i]));
        check (during < -5.0f && late < 0.002, "limiter: it turns a full-level hit down (" + num (during) + " dB) and has let go half a second later");

        check (fx->latencySamples() == 0, "limiter: adds no delay");
    }

    // ---- the line delay ----
    {
        auto fx = makeEffect ("linedelay");
        fx->prepare (rate, 64);
        const auto x = sine (440.0, 0.2, 0.3, rate);
        check (run (*fx, x) == x, "linedelay: at 0 ms the sound passes exactly as it was");

        fx->setParam ("ms", 100.0f);
        fx->reset();
        std::vector<float> click ((size_t) (0.3 * rate), 0.0f);
        click[100] = 1.0f;
        const auto out = run (*fx, click);
        size_t at = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > std::abs (out[at]))
                at = i;
        check (at == 100 + 9600 && out[at] == 1.0f && fx->latencySamples() == 9600,
               "linedelay: at 100 ms a click comes out exactly 9600 samples later (it took " + std::to_string ((long) at - 100) + ")");
    }

    // ---- the wet filter ----
    {
        auto fx = makeEffect ("wetfilter");
        fx->prepare (rate, 64);
        double flat = 0.0;
        for (double hz : { 60.0, 1000.0, 12000.0 })
            flat = std::max (flat, std::abs (gainAt (*fx, hz)));
        check (flat < 0.3, "wetfilter: with both cuts at their ends it changes nothing (within " + num (flat, 2) + " dB from 60 Hz to 12 kHz)");

        fx->setParam ("lowcut", 300.0f);
        fx->setParam ("highcut", 4000.0f);
        const double low = gainAt (*fx, 75.0), mid = gainAt (*fx, 1100.0), high = gainAt (*fx, 16000.0);
        check (low < -18.0 && high < -18.0 && std::abs (mid) < 1.0,
               "wetfilter: Low cut 300 Hz and High cut 4 kHz take 75 Hz down " + num (-low) + " dB and 16 kHz down " + num (-high) + " dB, and leave 1.1 kHz alone (" + num (mid) + ")");
    }
}

} // namespace gigrig::fx
