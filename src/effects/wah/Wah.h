#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "wah": a wah pedal in the spirit of a Dunlop Cry Baby (GCB95), with the things a player
    would otherwise mod the pedal for brought out as settings.

    The Pedal setting is the treadle (heel 0, toe 100). It moves one ringing peak from the
    Heel frequency to the Toe frequency (the length of the sweep), in equal musical steps, so
    the middle of the pedal is the middle of the sweep to the ear. Q sets how narrow and sharp
    the peak is; Peak depth sets how far it stands above the rest of the guitar.

    How it works: two "state variable" filters at the same frequency. One gives the peak (a
    band-pass, evened out so that Q changes its width and not its height). The other is a
    gentle low-pass that keeps the guitar's body below the peak and lets the top fall away
    above it, as the pedal does. Both stay smooth while the frequency moves.

    The pedal travels at the Sweep time, so a held switch can rock it (EffectsRack::sweepWah).
    No delay. */
class Wah final : public Effect
{
public:
    Wah();
    std::string typeId() const override     { return "wah"; }

    /** Where the peak sits for a pedal position (0..1). For checks and the screen. */
    static double peakHz (double heelHz, double toeHz, double pedal) noexcept;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pPedal, pHeel, pToe, pQ, pDepth, pLevel, pGlide;

    double pos = 0.0;                       // the pedal after its travel, 0..1
    double p1 = 0.0, p2 = 0.0;              // the peak filter's memory
    double b1 = 0.0, b2 = 0.0;              // the body filter's memory
};

} // namespace gigrig::fx
