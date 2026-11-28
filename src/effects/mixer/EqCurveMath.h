#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace gigrig::fx
{

/** The mixer EQ's curve, worked out with sums instead of sound (plain C++, no JUCE).

    Two screens draw the EQ as a curve: the phone and tablet page (remote/eq-curve.js, which has
    these same sums written in JavaScript) and the laptop's Mixer tab (src/EqCurveView.cpp, which
    uses this file). The check "the curve the screen draws" in MixerBlockChecks.cpp holds the real
    EQ (MixEq in MixerBlocks.cpp) to this file, so KEEP ALL THREE IN STEP: if MixEq changes, that
    check fails, and this file and remote/eq-curve.js must both follow. */
struct EqCurveValues
{
    double lowcut = 20.0, highcut = 20000.0;
    double lsf = 100.0, lsg = 0.0, hsf = 8000.0, hsg = 0.0;
    std::array<double, 4> bf { 250.0, 800.0, 2500.0, 6000.0 }, bg { 0.0, 0.0, 0.0, 0.0 }, bq { 1.0, 1.0, 1.0, 1.0 };
};

/** How much the whole EQ lifts or cuts one frequency, in dB. */
inline double eqCurveDb (const EqCurveValues& v, double hz, double rate)
{
    const double pi = 3.14159265358979323846, shelfK = 1.0 / 0.7071;
    struct Sec { double g, k, m0, m1, m2; };

    const auto warp = [&] (double f) { return std::tan (pi * std::clamp (f, 5.0, rate * 0.45) / rate); };
    const auto amp = [] (double db) { return std::pow (10.0, db / 40.0); };
    const auto dbOf = [&] (const Sec& sec)
    {
        const double w = std::tan (pi * std::min (hz, rate * 0.499) / rate) / sec.g;
        const double dr = 1.0 - w * w, di = sec.k * w;
        const double nr = sec.m0 * dr + sec.m2, ni = (sec.m0 * sec.k + sec.m1) * w;
        return 10.0 * std::log10 (std::max (1.0e-12, (nr * nr + ni * ni) / (dr * dr + di * di)));
    };

    // "20 Hz" on the low cut and "20 kHz" on the high cut mean off: the cut moves out of what can be heard.
    double sum = dbOf ({ warp (v.lowcut * std::clamp (0.25 + 0.075 * (v.lowcut - 20.0), 0.25, 1.0)), shelfK, 1.0, -shelfK, -1.0 });
    sum += dbOf ({ warp (v.highcut * (1.0 + std::max (0.0, (v.highcut - 18000.0) / 2000.0))), shelfK, 0.0, 0.0, 1.0 });

    { const double a = amp (v.lsg); sum += dbOf ({ warp (v.lsf) / std::sqrt (a), shelfK, 1.0, shelfK * (a - 1.0), a * a - 1.0 }); }
    { const double a = amp (v.hsg); sum += dbOf ({ warp (v.hsf) * std::sqrt (a), shelfK, a * a, shelfK * (1.0 - a) * a, 1.0 - a * a }); }

    for (size_t b = 0; b < 4; ++b)
    {
        const double a = amp (v.bg[b]), k = 1.0 / (v.bq[b] * a);
        sum += dbOf ({ warp (v.bf[b]), k, 1.0, k * (a * a - 1.0), 0.0 });
    }

    return sum;
}

} // namespace gigrig::fx
