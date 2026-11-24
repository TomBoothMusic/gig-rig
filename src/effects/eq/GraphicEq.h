#pragma once

#include "../Effect.h"

namespace gigrig::fx
{

/** "eq": a seven-band graphic EQ in the spirit of the Boss GE-7: sliders at 100, 200, 400 and
    800 Hz, 1.6, 3.2 and 6.4 kHz, each up or down 15 dB, and a Level slider. Tom uses it to
    trim the lows. All sliders at 0 dB passes the guitar through unchanged. */
class GraphicEq final : public Effect
{
public:
    GraphicEq();
    std::string typeId() const override     { return "eq"; }

    static constexpr int numBands = 7;
    static constexpr double bandHz[numBands] { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    std::array<int, numBands> pBand {};
    int pLevel = 0;
    std::array<Biquad, numBands> bands;
};

} // namespace gigrig::fx
