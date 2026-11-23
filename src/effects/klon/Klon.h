#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

namespace gigrig::fx
{

/** "klon": in the style of the Klon Centaur ("klone" boosts).

    The Klon mixes a clean copy of the guitar with a driven copy that has germanium diodes
    to ground. The Gain knob is two knobs on one shaft: turning it up brings in more of the
    driven copy and drives it harder, while the clean copy drops back a little. So at low
    gain it is a clean, slightly fattened boost, and the drive arrives smoothly as it turns.
    Treble is an active treble control; Output is the level. */
class Klon final : public Effect
{
public:
    Klon();
    std::string typeId() const override             { return "klon"; }
    int latencySamples() const noexcept override    { return os.latencySamples(); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pGain, pTreble, pOutput;
    Oversampler os;
    Biquad clipHp;                  // inner rate
    OnePole clipLp, dcBlock;        // inner rate
    Biquad treble, outLp;           // base rate
    float clean = 1.0f, drive = 1.0f, blend = 1.0f, level = 1.0f;
};

} // namespace gigrig::fx
