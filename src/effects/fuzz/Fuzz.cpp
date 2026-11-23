#include "Fuzz.h"
#include "../Shapers.h"

#include <algorithm>
#include <cmath>

namespace gigrig::fx
{

Fuzz::Fuzz()
{
    pFuzz   = addParam ("fuzz",   "Fuzz",   "", 0.0f, 10.0f, 7.0f);
    pVolume = addParam ("volume", "Volume", "dB", -24.0f, 12.0f, 0.0f);
    pBias   = addParam ("bias",   "Bias",   "", 0.0f, 10.0f, 5.0f);
    pTone   = addParam ("tone",   "Tone",   "", 0.0f, 10.0f, 5.0f);
}

void Fuzz::onPrepare (double rate, int maxBlock)
{
    os.prepare (rate, maxBlock);
    update();
}

void Fuzz::onReset() noexcept
{
    os.clear();
    inHp.clear(); inLp.clear(); midHp.clear();
    outLp.clear(); tilt.clear(); dcBlock.clear();
}

namespace
{
    // The first transistor: soft, uneven (the positive side gives out first).
    inline float stage1 (float u) noexcept   { return shape::uneven (u, 0.35f, 0.8f); }
}

void Fuzz::update()
{
    const double rate = sampleRate(), inner = rate * os.getFactor();
    const double f = value (pFuzz) / 10.0;

    pre = dbToGain ((float) (2.0 + 40.0 * f));

    // Bias: 5 and above = healthy. Below 5 the working point slides, which makes the clipping
    // more lopsided and opens a small dead zone (the sputter and gate of a starved pedal).
    const double starve = std::clamp ((5.0 - value (pBias)) / 5.0, 0.0, 1.0);
    const double over   = std::clamp ((value (pBias) - 5.0) / 5.0, 0.0, 1.0);
    offset   = (float) (0.12 * starve - 0.06 * over);
    deadZone = (float) (0.1 * starve * starve);
    rest1    = stage1 (offset);

    inHp.highPass (inner, 70.0);
    inLp.lowPass (inner, 6500.0);
    midHp.highPass (inner, 40.0);

    outLp.lowPass (rate, 5500.0, 0.6);
    const double tone = value (pTone) / 10.0;
    tilt.highShelf (rate, 900.0, -9.0 + 18.0 * tone, 0.7);
    dcBlock.highPass (rate, 25.0);

    // Make-up: a square wave at full fuzz is much louder than a guitar; at the defaults the
    // volume knob at 0 dB is about as loud as the pedal off.
    level = dbToGain (value (pVolume)) * 0.11f;
}

void Fuzz::processChunk (float* const* ch, int numCh, int n) noexcept
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
            float v = inLp.run (inHp.run (u[i] * 2.0f, c), c) * pre;

            // The dead zone of a starved pedal: small signals barely get through.
            if (deadZone > 0.0f)
            {
                const float a = std::abs (v);
                v = a > deadZone ? std::copysign (a - deadZone, v) : 0.0f;
            }

            float y = stage1 (v + offset) - rest1;      // zero in, zero out at any bias
            y = midHp.run (y, c) * 6.0f;
            y = shape::uneven (y, 0.9f, 0.6f);          // the second transistor squares it off
            u[i] = y;
        }

        os.down (x, n, c);

        for (int i = 0; i < n; ++i)
        {
            float y = outLp.run (x[i], c);
            y = tilt.run (y, c);
            x[i] = dcBlock.run (y, c) * level;
        }
    }
}

} // namespace gigrig::fx
