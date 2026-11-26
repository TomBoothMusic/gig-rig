#include "VolumePedal.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

static constexpr double heelSilentDb = -80.0;

VolumePedal::VolumePedal()
{
    pPedal = addParam ("pedal", "Pedal", "", 0.0f, 100.0f, 100.0f, 30.0f);
    pHeel  = addParam ("heel", "Heel level", "dB", (float) heelSilentDb, 0.0f, (float) heelSilentDb);
    pReverse = addToggle ("reverse", "Reversed (heel is loud)", false);
}

double VolumePedal::gainFor (double pedal, double heelDb) noexcept
{
    const double heel = heelDb <= heelSilentDb + 0.5 ? 0.0 : std::pow (10.0, heelDb / 20.0);
    const double p = std::clamp (pedal / 100.0, 0.0, 1.0);
    return heel + (1.0 - heel) * std::pow (p, 2.5);        // half way is about -15 dB, as an audio-taper pot
}

void VolumePedal::onPrepare (double, int)   { primed = false; }
void VolumePedal::onReset() noexcept        { primed = false; }

void VolumePedal::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const float pedal = value (pReverse) >= 0.5f ? 100.0f - value (pPedal) : value (pPedal);
    const float target = (float) gainFor (pedal, value (pHeel));

    if (! primed)
    {
        gain = target;
        primed = true;
    }

    const float step = (target - gain) / (float) n;

    for (int c = 0; c < numCh; ++c)
    {
        float g = gain;

        for (int i = 0; i < n; ++i)
        {
            g += step;
            ch[c][i] *= g;
        }
    }

    gain = target;
}

//==============================================================================
void volumeChecks (const Check& check)
{
    const double rate = 96000.0;
    const auto level = [rate] (float pedal, float heel)
    {
        VolumePedal v;
        v.prepare (rate, 64);
        v.setParam ("pedal", pedal);
        v.setParam ("heel", heel);
        v.reset();

        std::vector<float> x ((size_t) (rate * 0.2));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / rate);

        for (size_t at = 0; at < x.size(); at += 64)
        {
            float* p = x.data() + at;
            v.process (&p, 1, (int) std::min<size_t> (64, x.size() - at));
        }

        float peak = 0.0f;
        for (size_t i = x.size() / 2; i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));

        return peak / 0.5f;
    };

    const float toe = level (100.0f, -80.0f), heel = level (0.0f, -80.0f), half = level (50.0f, -80.0f), heelUp = level (0.0f, -12.0f);
    check (std::abs (toe - 1.0f) < 1.0e-4f && heel < 1.0e-6f, "volume: toe down is exactly the sound that came in, heel down is silence");
    check (half > 0.12f && half < 0.25f, "volume: half way is about a sixth of the level (" + std::to_string (20.0 * std::log10 (half)).substr (0, 5) + " dB), so the travel follows the ear");
    check (std::abs (20.0f * std::log10 (heelUp) + 12.0f) < 0.1f, "volume: with Heel level at -12 dB the heel is 12 dB down, not silent");

    // Reversed, for the other half of a blend: the acoustic is loud at the heel and gone at the toe.
    {
        VolumePedal r;
        r.prepare (rate, 64);
        r.setParam ("reverse", 1.0f);
        r.setParam ("pedal", 0.0f);
        r.reset();
        std::vector<float> y (4096, 0.5f);
        float* p = y.data();
        r.process (&p, 1, (int) y.size());
        const float atHeel = y.back();
        r.setParam ("pedal", 100.0f);
        for (int k = 0; k < 20; ++k)
        {
            std::fill (y.begin(), y.end(), 0.5f);
            r.process (&p, 1, (int) y.size());
        }
        check (std::abs (atHeel - 0.5f) < 1.0e-4f && std::abs (y.back()) < 1.0e-6f,
               "volume: reversed, heel down is full level and toe down is silence, so one pedal can fade the acoustic out as it fades the electric in");
    }

    // A fast swell: the level never steps.
    VolumePedal v;
    v.prepare (rate, 64);
    v.setParam ("pedal", 0.0f);
    v.reset();
    std::vector<float> x ((size_t) (rate * 0.3), 0.5f);
    float worst = 0.0f, last = 0.0f;

    for (size_t at = 0; at < x.size(); at += 64)
    {
        if (at == 64 * 20)
            v.setParam ("pedal", 100.0f);

        float* p = x.data() + at;
        const int n = (int) std::min<size_t> (64, x.size() - at);
        v.process (&p, 1, n);

        for (int i = 0; i < n; ++i)
        {
            worst = std::max (worst, std::abs (p[i] - last));
            last = p[i];
        }
    }

    check (std::abs (last - 0.5f) < 1.0e-4f && worst < 0.002f, "volume: a stamp from heel to toe swells in smoothly (biggest step " + std::to_string (worst).substr (0, 7) + " of 0.5) and arrives at full level");
}

} // namespace gigrig::fx
