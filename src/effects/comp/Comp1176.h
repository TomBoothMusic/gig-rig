#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "comp": a studio FET compressor in the spirit of the 1176 Rev E "blackface", the one Tom
    knows from the Purified Audio 1176.

    Like the real one, the threshold is fixed: turning Input up pushes the guitar harder into
    the compressor, and Output sets the level after it. Attack and Release are numbered 1 to 7
    as on the front panel, and 7 is the FASTEST (attack 20 microseconds to 800, release 50 ms
    to 1.1 s). Ratio buttons 4, 8, 12, 20 and "All" (all four buttons in: harder, with the
    aggressive, slightly dirty character that trick is known for). Mix blends the dry guitar
    back in (parallel compression); 100% is the plain 1176.

    The release is program dependent, as on the hardware: it lets go quickly after a short
    peak and more slowly after a long squeeze. A little FET colour comes in with the gain
    reduction. Its meter shows the gain reduction in dB, like the 1176's VU in GR mode. */
class Comp1176 final : public Effect
{
public:
    Comp1176();
    std::string typeId() const override         { return "comp"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pInput, pOutput, pAttack, pRelease, pRatio, pMix;
    double env = 0.0;          // detected level, linear
    double grFast = 0.0, grSlow = 0.0;   // gain reduction in dB (positive = turned down), two release stages
    double meterDb = 0.0;
    std::array<double, maxChannels> dcX {}, dcY {};
    double dcCoeff = 0.0;
};

} // namespace gigrig::fx
