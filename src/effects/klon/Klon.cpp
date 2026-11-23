#include "Klon.h"
#include "../Shapers.h"

namespace gigrig::fx
{

Klon::Klon()
{
    pGain   = addParam ("gain",   "Gain",   "", 0.0f, 10.0f, 3.0f);
    pTreble = addParam ("treble", "Treble", "", 0.0f, 10.0f, 5.0f);
    pOutput = addParam ("output", "Output", "dB", -24.0f, 12.0f, 0.0f);
}

void Klon::onPrepare (double rate, int maxBlock)
{
    os.prepare (rate, maxBlock);
    update();
}

void Klon::onReset() noexcept
{
    os.clear();
    clipHp.clear(); clipLp.clear(); dcBlock.clear(); treble.clear(); outLp.clear();
}

void Klon::update()
{
    const double rate = sampleRate(), inner = rate * os.getFactor();
    const double g = value (pGain) / 10.0;

    clean = (float) (1.0 - 0.45 * g);
    drive = (float) (1.5 + 90.0 * g * g);
    blend = (float) (0.25 + 1.1 * g);

    clipHp.highPass (inner, 420.0, 0.6);     // the driven copy is mostly mids
    clipLp.lowPass (inner, 4200.0);
    dcBlock.highPass (inner, 12.0);

    treble.highShelf (rate, 1100.0, -9.0 + 16.0 * (value (pTreble) / 10.0), 0.8);
    outLp.lowPass (rate, 11000.0);

    level = dbToGain (value (pOutput)) * (float) (1.0 / (0.85 + 0.6 * g));
}

void Klon::processChunk (float* const* ch, int numCh, int n) noexcept
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
            const float v = u[i] * 2.0f;
            float d = clipLp.run (clipHp.run (v, c) * drive, c);
            d = shape::soft (d, 0.32f);                           // germanium diodes to ground
            // 18 V inside the pedal: the summing amp has room to spare, only rounding off huge peaks.
            u[i] = dcBlock.run (shape::soft (v * clean + d * blend, 3.0f), c);
        }

        os.down (x, n, c);

        for (int i = 0; i < n; ++i)
            x[i] = outLp.run (treble.run (x[i], c), c) * 0.5f * level;
    }
}

} // namespace gigrig::fx
