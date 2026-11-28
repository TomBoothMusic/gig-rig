#include "BusComp.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

namespace
{
    double coeffFor (double seconds, double rate) noexcept
    {
        return 1.0 - std::exp (-1.0 / std::max (1.0, seconds * rate));
    }

    constexpr double kneeDb = 3.0;
}

BusComp::BusComp()
{
    pThreshold = addParam ("threshold", "Thresh", "dB", -40.0f, 0.0f, -14.0f);
    pRatio     = addChoice ("ratio", "Ratio", { "2", "4", "10" }, 1);
    pAttack    = addChoice ("attack", "Attack", { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 4);
    pRelease   = addChoice ("release", "Release", { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, 4);
    pMakeup    = addParam ("makeup", "Makeup", "dB", 0.0f, 15.0f, 0.0f);
    pListenCut = addParam ("listencut", "SC HPF", "Hz", 20.0f, 250.0f, 60.0f);
    pMix       = addParam ("mix", "Mix", "%", 0.0f, 100.0f, 100.0f);
}

void BusComp::onPrepare (double, int)   { onReset(); }

void BusComp::onReset() noexcept
{
    listen.clear();
    env = grFast = grSlow = 0.0;
    setMeter (0.0f);
}

void BusComp::processChunk (float* const* ch, int numCh, int n) noexcept
{
    static constexpr double ratios[3] { 2.0, 4.0, 10.0 };
    static constexpr double attacks[6] { 0.0001, 0.0003, 0.001, 0.003, 0.010, 0.030 };
    static constexpr double releases[4] { 0.1, 0.3, 0.6, 1.2 };

    const double rate = sampleRate();
    const double slope = 1.0 - 1.0 / ratios[std::clamp ((int) std::lround (value (pRatio)), 0, 2)];
    const double attackC = coeffFor (attacks[std::clamp ((int) std::lround (value (pAttack)), 0, 5)], rate);
    const int releaseChoice = std::clamp ((int) std::lround (value (pRelease)), 0, 4);
    const bool autoRelease = releaseChoice == 4;
    const double releaseC = coeffFor (autoRelease ? 0.15 : releases[releaseChoice], rate);
    const double releaseSlowC = coeffFor (1.8, rate);
    const double ripple = coeffFor (0.003, rate);
    const double thresholdDb = value (pThreshold);
    const double makeup = dbToGain (value (pMakeup));
    const double wet = value (pMix) * 0.01;
    double gr = 0.0;

    if (anyChanged())
        listen.highPass (rate, value (pListenCut));

    for (int i = 0; i < n; ++i)
    {
        // What it listens to: the loudest side, with the deep bass taken out.
        double level = 0.0;
        for (int c = 0; c < numCh; ++c)
            level = std::max (level, std::abs ((double) listen.run (ch[c][i], c)));

        env = level > env ? level : env + ripple * (level - env);
        const double over = (env > 1.0e-9 ? 20.0 * std::log10 (env) : -180.0) - thresholdDb;
        double target = 0.0;
        if (over >= kneeDb * 0.5)        target = over * slope;
        else if (over > -kneeDb * 0.5)   target = slope * (over + kneeDb * 0.5) * (over + kneeDb * 0.5) / (2.0 * kneeDb);

        grFast += (target > grFast ? attackC : releaseC) * (target - grFast);

        if (autoRelease)
        {
            // Auto: a short peak lets go quickly, a long squeeze lets go slowly.
            grSlow += (grFast > grSlow ? coeffFor (0.4, rate) : releaseSlowC) * (grFast - grSlow);
            gr = std::max (grFast, 0.6 * grSlow);
        }
        else
            gr = grFast;

        const double g = std::pow (10.0, -gr * 0.05) * makeup;

        for (int c = 0; c < numCh; ++c)
        {
            const double dry = ch[c][i];
            ch[c][i] = (float) (dry * g * wet + dry * (1.0 - wet));
        }
    }

    setMeter ((float) -gr);
}

//==============================================================================
void busCompChecks (const Check& check)
{
    using namespace test;
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.6 * rate);

    auto fx = makeEffect ("buscomp");
    fx->prepare (rate, 64);

    const auto changeDb = [&] (double hz, double inDb)
    {
        fx->reset();
        const auto x = sine (hz, 1.0, std::pow (10.0, inDb / 20.0), rate);
        return 20.0 * std::log10 (std::max (rms (run (*fx, x), from), 1.0e-12) / rms (x, from));
    };

    const double quiet = changeDb (1000.0, -40.0);
    check (std::abs (quiet) < 0.2 && fx->meterValue() > -0.2f, "buscomp: a quiet mix passes untouched (" + num (quiet, 2) + " dB)");

    // Ratio 4, threshold -14: a mix at -2 dB is 12 dB over, so about 9 dB comes off.
    const double loud = changeDb (1000.0, -2.0);
    check (loud < -7.5 && loud > -10.5 && fx->meterValue() < -7.5f, "buscomp: at ratio 4, a mix 12 dB over the threshold is turned down by about 9 dB (" + num (loud, 1) + " dB)");

    fx->setParam ("ratio", 0.0f);
    const double gentle = changeDb (1000.0, -2.0);
    check (gentle > loud + 2.0, "buscomp: ratio 2 is gentler (" + num (gentle, 1) + " dB)");
    fx->setParam ("ratio", 1.0f);

    // The low cut on what it listens to: a deep bass note at the same level moves it far less.
    fx->setParam ("listencut", 200.0f);
    const double bass = changeDb (45.0, -2.0);
    check (bass > loud + 4.0, "buscomp: with the bass ignored below 200 Hz, a 45 Hz note at the same level is turned down far less (" + num (bass, 1) + " dB), so a kick does not make the mix duck");
    fx->setParam ("listencut", 60.0f);

    fx->setParam ("mix", 0.0f);
    const double dry = changeDb (1000.0, -2.0);
    check (std::abs (dry) < 0.05, "buscomp: with Mix at 0 the mix passes untouched (" + num (dry, 2) + " dB)");
}

} // namespace gigrig::fx
