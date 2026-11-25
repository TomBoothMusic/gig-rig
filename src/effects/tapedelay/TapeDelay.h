#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "tapedelay": a tape echo in the spirit of a Roland Space Echo or Echoplex, for vocals as
    much as guitar. The tape's speed wanders (Wow, slow; Flutter, fast), each repeat loses a
    little top and low end and picks up tape saturation; Age makes the tape darker and
    dirtier. Follow tempo works as on the digital delay; turning Time (or a tempo change)
    glides the tape speed, so the repeats bend in pitch as a real machine does. */
class TapeDelay final : public Effect
{
public:
    TapeDelay();
    std::string typeId() const override         { return "tapedelay"; }
    bool hasTail() const noexcept override              { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    double targetSamples() const noexcept;

    int pFollow, pDivision, pTime, pFeedback, pMix, pWow, pFlutter, pAge;
    DelayLine line;
    Lfo wow, flutter, drift;
    OnePole headLow, headHigh;
    double delayNow = 0.0;
    float lastAge = -1.0f;
};

} // namespace gigrig::fx
