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

        // Adding a preset to what is already there (presets stacked, not replaced).
        {
            ChainModel row;
            row.slots.push_back ({ "comp", true, {}, {} });
            row.slots.push_back ({ "amp", true, {}, {} });
            ChainModel drives;
            drives.slots.push_back ({ "klon", true, {}, {} });
            drives.slots.push_back ({ "bd", true, { { "gain", 5.2f } }, {} });
            const int added = row.addFrom (drives, Chain::maxSlots);
            check (added == 2 && row.slots.size() == 4 && row.slots[0].type == "comp" && row.slots[1].type == "klon"
                   && row.slots[2].type == "bd" && row.slots[2].param ("gain", 0) == 5.2f && row.slots[3].type == "amp",
                   "chain: adding a preset keeps what is there and puts the new effects in front of the amp");

            ChainModel full;
            for (int k = 0; k < Chain::maxSlots - 1; ++k) full.slots.push_back ({ "boost", true, {}, {} });
            check (full.addFrom (drives, Chain::maxSlots) == 1 && (int) full.slots.size() == Chain::maxSlots,
                   "chain: adding a preset stops when the chain is full");
        }

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

namespace
{
    /** Where the loudest moment is between two times (seconds), and how loud. */
    std::pair<double, float> loudestBetween (const std::vector<float>& y, double from, double to, double rate)
    {
        size_t at = (size_t) (from * rate);
        float top = 0.0f;
        for (size_t i = (size_t) (from * rate); i < std::min (y.size(), (size_t) (to * rate)); ++i)
            if (std::abs (y[i]) > top) { top = std::abs (y[i]); at = i; }
        return { (double) at / rate, top };
    }

    std::vector<float> pluck (double rate, double seconds)
    {
        std::vector<float> x ((size_t) (seconds * rate), 0.0f);
        for (size_t i = 0; i < (size_t) (0.002 * rate); ++i)
            x[i] = (float) (0.3 * std::sin (twoPi * 1000.0 * (double) i / rate));
        return x;
    }
}

