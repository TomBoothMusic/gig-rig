#include "EffectChecks.h"
#include "Chain.h"
#include "Oversampler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

namespace gigrig::fx
{

namespace test
{
    constexpr double twoPi = 6.283185307179586;

    std::vector<float> sine (double hz, double seconds, double level, double rate)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (level * std::sin (twoPi * hz * (double) i / rate));
        return x;
    }

    double strength (const std::vector<float>& x, size_t from, double hz, double rate)
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
        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / n;
    }

    double rms (const std::vector<float>& x, size_t from)
    {
        double sum = 0.0;
        for (size_t i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return x.size() > from ? std::sqrt (sum / (double) (x.size() - from)) : 0.0;
    }

    std::vector<float> run (Effect& fx, std::vector<float> x, int block)
    {
        for (size_t i = 0; i < x.size(); i += (size_t) block)
        {
            float* p = x.data() + i;
            fx.process (&p, 1, (int) std::min<size_t> ((size_t) block, x.size() - i));
        }
        return x;
    }

    std::string num (double v, int places)
    {
        char t[48];
        std::snprintf (t, sizeof (t), "%.*f", places, v);
        return t;
    }
}

using namespace test;

namespace
{
    bool allFinite (const std::vector<float>& x, float limit = 1.0e3f)
    {
        for (auto v : x)
            if (! std::isfinite (v) || std::abs (v) > limit)
                return false;
        return true;
    }

    float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    /** Biggest jump from one sample to the next: a click shows up as a jump far bigger than the music makes. */
    float biggestStep (const std::vector<float>& x, size_t from, size_t to)
    {
        float s = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
            s = std::max (s, std::abs (x[i] - x[i - 1]));
        return s;
    }

    std::unique_ptr<Effect> fresh (const EffectType& t, double rate, int block)
    {
        auto fx = t.make();
        fx->prepare (rate, block);
        return fx;
    }

