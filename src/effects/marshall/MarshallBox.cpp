#include "MarshallBox.h"
#include "../Shapers.h"

namespace gigrig::fx
{

MarshallBox::MarshallBox()
{
    pGain     = addParam ("gain",     "Gain",     "", 0.0f, 10.0f, 6.0f);
    pBass     = addParam ("bass",     "Bass",     "", 0.0f, 10.0f, 5.0f);
    pMiddle   = addParam ("middle",   "Middle",   "", 0.0f, 10.0f, 6.0f);
    pTreble   = addParam ("treble",   "Treble",   "", 0.0f, 10.0f, 5.0f);
    pPresence = addParam ("presence", "Presence", "", 0.0f, 10.0f, 5.0f);
    pLevel    = addParam ("level",    "Level",    "dB", -24.0f, 12.0f, 0.0f);
}

void MarshallBox::onPrepare (double rate, int maxBlock)
{
    os.prepare (rate, maxBlock);
    update();
}

void MarshallBox::onReset() noexcept
{
    os.clear();
    for (auto* f : { &inHp, &hp1, &lp1, &hp2, &lp2, &hp3, &lp3 }) f->clear();
    for (auto* f : { &bright, &bass, &middle, &trebleShelf, &presence, &outLp, &tight }) f->clear();
}

void MarshallBox::update()
{
    const double rate = sampleRate(), inner = rate * os.getFactor();
    const double g = value (pGain) / 10.0;

    pre = dbToGain ((float) (-16.0 + 52.0 * g));

    inHp.highPass (inner, 110.0);           // small coupling caps: tight lows before the gain
    bright.highShelf (inner, 2500.0, 4.0 * (1.0 - g * 0.5), 0.7);   // the bright cap, strongest at lower gain
    hp1.highPass (inner, 140.0);  lp1.lowPass (inner, 9000.0);
    hp2.highPass (inner, 70.0);   lp2.lowPass (inner, 7000.0);
    hp3.highPass (inner, 35.0);   lp3.lowPass (inner, 6000.0);

    const auto knob = [this] (int p) { return value (p) / 10.0; };
    tight.highPass (rate, 60.0, 0.6);
    bass.lowShelf (rate, 120.0, -12.0 + 18.0 * knob (pBass), 0.8);
    middle.peak (rate, 650.0, 0.7, -14.0 + 20.0 * knob (pMiddle));
    trebleShelf.highShelf (rate, 2800.0, -12.0 + 20.0 * knob (pTreble), 0.8);
    presence.peak (rate, 4200.0, 0.9, -4.0 + 10.0 * knob (pPresence));
    outLp.lowPass (rate, 10000.0);

    level = dbToGain (value (pLevel)) * 0.09f;
}

void MarshallBox::processChunk (float* const* ch, int numCh, int n) noexcept
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
            float v = bright.run (inHp.run (u[i] * 2.0f, c), c) * pre;
            v = shape::triode (v);
            v = lp1.run (hp1.run (v, c), c) * 6.0f;
            v = shape::triode (-v);                     // each stage turns the signal over
            v = lp2.run (hp2.run (v, c), c) * 3.0f;
            v = shape::triode (-v - 0.25f) + shape::triode (0.25f);   // the cold-biased stage: uneven
            u[i] = lp3.run (hp3.run (v, c), c);
        }

        os.down (x, n, c);

        for (int i = 0; i < n; ++i)
        {
            float y = bass.run (tight.run (x[i], c), c);
            y = trebleShelf.run (middle.run (y, c), c);
            y = outLp.run (presence.run (y, c), c);
            x[i] = y * level;
        }
    }
}

} // namespace gigrig::fx
