#pragma once

// Runs a nonlinear stage (a drive's clipping) at 2x or 4x the sample rate, so the harmonics
// it makes above the top of hearing don't fold back down as harsh, out-of-tune whistles.
// Built from polyphase IIR half-band filters (the "HIIR" design by Laurent de Soras, from
// Valenzuela and Constantinides): cheap, and with only a few samples of delay.

#include "Effect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gigrig::fx
{

class HalfBand
{
public:
    static constexpr int numCoefs = 10;

    /** transition: width of the band between pass and stop, as a fraction of the (higher) rate. */
    static std::array<double, numCoefs> design (double transition = 0.04)
    {
        constexpr double pi = 3.14159265358979323846;
        const int order = numCoefs * 2 + 1;

        // Elliptic modulus and nome for the transition band.
        double k = std::tan ((1.0 - 2.0 * transition) * pi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e, e4 = e2 * e2;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

        std::array<double, numCoefs> c {};

        for (int index = 0; index < numCoefs; ++index)
        {
            const double cc = index + 1;
            double num = 0.0, den = 0.0, term = 0.0;
            int sign = 1;

            for (int i = 0; ; ++i, sign = -sign)
            {
                term = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * cc * pi / order) * sign;
                num += term;
                if (std::abs (term) < 1.0e-100 || i > 100) break;
            }

            sign = -1;

            for (int i = 1; ; ++i, sign = -sign)
            {
                term = std::pow (q, i * i) * std::cos (i * 2 * cc * pi / order) * sign;
                den += term;
                if (std::abs (term) < 1.0e-100 || i > 100) break;
            }

            num *= std::pow (q, 0.25);
            den += 0.5;
            const double ww = num / den, wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            c[(size_t) index] = (1.0 - x) / (1.0 + x);
        }

        return c;
    }

    void setCoefs (const std::array<double, numCoefs>& c) noexcept   { coef = c; }
    void clear() noexcept                                           { x = {}; y = {}; }

    /** One input sample in, two output samples out (at twice the rate). */
    void up (double in, double& out0, double& out1) noexcept
    {
        double even = in, odd = in;
        stages (even, odd);
        out0 = even;
        out1 = odd;
    }

    /** Two input samples in (at the higher rate), one out. */
    double down (double in0, double in1) noexcept
    {
        double a = in1, b = in0;
        stages (a, b);
        return 0.5 * (a + b);
    }

private:
    // First-order allpass sections, alternately on the two paths.
    void stages (double& a, double& b) noexcept
    {
        for (int i = 0; i < numCoefs; i += 2)
        {
            const double ta = (a - y[(size_t) i]) * coef[(size_t) i] + x[(size_t) i];
            x[(size_t) i] = a;
            y[(size_t) i] = ta;
            a = ta;

            const double tb = (b - y[(size_t) i + 1]) * coef[(size_t) i + 1] + x[(size_t) i + 1];
            x[(size_t) i + 1] = b;
            y[(size_t) i + 1] = tb;
            b = tb;
        }
    }

    std::array<double, numCoefs> coef {}, x {}, y {};
};

/** Up to 4x, one per channel. The factor is chosen so the inner rate is at least 176 kHz. */
class Oversampler
{
public:
    void prepare (double baseRate, int maxBlock)
    {
        factor = baseRate >= 88000.0 ? 2 : 4;
        const auto c1 = HalfBand::design (0.04), c2 = HalfBand::design (0.2);

        for (auto& s : st)
        {
            s.upA.setCoefs (c1); s.downA.setCoefs (c1);
            s.upB.setCoefs (c2); s.downB.setCoefs (c2);
        }

        buffer.assign ((size_t) (std::max (maxBlock, 256) * factor + 8), 0.0f);
        clear();
        latency = measureLatency();
        clear();
    }

    void clear() noexcept
    {
        for (auto& s : st)
        {
            s.upA.clear(); s.downA.clear(); s.upB.clear(); s.downB.clear();
        }
    }

    int getFactor() const noexcept          { return factor; }
    int latencySamples() const noexcept     { return latency; }

    /** Upsamples n samples of channel ch into the inner buffer and returns it (n * factor samples). */
    float* up (const float* in, int n, int ch) noexcept
    {
        auto& s = st[(size_t) ch];
        float* o = buffer.data();

        for (int i = 0; i < n; ++i)
        {
            double a, b;
            s.upA.up (in[i], a, b);

            if (factor == 2)
            {
                *o++ = (float) a;
                *o++ = (float) b;
            }
            else
            {
                double c, d;
                s.upB.up (a, c, d); *o++ = (float) c; *o++ = (float) d;
                s.upB.up (b, c, d); *o++ = (float) c; *o++ = (float) d;
            }
        }

        return buffer.data();
    }

    /** Takes the inner buffer back down into out (n samples). */
    void down (float* out, int n, int ch) noexcept
    {
        auto& s = st[(size_t) ch];
        const float* p = buffer.data();

        for (int i = 0; i < n; ++i)
        {
            if (factor == 2)
            {
                out[i] = (float) s.downA.down (p[0], p[1]);
                p += 2;
            }
            else
            {
                const double a = s.downB.down (p[0], p[1]);
                const double b = s.downB.down (p[2], p[3]);
                out[i] = (float) s.downA.down (a, b);
                p += 4;
            }
        }
    }

private:
    int measureLatency()
    {
        // Where a 1 kHz-ish click lands: the peak of an impulse through up and down.
        std::vector<float> impulse (256, 0.0f), out (256, 0.0f);
        impulse[0] = 1.0f;
        up (impulse.data(), 256, 0);
        down (out.data(), 256, 0);

        int best = 0;
        for (int i = 1; i < 256; ++i)
            if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) best]))
                best = i;

        return best;
    }

    struct State { HalfBand upA, downA, upB, downB; };
    std::array<State, maxChannels> st;
    std::vector<float> buffer;
    int factor = 2, latency = 0;
};

} // namespace gigrig::fx
