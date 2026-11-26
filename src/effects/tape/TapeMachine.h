#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "tape": the sound of a mix printed to a reel-to-reel tape machine, for the main output.
    Drive is how hot the tape is hit: low, it is nearly clean; higher, the peaks squash softly
    and the top squashes first, as on tape. Speed sets the machine's own tone: slower has a
    lower, fuller bump in the bass and a softer top; faster is cleaner and more open.

    How it works: the highs are lifted before the tape's rounding and cut back by the same
    amount after it (so quiet sounds come out as they went in, and bright loud ones are tamed),
    then the head's bass bump and the top roll-off. There is no wow and flutter: that needs
    a delay, and nothing in the mixer may add one. */
class TapeMachine final : public Effect
{
public:
    TapeMachine();
    std::string typeId() const override     { return "tape"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pDrive, pSpeed, pLevel;
    Biquad pre, de, bump, top;
    double in = 1.0, out = 1.0;
};

} // namespace gigrig::fx