    void standardChecks (const EffectType& t, const Check& check)
    {
        constexpr double rate = 96000.0;
        constexpr int block = 64;
        const auto name = t.id + ": ";

        // ---- the type is what the registry says ----
        {
            auto fx = t.make();
            check (fx != nullptr && fx->typeId() == t.id, name + "makes an effect whose id matches the list");

            bool unique = true, ranged = true;
            for (size_t i = 0; i < fx->params().size(); ++i)
            {
                const auto& p = fx->params()[i];
                ranged = ranged && p.min < p.max && p.def >= p.min && p.def <= p.max && ! p.id.empty();
                for (size_t j = 0; j < i; ++j)
                    unique = unique && fx->params()[j].id != p.id;
            }
            check (unique && ranged, name + "every setting has its own id, a range and a default inside it");
        }

        // ---- silence: nothing broken, and no sound out of nothing ----
        {
            auto fx = fresh (t, rate, block);
            const auto out = run (*fx, std::vector<float> ((size_t) rate, 0.0f));
            check (allFinite (out), name + "silence in gives no broken numbers");
            if (! fx->makesSoundFromSilence())
                check (peak (out, (size_t) (0.5 * rate)) < 1.0e-4f, name + "silence in gives silence out (peak " + num (gainToDb (peak (out, (size_t) (0.5 * rate)))) + " dB)");
        }

        // ---- full-scale noise and a sine sweep ----
        {
            auto fx = fresh (t, rate, block);
            std::mt19937 random (11);
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            std::vector<float> noise ((size_t) rate);
            for (auto& v : noise) v = u (random);
            check (allFinite (run (*fx, noise)), name + "full-scale noise gives no broken numbers");

            std::vector<float> sweep ((size_t) (2.0 * rate));
            double phase = 0.0;
            for (size_t i = 0; i < sweep.size(); ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) i / (double) sweep.size());
                phase += twoPi * hz / rate;
                sweep[i] = (float) (0.25 * std::sin (phase));
            }
            fx->reset();
            check (allFinite (run (*fx, sweep)), name + "a sine sweep from 20 Hz to 20 kHz gives no broken numbers");
        }

        // ---- sound in, sound out ----
        {
            auto fx = fresh (t, rate, block);
            const auto out = run (*fx, sine (220.0, 0.6, 0.1, rate));
            const auto level = rms (out, (size_t) (0.3 * rate));
            check (level > 0.003, name + "a -20 dB note in gives sound out (" + num (gainToDb ((float) level)) + " dB)");
        }

        // ---- every setting jumped between its ends: no broken numbers, no clicks ----
        {
            auto fx = fresh (t, rate, block);
            auto x = sine (220.0, 1.2, 0.1, rate);

            for (int p = 0; p < (int) fx->params().size(); ++p)
            {
                const auto info = fx->params()[(size_t) p];
                const auto saved = fx->getParam (p);

                // Steady at each end first, to know how steep the music itself is there.
                float steady = 0.0f;
                for (float v : { info.min, info.max })
                {
                    fx->setParam (p, v);
                    fx->reset();
                    const auto out = run (*fx, x);
                    steady = std::max (steady, biggestStep (out, (size_t) (0.2 * rate), out.size()));
                }

                // Then jumping every 0.1 s.
                std::vector<float> out = x;
                bool high = false;
                for (size_t i = 0; i < out.size(); i += (size_t) block)
                {
                    if (i % (size_t) (0.1 * rate) < (size_t) block)
                        fx->setParam (p, (high = ! high) ? info.max : info.min);
                    float* ptr = out.data() + i;
                    fx->process (&ptr, 1, (int) std::min<size_t> ((size_t) block, out.size() - i));
                }

                check (allFinite (out), name + "jumping '" + info.label + "' between its ends gives no broken numbers");

                if (info.kind == ParamInfo::Kind::continuous)
                {
                    const auto jump = biggestStep (out, (size_t) (0.2 * rate), out.size());
                    check (jump <= steady * 2.0f + 1.0e-4f, name + "jumping '" + info.label + "' does not click (biggest step "
                                                            + num (jump, 4) + ", music alone " + num (steady, 4) + ")");
                }

                fx->setParam (p, saved);
            }
        }

        // ---- switched on and off in a chain: no clicks ----
        {
            Chain chain;
            ChainModel m;
            m.slots.push_back ({ t.id, true, {}, {} });
            chain.setModel (m);
            chain.prepare (rate, block);
            auto x = sine (220.0, 1.6, 0.1, rate);

            const auto steadyRun = [&] (bool on)
            {
                chain.setSlotOn (0, on);
                auto out = x;
                for (size_t i = 0; i < out.size(); i += (size_t) block)
                    chain.process (out.data() + i, (int) std::min<size_t> ((size_t) block, out.size() - i));
                return biggestStep (out, (size_t) (0.3 * rate), out.size());
            };
            const float steady = std::max (steadyRun (true), steadyRun (false));

            auto out = x;
            bool on = true;
            for (size_t i = 0; i < out.size(); i += (size_t) block)
            {
                if (i % (size_t) (0.15 * rate) < (size_t) block)
                    chain.setSlotOn (0, on = ! on);
                chain.process (out.data() + i, (int) std::min<size_t> ((size_t) block, out.size() - i));
            }
            const auto jump = biggestStep (out, (size_t) (0.2 * rate), out.size());
            check (allFinite (out) && jump <= steady * 2.0f + 1.0e-4f,
                   name + "switching on and off does not click (biggest step " + num (jump, 4) + ", music alone " + num (steady, 4) + ")");
        }

        // ---- latency: an impulse comes out where the effect says ----
        {
            auto fx = fresh (t, rate, block);
            run (*fx, std::vector<float> ((size_t) (0.3 * rate), 0.0f));
            // A short, sharp burst (a pick attack), not a single sample, so a gate opens for it.
            std::vector<float> click ((size_t) (0.1 * rate), 0.0f);
            for (size_t i = 0; i < (size_t) (0.002 * rate); ++i)
                click[i] = (float) (0.05 * std::cos (twoPi * 1500.0 * (double) i / rate));
            const auto out = run (*fx, click);
            const auto top = peak (out);
            size_t onset = 0;
            while (onset < out.size() && std::abs (out[onset]) < top * 0.05f)
                ++onset;
            const int said = fx->latencySamples();
            check (top > 0.0f && (int) onset >= said - 3 && (int) onset <= said + (int) (0.005 * rate),
                   name + "says its delay is " + std::to_string (said) + " samples; a click arrived after " + std::to_string (onset));
        }

        // ---- processor time at 96 kHz / 64 samples ----
        {
            auto fx = fresh (t, rate, block);
            auto x = sine (220.0, 2.0, 0.2, rate);
            double best = 1.0e9;

            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto y = x;
                const auto start = std::chrono::steady_clock::now();
                for (size_t i = 0; i + block <= y.size(); i += block)
                {
                    float* p = y.data() + i;
                    fx->process (&p, 1, block);
                }
                const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                best = std::min (best, seconds / ((double) y.size() / rate) * 100.0);
            }

            check (best <= fx->cpuBudgetPercent(), name + "uses " + num (best, 2) + "% of each block (budget " + num (fx->cpuBudgetPercent(), 0) + "%)");
        }
    }

    void oversamplerChecks (const Check& check)
    {
        for (double base : { 48000.0, 96000.0 })
        {
            Oversampler os;
            os.prepare (base, 8192);
            const int f = os.getFactor();
            const auto tag = "oversampler at " + num (base / 1000.0, 0) + " kHz: ";

            double worstPass = 0.0, worstImage = -200.0;
            for (double hz : { 1000.0, 10000.0, 18000.0 })
            {
                auto in = sine (hz, 8192.0 / base, 1.0, base);
                in.resize (8192);
                os.clear();
                const float* u = os.up (in.data(), 8192, 0);
                std::vector<float> up (u, u + 8192 * f), back (8192);
                os.down (back.data(), 8192, 0);
                worstPass = std::max (worstPass, std::abs (20.0 * std::log10 (strength (back, 4096, hz, base))));
                worstImage = std::max (worstImage, 20.0 * std::log10 (strength (up, 4096 * (size_t) f, base - hz, base * f) + 1.0e-12));
            }

            check (worstPass < 0.1, tag + "a tone up and back down keeps its level (worst " + num (worstPass, 3) + " dB)");
            check (worstImage < -70.0, tag + "upsampling leaves no mirror tones (worst " + num (worstImage) + " dB)");
            check (os.latencySamples() >= 0 && os.latencySamples() < 16, tag + "adds " + std::to_string (os.latencySamples()) + " samples of delay");
        }
    }

    void chainChecks (const Check& check)
    {
        // Presets as text.
        ChainModel m;
        m.slots.push_back ({ "ts", true, { { "drive", 7.5f }, { "tone", 3.0f } }, {} });
        m.slots.push_back ({ "klon", false, { { "gain", 2.0f } }, { { "note", "a b=c" } } });
        const auto back = ChainModel::fromText (m.toText());
        check (back.slots.size() == 2 && back.slots[0].type == "ts" && back.slots[0].param ("drive", 0) == 7.5f
               && ! back.slots[1].on && back.slots[1].text ("note") == "a b=c",
               "chain: a preset written as text reads back the same");

        std::vector<std::string> skipped;
        const auto odd = ChainModel::fromText ("# note\r\nslot=nonsense\ndrive=3\nslot=bd\ngain=abc\ntone=2\n", &skipped);
        check (odd.slots.size() == 1 && odd.slots[0].type == "bd" && odd.slots[0].param ("tone", 0) == 2.0f
               && odd.slots[0].param ("gain", -1) == -1.0f && skipped.size() == 1,
               "chain: a preset with an unknown effect, bad numbers and Windows line ends still loads the rest");

        // Changing the row while sound plays: no clicks, settings carried across.
        constexpr double rate = 96000.0;
        Chain chain;
        ChainModel a;
        a.slots.push_back ({ "boost", true, { { "boost", 6.0f } }, {} });
        chain.setModel (a);
        chain.prepare (rate, 64);
        auto x = sine (330.0, 1.0, 0.1, rate);

        ChainModel b = a;
        b.slots.push_back ({ "bd", true, {}, {} });
        ChainModel c;
        c.slots.push_back (b.slots[1]);
        c.slots.push_back (b.slots[0]);

        float worst = 0.0f;
        std::vector<float> steadyOut = x;
        for (size_t i = 0; i < steadyOut.size(); i += 64) chain.process (steadyOut.data() + i, 64);
        const float steadyA = biggestStep (steadyOut, 9600, steadyOut.size());

        auto out = x;
        for (size_t i = 0; i < out.size(); i += 64)
        {
            if (i == 32000) chain.setModel (b);
            if (i == 64000) chain.setModel (c);
            chain.process (out.data() + i, 64);
            chain.collectGarbage();
        }
        worst = biggestStep (out, 9600, out.size());
        check (allFinite (out) && worst < std::max (steadyA, 0.01f) * 4.0f,
               "chain: adding and reordering effects while playing fades across without a click (biggest step " + num (worst, 4) + ")");
        check (chain.model().slots.size() == 2 && chain.model().slots[0].type == "bd" && chain.effect (1) != nullptr
               && std::abs (chain.effect (1)->getParam (0) - 6.0f) < 1.0e-6f,
               "chain: a reordered effect keeps its settings");

        // Latency adds up over the slots that are on.
        int expect = 0;
        for (int s = 0; s < 2; ++s) expect += chain.effect (s)->latencySamples();
        const bool sums = chain.latencySamples() == expect;
        chain.setSlotOn (0, false);
        check (sums && chain.latencySamples() == chain.effect (1)->latencySamples(), "chain: its delay is the sum of the effects that are on");
    }

    void driveCommonChecks (const std::string& id, const std::string& gainId, const std::string& levelId, const Check& check)
    {
        constexpr double rate = 96000.0;
        const auto name = id + ": ";
        const auto x = sine (196.0, 0.8, 0.1, rate);
        const size_t from = (size_t) (0.4 * rate);

        const auto harmonics = [&] (float gain)
        {
            auto fx = makeEffect (id);
            fx->setParam (gainId, gain);
            fx->prepare (rate, 64);
            const auto y = run (*fx, x);
            const double f = strength (y, from, 196.0, rate);
            double h = 0.0;
            for (int k = 2; k <= 6; ++k) h += std::pow (strength (y, from, 196.0 * k, rate), 2.0);
            return std::sqrt (h) / std::max (f, 1.0e-9);
        };

        const auto lo = harmonics (0.0f), hi = harmonics (10.0f);
        check (hi > lo * 2.0 && hi > 0.1, name + "more gain gives more drive (harmonics " + num (lo * 100.0) + "% at 0, " + num (hi * 100.0) + "% at 10)");

        if (! levelId.empty())
        {
            const auto loud = [&] (float db)
            {
                auto fx = makeEffect (id);
                fx->setParam (levelId, db);
                fx->prepare (rate, 64);
                return rms (run (*fx, x), from);
            };
            const auto diff = 20.0 * std::log10 (loud (6.0f) / loud (0.0f));
            check (std::abs (diff - 6.0) < 0.2, name + "level +6 dB is 6 dB louder (" + num (diff, 2) + ")");
        }

        // At its default settings, roughly as loud as the guitar went in, so A/B is fair from the start.
        {
            auto fx = makeEffect (id);
            fx->prepare (rate, 64);
            const auto ratioDb = 20.0 * std::log10 (rms (run (*fx, x), from) / rms (x, from));
            check (std::abs (ratioDb) < 9.0, name + "at its defaults it is within 9 dB of the dry level (" + num (ratioDb) + " dB)");
        }
    }
}

