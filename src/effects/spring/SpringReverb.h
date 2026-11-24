#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "spring": a spring reverb tank in the spirit of a Fender Deluxe Reverb's (and the Dream
    '65's): two springs, each a long chain of all-pass filters (the "drip": high notes arrive
    before low ones) inside a feedback loop, darker as it rings. Reverb is the amount, as on
    the amp; Decay how long it rings; Tone how bright; Dwell how hard the tank is driven
    (higher splashes more). Goes after the amp. */
class SpringReverb final : public Effect
{
public:
    SpringReverb();
    std::string typeId() const override         { return "spring"; }
    double cpuBudgetPercent() const noexcept override { return 8.0; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    static constexpr int numSprings = 2, stages = 24;

    struct Spring
    {
        std::array<float, stages> ap {};    // all-pass states
        DelayLine line;
        OnePole low;
        double length = 0.0;                // samples
        float last = 0.0f;
    };

    int pReverb, pDecay, pTone, pDwell;
    std::array<Spring, numSprings> springs;
    OnePole drive, outHigh;
    Lfo wobble;
    float lastTone = -1.0f;
};

} // namespace gigrig::fx