void delayChecks (const std::string& id, const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect (id);
    fx->prepare (rate, 64);
    fx->setParam ("mix", 100.0f);
    if (fx->paramIndex ("wow") >= 0) { fx->setParam ("wow", 0.0f); fx->setParam ("flutter", 0.0f); }
    if (fx->paramIndex ("mod") >= 0) fx->setParam ("mod", 0.0f);
    if (fx->paramIndex ("follow") >= 0) fx->setParam ("follow", 0.0f);
    fx->setParam ("time", 250.0f);
    fx->setParam ("feedback", 40.0f);
    fx->reset();

    const auto y = run (*fx, pluck (rate, 1.0));
    const auto first = loudestBetween (y, 0.1, 0.4, rate);
    const auto second = loudestBetween (y, 0.4, 0.65, rate);
    check (std::abs (first.first - 0.250) < 0.004 && first.second > 0.05f,
           id + ": with Time at 250 ms the echo comes back after " + num (first.first * 1000.0, 1) + " ms");
    check (std::abs (second.first - 0.500) < 0.008 && second.second < first.second && second.second > first.second * 0.15f,
           id + ": the second repeat comes 250 ms later and quieter (" + num (gainToDb (second.second / first.second)) + " dB)");

    if (fx->paramIndex ("follow") >= 0)
    {
        fx->setParam ("follow", 1.0f);
        fx->setParam ("division", 0.0f);     // 1/4
        fx->setTempo (120.0);
        fx->reset();
        const auto z = run (*fx, pluck (rate, 0.8));
        const auto e = loudestBetween (z, 0.1, 0.7, rate);
        check (std::abs (e.first - 0.500) < 0.006, id + ": following a 120 bpm tempo, a 1/4 note echo comes back after " + num (e.first * 1000.0, 1) + " ms (500)");

        fx->setParam ("division", 1.0f);     // dotted 1/8
        fx->reset();
        const auto d = run (*fx, pluck (rate, 0.8));
        const auto e2 = loudestBetween (d, 0.1, 0.7, rate);
        check (std::abs (e2.first - 0.375) < 0.006, id + ": a dotted 1/8 at 120 bpm comes back after " + num (e2.first * 1000.0, 1) + " ms (375)");

        fx->setTempo (0.0);
        fx->setParam ("division", 0.0f);
        fx->reset();
        const auto u = run (*fx, pluck (rate, 0.8));
        const auto e3 = loudestBetween (u, 0.1, 0.7, rate);
        check (std::abs (e3.first - 0.250) < 0.006, id + ": with no tempo known yet it uses its own Time (" + num (e3.first * 1000.0, 1) + " ms)");
    }

    // Repeats at the top setting die away on their own rather than running away.
    fx->setParam ("feedback", 95.0f);
    if (fx->paramIndex ("follow") >= 0) fx->setParam ("follow", 0.0f);
    fx->reset();
    auto loud = sine (220.0, 0.5, 0.9, rate);
    loud.resize ((size_t) (8.0 * rate), 0.0f);
    const auto r = run (*fx, loud);
    check (allFinite (r) && peak (r) < 4.0f, id + ": Repeats at full with a loud note stays under control (peak " + num (gainToDb (peak (r))) + " dB)");

    // The throw: clean while it waits, catches while the switch is held, and what it caught
    // rings out after the switch is let go while nothing new goes in.
    if (fx->paramIndex ("throw") >= 0)
    {
        fx->setParam ("feedback", 50.0f);
        fx->setParam ("time", 250.0f);
        fx->setParam ("throw", 1.0f);
        fx->reset();
        const auto word = pluck (rate, 1.0);
        const auto clean = run (*fx, word);
        float changed = 0.0f;
        for (size_t i = 0; i < word.size(); ++i) changed = std::max (changed, std::abs (clean[i] - word[i]));
        check (changed < 1.0e-6f, id + ": throw: while it waits for the switch the sound is clean, with no echo at all");

        fx->reset();
        fx->setParam ("throw", 2.0f);                         // the switch goes down on the first word
        auto y = run (*fx, pluck (rate, 0.06));
        fx->setParam ("throw", 1.0f);                         // and is let go
        const auto quiet = run (*fx, std::vector<float> ((size_t) (0.56 * rate), 0.0f));
        const auto later = run (*fx, pluck (rate, 0.6));      // a second word at 0.62 s, switch up
        y.insert (y.end(), quiet.begin(), quiet.end());
        y.insert (y.end(), later.begin(), later.end());

        const auto caught = loudestBetween (y, 0.2, 0.3, rate);
        const auto rings = loudestBetween (y, 0.7, 0.8, rate);
        const auto stray = loudestBetween (y, 0.84, 0.96, rate);     // the second word's echo would land at 0.87 s
        check (std::abs (caught.first - 0.250) < 0.004 && caught.second > 0.05f,
               id + ": throw: a word sung while the switch is held comes back as an echo (" + num (caught.first * 1000.0, 1) + " ms later)");
        check (std::abs (rings.first - 0.750) < 0.006 && rings.second > 0.01f,
               id + ": throw: after the switch is let go the repeats ring out on their own (third repeat " + num (gainToDb (rings.second)) + " dB)");
        check (stray.second < 0.003f,
               id + ": throw: a word sung after the switch is let go is not caught (" + num (gainToDb (std::max (stray.second, 1.0e-9f))) + " dB where its echo would be)");

        fx->setParam ("throw", 0.0f);
    }
}

void springChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect ("spring");
    fx->prepare (rate, 64);
    fx->setParam ("reverb", 10.0f);
    fx->reset();
    const auto y = run (*fx, pluck (rate, 6.0));
    const double early = rms (std::vector<float> (y.begin() + (long) (0.1 * rate), y.begin() + (long) (0.6 * rate)));
    const double late = rms (std::vector<float> (y.begin() + (long) (4.5 * rate), y.begin() + (long) (5.5 * rate)));
    check (early > 1.0e-3 && late < early * 0.05, "spring: a pluck rings on in the springs, then dies away (" + num (gainToDb ((float) early)) + " dB early, "
                                                  + num (gainToDb ((float) std::max (late, 1.0e-9))) + " dB after 4.5 s)");

    fx->setParam ("decay", 10.0f);
    fx->reset();
    const auto longer = run (*fx, pluck (rate, 3.0));
    const double lateLong = rms (std::vector<float> (longer.begin() + (long) (2.0 * rate), longer.end()));
    fx->setParam ("decay", 0.0f);
    fx->reset();
    const auto shorter = run (*fx, pluck (rate, 3.0));
    const double lateShort = rms (std::vector<float> (shorter.begin() + (long) (2.0 * rate), shorter.end()));
    check (lateLong > lateShort * 4.0, "spring: Decay at 10 rings much longer than at 0 (" + num (gainToDb ((float) (lateLong / std::max (lateShort, 1.0e-12)))) + " dB more after 2 s)");

    fx->setParam ("reverb", 0.0f);
    fx->reset();
    const auto x = sine (196.0, 0.6, 0.1, rate);
    const auto dry = run (*fx, x);
    double diff = 0.0;
    for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, (double) std::abs (dry[i] - x[i]));
    check (diff < 1.0e-6, "spring: Reverb at 0 leaves the guitar as it was");
}

void tremoloChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect ("tremolo");
    fx->prepare (rate, 64);
    fx->setParam ("speed", 4.0f);
    fx->setParam ("intensity", 50.0f);
    fx->reset();
    const auto y = run (*fx, sine (440.0, 2.0, 0.1, rate));

    // Level in 5 ms windows: the quietest should be about half the loudest, and it should
    // pulse 4 times a second.
    std::vector<double> env;
    const size_t w = (size_t) (0.005 * rate);
    for (size_t i = (size_t) (0.5 * rate); i + w <= y.size(); i += w)
        env.push_back (rms (std::vector<float> (y.begin() + (long) i, y.begin() + (long) (i + w))));
    const double top = *std::max_element (env.begin(), env.end()), bottom = *std::min_element (env.begin(), env.end());
    // Count the times the level falls through the middle.
    int dips = 0;
    const double middle = (top + bottom) * 0.5;
    for (size_t k = 1; k < env.size(); ++k)
        if (env[k - 1] >= middle && env[k] < middle)
            ++dips;
    check (bottom / top > 0.4 && bottom / top < 0.6 && dips >= 5 && dips <= 7,
           "tremolo: at 50% the level dips to " + num (bottom / top * 100.0, 0) + "% and pulses " + std::to_string (dips) + " times in 1.5 s at 4 Hz");
}

void modulationChecks (const std::string& id, const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect (id);
    fx->prepare (rate, 64);
    const auto x = sine (330.0, 1.0, 0.1, rate);

    if (id == "chorus")
    {
        fx->setParam ("mix", 0.0f);
        fx->reset();
        const auto y = run (*fx, x);
        double diff = 0.0;
        for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, (double) std::abs (y[i] - x[i]));
        check (diff < 1.0e-6, "chorus: Mix at 0 leaves the guitar as it was");
    }
    else
    {
        fx->setParam ("depth", 0.0f);
        fx->reset();
        const auto y = run (*fx, x);
        const int lag = fx->latencySamples();
        double diff = 0.0;
        for (size_t i = (size_t) (0.3 * rate); i < x.size(); ++i) diff = std::max (diff, (double) std::abs (y[i] - x[i - (size_t) lag]));
        check (diff < 1.0e-3, "vibrato: at depth 0 it is the guitar itself, " + std::to_string (lag) + " samples late (as it says)");
    }

    // With depth up, the sound really moves: compare against the plain copy.
    fx->setParam (id == "chorus" ? "mix" : "depth", 100.0f);
    if (id == "chorus") fx->setParam ("depth", 100.0f);
    fx->reset();
    const auto moved = run (*fx, x);
    const int lag = fx->latencySamples();
    double diff = 0.0;
    for (size_t i = (size_t) (0.3 * rate); i < x.size(); ++i) diff = std::max (diff, (double) std::abs (moved[i] - x[i - (size_t) lag]));
    check (diff > 0.01, id + ": with depth up the sound moves (differs by up to " + num (diff, 3) + ")");
}

void whammyChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.4 * rate);
    auto fx = makeEffect ("whammy");
    fx->prepare (rate, 64);
    const auto modeCount = (int) fx->params()[(size_t) fx->paramIndex ("mode")].choices.size();
    check (modeCount == 21, "whammy: has its 21 modes (10 whammy, 9 harmony, 2 detune)");

    // Heel down in a Whammy mode: the guitar itself, to the sample.
    {
        const auto x = sine (196.0, 0.6, 0.1, rate);
        fx->reset();
        const auto y = run (*fx, x);
        double diff = 0.0;
        for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, (double) std::abs (y[i] - x[i]));
        check (diff < 1.0e-6, "whammy: with the pedal at the heel the guitar passes untouched and undelayed");
    }

    // Toe down: the note lands on the interval the mode names, and stays a clean note.
    // A guitar-like note (a fundamental and a few harmonics), low E to the 12th fret.
    const auto note = [&] (double hz)
    {
        std::vector<float> x ((size_t) (1.0 * rate));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = twoPi * hz * (double) i / rate;
            x[i] = (float) (0.1 * (std::sin (t) + 0.5 * std::sin (2.0 * t + 0.4) + 0.3 * std::sin (3.0 * t + 1.1) + 0.15 * std::sin (5.0 * t)));
        }
        return x;
    };

    struct Case { int mode; double semis; const char* name; };
    for (const auto& c : { Case { 0, 12.0, "1 octave up" }, Case { 1, 24.0, "2 octaves up" }, Case { 2, 7.0, "5th up" },
                           Case { 5, -5.0, "4th down" }, Case { 7, -12.0, "1 octave down" }, Case { 8, -24.0, "2 octaves down" } })
    {
        double worstLevel = 1.0e9, worstStray = 0.0;

        for (double hz : { 82.41, 196.0, 329.63, 659.26 })
        {
            const auto x = note (hz);
            fx->setParam ("mode", (float) c.mode);
            fx->setParam ("pedal", 100.0f);
            fx->reset();
            const auto y = run (*fx, x);
            const double want = hz * std::pow (2.0, c.semis / 12.0);

            // The four partials at their new pitches should hold nearly all the sound.
            double tones = 0.0;
            for (double k : { 1.0, 2.0, 3.0, 5.0 })
                tones += 0.5 * std::pow (strength (y, from, want * k, rate), 2.0);
            const double total = std::pow (rms (y, from), 2.0);
            worstLevel = std::min (worstLevel, strength (y, from, want, rate) / 0.1);
            worstStray = std::max (worstStray, std::max (0.0, total - tones) / std::max (total, 1.0e-12));
        }

        check (worstLevel > 0.8 && worstLevel < 1.2 && worstStray < 0.05,
               std::string ("whammy: ") + c.name + " puts notes from low E to high E on pitch (level " + num (worstLevel * 100.0, 0)
               + "% at worst, stray sound " + num (worstStray * 100.0, 1) + "% at worst)");
    }

    // Halfway down the pedal in the octave-up mode is half the interval (6 semitones).
    {
        fx->setParam ("mode", 0.0f);
        fx->setParam ("pedal", 50.0f);
        fx->reset();
        const auto y = run (*fx, sine (220.0, 1.0, 0.1, rate));
        const double got = strength (y, from, 220.0 * std::pow (2.0, 0.5), rate);
        check (got > 0.08, "whammy: the pedal halfway gives half the bend (" + num (got * 1000.0, 0) + "% of the note at 6 semitones up)");
    }

    // Harmony: the dry guitar and the added voice together.
    {
        fx->setParam ("mode", 10.0f);       // octave down (heel) to octave up (toe)
        fx->setParam ("pedal", 0.0f);
        fx->reset();
        auto y = run (*fx, sine (220.0, 1.0, 0.1, rate));
        const double dryHeel = strength (y, from, 220.0, rate), voiceHeel = strength (y, from, 110.0, rate);
        fx->setParam ("pedal", 100.0f);
        fx->reset();
        y = run (*fx, sine (220.0, 1.0, 0.1, rate));
        const double voiceToe = strength (y, from, 440.0, rate);
        check (dryHeel > 0.06 && voiceHeel > 0.05 && voiceToe > 0.05,
               "whammy: harmony mode plays the guitar with a voice an octave below at the heel and an octave above at the toe");
    }

    // Detune: nothing added at the heel, a slightly sharp copy at the toe.
    {
        const auto x = sine (220.0, 1.0, 0.1, rate);
        fx->setParam ("mode", 20.0f);
        fx->setParam ("pedal", 0.0f);
        fx->reset();
        auto y = run (*fx, x);
        double diff = 0.0;
        for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, (double) std::abs (y[i] - x[i]));
        fx->setParam ("pedal", 100.0f);
        fx->reset();
        y = run (*fx, x);
        double moved = 0.0;
        for (size_t i = from; i < x.size(); ++i) moved = std::max (moved, (double) std::abs (y[i] - x[i]));
        check (diff < 1.0e-6 && moved > 0.01, "whammy: detune adds nothing at the heel and a thickening copy at the toe");
    }

    // Bend time: a jump of the pedal takes that long to arrive, then lands exactly.
    {
        fx->setParam ("mode", 0.0f);
        fx->setParam ("pedal", 0.0f);
        fx->setParam ("glide", 500.0f);
        fx->reset();
        auto x = sine (220.0, 1.5, 0.1, rate);
        const size_t jumpAt = (size_t) (0.2 * rate);
        for (size_t i = 0; i < x.size(); i += 64)
        {
            if (i >= jumpAt) fx->setParam ("pedal", 100.0f);
            float* p = x.data() + i;
            fx->process (&p, 1, (int) std::min<size_t> (64, x.size() - i));
        }
        const std::vector<float> early (x.begin() + (long) (0.3 * rate), x.begin() + (long) (0.5 * rate));
        const std::vector<float> late (x.begin() + (long) (1.0 * rate), x.end());
        check (strength (early, 0, 440.0, rate) < 0.02 && strength (late, 0, 440.0, rate) > 0.08,
               "whammy: with Bend time at half a second the bend is still on its way after 0.2 s and has arrived after 0.8 s");
        fx->setParam ("glide", 60.0f);
    }
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

