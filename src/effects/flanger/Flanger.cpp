#include "Flanger.h"

namespace gigrig::fx
{

Flanger::Flanger()
{
    pSpeed  = addParam ("speed",     "Speed",     "Hz", 0.05f, 8.0f, 0.35f);
    pDepth  = addParam ("depth",     "Depth",     "",   0.0f, 10.0f, 6.0f);
    pManual = addParam ("manual",    "Manual",    "",   0.0f, 10.0f, 5.0f);
    pRes    = addParam ("resonance", "Resonance", "",   0.0f, 10.0f, 4.0f);
    pMix    = addParam ("mix",       "Mix",       "%",  0.0f, 100.0f, 50.0f);
}

void Flanger::onPrepare (double rate, int)
{
    line.setMaxSamples ((int) (0.02 * rate) + 16);
    dark.lowPass (rate, 9000.0);
    fbHigh.highPass (rate, 80.0);
    onReset();
}

void Flanger::onReset() noexcept
{
    line.clear();
    dark.clear(); fbHigh.clear();
    lfo.phase = 0.0;
    last = 0.0f;
}

void Flanger::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    // The gap: Manual sets the middle between 6 ms (0) and 0.6 ms (10); Depth sweeps it up to
    // three octaves either side of that, kept between 0.3 and 12 ms.
    const double centreMs = 6.0 * std::pow (0.1, value (pManual) / 10.0);
    const double octaves = value (pDepth) * 0.15;
    const double hz = value (pSpeed);
    const float fb = value (pRes) * 0.088f;
    const float wet = value (pMix) * 0.01f;

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];
        const double ms = std::clamp (centreMs * std::pow (2.0, octaves * lfo.next (hz, rate)), 0.3, 12.0);

        float in = x + fb * last;
        if (std::abs (in) > 1.5f) in = 1.5f * std::tanh (in / 1.5f);     // a safety for Resonance at full
        if (std::abs (in) < 1.0e-20f) in = 0.0f;
        line.write (in);

        const float copy = dark.run (line.read (ms * 0.001 * rate), 0);
        last = fbHigh.run (copy, 0);

        // 0% is the dry sound, 50% equal parts (the deepest notches), 100% the swept copy alone.
        // Equal parts would sound quieter, so the middle gets a little back.
        const float y = (x + wet * (copy - x)) * (1.0f + 1.2f * wet * (1.0f - wet));
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }
}

} // namespace gigrig::fx
