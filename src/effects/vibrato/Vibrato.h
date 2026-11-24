#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "vibrato": true pitch vibrato (a gently wobbling delay), in the spirit of a Boss VB-2 or
    the vibrato channel of a Magnatone. Only the wobbled sound comes out, no dry. */
class Vibrato final : public Effect
{
public:
    Vibrato();
    std::string typeId() const override         { return "vibrato"; }
    int latencySamples() const noexcept override { return centre; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pSpeed, pDepth;
    DelayLine line;
    Lfo lfo;
    int centre = 0;     // samples: the shortest delay of the wobble (what it reports)
};

} // namespace gigrig::fx
