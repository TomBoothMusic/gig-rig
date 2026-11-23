#include "BluesDriver.h"
#include "../Shapers.h"

namespace gigrig::fx
{

BluesDriver::BluesDriver()
{
    pGain  = addParam ("gain",  "Gain",  "", 0.0f, 10.0f, 4.0f);
    pTone  = addParam ("tone",  "Tone",  "", 0.0f, 10.0f, 5.0f);
    pLevel = addParam ("level", "Level", "dB", -24.0f, 12.0f, 0.0f);
}

void BluesDriver::onPrepare (double rate, int maxBlock)
{
    os.prepare (rate, maxBlock);
    update();
}

void BluesDriver::onReset() noexcept
{
    os.clear();
    inHp.clear(); mid1Lp.clear(); mid1Hp.clear(); mid2Lp.clear(); dcBlock.clear();
    lowTilt.clear(); highTilt.clear(); scoop.clear(); outLp.clear();
}

void BluesDriver::update()
{
    const double rate = sampleRate(), inner = rate * os.getFactor();
    const double g = value (pGain) / 10.0;

    // About x1.5 to x300 over the two stages, shared between them.
    const double total = 1.5 * std::pow (200.0, std::pow (g, 1.3));
    g1 = (float) std::pow (total, 0.55);
    g2 = (float) std::pow (total, 0.45);

    inHp.highPass (inner, 80.0);
    mid1Lp.lowPass (inner, 7000.0);
    mid1Hp.highPass (inner, 40.0);
    mid2Lp.lowPass (inner, 5500.0);
    dcBlock.highPass (inner, 15.0);

    const double t = value (pTone) / 10.0 * 2.0 - 1.0;     // -1 .. +1
    lowTilt.lowShelf (rate, 350.0, -7.0 * t, 0.7);
    highTilt.highShelf (rate, 1800.0, 9.0 * t, 0.7);
    scoop.peak (rate, 550.0, 0.8, -2.5);
    outLp.lowPass (rate, 9000.0);

    // Louder when driven harder (as on the pedal), but not by much.
    level = dbToGain (value (pLevel)) * (float) (0.3 / std::pow (total, 0.12));
}

void BluesDriver::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
        update();

    for (int c = 0; c < numCh; ++c)
    {
        float* x = ch[c];
        float* u = os.up (x, n, c);
        const int m = n * os.getFactor();

        for (int i = 0; i < m; ++i)
        {
            float v = inHp.run (u[i] * 2.0f, c) * g1;
            v = shape::uneven (v, 0.9f, 1.4f);                  // stage 1
            v = mid1Hp.run (mid1Lp.run (v, c), c) * g2;
            v = shape::uneven (v, 0.75f, 1.1f);                 // stage 2, firmer
            v = mid2Lp.run (v, c);
            u[i] = dcBlock.run (v, c);
        }

        os.down (x, n, c);

        for (int i = 0; i < n; ++i)
        {
            float y = scoop.run (lowTilt.run (x[i], c), c);
            y = outLp.run (highTilt.run (y, c), c);
            x[i] = y * 0.5f * level;
        }
    }
}

} // namespace gigrig::fx