namespace
{
    std::vector<float> hiss (double seconds, double level, double rate, unsigned seed = 1)
    {
        std::mt19937 gen (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> x ((size_t) (seconds * rate));
        for (auto& v : x) v = (float) level * dist (gen);
        return x;
    }

    /** How long a tail takes to fall 60 dB, from its slope between 5 and 35 dB down (the way rooms are measured). */
    double ringSeconds (const std::vector<float>& tail, double rate)
    {
        std::vector<double> left (tail.size() + 1, 0.0);
        for (size_t i = tail.size(); i-- > 0;)
            left[i] = left[i + 1] + (double) tail[i] * tail[i];

        double at5 = -1.0;
        for (size_t i = 0; i < tail.size() && left[0] > 0.0; ++i)
        {
            const double down = 10.0 * std::log10 (std::max (left[i] / left[0], 1.0e-30));
            if (at5 < 0.0 && down <= -5.0)  at5 = (double) i / rate;
            if (down <= -35.0)              return 2.0 * ((double) i / rate - at5);
        }
        return -1.0;
    }

    double biggestDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        double diff = 0.0;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) diff = std::max (diff, (double) std::abs (a[i] - b[i]));
        return diff;
    }

    double toneGainDb (Effect& fx, double hz, double level, double rate)
    {
        fx.reset();
        const auto y = run (fx, sine (hz, 0.8, level, rate));
        return 20.0 * std::log10 (std::max (strength (y, (size_t) (0.4 * rate), hz, rate), 1.0e-12) / level);
    }
}