void driveChecks (const std::string& id, const Check& check)
{
    if (id == "bd")             driveCommonChecks (id, "gain", "level", check);
    else if (id == "klon")      driveCommonChecks (id, "gain", "output", check);
    else if (id == "marshall")  driveCommonChecks (id, "gain", "level", check);
    else if (id == "ts")        driveCommonChecks (id, "drive", "level", check);
    else if (id == "fuzz")      driveCommonChecks (id, "fuzz", "volume", check);
}

void gateChecks (const Check& check)
{
    constexpr double rate = 96000.0;

    // Hiss alone (-75 dB) is turned right down; a note over the same hiss comes through whole.
    std::vector<float> hiss ((size_t) rate);
    unsigned seed = 1;
    for (auto& v : hiss)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) (((seed >> 8) / 8388608.0) - 1.0) * 0.00018f;
    }

    auto fx = makeEffect ("gate");
    fx->prepare (rate, 64);
    const auto quiet = run (*fx, hiss);
    const auto inDb = 20.0 * std::log10 (rms (hiss, (size_t) (0.5 * rate)));
    const auto outDb = 20.0 * std::log10 (std::max (rms (quiet, (size_t) (0.5 * rate)), 1.0e-12));
    check (outDb < inDb - 40.0 && fx->meterValue() < -40.0f,
           "gate: hiss on its own is turned down (" + num (inDb) + " dB in, " + num (outDb) + " dB out, meter " + num (fx->meterValue()) + " dB)");

    auto note = sine (196.0, 1.0, 0.05, rate);
    for (size_t i = 0; i < note.size(); ++i) note[i] += hiss[i % hiss.size()];
    fx->reset();
    const auto played = run (*fx, note);
    const auto lossDb = 20.0 * std::log10 (rms (played, (size_t) (0.1 * rate)) / rms (note, (size_t) (0.1 * rate)));
    check (std::abs (lossDb) < 0.2 && fx->meterValue() > -0.5f, "gate: a played note goes through untouched (" + num (lossDb, 2) + " dB)");

    // A note that stops: the gate closes after it, but not before its tail has died away.
    {
        auto x = sine (196.0, 1.0, 0.05, rate);
        for (size_t i = (size_t) (0.5 * rate); i < x.size(); ++i)
            x[i] *= (float) std::exp (-(double) (i - 0.5 * rate) / (0.05 * rate));   // dies away over a few hundred ms
        fx->reset();
        const auto y = run (*fx, x);
        double tailIn = 0.0, tailOut = 0.0;
        for (size_t i = (size_t) (0.55 * rate); i < (size_t) (0.65 * rate); ++i) { tailIn += x[i] * x[i]; tailOut += y[i] * y[i]; }
        check (std::abs (10.0 * std::log10 (tailOut / tailIn)) < 1.0 && fx->meterValue() < -60.0f,
               "gate: a note's tail rings on, then the gate closes (" + num (10.0 * std::log10 (tailOut / tailIn), 2) + " dB on the tail, closed to " + num (fx->meterValue()) + " dB)");
    }
}

void compChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.5 * rate);

    const auto levelDb = [&] (Effect& fx, double inDb)
    {
        fx.reset();
        const auto x = sine (196.0, 1.0, std::pow (10.0, inDb / 20.0), rate);
        return 20.0 * std::log10 (std::max (rms (run (fx, x), from), 1.0e-12)) - 20.0 * std::log10 (rms (x, from));
    };

    auto fx = makeEffect ("comp");
    fx->prepare (rate, 64);

    // Well under the threshold: passes at the same level.
    const double quiet = levelDb (*fx, -50.0);
    check (std::abs (quiet) < 0.5 && fx->meterValue() > -0.5f, "comp: a quiet note passes at its own level (" + num (quiet, 2) + " dB)");

    // Ratio 4: a note 10 dB louder comes out only about 2.5 dB louder.
    const double a = levelDb (*fx, -10.0), b = levelDb (*fx, 0.0);
    const double growth = 10.0 + b - a;                 // how much louder the output got
    check (growth > 1.5 && growth < 4.5 && fx->meterValue() < -10.0f,
           "comp: at ratio 4, 10 dB more in gives about 2.5 dB more out (" + num (growth, 1) + " dB, meter " + num (fx->meterValue()) + " dB)");

    // A higher ratio squeezes harder.
    fx->setParam ("ratio", 3.0f);
    const double a20 = levelDb (*fx, -10.0), b20 = levelDb (*fx, 0.0);
    const double growth20 = 10.0 + b20 - a20;
    check (growth20 < growth - 1.0, "comp: ratio 20 squeezes harder than ratio 4 (" + num (growth20, 1) + " dB more out for 10 dB more in)");
    fx->setParam ("ratio", 0.0f);

    // Mix at 0% is the dry guitar.
    fx->setParam ("mix", 0.0f);
    const double dry = levelDb (*fx, 0.0);
    check (std::abs (dry) < 0.2, "comp: Mix at 0% gives the dry guitar (" + num (dry, 2) + " dB)");
    fx->setParam ("mix", 100.0f);

    // Attack 7 grabs a sudden loud note much sooner than attack 1.
    const auto grabbedAfter = [&] (float attack)
    {
        fx->setParam ("attack", attack);
        fx->reset();
        run (*fx, std::vector<float> ((size_t) (0.2 * rate), 0.0f));
        auto x = sine (1000.0, 0.0005, 0.9, rate);       // half a millisecond of a loud note
        run (*fx, x);
        return fx->meterValue();
    };
    const float fast = grabbedAfter (7.0f), slow = grabbedAfter (1.0f);
    check (fast < slow - 3.0f, "comp: attack 7 grabs a loud note sooner than attack 1 (after 0.5 ms: " + num (fast) + " dB against " + num (slow) + " dB)");
    fx->setParam ("attack", 3.0f);

    // Release 7 lets go much sooner than release 1.
    const auto heldAfter = [&] (float release)
    {
        fx->setParam ("release", release);
        fx->reset();
        run (*fx, sine (196.0, 0.5, 0.9, rate));
        run (*fx, std::vector<float> ((size_t) (0.15 * rate), 0.0f));
        return fx->meterValue();
    };
    const float quick = heldAfter (7.0f), long_ = heldAfter (1.0f);
    check (quick > long_ + 3.0f, "comp: release 7 lets go sooner than release 1 (150 ms after: " + num (quick) + " dB against " + num (long_) + " dB)");
}

void eqChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.3 * rate);

    const auto gainAt = [&] (Effect& fx, double hz)
    {
        fx.reset();
        const auto x = sine (hz, 0.6, 0.1, rate);
        return 20.0 * std::log10 (rms (run (fx, x), from) / rms (x, from));
    };

    auto fx = makeEffect ("eq");
    fx->prepare (rate, 64);

    double worst = 0.0;
    for (double hz : { 60.0, 150.0, 500.0, 1000.0, 3000.0, 8000.0 })
        worst = std::max (worst, std::abs (gainAt (*fx, hz)));
    check (worst < 0.05, "eq: all sliders at 0 dB leaves the guitar unchanged (within " + num (worst, 3) + " dB)");

    fx->setParam ("b100", -15.0f);
    const double low = gainAt (*fx, 100.0), mid = gainAt (*fx, 1000.0);
    check (low < -13.0 && std::abs (mid) < 0.5, "eq: 100 Hz down 15 dB cuts the lows (" + num (low) + " dB at 100 Hz, " + num (mid, 2) + " dB at 1 kHz)");
    fx->setParam ("b100", 0.0f);

    fx->setParam ("b6k4", 15.0f);
    const double top = gainAt (*fx, 6400.0);
    check (top > 13.0 && top < 16.0, "eq: 6.4 kHz up 15 dB lifts the top (" + num (top) + " dB)");
    fx->setParam ("b6k4", 0.0f);

    fx->setParam ("level", -6.0f);
    const double lv = gainAt (*fx, 1000.0);
    check (std::abs (lv + 6.0) < 0.2, "eq: Level -6 dB turns it down 6 dB (" + num (lv, 2) + " dB)");
}

void ampChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.4 * rate);

    // A new amp starts with the default model (the self-test sets one up before these run).
    {
        auto fx = makeEffect ("amp");
        check (fx->status().rfind ("playing", 0) == 0, "amp: a new amp starts with a model (" + fx->status() + ")");
    }

    // A real model changes the sound (it is not a straight wire) and keeps it in proportion.
    {
        auto fx = makeEffect ("amp");
        fx->prepare (rate, 64);
        const auto x = sine (196.0, 0.8, 0.1, rate);
        const auto y = run (*fx, x);
        double h = 0.0;
        for (int k = 2; k <= 6; ++k) h += std::pow (strength (y, from, 196.0 * k, rate), 2.0);
        const double f = strength (y, from, 196.0, rate);
        const auto gainDb = 20.0 * std::log10 (rms (y, from) / rms (x, from));
        check (allFinite (y) && f > 1.0e-4 && std::abs (gainDb) < 40.0,
               "amp: a guitar note comes out shaped by the model (" + num (gainDb) + " dB, harmonics " + num (100.0 * std::sqrt (h) / std::max (f, 1.0e-9)) + "%)");
    }

    // Changing model while playing fades across without a click.
    {
        auto fx = makeEffect ("amp");
        fx->prepare (rate, 64);
        const auto x = sine (196.0, 1.2, 0.1, rate);
        auto steady = run (*fx, x);
        const float steadyStep = biggestStep (steady, from, steady.size());

        auto y = x;
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i == (size_t) (0.6 * rate))
                fx->setText ("model", "Dream 65 - Clean");
            float* c[1] { y.data() + i };
            fx->process (c, 1, (int) std::min<size_t> (64, y.size() - i));
        }
        const float step = biggestStep (y, from, y.size());
        check (allFinite (y) && step < std::max (steadyStep, 0.01f) * 2.5f && fx->status().find ("Clean") != std::string::npos,
               "amp: changing model while playing fades across without a click (biggest step " + num (step, 4) + ", steady " + num (steadyStep, 4) + ")");
    }

    // A model that isn't there says so, and the amp keeps playing the one it had.
    {
        auto fx = makeEffect ("amp");
        fx->prepare (rate, 64);
        fx->setText ("model", "No such amp");
        const auto y = run (*fx, sine (196.0, 0.6, 0.1, rate));
        check (fx->status().find ("can't find") != std::string::npos && rms (y, from) > 1.0e-4,
               "amp: a missing model is reported and the amp keeps its sound (" + fx->status() + ")");
    }
}

