#pragma once

#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "body": puts the wooden body back into an acoustic guitar's pickup sound, in the spirit
    of the TC Electronic BodyRez (one knob). A pickup under the saddle hears the strings but
    little of the box: it is thin in the low mids, hard and "quacky" between 1 and 3 kHz
    when you dig in, and glassy on top. A microphone in front of the guitar hears the opposite.

    Body 0 to 10 blends in three things together:
      - the box's two main resonances (about 110 and 220 Hz) lifted,
      - the quack region (1.2 and 2.8 kHz) eased down, and
      - the quack tamed further only on hard-played notes (a fast, gentle squeeze of the
        2 kHz region alone, which is what makes a pickup sound like a pickup).
    Flip phase turns the signal upside down, which often stops a low feedback howl on stage.

    This is Claude's own voicing from what a pickup lacks, NOT a measured copy of the pedal:
    for a measured copy, capture the real BodyRez and play it with the "ir" effect.
    The filters are fixed and only their amounts change, so the knob never clicks. No delay. */
class AcousticBody final : public Effect
{
public:
    AcousticBody();
    std::string typeId() const override     { return "body"; }
    bool hasMeter() const noexcept override { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    static constexpr int numBands = 5;

    int pBody, pPhase, pLevel;
    std::array<Biquad, numBands> bands;
    Biquad quack, quackOut;         // the region the squeeze listens to (before the curve) and turns down (after it)
    double env = 0.0;
    float gainNow = 1.0f, glide = 0.05f;
    float polarity = 1.0f;
    double attack = 0.0, release = 0.0;
};

} // namespace gigrig::fx
