#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "chorus": an analog-style chorus in the spirit of a Boss CE-2: two gently swept, slightly
    dark copies mixed with the dry guitar. */
class Chorus final : public Effect
{
public:
    Chorus();
    std::string typeId() const override         { return "chorus"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pSpeed, pDepth, pMix;
    DelayLine line;
    Lfo lfo;
    OnePole dark;
};

} // namespace gigrig::fx
