#pragma once

#include "../Effect.h"

#include <cstdint>
#include <vector>

namespace gigrig::fx
{

/** "whammy": a pitch-bending pedal in the spirit of the DigiTech Whammy. The Pedal setting is
    the treadle (heel 0, toe 100): in the Whammy modes it bends the whole guitar up or down to
    the chosen interval, in the Harmony modes it sweeps an added voice between two intervals
    under the dry guitar, and in the Detune modes it brings in a slightly sharp copy.

    How it works: the guitar is written to a short memory and read back faster or slower than
    it went in. A read point that runs out of memory has to hop; before each hop the effect
    finds how far back (or forward) the sound repeats itself and hops by exactly that, fading
    across, so single notes stay smooth. Chords warble a little, as on the pedal.

    With the pedal at the heel in a Whammy mode it is the guitar itself with no delay. */
class Whammy final : public Effect
{
public:
    Whammy();
    std::string typeId() const override                     { return "whammy"; }
    double cpuBudgetPercent() const noexcept override       { return 8.0; }

    /** The interval in semitones a mode gives at a pedal position (0..1). For checks and the screen. */
    static double semitones (int mode, double pedal) noexcept;
    static const std::vector<std::string>& modeNames();

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    float readAt (double delay) const noexcept;
    double findHop (double delay, bool earlier) const noexcept;
    void startFade (double toDelay, int length) noexcept;

    int pMode, pPedal, pGlide, pMix;

    std::vector<float> buf, dec;        // the memory, and a thinned-out copy for the quick search
    int mask = 0, decMask = 0;
    std::int64_t w = 0, dw = 0;         // samples written so far to each
    int decFactor = 12, decCount = 0;
    float decSum = 0.0f;

    double curDelay = 0.0, nextDelay = 0.0;
    bool fading = false;
    int fadePos = 0, fadeLen = 1, restCount = 0;

    double pos = 0.0;                   // the pedal after its glide, 0..1
    float dryGain = 0.0f, wetGain = 1.0f;

    // Lengths in samples at the prepared rate.
    int hopMin = 0, hopMax = 0, hopMid = 0, winBack = 0, winAhead = 0, fineWin = 0, fadeMin = 0, fadeMax = 0, downTrigger = 0, maxDelay = 0;
};

} // namespace gigrig::fx
