#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "buscomp": a mix-bus compressor in the spirit of the one in the centre of an SSL 4000
    desk, the "glue" that makes a mix sound like one thing. The same few controls: Threshold,
    Ratio (2, 4 or 10), stepped Attack and Release (with Auto), Make-up.

    Two additions the classic ones gained later: a low cut on what it listens to (so a kick or
    a stomp box does not make the whole mix duck) and Mix, to blend the squeezed mix with the
    untouched one. No delay. Its meter shows how far it is turning down. */
class BusComp final : public Effect
{
public:
    BusComp();
    std::string typeId() const override         { return "buscomp"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pThreshold, pRatio, pAttack, pRelease, pMakeup, pListenCut, pMix;
    OnePole listen;
    double env = 0.0, grFast = 0.0, grSlow = 0.0;
};

} // namespace gigrig::fx