void reverbChecks (const std::string& id, const Check& check)
{
    constexpr double rate = 96000.0;
    const bool room = id == "room";
    auto fx = makeEffect (id);
    fx->prepare (rate, 64);

    // A burst of hiss, then silence: how long does the tail ring?
    const auto ring = [&] (float decay, int size)
    {
        fx->setParam ("reverb", 10.0f);
        fx->setParam ("decay", decay);
        if (room) fx->setParam ("size", (float) size);
        fx->reset();
        auto x = hiss (0.3, 0.1, rate);
        x.resize ((size_t) (5.0 * rate), 0.0f);
        const auto y = run (*fx, x);
        return ringSeconds (std::vector<float> (y.begin() + (long) (0.33 * rate), y.end()), rate);
    };

    const double middle = ring (5.0f, 1), shortest = ring (0.0f, 1);
    const double wanted = room ? 0.81 : 1.79;
    check (middle > wanted * 0.7 && middle < wanted * 1.3,
           id + ": with Decay at 5 the tail takes " + num (middle, 2) + " s to die away (meant to be about " + num (wanted, 1) + ")");
    check (shortest > 0.05 && shortest < middle * 0.6, id + ": Decay at 0 is much shorter (" + num (shortest, 2) + " s)");

    if (room)
    {
        const double small = ring (5.0f, 0), hall = ring (5.0f, 2);
        check (small < middle * 0.7 && hall > middle * 1.6, "room: Small room rings shorter and Hall longer at the same Decay ("
                                                            + num (small, 2) + ", " + num (middle, 2) + " and " + num (hall, 2) + " s)");
        fx->setParam ("size", 1.0f);
    }

    // At its starting settings the reverb sits well under the voice, and Reverb at 10 is about level with it.
    const auto wetDb = [&] (float reverb)
    {
        fx->setParam ("reverb", reverb);
        fx->setParam ("decay", 5.0f);
        fx->reset();
        const auto x = hiss (4.0, 0.1, rate, 3);
        auto y = run (*fx, x);
        for (size_t i = 0; i < x.size(); ++i) y[i] -= x[i];
        return 20.0 * std::log10 (std::max (rms (y, (size_t) (3.0 * rate)), 1.0e-12) / rms (x, (size_t) (3.0 * rate)));
    };

    const double start = wetDb (room ? 2.5f : 3.0f), full = wetDb (10.0f);
    check (start > -20.0 && start < -9.0 && full > -7.0 && full < 2.0,
           id + ": the reverb is " + num (start) + " dB under the sound at its starting setting and " + num (full) + " dB at Reverb 10");

    fx->setParam ("reverb", 0.0f);
    fx->reset();
    const auto x = sine (196.0, 0.6, 0.1, rate);
    check (biggestDifference (run (*fx, x), x) < 1.0e-6, id + ": Reverb at 0 leaves the sound as it was");

    fx->setParam ("reverb", 10.0f);
    fx->setParam ("decay", 10.0f);
    if (room) fx->setParam ("size", 2.0f);
    fx->reset();
    auto loud = sine (220.0, 3.0, 0.9, rate);
    loud.resize ((size_t) (4.0 * rate), 0.0f);
    const auto r = run (*fx, loud);
    check (allFinite (r) && peak (r) < 4.0f, id + ": everything at full with a loud held note stays under control (peak " + num (gainToDb (peak (r))) + " dB)");
}

