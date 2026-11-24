#include "Chorus.h"

namespace gigrig::fx
{

Chorus::Chorus()
{
    pSpeed = addParam ("speed", "Speed", "Hz", 0.1f, 5.0f, 0.8f);
    pDepth = addParam ("depth", "Depth", "%",  0.0f, 100.0f, 50.0f);
    pMix   = addParam ("mix",   "Mix",   "%",  0.0f, 100.0f, 50.0f);
}

void Chorus::onPrepare (double rate, int)
{
    line.setMaxSamples ((int) (0.02 * rate) + 16);
    dark.lowPass (rate, 7000.0);
    onReset();
}

void Chorus::onReset() noexcept
{
    line.clear();
    dark.clear();
    lfo.phase = 0.0;
}

void Chorus::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double base = 0.008 * rate;
    const double swing = value (pDepth) * 0.01 * 0.003 * rate;
    const double hz = value (pSpeed);
    const float wet = value (pMix) * 0.01f;

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];
        line.write (x);
        const double s = lfo.next (hz, rate);
        // A second voice a third of a cycle behind, for width in mono.
        const double s2 = std::sin ((lfo.phase + 1.0 / 3.0) * 6.283185307179586);
        const float voices = 0.5f * (line.read (base + swing * s) + line.read (base * 1.3 + swing * s2));
        const float y = x * (1.0f - 0.5f * wet) + dark.run (voices, 0) * wet;
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }
}

} // namespace gigrig::fx
