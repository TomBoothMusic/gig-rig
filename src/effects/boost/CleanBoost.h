#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "boost": a clean boost, in the style of a simple one-transistor booster (EP Booster,
    MXR Micro Amp). Makes the signal louder with a little colour, so it can push a drive or
    the amp harder. Bright lifts the top and trims the lows. Very large boosts round off
    gently instead of hard clipping. */
class CleanBoost final : public Effect
{
public:
    CleanBoost();
    std::string typeId() const override     { return "boost"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pBoost, pBright;
    Biquad high, low;
    float gain = 1.0f;
};

} // namespace gigrig::fx
