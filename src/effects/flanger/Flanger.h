#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "flanger": the jet-plane sweep (BF-2 style). A copy of the sound a few milliseconds
    late is mixed back with the original while that gap sweeps slowly, and some of the copy
    is fed round again (Resonance) for the metallic ring.

    Speed and Depth set the sweep, Manual where it sits (low = long gap, deeper and more
    hollow), Resonance the ring, Mix how much of the copy (50% is the strongest flange).
    No delay on the dry sound. */
class Flanger final : public Effect
{
public:
    Flanger();
    std::string typeId() const override     { return "flanger"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pSpeed, pDepth, pManual, pRes, pMix;
    DelayLine line;
    OnePole dark, fbHigh;
    Lfo lfo;
    float last = 0.0f;
};

} // namespace gigrig::fx
