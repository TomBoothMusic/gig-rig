#pragma once

#include "../Effect.h"

#include <array>

namespace gigrig::fx
{

/** "peq": a channel EQ for a voice or an acoustic guitar, the kind a mixer has on every
    channel: a low cut, a low shelf, two sweepable mid bands, a high shelf and a high cut,
    then a level.

    Low cut at 20 Hz and High cut at 20 kHz are as good as off. The two mid bands share one
    Width (low = a narrow notch for a honk or a feedback note, high = a broad, gentle tilt).

    Built from "state variable" filters (the Zavalishin / Simper kind), which stay smooth
    while their frequency and gain move, so sweeping a band while playing never clicks.
    No delay. */
class ChannelEq final : public Effect
{
public:
    ChannelEq();
    std::string typeId() const override     { return "peq"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    struct Svf
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
        void set (double g, double k) noexcept
        {
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void highPass (double rate, double hz, double q) noexcept;
        void lowPass (double rate, double hz, double q) noexcept;
        void bell (double rate, double hz, double q, double db) noexcept;
        void lowShelf (double rate, double hz, double db) noexcept;
        void highShelf (double rate, double hz, double db) noexcept;
    };

    int pLowCut, pLow, pLmf, pLmg, pHmf, pHmg, pWidth, pHigh, pHighCut, pLevel;
    Svf lowCut, lowShelf, lowMid, highMid, highShelf, highCut;
};

} // namespace gigrig::fx
