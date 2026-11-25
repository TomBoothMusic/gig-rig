#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "plate": a plate reverb for vocals (and anything else that wants a smooth, bright tail
    with no slap-back), after Jon Dattorro's well-known plate design ("Effect Design, part 1",
    1997): the sound is smeared by four short all-pass filters, then goes round a figure-of-
    eight of two delay loops that feed each other, a little wobbled so it never rings on one note.

    Reverb is the amount (the dry sound always stays at full level); Decay how long it rings
    (about 0.4 to 8 seconds); Pre-delay the gap before the reverb starts, which keeps words
    clear; Tone how bright the tail is; Low cut keeps the reverb out of the bass.

    Mono, like every chain for now. No delay on the dry sound. */
class PlateReverb final : public Effect
{
public:
    PlateReverb();
    std::string typeId() const override                 { return "plate"; }
    bool hasTail() const noexcept override              { return true; }
    double cpuBudgetPercent() const noexcept override   { return 8.0; }

    /** Seconds for the tail to fall 60 dB at a Decay setting (for the checks and the docs). */
    static double decaySeconds (float knob) noexcept    { return 0.4 * std::pow (20.0, (double) knob / 10.0); }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    struct AllPass
    {
        DelayLine line;
        int length = 1;
        float run (float x, float g) noexcept
        {
            const float d = line.readWhole (length - 1);
            const float v = x - g * d;
            line.write (v);
            return d + g * v;
        }
    };

    int pReverb, pDecay, pPre, pTone, pLowCut;
    double scale = 1.0;                         // delay lengths were written for 29761 Hz
    DelayLine pre;
    double preNow = 0.0;
    OnePole bandwidth, lowCut;
    std::array<OnePole, 2> damp;
    std::array<AllPass, 4> inAp;
    std::array<DelayLine, 2> modAp;             // the two wobbled all-passes in the loops
    std::array<double, 2> modLen {};
    std::array<DelayLine, 2> d1, d2;            // each loop's two delays
    std::array<int, 2> d1Len {}, d2Len {};
    std::array<AllPass, 2> ap2;
    std::array<float, 2> loopOut {};
    std::array<int, 14> tap {};
    Lfo lfo;
    float lastTone = -1.0f, lastLowCut = -1.0f;
};

} // namespace gigrig::fx
