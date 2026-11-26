#pragma once

// The mixer's own processing blocks (built 26 Nov 2026, see docs/mixer.md). They follow the
// same Effect interface as every pedal-style effect, so the standard checks in
// EffectChecks.cpp run on them and a Chain can hold them, but they are listed apart
// (mixerEffectTypes() in Registry.cpp) so the Effects tab does not offer them: the design
// keeps guitar-style effects out of the mixer and mixer blocks out of the pedal chains.
//
// Their ids and setting ids are saved in act, venue, monitor and output presets on Tom's
// disk: never rename them, only add.
//
// None of them adds delay (the line delay adds exactly the delay it is set to, on purpose).

#include "../Effect.h"

#include <array>
#include <vector>

namespace gigrig::fx
{

/** A "state variable" filter (the Zavalishin / Simper kind): stays smooth while its
    frequency and gain move, so sweeping a band while playing never clicks. */
struct MixSvf
{
    double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    std::array<double, maxChannels> s1 {}, s2 {};

    float run (float x, int c) noexcept
    {
        const auto i = (size_t) c;
        const double v3 = x - s2[i];
        const double v1 = a1 * s1[i] + a2 * v3;
        const double v2 = s2[i] + a2 * s1[i] + a3 * v3;
        s1[i] = 2.0 * v1 - s1[i];
        s2[i] = 2.0 * v2 - s2[i];
        return (float) (m0 * x + m1 * v1 + m2 * v2);
    }

    void clear() noexcept   { s1 = {}; s2 = {}; }
    void flat() noexcept    { m0 = 1.0; m1 = 0.0; m2 = 0.0; }

    void highPass  (double rate, double hz, double q) noexcept;
    void lowPass   (double rate, double hz, double q) noexcept;
    void bell      (double rate, double hz, double q, double db) noexcept;
    void lowShelf  (double rate, double hz, double db) noexcept;
    void highShelf (double rate, double hz, double db) noexcept;

private:
    void set (double g, double k) noexcept
    {
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

//==============================================================================
/** "mixeq": the mixer's parametric EQ, on every channel and every output. A low cut, a low
    shelf, four fully sweepable bells (frequency, gain and width each), a high shelf and a
    high cut: the CQ12T's layout with the Wing's extra bands.

    Low cut at 20 Hz and High cut at 20 kHz are as good as off. */
class MixEq final : public Effect
{
public:
    MixEq();
    std::string typeId() const override     { return "mixeq"; }

    static constexpr int numBells = 4;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pLowCut, pHighCut, pLsf, pLsg, pHsf, pHsg;
    std::array<int, numBells> pF {}, pG {}, pQ {};
    MixSvf lowCut, lowShelf, highShelf, highCut;
    std::array<MixSvf, numBells> bell;
};

//==============================================================================
/** "mixcomp": a clean channel and output compressor, set the way a mixer's is: threshold,
    ratio, attack, release, knee and make-up gain. On two channels it turns both down
    together, so a stereo picture does not wander. Its meter shows how far it is turning
    down now. */
class MixComp final : public Effect
{
public:
    MixComp();
    std::string typeId() const override         { return "mixcomp"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pThreshold, pRatio, pAttack, pRelease, pKnee, pMakeup;
    double env = 0.0, gr = 0.0;
};

//==============================================================================
/** "notches": six narrow cuts for feedback notes and room booms, on an output. Each has a
    frequency and a depth; one Width sets how narrow they all are. A notch at 0 dB does nothing. */
class Notches final : public Effect
{
public:
    Notches();
    std::string typeId() const override     { return "notches"; }

    static constexpr int count = 6;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    std::array<int, count> pF {}, pCut {};
    int pWidth;
    std::array<MixSvf, count> notch;
};

//==============================================================================
/** "limiter": the last thing on an output. Nothing leaves louder than the Ceiling, ever:
    it turns down the instant a peak arrives (no look-ahead, so no added delay) and lets go
    over the Release time. Set just under where the speaker's own limiter starts, it catches
    the peaks first. Its meter shows how far it is turning down now. */
class Limiter final : public Effect
{
public:
    Limiter();
    std::string typeId() const override         { return "limiter"; }
    bool hasMeter() const noexcept override     { return true; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pCeiling, pRelease;
    double gain = 1.0;
};

//==============================================================================
/** "linedelay": holds an output back by a set time, to line a far speaker up with the
    near ones (the CQ12T offers up to 682 ms; this goes to 700). At 0 ms it is a straight wire.
    Changing the time fades from the old delay to the new one over 20 ms. */
class LineDelay final : public Effect
{
public:
    LineDelay();
    std::string typeId() const override     { return "linedelay"; }
    int latencySamples() const noexcept override;

    static constexpr double maxMs = 700.0;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pMs;
    std::array<std::vector<float>, maxChannels> line;
    int size = 0, writeAt = 0, delayNow = 0, delayNext = 0;
    bool fading = false, primed = false;
    float fade = 0.0f, fadeStep = 0.0f;
};

//==============================================================================
/** "wetfilter": a low cut and a high cut for the wet sound of a mixer reverb or delay, so a
    clearly audible effect on a voice stays clean (no rumble, no hiss of esses). 20 Hz and
    20 kHz are as good as off. */
class WetFilter final : public Effect
{
public:
    WetFilter();
    std::string typeId() const override     { return "wetfilter"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    int pLowCut, pHighCut;
    MixSvf lowCut, highCut;
};

} // namespace gigrig::fx
