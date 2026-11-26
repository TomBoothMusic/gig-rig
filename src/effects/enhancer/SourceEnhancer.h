#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "enhancer": a source enhancer for a microphone, in the spirit of the Rupert Neve Portico
    5045 (and the "source extractor" on big desks). It is the gentle alternative to a gate on
    a voice: while the singer sings the channel is untouched; between phrases it is turned
    down by a set amount (Depth, a few dB), never shut. So the stage spill and the feedback
    that live in the gaps drop, and nothing chatters or cuts a word off.

    How it decides: it listens to the middle of the sound (where a voice is, not the kick or
    the cymbals), and when that falls under Threshold it eases the level down, 2 dB for every
    dB under, until Depth is reached. It opens again in a few milliseconds and closes at the
    pace Time sets (A fastest to F slowest), after a short hold so the ends of words are kept.
    RMS follows loudness as the ear does; Peak reacts to the sharpest sounds.

    No delay. Its meter shows how far it is turning down. */
class SourceEnhancer final : public Effect
{
public:
    SourceEnhancer();
    std::string typeId() const override         { return "enhancer"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pThreshold, pDepth, pTime, pMode;
    double hp = 0.0, lp = 0.0, power = 0.0, peak = 0.0;     // what it listens to
    double reduction = 0.0;                                 // dB it is turning down now
    int hold = 0;
};

} // namespace gigrig::fx
