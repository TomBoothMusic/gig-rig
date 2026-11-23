#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

namespace gigrig::fx
{

/** "bd": in the style of the Boss BD-2 Blues Driver.

    Two gain stages, each clipping unevenly (so it adds even harmonics, like a small valve
    amp turned up), with more low end left in than a Tube Screamer and no big mid hump.
    At low gain it stays nearly clean and touch sensitive; dig in and it breaks up.
    Tone is a tilt: down = warmer, up = brighter. */
class BluesDriver final : public Effect
{
public:
    BluesDriver();
    std::string typeId() const override             { return "bd"; }
    int latencySamples() const noexcept override    { return os.latencySamples(); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pGain, pTone, pLevel;
    Oversampler os;
    OnePole inHp, mid1Lp, mid1Hp, mid2Lp, dcBlock;     // inner rate
    Biquad lowTilt, highTilt, scoop, outLp;            // base rate
    float g1 = 1.0f, g2 = 1.0f, level = 1.0f;
};

} // namespace gigrig::fx
