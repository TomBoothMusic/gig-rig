#pragma once

#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "univibe": the throbbing, swirling sound of a Uni-Vibe (Hendrix's "Machine Gun", and the
    rotary-speaker flavour Tom wants for songs like Lightning Crashes). The real pedal has a
    little lamp that pulses, and four light-sensitive cells around it that each pull a
    different phase-shift stage, so the four stages never move together: that uneven sweep
    is the sound, and it is what makes it different from a phaser.

    Here: four phase-shift stages with the real pedal's four capacitor values, swept by one
    pulsing "lamp" that brightens faster than it dims. Chorus mode mixes the swept sound with
    the dry guitar (the famous setting); Vibrato mode is the swept sound alone (pitch wobble).
    No delay. */
class UniVibe final : public Effect
{
public:
    UniVibe();
    std::string typeId() const override     { return "univibe"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pSpeed, pIntensity, pMode, pLevel;
    double phase = 0.0, lamp = 0.5;
    std::array<float, 4> state {};
    float modeMix = 1.0f;       // 1 = chorus (dry mixed in), 0 = vibrato: glides, so changing mode never clicks
};

} // namespace gigrig::fx
