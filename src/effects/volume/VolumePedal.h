#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "volume": a volume pedal as its own effect, so it can sit anywhere in a chain. Tom keeps
    his between the drives and the amp: backing it off there cleans the amp up as well as
    turning it down, and because it is in the chain it is before the looper, the mixer and
    every send to front of house.

    Pedal is the treadle (heel 0, toe 100). Heel level is how loud the heel is (all the way
    down is silence). The travel follows the ear, like an audio-taper pot: half way sounds
    about half as loud. The level glides per sample, so a swell never zips. No delay.

    Reversed turns it round (heel loud, toe silent). Tom's one pedal is a blend: the electric
    chain has this effect as it is, the acoustic chain has it reversed, and both follow the
    same pedal, so toe down is all electric and heel down is all acoustic. With Travel set to
    Blend on both, the two together stay the same loudness all the way.

    A real expression pedal should set `pedal`, as for the wah and the whammy. */
class VolumePedal final : public Effect
{
public:
    VolumePedal();
    std::string typeId() const override     { return "volume"; }

    /** The gain for a pedal position (0..100) and a heel level in dB. For checks and the screen. */
    static double gainFor (double pedal, double heelDb, bool blend = false) noexcept;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pPedal, pHeel, pReverse, pCurve;
    float gain = 1.0f;
    bool primed = false;
};

} // namespace gigrig::fx