void sweepChecks (const std::string& id, const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect (id);
    fx->prepare (rate, 64);

    // A steady note through a sweeping notch gets louder and quieter as the notch passes it.
    const auto x = sine (1000.0, 6.0, 0.1, rate);
    const auto y = run (*fx, x);
    double top = 0.0, bottom = 1.0e9;
    const size_t w = (size_t) (0.02 * rate);
    for (size_t i = (size_t) rate; i + w <= y.size(); i += w)
    {
        const double level = rms (std::vector<float> (y.begin() + (long) i, y.begin() + (long) (i + w)));
        top = std::max (top, level);
        bottom = std::min (bottom, level);
    }
    const double swing = 20.0 * std::log10 (top / std::max (bottom, 1.0e-9));
    check (swing > 6.0 && top < 0.25, id + ": a steady note swells and dips by " + num (swing) + " dB as the sweep passes it");

    if (id == "flanger")
    {
        fx->setParam ("mix", 0.0f);
        fx->reset();
        check (biggestDifference (run (*fx, x), x) < 1.0e-6, "flanger: Mix at 0 leaves the sound as it was");

        fx->setParam ("mix", 50.0f);
        fx->setParam ("resonance", 10.0f);
        fx->setParam ("depth", 10.0f);
        fx->reset();
        const auto r = run (*fx, sine (220.0, 4.0, 0.9, rate));
        check (allFinite (r) && peak (r) < 4.0f, "flanger: Resonance at full with a loud note stays under control (peak " + num (gainToDb (peak (r))) + " dB)");
    }
    else
    {
        // Vibrato mode is the swept sound alone: the level holds steady and only the pitch moves.
        fx->setParam ("mode", 1.0f);
        fx->reset();
        const auto v = run (*fx, x);
        double vTop = 0.0, vBottom = 1.0e9;
        for (size_t i = (size_t) rate; i + w <= v.size(); i += w)
        {
            const double level = rms (std::vector<float> (v.begin() + (long) i, v.begin() + (long) (i + w)));
            vTop = std::max (vTop, level);
            vBottom = std::min (vBottom, level);
        }
        check (20.0 * std::log10 (vTop / vBottom) < 1.0 && biggestDifference (v, x) > 0.01,
               "univibe: in Vibrato mode the level holds steady (within " + num (20.0 * std::log10 (vTop / vBottom), 2) + " dB) while the pitch moves");
    }
}

void channelEqChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect ("peq");
    fx->prepare (rate, 64);

    double worst = 0.0;
    for (double hz : { 40.0, 100.0, 500.0, 1000.0, 3000.0, 8000.0, 15000.0 })
        worst = std::max (worst, std::abs (toneGainDb (*fx, hz, 0.1, rate)));
    check (worst < 0.05, "peq: everything at its starting place leaves the sound unchanged (within " + num (worst, 3) + " dB from 40 Hz to 15 kHz)");

    fx->setParam ("lowcut", 160.0f);
    const double corner = toneGainDb (*fx, 160.0, 0.1, rate), deep = toneGainDb (*fx, 40.0, 0.1, rate), clear = toneGainDb (*fx, 1000.0, 0.1, rate);
    check (std::abs (corner + 3.0) < 0.5 && deep < -20.0 && std::abs (clear) < 0.1,
           "peq: Low cut at 160 Hz is 3 dB down there (" + num (corner) + "), " + num (deep) + " dB at 40 Hz, and leaves 1 kHz alone");
    fx->setParam ("lowcut", 20.0f);

    fx->setParam ("highcut", 5000.0f);
    const double hc = toneGainDb (*fx, 5000.0, 0.1, rate), hcFar = toneGainDb (*fx, 15000.0, 0.1, rate);
    check (std::abs (hc + 3.0) < 0.5 && hcFar < -15.0, "peq: High cut at 5 kHz is 3 dB down there (" + num (hc) + ") and " + num (hcFar) + " dB at 15 kHz");
    fx->setParam ("highcut", 20000.0f);

    fx->setParam ("low", 10.0f);
    fx->setParam ("high", -10.0f);
    const double lowShelf = toneGainDb (*fx, 40.0, 0.1, rate), highShelf = toneGainDb (*fx, 15000.0, 0.1, rate), between = toneGainDb (*fx, 900.0, 0.1, rate);
    check (lowShelf > 8.5 && lowShelf < 10.5 && highShelf < -8.5 && highShelf > -10.5 && std::abs (between) < 0.5,
           "peq: Low up 10 lifts the bottom " + num (lowShelf) + " dB, High down 10 drops the top " + num (highShelf) + " dB, and the middle stays (" + num (between, 2) + " dB)");
    fx->setParam ("low", 0.0f);
    fx->setParam ("high", 0.0f);

    fx->setParam ("hmf", 2500.0f);
    fx->setParam ("hmg", -12.0f);
    fx->setParam ("width", 0.0f);
    const double narrowAt = toneGainDb (*fx, 2500.0, 0.1, rate), narrowBeside = toneGainDb (*fx, 1250.0, 0.1, rate);
    fx->setParam ("width", 10.0f);
    const double wideBeside = toneGainDb (*fx, 1250.0, 0.1, rate);
    check (std::abs (narrowAt + 12.0) < 0.5 && narrowBeside > -1.0 && wideBeside < -4.0,
           "peq: High-mid down 12 at 2.5 kHz cuts " + num (narrowAt) + " dB there; an octave below it is " + num (narrowBeside) + " dB with Mid width at 0 and "
           + num (wideBeside) + " dB at 10");
    fx->setParam ("hmg", 0.0f);

    fx->setParam ("lmf", 250.0f);
    fx->setParam ("lmg", 9.0f);
    fx->setParam ("width", 5.0f);
    const double lm = toneGainDb (*fx, 250.0, 0.1, rate);
    check (std::abs (lm - 9.0) < 0.5, "peq: Low-mid up 9 at 250 Hz lifts " + num (lm) + " dB there");
}

