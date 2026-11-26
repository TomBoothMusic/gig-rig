#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "warmth": a valve microphone preamp's colour for a mixer channel, in the spirit of the
    warm preamp models on big desks. Quiet sounds pass almost untouched; as Drive comes up the
    peaks round off and harmonics appear, with a little more weight in the low mids and a
    slightly softer top. Colour moves it from even rounding (tape-like) towards the lopsided
    rounding of a valve, which adds the octave above. Level makes up what Drive took.

    No oversampling, so no delay: the rounding is gentle enough not to need it. */
class Warmth final : public Effect
{
public:
    Warmth();
    std::string typeId() const override     { return "warmth"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pDrive, pColour, pLevel;
    Biquad low, high;
    OnePole dc;
    double k = 0.5, bias = 0.0, offset = 0.0, norm = 1.0, out = 1.0;
};

} // namespace gigrig::fx
