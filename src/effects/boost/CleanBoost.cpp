#include "CleanBoost.h"
#include "../Shapers.h"

namespace gigrig::fx
{

CleanBoost::CleanBoost()
{
    pBoost  = addParam ("boost",  "Boost",  "dB", 0.0f, 24.0f, 9.0f);
    pBright = addParam ("bright", "Bright", "", 0.0f, 10.0f, 3.0f);
}

void CleanBoost::onPrepare (double, int)
{
    update();
}

void CleanBoost::onReset() noexcept
{
    high.clear();
    low.clear();
}

void CleanBoost::update()
{
    const double b = value (pBright) / 10.0;
    high.highShelf (sampleRate(), 2200.0, 7.0 * b, 0.7);
    low.lowShelf (sampleRate(), 150.0, -5.0 * b, 0.7);
    gain = dbToGain (value (pBoost));
}

void CleanBoost::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
        update();

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = shape::soft (low.run (high.run (ch[c][i], c), c) * gain, 1.4f);
}

} // namespace gigrig::fx
