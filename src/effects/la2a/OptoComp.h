#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "la2a": a compressor in the spirit of the Teletronix LA-2A, the slow, smooth optical
    levelling amplifier used on voices and bass. Two knobs, as on the real one: Peak reduction
    (how much it turns down) and Gain (to bring the level back up), plus Compress or Limit.

    What makes it sound the way it does is the light cell: it reacts in about 10 ms, lets go
    of half the squeeze in about 60 ms and the rest over seconds, and the longer it has been
    squeezing the slower that last part is. So it rides a voice instead of grabbing at it.
    A little valve-style rounding on the way out. No delay. Its meter shows how far it is
    turning down. */
class OptoComp final : public Effect
{
public:
    OptoComp();
    std::string typeId() const override         { return "la2a"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pReduction, pGain, pMode;
    double fast = 0.0, slow = 0.0;      // the cell's two memories, in dB of squeeze
};

} // namespace gigrig::fx
