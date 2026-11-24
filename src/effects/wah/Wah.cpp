#include "Wah.h"

#include <algorithm>

namespace gigrig::fx
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

double Wah::peakHz (double heelHz, double toeHz, double pedal) noexcept
{
    return heelHz * std::pow (toeHz / heelHz, std::clamp (pedal, 0.0, 1.0));
}

Wah::Wah()
{
    // The pedal travels on its own (Sweep time), so no glide from the base class.
    pPedal = addParam ("pedal", "Pedal (heel to toe)", "%", 0.0f, 100.0f, 0.0f, 0.0f);
    pHeel  = addParam ("heel",  "Heel frequency", "Hz", 200.0f, 900.0f, 400.0f);
    pToe   = addParam ("toe",   "Toe frequency", "Hz", 1000.0f, 4000.0f, 2200.0f);
    pQ     = addParam ("q",     "Q (peak sharpness)", "", 1.0f, 15.0f, 6.0f);
    pDepth = addParam ("depth", "Peak depth", "dB", 0.0f, 24.0f, 16.0f);
    pLevel = addParam ("level", "Level", "dB", -12.0f, 12.0f, 0.0f);
    pGlide = addParam ("glide", "Sweep time", "ms", 20.0f, 2000.0f, 30.0f, 0.0f);
}

void Wah::onPrepare (double, int)
{
    onReset();
}

void Wah::onReset() noexcept
{
    pos = value (pPedal) * 0.01;
    p1 = p2 = b1 = b2 = 0.0;
}

void Wah::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double target = value (pPedal) * 0.01;
    const double slew = 1.0 / std::max (1.0, value (pGlide) * 0.001 * rate);    // heel to toe in "Sweep time"
    const double heel = value (pHeel), toe = value (pToe);
    const double span = std::log (toe / heel);
    const double kPeak = 1.0 / std::max (0.5, (double) value (pQ));
    constexpr double kBody = 1.41421356;                                        // a gentle corner, no bump of its own

    // The peak comes up and the body comes down as Peak depth rises, so the pedal stays
    // about as loud as the guitar going in. Peak above body = Peak depth.
    const double depthDb = value (pDepth);
    const double level = dbToGain (value (pLevel));
    const double bodyGain = level * std::pow (10.0, -0.35 * depthDb / 20.0);
    const double peakGain = level * std::pow (10.0, 0.65 * depthDb / 20.0);

    for (int i = 0; i < n; ++i)
    {
        pos += std::clamp (target - pos, -slew, slew);

        const double hz = std::min (heel * std::exp (span * pos), 0.45 * rate);
        const double g = std::tan (pi * hz / rate);
        const double x = ch[0][i];

        // The peak: a band-pass. The guitar goes in times k, so the top of the peak is always 1
        // whatever the Q, and the filter's memory stays the same size while Q is being turned
        // (scaling what comes out instead makes a swell when Q drops quickly).
        double a1 = 1.0 / (1.0 + g * (g + kPeak));
        double a2 = g * a1;
        double v3 = kPeak * x - p2;
        double v1 = a1 * p1 + a2 * v3;
        double v2 = p2 + a2 * p1 + g * a2 * v3;
        p1 = 2.0 * v1 - p1;
        p2 = 2.0 * v2 - p2;
        const double peak = v1;

        // The body: what lies below the peak.
        a1 = 1.0 / (1.0 + g * (g + kBody));
        a2 = g * a1;
        v3 = x - b2;
        v1 = a1 * b1 + a2 * v3;
        v2 = b2 + a2 * b1 + g * a2 * v3;
        b1 = 2.0 * v1 - b1;
        b2 = 2.0 * v2 - b2;

        const auto out = (float) (peakGain * peak + bodyGain * v2);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }

    if (std::abs (p1) < 1.0e-30) p1 = 0.0;
    if (std::abs (p2) < 1.0e-30) p2 = 0.0;
    if (std::abs (b1) < 1.0e-30) b1 = 0.0;
    if (std::abs (b2) < 1.0e-30) b2 = 0.0;
}

} // namespace gigrig::fx
