#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

namespace gigrig::fx
{

/** "ts": in the style of the Ibanez TS808 / TS9 Tube Screamer.

    How it works, as in the pedal: the clipping amp only boosts what is above about 720 Hz
    (the 4.7k resistor and 0.047 uF capacitor), and its diodes round that boosted part off.
    The clean signal is added back underneath, which gives the famous mid hump with a tight
    low end. Then a fixed 723 Hz roll-off and the tone control, then the level.

    Settings: drive, tone, level, plus "low end" (the 720 Hz corner moved down towards
    250 Hz for a fuller sound, a common modification; 0 = stock). */
class TubeScreamer final : public Effect
{
public:
    TubeScreamer();
    std::string typeId() const override             { return "ts"; }
    int latencySamples() const noexcept override    { return os.latencySamples(); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pDrive, pTone, pLevel, pLow;
    Oversampler os;
    OnePole clipHp, clipLp;     // inner rate
    OnePole fixedLp;            // base rate
    Biquad toneShelf, outHp;
    float level = 1.0f, gain = 1.0f;
};

} // namespace gigrig::fx
