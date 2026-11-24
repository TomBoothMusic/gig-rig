#include "Tremolo.h"

namespace gigrig::fx
{

Tremolo::Tremolo()
{
    pSpeed     = addParam ("speed",     "Speed",     "Hz", 0.5f, 12.0f, 4.5f);
    pIntensity = addParam ("intensity", "Intensity", "%",  0.0f, 100.0f, 30.0f);
    pShape     = addParam ("shape",     "Shape",     "",   0.0f, 10.0f, 3.0f);
}

void Tremolo::onPrepare (double, int)   { onReset(); }
void Tremolo::onReset() noexcept        { lfo.phase = 0.25; }   // start at the top of the pulse: no dip when switched on

void Tremolo::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double depth = value (pIntensity) * 0.01;
    const double k = 0.6 + value (pShape) * 0.45;          // how square the pulse gets
    const double norm = 1.0 / std::tanh (k);
    const double hz = value (pSpeed);

    for (int i = 0; i < n; ++i)
    {
        const double w = std::tanh (k * lfo.next (hz, rate)) * norm;     // -1..1
        const float g = (float) (1.0 - depth * (0.5 - 0.5 * w));         // 1 at the top, 1 - depth at the bottom
        for (int c = 0; c < numCh; ++c)
            ch[c][i] *= g;
    }
}

} // namespace gigrig::fx
