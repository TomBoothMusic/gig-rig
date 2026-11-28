#include "NoiseGate.h"

#include <algorithm>
#include <cmath>

namespace gigrig::fx
{

NoiseGate::NoiseGate()
{
    pThreshold = addParam ("threshold", "Thresh",    "dB", -90.0f, -20.0f, -62.0f);
    pRelease   = addParam ("release",   "Release",   "ms", 20.0f, 600.0f, 150.0f);
    pDepth     = addParam ("depth",     "Depth",     "dB", -90.0f, 0.0f, -70.0f);
}

void NoiseGate::onPrepare (double rate, int)
{
    envUp = 1.0 - std::exp (-1.0 / (0.0003 * rate));      // the level follower rises in 0.3 ms
    envDown = 1.0 - std::exp (-1.0 / (0.030 * rate));     // and falls over 30 ms
    attackStep = 1.0 / (0.002 * rate);                    // opening: a 2 ms fade
    onReset();
}

void NoiseGate::onReset() noexcept
{
    env = 0.0;
    gain = dbToGain (value (pDepth));
    open = false;
    holdLeft = 0;
    setMeter (value (pDepth));
}

void NoiseGate::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double openAt = dbToGain (value (pThreshold));
    const double closeAt = openAt * 0.5;                  // 6 dB below: room for a note's tail
    const double floorGain = dbToGain (value (pDepth));
    const double releaseStep = (1.0 - floorGain) / std::max (1.0, value (pRelease) * 0.001 * rate);
    const int holdSamples = (int) (0.040 * rate);

    for (int i = 0; i < n; ++i)
    {
        double level = 0.0;
        for (int c = 0; c < numCh; ++c)
            level = std::max (level, (double) std::abs (ch[c][i]));

        env += (level > env ? envUp : envDown) * (level - env);

        if (env >= openAt)
        {
            open = true;
            holdLeft = holdSamples;
        }
        else if (env < closeAt)
        {
            if (holdLeft > 0) --holdLeft;
            else open = false;
        }

        if (open)  gain = std::min (1.0, gain + attackStep);
        else       gain = std::max (floorGain, gain - releaseStep);

        // A depth moved above where the gate sits now pulls it up smoothly too.
        if (gain < floorGain)
            gain = std::min (floorGain, gain + attackStep);

        for (int c = 0; c < numCh; ++c)
            ch[c][i] = (float) (ch[c][i] * gain);
    }

    setMeter (gainToDb ((float) gain));
}

} // namespace gigrig::fx
