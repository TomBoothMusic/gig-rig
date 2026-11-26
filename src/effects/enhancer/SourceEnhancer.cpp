#include "SourceEnhancer.h"
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

    constexpr double closeSeconds[] { 0.08, 0.16, 0.3, 0.6, 1.2, 2.5 };     // Time A to F
    constexpr double kneeDb = 6.0;
}

SourceEnhancer::SourceEnhancer()
{
    pThreshold = addParam ("threshold", "Threshold", "dB", -70.0f, 0.0f, -38.0f);
    pDepth     = addParam ("depth", "Depth", "dB", 0.0f, 20.0f, 8.0f);
    pTime      = addChoice ("time", "Time", { "A (fastest)", "B", "C", "D", "E", "F (slowest)" }, 2);
    pMode      = addChoice ("mode", "Listens to", { "RMS (loudness)", "Peak" }, 0);
}

void SourceEnhancer::onPrepare (double, int)    { onReset(); }

void SourceEnhancer::onReset() noexcept
{
    hp = lp = power = peak = 0.0;
    reduction = 0.0;
    hold = 0;
    setMeter (0.0f);
}

void SourceEnhancer::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double thresholdDb = value (pThreshold), depth = value (pDepth);
    const bool usePeak = value (pMode) >= 0.5f;
    const int time = std::clamp ((int) std::lround (value (pTime)), 0, 5);
    const double hpC = coeffFor (1.0 / (6.283185307179586 * 150.0), rate), lpC = coeffFor (1.0 / (6.283185307179586 * 5000.0), rate);
    const double rmsC = coeffFor (0.010, rate), peakFall = coeffFor (0.020, rate);
    const double openC = coeffFor (0.004, rate), closeC = coeffFor (closeSeconds[time], rate);
    const int holdSamples = (int) (0.06 * rate);

    for (int i = 0; i < n; ++i)
    {
        double x = 0.0;
        for (int c = 0; c < numCh; ++c)
            x += (double) ch[c][i];
        x /= (double) std::max (1, numCh);

        // The listening path: under 150 Hz and over 5 kHz count for less, so a kick drum or a
        // cymbal next to the microphone does not open it the way a voice does.
        hp += hpC * (x - hp);
        lp += lpC * ((x - hp) - lp);
        const double heard = lp;

        power += rmsC * (heard * heard - power);
        const double mag = std::abs (heard);
        peak = mag > peak ? mag : peak + peakFall * (mag - peak);

        const double level = usePeak ? peak : std::sqrt (std::max (power, 0.0)) * 1.41421356;      // a steady note reads the same both ways
        const double under = thresholdDb - (level > 1.0e-9 ? 20.0 * std::log10 (level) : -180.0);

        double target = 0.0;
        if (under >= kneeDb * 0.5)          target = under;
        else if (under > -kneeDb * 0.5)     target = (under + kneeDb * 0.5) * (under + kneeDb * 0.5) / (2.0 * kneeDb);
        target = std::min (target, depth);

        if (target < reduction)
        {
            reduction += openC * (target - reduction);      // the source is back: open quickly
            hold = holdSamples;
        }
        else if (hold > 0)
        {
            --hold;                                         // keep the end of the word
        }
        else
        {
            reduction += closeC * (target - reduction);
        }

        const float g = (float) std::pow (10.0, -reduction * 0.05);

        for (int c = 0; c < numCh; ++c)
            ch[c][i] *= g;
    }

    setMeter ((float) -reduction);
}

//==============================================================================
void sourceEnhancerChecks (const Check& check)
{
    using namespace test;
    constexpr double rate = 96000.0;

    const auto changeDb = [&] (Effect& fx, double inDb, double hz = 440.0)
    {
        fx.reset();
        const auto x = sine (hz, 2.0, std::pow (10.0, inDb / 20.0), rate);
        const size_t from = (size_t) (1.5 * rate);
        return 20.0 * std::log10 (std::max (rms (run (fx, x), from), 1.0e-12) / rms (x, from));
    };

    auto fx = makeEffect ("enhancer");
    fx->prepare (rate, 64);
    fx->setParam ("time", 0.0f);

    const double voice = changeDb (*fx, -20.0);
    check (std::abs (voice) < 0.1 && fx->meterValue() > -0.1f, "enhancer: a voice over the threshold passes untouched (" + num (voice, 2) + " dB)");

    const double spill = changeDb (*fx, -60.0);
    check (std::abs (spill + 8.0) < 0.5, "enhancer: spill well under the threshold is turned down by the Depth and no further (" + num (spill, 1) + " dB with Depth at 8)");

    const double edge = changeDb (*fx, -41.0);
    check (edge < -1.0 && edge > -6.0, "enhancer: just under the threshold it eases down, it does not snap shut (" + num (edge, 1) + " dB at 3 dB under)");

    fx->setParam ("depth", 0.0f);
    const double none = changeDb (*fx, -60.0);
    check (std::abs (none) < 0.1, "enhancer: with Depth at 0 it does nothing (" + num (none, 2) + " dB)");
    fx->setParam ("depth", 8.0f);

    // A kick drum's thump next to the microphone opens it less than a voice at the same level.
    const double thump = changeDb (*fx, -34.0, 50.0), sung = changeDb (*fx, -34.0, 440.0);
    check (sung > -0.3 && thump < -3.0, "enhancer: it listens to the voice's part of the sound: a 50 Hz thump at -34 dB is still turned down (" + num (thump, 1) + " dB), a sung note at the same level is not (" + num (sung, 1) + " dB)");

    // The end of a word is kept, the gap closes at the pace Time sets, and it opens again at once.
    const auto closedAfter = [&] (int time, double seconds)
    {
        fx->setParam ("time", (float) time);
        fx->reset();
        auto x = sine (440.0, 0.5, 0.1, rate);
        x.resize (x.size() + (size_t) (seconds * rate), 0.0f);
        run (*fx, x);
        return (double) -fx->meterValue();
    };
    const double held = closedAfter (0, 0.04), fastGap = closedAfter (0, 0.4), slowGap = closedAfter (5, 0.4);
    check (held < 0.5 && fastGap > 7.0 && slowGap < 3.0 && slowGap > 0.2,
           "enhancer: 40 ms after a word ends it has not moved (" + num (held, 1) + " dB); 0.4 s after, Time A has closed (" + num (fastGap, 1) + " dB) and Time F has only begun to (" + num (slowGap, 1) + " dB)");

    fx->setParam ("time", 2.0f);
    fx->reset();
    std::vector<float> x ((size_t) rate, 0.0f);
    const auto word = sine (440.0, 0.1, 0.1, rate);
    x.insert (x.end(), word.begin(), word.end());
    const auto y = run (*fx, x);
    const size_t at = (size_t) rate + (size_t) (0.03 * rate);
    const double opened = 20.0 * std::log10 (std::max (rms (y, at), 1.0e-12) / rms (x, at));
    check (opened > -0.5, "enhancer: when the voice comes back it is fully open within 30 ms (" + num (opened, 2) + " dB)");
}

} // namespace gigrig::fx
