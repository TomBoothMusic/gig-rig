#include "TubeScreamer.h"
#include "../Shapers.h"

#include <algorithm>

namespace gigrig::fx
{

TubeScreamer::TubeScreamer()
{
    pDrive = addParam ("drive", "Drive", "", 0.0f, 10.0f, 5.0f);
    pTone  = addParam ("tone",  "Tone",  "", 0.0f, 10.0f, 5.0f);
    pLevel = addParam ("level", "Level", "dB", -24.0f, 12.0f, 0.0f);
    pLow   = addParam ("low",   "Low end", "", 0.0f, 10.0f, 0.0f);
}

void TubeScreamer::onPrepare (double rate, int maxBlock)
{
    os.prepare (rate, maxBlock);
    update();
}

void TubeScreamer::onReset() noexcept
{
    os.clear();
    clipHp.clear(); clipLp.clear(); fixedLp.clear(); toneShelf.clear(); outHp.clear();
}

void TubeScreamer::update()
{
    const double rate = sampleRate(), inner = rate * os.getFactor();
    const double drive = value (pDrive) / 10.0;

    // Drive pot 0 to 500k (audio taper) plus 51k, over the 4.7k to ground.
    const double rDrive = 51.0e3 + 500.0e3 * std::pow (drive, 2.2);
    gain = (float) (rDrive / 4.7e3);

    const double corner = 720.0 - 470.0 * (value (pLow) / 10.0);
    clipHp.highPass (inner, corner);
    clipLp.lowPass (inner, 1.0 / (2.0 * 3.14159265 * rDrive * 51.0e-12));   // the 51 pF across the drive pot

    fixedLp.lowPass (rate, 723.0 * 4.0);   // the 1k/0.22 uF network sits in a feedback loop: about 3 kHz overall
    const double tone = value (pTone) / 10.0;
    toneShelf.highShelf (rate, 1200.0, -14.0 + 20.0 * tone, 0.8);
    outHp.highPass (rate, 30.0);

    // Make-up so the level knob at 0 dB is about as loud as the pedal off, at the middle settings.
    level = dbToGain (value (pLevel)) * 0.27f;
}

void TubeScreamer::processChunk (float* const* ch, int numCh, int n) noexcept
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
            const float v = u[i] * 2.0f;                        // to "volts"
            const float boosted = clipLp.run (clipHp.run (v, c) * gain, c);
            u[i] = v + shape::diodePair (boosted, 0.55f);       // clean underneath plus the clipped mids
        }

        os.down (x, n, c);

        for (int i = 0; i < n; ++i)
        {
            float y = fixedLp.run (x[i], c);
            y = toneShelf.run (y, c);
            x[i] = outHp.run (y, c) * 0.5f * level;
        }
    }
}

} // namespace gigrig::fx