void fuzzChecks (const Check& check)
{
    driveChecks ("fuzz", check);

    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.4 * rate);

    // Full fuzz squares a note off: the loudest peak is close to the average level
    // (a sine is 3 dB apart, a square 0 dB).
    {
        auto fx = makeEffect ("fuzz");
        fx->setParam ("fuzz", 10.0f);
        fx->prepare (rate, 64);
        const auto y = run (*fx, sine (196.0, 0.8, 0.1, rate));
        const auto crestDb = 20.0 * std::log10 (peak (y, from) / rms (y, from));
        check (crestDb < 6.0, "fuzz: full fuzz squares the note off (peak " + num (crestDb) + " dB over average)");
    }

    // Low bias gates a quiet note tail far more than a healthy bias does.
    {
        const auto quiet = [&] (float bias)
        {
            auto fx = makeEffect ("fuzz");
            fx->setParam ("bias", bias);
            fx->prepare (rate, 64);
            return rms (run (*fx, sine (196.0, 0.8, 0.002, rate)), from);
        };
        const auto healthy = quiet (5.0f), starved = quiet (0.0f);
        check (starved < healthy * 0.5, "fuzz: low bias gates a quiet note (" + num (20.0 * std::log10 (starved / std::max (healthy, 1.0e-12)) + 0.0) + " dB against bias 5)");
    }
}

void tubeScreamerChecks (const Check& check)
{
    driveChecks ("ts", check);

    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.3 * rate);

    // The mid hump: at low drive, 100 Hz comes through much weaker than 1 kHz (relative to the input).
    {
        const auto through = [&] (double hz)
        {
            auto fx = makeEffect ("ts");
            fx->setParam ("drive", 0.0f);
            fx->setParam ("tone", 5.0f);
            fx->prepare (rate, 64);
            return strength (run (*fx, sine (hz, 0.6, 0.02, rate)), from, hz, rate) / 0.02;
        };
        const auto humpDb = 20.0 * std::log10 (through (1000.0) / through (100.0));
        check (humpDb > 4.0, "ts: the mid hump: 1 kHz comes through " + num (humpDb) + " dB stronger than 100 Hz");
    }

    // The tone knob.
    {
        const auto at = [&] (float tone)
        {
            auto fx = makeEffect ("ts");
            fx->setParam ("drive", 0.0f);
            fx->setParam ("tone", tone);
            fx->prepare (rate, 64);
            return strength (run (*fx, sine (4000.0, 0.6, 0.01, rate)), from, 4000.0, rate);
        };
        const auto spanDb = 20.0 * std::log10 (at (10.0f) / at (0.0f));
        check (spanDb > 10.0, "ts: the tone knob changes 4 kHz by " + num (spanDb) + " dB end to end");
    }
}

void runEffectChecks (const Check& check)
{
    oversamplerChecks (check);
    chainChecks (check);

    for (const auto& t : effectTypes())
    {
        standardChecks (t, check);

        if (t.extraChecks)
            t.extraChecks (check);
    }
}

} // namespace gigrig::fx
