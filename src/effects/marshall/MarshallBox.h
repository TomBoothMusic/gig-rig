#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

namespace gigrig::fx
{

/** "marshall": a "Marshall in a box" drive, in the style of a cranked JCM800 / Plexi
    preamp (the pedals that copy it: Friedman BE-OD, Wampler Plexi-Drive, Suhr Riot...).

    Three valve-style gain stages one after another, with the filtering between them that
    keeps the lows tight, then a Marshall-style tone stack (bass, middle, treble) and
    presence. It is a preamp, so it is meant to go into an amp or amp model, as Tom's will
    go into the Dream '65. */
class MarshallBox final : public Effect
{
public:
    MarshallBox();
    std::string typeId() const override             { return "marshall"; }
    int latencySamples() const noexcept override    { return os.latencySamples(); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void update();

    int pGain, pBass, pMiddle, pTreble, pPresence, pLevel;
    Oversampler os;
    OnePole inHp, hp1, lp1, hp2, lp2, hp3, lp3;     // inner rate
    Biquad bright;                                  // inner rate
    Biquad bass, middle, trebleShelf, presence, outLp, tight;   // base rate
    float pre = 1.0f, level = 1.0f;
};

} // namespace gigrig::fx
