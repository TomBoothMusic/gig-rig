#include "Warmth.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

Warmth::Warmth()
{
    pDrive  = addParam ("drive", "Drive", "", 0.0f, 10.0f, 3.0f);
    pColour = addParam ("colour", "Colour (valve)", "", 0.0f, 10.0f, 4.0f);
    pLevel  = addParam ("level", "Level", "dB", -12.0f, 12.0f, 0.0f);
    pMix    = addParam ("mix", "Mix", "%", 0.0f, 100.0f, 100.0f);
}

void Warmth::onPrepare (double rate, int)
{
    dc.highPass (rate, 5.0);
    update();
}

void Warmth::onReset() noexcept
{
    low.clear();
    high.clear();
    dc.clear();
}

void Warmth::update()
{
    const double drive = value (pDrive), colour = value (pColour);
    k = 0.4 + 0.5 * drive;                      // how hard the signal leans into the curve
    bias = 0.035 * colour * (0.3 + 0.07 * drive);
    offset = std::tanh (bias);
    norm = 1.0 / (k * (1.0 - offset * offset)); // so a quiet sound comes out at the level it went in
    out = dbToGain (value (pLevel));
    mix = 0.01 * value (pMix);
    low.lowShelf (sampleRate(), 160.0, 0.12 * drive, 0.7);
    high.highShelf (sampleRate(), 9000.0, -0.15 * drive, 0.7);
}

void Warmth::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
        update();

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            const double x = low.run (ch[c][i], c);
            const double y = (std::tanh (k * x + bias) - offset) * norm;
            const double wet = high.run (dc.run ((float) y, c), c);
            ch[c][i] = (float) (out * (ch[c][i] + mix * (wet - ch[c][i])));
        }
}

//==============================================================================
void warmthChecks (const Check& check)
{
    using namespace test;
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.5 * rate);

    auto fx = makeEffect ("warmth");
    fx->prepare (rate, 64);

    const auto quietIn = sine (1000.0, 1.0, 0.01, rate);
    const auto quiet = run (*fx, quietIn);
    const double change = 20.0 * std::log10 (rms (quiet, from) / rms (quietIn, from));
    const double h2q = strength (quiet, from, 2000.0, rate) / strength (quiet, from, 1000.0, rate);
    check (std::abs (change) < 0.3 && h2q < 0.01, "warmth: a quiet sound passes at its own level and clean (" + num (change, 2) + " dB)");

    fx->reset();
    const auto loudIn = sine (1000.0, 1.0, 0.7, rate);
    const auto loud = run (*fx, loudIn);
    const double f = strength (loud, from, 1000.0, rate);
    const double h2 = strength (loud, from, 2000.0, rate) / f, h3 = strength (loud, from, 3000.0, rate) / f;
    float top = 0.0f;
    for (size_t i = from; i < loud.size(); ++i)
        top = std::max (top, std::abs (loud[i]));
    check (h2 > 0.005 && h3 > 0.01 && h3 < 0.25 && top < 0.69f,
           "warmth: a loud sound has its peaks rounded (0.70 in, " + num (top, 2) + " out) and gains harmonics: the octave at " + num (20.0 * std::log10 (h2)) + " dB and the next at " + num (20.0 * std::log10 (h3)) + " dB");

    fx->setParam ("colour", 0.0f);
    fx->reset();
    const auto even = run (*fx, loudIn);
    const double h2none = strength (even, from, 2000.0, rate) / strength (even, from, 1000.0, rate);
    fx->setParam ("mix", 0.0f);
    fx->reset();
    const auto dry = run (*fx, loudIn);
    float worst = 0.0f;
    for (size_t i = 0; i < dry.size(); ++i)
        worst = std::max (worst, std::abs (dry[i] - loudIn[i]));
    fx->setParam ("mix", 100.0f);
    check (worst < 1.0e-6f, "warmth: with Mix at 0 the sound comes out exactly as it went in");

    check (h2none < h2 * 0.2, "warmth: with Colour at 0 the rounding is even on both sides, so the octave harmonic goes (" + num (20.0 * std::log10 (std::max (h2none, 1.0e-9))) + " dB)");
}

} // namespace gigrig::fx
