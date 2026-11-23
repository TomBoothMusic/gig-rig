#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

namespace gigrig::fx
{

/** "fuzz": in the style of the Dallas Arbiter Fuzz Face (two germanium transistors).

    How it works, as in the pedal: two transistor stages with a lot of gain, so the guitar is
    squared off hard; the stages clip unevenly, which gives the round, woolly, slightly
    "spitty" sound rather than a buzzy one. The low input coupling keeps the low end thick.

    Settings: fuzz and volume (the pedal's two knobs), plus bias (5 = a healthy pedal;
    lower = the starved, sputtering "dying battery" sound with a gate on note tails) and
    tone (5 = stock; up for brighter, down for darker; the real pedal has none).

    Not copied: the way a real Fuzz Face cleans up when the guitar's volume is turned down,
    which depends on the guitar's pickups being wired straight into it. */
class Fuzz final : public Effect
{
public:
    Fuzz();
    std::string typeId() const override             { return "fuzz"; }
    int latencySamples() const noexcept override    { return os.latencySamples(); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pFuzz, pVolume, pBias, pTone;
    Oversampler os;
    OnePole inHp, inLp, midHp;      // inner rate
    Biquad outLp, tilt, dcBlock;    // base rate
    float pre = 1.0f, offset = 0.0f, deadZone = 0.0f, rest1 = 0.0f, level = 1.0f;
};

} // namespace gigrig::fx
