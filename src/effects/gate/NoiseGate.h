#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "gate": a noise gate, in the spirit of a Boss NS-2 or ISP Decimator: when the guitar stops,
    the hiss and hum of the drives in front of it are turned down; when a note starts, it opens
    in about a millisecond.

    How it works: it follows the level of what comes in (fast up, slower down). Above the
    threshold it opens; it closes only once the level has dropped 6 dB below the threshold and
    stayed there for the hold time, so a note's tail doesn't chatter. Opening and closing are
    smooth fades, never a step.

    Settings: threshold, release (how slowly it closes), depth (how far it turns down when
    closed; full is silence). Its meter shows how far it is turning down now. */
class NoiseGate final : public Effect
{
public:
    NoiseGate();
    std::string typeId() const override         { return "gate"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pThreshold, pRelease, pDepth;
    double env = 0.0, gain = 1.0;
    int holdLeft = 0;
    bool open = false;
    double envUp = 0.0, envDown = 0.0, attackStep = 0.0;
};

} // namespace gigrig::fx
