#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "tremolo": the volume pulses, as in a Fender amp's tremolo (the Dream '65 calls it
    "vibrato"). Speed and Intensity as on the amp; Shape goes from a smooth, round pulse (0)
    to a choppy, almost on-off one (10). Goes after the amp, as on a Deluxe Reverb. */
class Tremolo final : public Effect
{
public:
    Tremolo();
    std::string typeId() const override         { return "tremolo"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pSpeed, pIntensity, pShape;
    Lfo lfo;
};

} // namespace gigrig::fx
