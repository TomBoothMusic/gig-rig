#include "UniVibe.h"

#include <algorithm>

namespace gigrig::fx
{

UniVibe::UniVibe()
{
    pSpeed     = addParam ("speed",     "Speed",     "Hz", 0.3f, 10.0f, 2.2f);
    pIntensity = addParam ("intensity", "Intensity", "",   0.0f, 10.0f, 7.0f);
    pMode      = addChoice ("mode",     "Mode", { "Chorus", "Vibrato" }, 0);
    pLevel     = addParam ("level",     "Level",     "dB", -12.0f, 12.0f, 0.0f);
}

void UniVibe::onPrepare (double, int)
{
    onReset();
}

void UniVibe::onReset() noexcept
{
    phase = 0.0;
    lamp = 0.5;
    state = {};
    modeMix = value (pMode) < 0.5f ? 1.0f : 0.0f;
}

void UniVibe::processChunk (float* const* ch, int numCh, int n) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    // The pedal's four capacitors (farads). With the cells swinging between about 5 k and
    // 200 k ohms, each stage turns the phase round at a very different part of the range.
    constexpr double caps[4] { 15.0e-9, 220.0e-9, 470.0e-12, 4.7e-9 };

    const double rate = sampleRate();
    const double hz = value (pSpeed);
    const double depth = value (pIntensity) * 0.1;
    const float level = dbToGain (value (pLevel));
    const float modeTarget = value (pMode) < 0.5f ? 1.0f : 0.0f;
    const float modeStep = (float) (1.0 / (0.02 * rate));
    // The lamp heats quickly and cools slowly.
    const double up = 1.0 - std::exp (-1.0 / (0.012 * rate)), down = 1.0 - std::exp (-1.0 / (0.045 * rate));

    for (int i = 0; i < n; ++i)
    {
        phase += hz / rate;
        phase -= std::floor (phase);
        const double drive = 0.5 + 0.5 * std::sin (2.0 * pi * phase);
        lamp += (drive > lamp ? up : down) * (drive - lamp);

        // Brighter lamp, lower resistance. Intensity sets how far the lamp swings.
        const double bright = 0.5 + (lamp - 0.5) * depth;
        const double ohms = 200.0e3 * std::pow (5.0e3 / 200.0e3, bright);

        const float x = ch[0][i];
        float v = x;

        for (int k = 0; k < 4; ++k)
        {
            const double corner = std::min (1.0 / (2.0 * pi * ohms * caps[k]), rate * 0.45);
            const double t = std::tan (pi * corner / rate);
            const float a = (float) ((t - 1.0) / (t + 1.0));
            // First-order all-pass.
            const float y = a * v + state[(size_t) k];
            state[(size_t) k] = v - a * y;
            if (std::abs (state[(size_t) k]) < 1.0e-20f) state[(size_t) k] = 0.0f;
            v = y;
        }

        modeMix += std::clamp (modeTarget - modeMix, -modeStep, modeStep);
        // Chorus: dry and swept in equal parts (the notches that move). Vibrato: swept only.
        // Equal parts come out quieter than either alone, so Chorus gets 2.5 dB back.
        const float out = level * (1.0f + 0.33f * modeMix) * (v * (1.0f - 0.5f * modeMix) + x * 0.5f * modeMix);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }
}

} // namespace gigrig::fx
