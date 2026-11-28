#include "Comp1176.h"

#include <algorithm>
#include <cmath>

namespace gigrig::fx
{

namespace
{
    // Fixed threshold, in the digital level after Input: a guitar peaking around -20 dBFS sits
    // just over it with Input at 0, so the first turn of the knob already compresses.
    constexpr double thresholdDb = -24.0;
    constexpr double kneeDb = 6.0;

    double ratioFor (int choice) noexcept
    {
        switch (choice)
        {
            case 0:  return 4.0;
            case 1:  return 8.0;
            case 2:  return 12.0;
            case 3:  return 20.0;
            default: return 30.0;     // all buttons in
        }
    }

    /** Front-panel 1..7 (7 fastest) to seconds, spaced evenly on a log scale. */
    double knobToSeconds (double knob, double slowest, double fastest) noexcept
    {
        const double t = (std::clamp (knob, 1.0, 7.0) - 1.0) / 6.0;
        return slowest * std::pow (fastest / slowest, t);
    }

    double coeffFor (double seconds, double rate) noexcept
    {
        return 1.0 - std::exp (-1.0 / std::max (1.0, seconds * rate));
    }
}

Comp1176::Comp1176()
{
    pInput   = addParam ("input",   "Input",   "dB", -20.0f, 30.0f, 0.0f);
    pOutput  = addParam ("output",  "Output",  "dB", -30.0f, 20.0f, 0.0f);
    pAttack  = addParam ("attack",  "Attack (7 = fast)",  "", 1.0f, 7.0f, 3.0f);
    pRelease = addParam ("release", "Release (7 = fast)", "", 1.0f, 7.0f, 5.0f);
    pRatio   = addChoice ("ratio",  "Ratio", { "4", "8", "12", "20", "All" }, 0);
    pMix     = addParam ("mix",     "Mix",     "%", 0.0f, 100.0f, 100.0f);
}

void Comp1176::onPrepare (double rate, int)
{
    dcCoeff = 1.0 - (2.0 * 3.14159265358979 * 5.0 / rate);
    onReset();
}

void Comp1176::onReset() noexcept
{
    env = 0.0;
    grFast = grSlow = 0.0;
    meterDb = 0.0;
    dcX = {}; dcY = {};
    setMeter (0.0f);
}

void Comp1176::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const int ratioChoice = (int) std::lround (value (pRatio));
    const bool allButtons = ratioChoice >= 4;
    const double ratio = ratioFor (ratioChoice);
    const double slope = 1.0 - 1.0 / ratio;

    // "All buttons" famously slows the attack a touch (the first transient pokes through).
    const double attackS = knobToSeconds (value (pAttack), 800.0e-6, 20.0e-6) * (allButtons ? 3.0 : 1.0);
    const double releaseS = knobToSeconds (value (pRelease), 1.1, 0.05);
    const double attackC = coeffFor (attackS, rate);
    const double releaseFastC = coeffFor (releaseS * 0.35, rate);
    const double releaseSlowC = coeffFor (releaseS * 2.0, rate);
    const double detectRelease = coeffFor (0.002, rate);     // the detector's own ripple filter

    const double inGain = dbToGain (value (pInput));
    const double outGain = dbToGain (value (pOutput));
    const double wet = value (pMix) * 0.01;
    const double colour = allButtons ? 0.10 : 0.03;          // FET colour per unit of squeeze

    for (int i = 0; i < n; ++i)
    {
        double level = 0.0;
        for (int c = 0; c < numCh; ++c)
            level = std::max (level, std::abs ((double) ch[c][i] * inGain));

        // Peak detector: instant up, a 2 ms ripple filter down.
        env = level > env ? level : env + detectRelease * (level - env);
        const double levelDb = env > 1.0e-9 ? 20.0 * std::log10 (env) : -180.0;

        // Soft-knee gain computer.
        const double over = levelDb - thresholdDb;
        double target = 0.0;
        if (over >= kneeDb * 0.5)        target = over * slope;
        else if (over > -kneeDb * 0.5)   target = slope * (over + kneeDb * 0.5) * (over + kneeDb * 0.5) / (2.0 * kneeDb);
        if (allButtons && target > 0.0)  target *= 1.0 + 0.15 * std::min (1.0, target / 12.0);   // the extra bite

        // Attack pulls both stages down together; release lets the fast stage go first,
        // so a short peak recovers quickly and a long squeeze recovers slowly.
        if (target > grFast)  grFast += attackC * (target - grFast);
        else                  grFast += releaseFastC * (target - grFast);
        if (target > grSlow)  grSlow += attackC * 0.5 * (target - grSlow);
        else                  grSlow += releaseSlowC * (target - grSlow);
        const double gr = std::max (grFast * 0.7 + grSlow * 0.3, 0.0);
        const double g = dbToGain ((float) -gr);
        meterDb = gr;

        const double squeeze = std::min (1.0, gr / 20.0) * colour;
        for (int c = 0; c < numCh; ++c)
        {
            const double dry = ch[c][i];
            double y = dry * inGain * g;
            // A touch of even-order FET colour, more as it squeezes harder. It adds a little
            // DC, which a 5 Hz blocker takes out again.
            y += squeeze * y * std::abs (y) * 4.0;
            const double blocked = y - dcX[(size_t) c] + dcCoeff * dcY[(size_t) c];
            dcX[(size_t) c] = y;
            dcY[(size_t) c] = blocked;

            // Output, then the blend with the dry guitar (Mix below 100%).
            ch[c][i] = (float) ((blocked * wet + dry * (1.0 - wet)) * outGain);
        }
    }

    setMeter ((float) -meterDb);
}

} // namespace gigrig::fx
