#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "room": the sound of a room around an instrument, from a small booth to a hall. Eight
    delay lines of different lengths feed each other through a mixing matrix (a "feedback
    delay network"), each a little darker every time round, as sound is off walls.

    Reverb is the amount (the dry sound always stays at full level); Size picks the room
    (changing it fades the tail out and in over a few milliseconds, so it never clicks);
    Decay how long it rings within that size; Tone how bright; Low cut keeps it out of the bass.

    Mono, like every chain for now. No delay on the dry sound. */
class RoomReverb final : public Effect
{
public:
    RoomReverb();
    std::string typeId() const override                 { return "room"; }
    bool hasTail() const noexcept override              { return true; }
    double cpuBudgetPercent() const noexcept override   { return 8.0; }

    /** Seconds for the tail to fall 60 dB (for the checks and the docs). */
    static double decaySeconds (int size, float knob) noexcept;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void setSize (int size) noexcept;

    static constexpr int lines = 8;

    int pReverb, pSize, pDecay, pTone, pLowCut;
    std::array<DelayLine, lines> line;
    std::array<double, lines> length {};
    std::array<float, lines> lowState {};
    std::array<DelayLine, 2> diffuser;
    std::array<int, 2> diffuserLength {};
    DelayLine pre;
    int preLength = 1;
    OnePole lowCut;
    Lfo lfo;
    int sizeNow = -1;
    float duck = 1.0f;          // the tail's own fader: down and up again around a change of size
    float lastLowCut = -1.0f;
};

} // namespace gigrig::fx