void bodyChecks (const Check& check)
{
    constexpr double rate = 96000.0;
    auto fx = makeEffect ("body");
    fx->prepare (rate, 64);

    fx->setParam ("body", 0.0f);
    fx->reset();
    const auto x = sine (196.0, 0.6, 0.3, rate);
    check (biggestDifference (run (*fx, x), x) < 1.0e-6, "body: Body at 0 leaves the guitar as it was");

    fx->setParam ("phase", 1.0f);
    fx->reset();
    auto flipped = run (*fx, x);
    for (auto& v : flipped) v = -v;
    check (biggestDifference (flipped, x) < 1.0e-6, "body: Flip phase turns the sound upside down and changes nothing else");
    fx->setParam ("phase", 0.0f);

    // Played softly, Body 10 is its fixed curve: the box lifted, the quack eased, a little air.
    fx->setParam ("body", 10.0f);
    const double box = toneGainDb (*fx, 110.0, 0.003, rate), top = toneGainDb (*fx, 220.0, 0.003, rate), quack = toneGainDb (*fx, 2000.0, 0.003, rate),
                 air = toneGainDb (*fx, 9500.0, 0.003, rate), middle = toneGainDb (*fx, 500.0, 0.003, rate);
    check (box > 4.0 && box < 7.0 && top > 3.5 && top < 6.5 && quack < -3.0 && quack > -6.0 && air > 0.5 && air < 3.0 && std::abs (middle) < 2.5,
           "body: at Body 10, played softly: " + num (box) + " dB at 110 Hz, " + num (top) + " at 220 Hz, " + num (middle) + " at 500 Hz, " + num (quack)
           + " at 2 kHz, " + num (air) + " at 9.5 kHz");

    // Dug in, the 2 kHz region alone is squeezed further, and the meter shows it.
    const double hard = toneGainDb (*fx, 2000.0, 0.3, rate);
    const float meter = fx->meterValue();
    const double hardLow = toneGainDb (*fx, 110.0, 0.3, rate);
    check (hard < quack - 3.0 && hard > quack - 8.0 && meter < -3.0f && std::abs (hardLow - box) < 0.3,
           "body: played hard, 2 kHz is turned down a further " + num (quack - hard) + " dB (meter " + num (meter) + " dB) and the low end is not touched");

    fx->setParam ("body", 5.0f);
    const double half = toneGainDb (*fx, 110.0, 0.003, rate);
    check (half > box * 0.35 && half < box * 0.65, "body: Body at 5 is about half of that (" + num (half) + " dB at 110 Hz)");
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
