#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "analogdelay": a bucket-brigade analog delay in the spirit of a Boss DM-2 or MXR Carbon
    Copy, for slapback and short echoes. Each repeat comes back darker and a little saturated,
    with a gentle wobble (Mod). Not synced to the tempo: Tom sets it by ear (design doc). The
    dry guitar always passes at full level; Mix sets how loud the repeats are.

    Turning Time while playing glides the repeats in pitch, as on the real pedal. */
class AnalogDelay final : public Effect
{
public:
    AnalogDelay();
    std::string typeId() const override         { return "analogdelay"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pTime, pFeedback, pMix, pTone, pMod;
    DelayLine line;
    Lfo lfo;
    OnePole inLow, fbLow;
    OnePole fbHigh;
    double delayNow = 0.0;      // samples, slewed toward the knob
    float lastTone = -1.0f;
};

} // namespace gigrig::fx
