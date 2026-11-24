#include "Vibrato.h"

namespace gigrig::fx
{

Vibrato::Vibrato()
{
    pSpeed = addParam ("speed", "Speed", "Hz", 0.5f, 10.0f, 5.0f);
    pDepth = addParam ("depth", "Depth", "%",  0.0f, 100.0f, 40.0f);
}

void Vibrato::onPrepare (double rate, int)
{
    centre = (int) std::lround (0.0005 * rate);            // half a millisecond at the least, swinging up to 5 ms later
    line.setMaxSamples ((int) (0.008 * rate) + 16);
    onReset();
}

void Vibrato::onReset() noexcept
{
    line.clear();
    lfo.phase = 0.0;
}

void Vibrato::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double swing = value (pDepth) * 0.01 * 0.0025 * rate;
    const double hz = value (pSpeed);

    for (int i = 0; i < n; ++i)
    {
        line.write (ch[0][i]);
        const float y = line.read ((double) centre + swing * (1.0 + lfo.next (hz, rate)));
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }
}

} // namespace gigrig::fx
