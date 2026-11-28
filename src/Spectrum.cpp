#include "Spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace gigrig
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** What is the same for every picture at one window size: the window's shape and the turns of the transform. */
    struct Plan
    {
        int size = 0;
        std::vector<float> window;
        std::vector<std::complex<float>> turn;
        std::vector<int> swap;
        double windowPower = 0.0;

        void make (int n)
        {
            size = n;
            window.resize ((size_t) n);
            windowPower = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / n);       // a Hann window
                window[(size_t) i] = (float) w;
                windowPower += w * w;
            }

            turn.resize ((size_t) n / 2);
            for (int i = 0; i < n / 2; ++i)
                turn[(size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * i / n));

            int bits = 0;
            while ((1 << bits) < n) ++bits;

            swap.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                int r = 0;
                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b))
                        r |= 1 << (bits - 1 - b);
                swap[(size_t) i] = r;
            }
        }
    };

    /** The plain radix-2 transform, in place. */
    void transform (std::vector<std::complex<float>>& x, const Plan& plan)
    {
        const int n = plan.size;

        for (int i = 0; i < n; ++i)
            if (const int j = plan.swap[(size_t) i]; j > i)
                std::swap (x[(size_t) i], x[(size_t) j]);

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, stride = n / len;

            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const auto t = x[(size_t) (start + k + half)] * plan.turn[(size_t) (k * stride)];
                    const auto u = x[(size_t) (start + k)];
                    x[(size_t) (start + k)] = u + t;
                    x[(size_t) (start + k + half)] = u - t;
                }
        }
    }
}

double spectrumBandHz (int band) noexcept
{
    return spectrumLowHz * std::pow (2.0, (double) band / spectrumBandsPerOctave);
}

int spectrumWindow (double sampleRate) noexcept
{
    return sampleRate > 50000.0 ? 32768 : 16384;
}

std::vector<float> spectrumBands (const float* samples, double sampleRate, float floorDb)
{
    // Message thread only, so one plan kept between calls is enough.
    static Plan plan;
    static std::vector<std::complex<float>> work;
    static std::vector<double> power;

    const int n = spectrumWindow (sampleRate);

    if (plan.size != n)
    {
        plan.make (n);
        work.resize ((size_t) n);
        power.resize ((size_t) n / 2 + 1);
    }

    for (int i = 0; i < n; ++i)
        work[(size_t) i] = { samples[i] * plan.window[(size_t) i], 0.0f };

    transform (work, plan);

    for (int k = 0; k <= n / 2; ++k)
        power[(size_t) k] = k < n / 2 ? (double) std::norm (work[(size_t) k]) : 0.0;

    // Running totals, so a band's share is two look-ups (a part of a bin counts as that part).
    static std::vector<double> total;
    total.resize ((size_t) n / 2 + 2);
    total[0] = 0.0;
    for (int k = 0; k <= n / 2; ++k)
        total[(size_t) k + 1] = total[(size_t) k] + power[(size_t) k];

    const auto upTo = [&] (double bin)       // the power in bins below this point; bin k covers k - 0.5 to k + 0.5
    {
        const double x = std::clamp (bin + 0.5, 0.0, (double) (n / 2 + 1));
        const int whole = (int) x;
        return total[(size_t) whole] + (whole <= n / 2 ? (x - whole) * power[(size_t) whole] : 0.0);
    };

    const double binHz = sampleRate / n;
    const double edge = std::pow (2.0, 0.5 / spectrumBandsPerOctave);
    // A sine of height A puts A*A/4 x n x windowPower into the bins around it.
    const double scale = 4.0 / ((double) n * plan.windowPower);

    std::vector<float> bands ((size_t) spectrumBandCount, floorDb);

    for (int b = 0; b < spectrumBandCount; ++b)
    {
        const double hz = spectrumBandHz (b);
        double low = hz / edge / binHz, high = hz * edge / binHz;

        // A windowed sine spreads over about three bins: a band never looks at less than that.
        if (high - low < 3.0)
        {
            const double mid = 0.5 * (low + high);
            low = mid - 1.5;
            high = mid + 1.5;
        }

        if (low >= n / 2)
            continue;

        const double p = (upTo (high) - upTo (low)) * scale;
        bands[(size_t) b] = p > 1.0e-10 ? std::max (floorDb, (float) (10.0 * std::log10 (p))) : floorDb;
    }

    return bands;
}

} // namespace gigrig
