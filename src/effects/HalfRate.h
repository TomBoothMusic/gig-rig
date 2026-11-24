#pragma once

// Runs part of an effect at half the sample rate when the app runs at 88.2 or 96 kHz, so
// heavy work (a long response file, a pitch tracker) costs half as much. The sound goes down
// to half the rate and back up through the same half-band filters the drives use, which
// costs a few samples of delay (latencySamples() says how many). At 44.1 or 48 kHz it is a
// straight wire with no delay. The amp model (amp/NamAmp) has its own copy of this idea.

#include "Oversampler.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gigrig::fx
{

class HalfRate
{
public:
    /** Most samples per call (effects process at most 16 at a time). */
    static constexpr int maxChunk = 16;

    void prepare (double rate)
    {
        factor = rate >= 88000.0 ? 2 : 1;
        const auto c = HalfBand::design (0.04);
        down.setCoefs (c);
        up.setCoefs (c);
        clear();

        // The delay of the trip down and back up, measured with a click through a straight wire.
        std::array<float, 64> y {};
        y[0] = 1.0f;
        for (int i = 0; i < 64; i += maxChunk)
            process (y.data() + i, maxChunk, [] (float*, int) {});
        int peakAt = 0;
        for (int i = 1; i < 64; ++i)
            if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) peakAt]))
                peakAt = i;
        latency = factor == 2 ? peakAt : 0;
        clear();
    }

    void clear() noexcept
    {
        down.clear();
        up.clear();
        haveCarry = false;
        carry = 0.0f;
        here.fill (0.0f);
        hereCount = factor == 2 ? 1 : 0;    // one sample of slack, so an odd-sized chunk always has one to give
    }

    int latencySamples() const noexcept     { return latency; }
    double innerRate (double rate) const noexcept { return rate / factor; }
    int getFactor() const noexcept          { return factor; }

    /** x: n samples in, n samples out, in place (n at most maxChunk). inner (buffer, m) works
        in place on the m samples at the inner rate (m is about n / 2 at 96 kHz, n at 48 kHz). */
    template <typename Inner>
    void process (float* x, int n, Inner&& inner) noexcept
    {
        if (factor == 1)
        {
            inner (x, n);
            return;
        }

        int m = 0;
        for (int i = 0; i < n; ++i)
        {
            if (! haveCarry) { carry = x[i]; haveCarry = true; }
            else             { low[(size_t) m++] = (float) down.down (carry, x[i]); haveCarry = false; }
        }

        inner (low.data(), m);

        for (int i = 0; i < m; ++i)
        {
            double a, b;
            up.up (low[(size_t) i], a, b);
            here[(size_t) hereCount++] = (float) a;
            here[(size_t) hereCount++] = (float) b;
        }

        const int give = std::min (n, hereCount);
        std::copy (here.begin(), here.begin() + give, x);
        std::fill (x + give, x + n, 0.0f);
        std::copy (here.begin() + give, here.begin() + hereCount, here.begin());
        hereCount -= give;
    }

private:
    int factor = 1, latency = 0, hereCount = 0;
    HalfBand down, up;
    float carry = 0.0f;
    bool haveCarry = false;
    std::array<float, maxChunk> low {};
    std::array<float, 2 * maxChunk + 4> here {};
};

} // namespace gigrig::fx
