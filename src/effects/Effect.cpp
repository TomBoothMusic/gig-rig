#include "Effect.h"

#include <algorithm>
#include <complex>
#include <cstdio>

namespace gigrig::fx
{

std::string ParamInfo::format (float v) const
{
    if (kind == Kind::toggle)
        return v >= 0.5f ? "On" : "Off";

    if (kind == Kind::choice)
    {
        const auto i = (size_t) std::clamp ((int) std::lround (v), 0, (int) choices.size() - 1);
        return choices.empty() ? std::string() : choices[i];
    }

    char text[48];
    const auto a = std::abs (v);
    std::snprintf (text, sizeof (text), a >= 100.0f ? "%.0f" : (a >= 10.0f ? "%.1f" : "%.2f"), (double) v);
    return unit.empty() ? std::string (text) : std::string (text) + " " + unit;
}

int Effect::paramIndex (const std::string& id) const noexcept
{
    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].id == id)
            return (int) i;

    return -1;
}

int Effect::addParam (ParamInfo info)
{
    // Only called from constructors, before anything else can see the effect.
    infos.push_back (std::move (info));

    auto fresh = std::make_unique<std::atomic<float>[]> (infos.size());

    for (size_t i = 0; i < infos.size(); ++i)
        fresh[i].store (i + 1 < infos.size() ? targets[i].load() : infos[i].def);

    targets = std::move (fresh);
    smooth.push_back ({ infos.back().def, 1.0f });
    return (int) infos.size() - 1;
}

int Effect::addParam (std::string id, std::string label, std::string unit, float min, float max, float def, float smoothingMs)
{
    ParamInfo p;
    p.id = std::move (id);
    p.label = std::move (label);
    p.unit = std::move (unit);
    p.min = min;
    p.max = max;
    p.def = def;
    p.smoothingMs = smoothingMs;
    return addParam (std::move (p));
}

int Effect::addToggle (std::string id, std::string label, bool def)
{
    ParamInfo p;
    p.id = std::move (id);
    p.label = std::move (label);
    p.min = 0.0f;
    p.max = 1.0f;
    p.def = def ? 1.0f : 0.0f;
    p.kind = ParamInfo::Kind::toggle;
    p.smoothingMs = 0.0f;
    return addParam (std::move (p));
}

int Effect::addChoice (std::string id, std::string label, std::vector<std::string> choices, int def)
{
    ParamInfo p;
    p.id = std::move (id);
    p.label = std::move (label);
    p.min = 0.0f;
    p.max = (float) std::max (0, (int) choices.size() - 1);
    p.def = (float) def;
    p.kind = ParamInfo::Kind::choice;
    p.choices = std::move (choices);
    p.smoothingMs = 0.0f;
    return addParam (std::move (p));
}

void Effect::setParam (int index, float v) noexcept
{
    if (index < 0 || index >= (int) infos.size())
        return;

    const auto& p = infos[(size_t) index];
    v = std::clamp (std::isfinite (v) ? v : p.def, p.min, p.max);

    if (p.kind != ParamInfo::Kind::continuous)
        v = (float) std::lround (v);

    targets[(size_t) index].store (v, std::memory_order_relaxed);

    // Before audio starts there is nothing to glide from.
    if (! prepared)
        smooth[(size_t) index].current = v;
}

bool Effect::setParam (const std::string& id, float v) noexcept
{
    const auto i = paramIndex (id);
    setParam (i, v);
    return i >= 0;
}

float Effect::getParam (int index) const noexcept
{
    return index >= 0 && index < (int) infos.size() ? targets[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

void Effect::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    for (size_t i = 0; i < infos.size(); ++i)
    {
        // A steady glide: a full sweep of the knob's range takes smoothingMs.
        const auto ms = infos[i].smoothingMs;
        const auto range = std::abs (infos[i].max - infos[i].min);
        smooth[i].coeff = ms <= 0.0f ? 1.0e30f : (float) (range * chunk / (rate * ms * 0.001));
        smooth[i].current = targets[i].load();
    }

    changedFlag = true;
    onPrepare (rate, std::max (1, maxBlockSize));
    prepared = true;
    onReset();
}

void Effect::reset() noexcept
{
    for (size_t i = 0; i < infos.size(); ++i)
        smooth[i].current = targets[i].load (std::memory_order_relaxed);

    changedFlag = true;
    onReset();
}

void Effect::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 1, maxChannels);
    float* part[maxChannels] {};

    for (int start = 0; start < numSamples; start += chunk)
    {
        const auto n = std::min (chunk, numSamples - start);

        for (size_t i = 0; i < infos.size(); ++i)
        {
            auto& s = smooth[i];
            const auto target = targets[i].load (std::memory_order_relaxed);

            if (s.current != target)
            {
                const auto gap = target - s.current;
                s.current = std::abs (gap) <= s.coeff ? target : s.current + (gap > 0.0f ? s.coeff : -s.coeff);
                changedFlag = true;
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            part[ch] = channels[ch] + start;

        processChunk (part, numChannels, n);
        changedFlag = false;
    }
}

//==============================================================================
namespace
{
    constexpr double pi = 3.14159265358979323846;
}

void Biquad::lowPass (double rate, double hz, double q)
{
    const double w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q), n = 1.0 + a;
    b0 = (1.0 - c) / 2.0 / n; b1 = (1.0 - c) / n; b2 = b0; a1 = -2.0 * c / n; a2 = (1.0 - a) / n;
}

void Biquad::highPass (double rate, double hz, double q)
{
    const double w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q), n = 1.0 + a;
    b0 = (1.0 + c) / 2.0 / n; b1 = -(1.0 + c) / n; b2 = b0; a1 = -2.0 * c / n; a2 = (1.0 - a) / n;
}

void Biquad::bandPass (double rate, double hz, double q)
{
    const double w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q), n = 1.0 + a;
    b0 = a / n; b1 = 0.0; b2 = -a / n; a1 = -2.0 * c / n; a2 = (1.0 - a) / n;
}

void Biquad::peak (double rate, double hz, double q, double db)
{
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q), n = 1.0 + a / A;
    b0 = (1.0 + a * A) / n; b1 = -2.0 * c / n; b2 = (1.0 - a * A) / n; a1 = -2.0 * c / n; a2 = (1.0 - a / A) / n;
}

void Biquad::lowShelf (double rate, double hz, double db, double slope)
{
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), s = std::sin (w);
    const double a = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0), r = 2.0 * std::sqrt (A) * a;
    const double n = (A + 1) + (A - 1) * c + r;
    b0 = A * ((A + 1) - (A - 1) * c + r) / n; b1 = 2 * A * ((A - 1) - (A + 1) * c) / n; b2 = A * ((A + 1) - (A - 1) * c - r) / n;
    a1 = -2 * ((A - 1) + (A + 1) * c) / n; a2 = ((A + 1) + (A - 1) * c - r) / n;
}

void Biquad::highShelf (double rate, double hz, double db, double slope)
{
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * std::clamp (hz, 1.0, rate * 0.49) / rate, c = std::cos (w), s = std::sin (w);
    const double a = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0), r = 2.0 * std::sqrt (A) * a;
    const double n = (A + 1) - (A - 1) * c + r;
    b0 = A * ((A + 1) + (A - 1) * c + r) / n; b1 = -2 * A * ((A - 1) + (A + 1) * c) / n; b2 = A * ((A + 1) + (A - 1) * c - r) / n;
    a1 = 2 * ((A - 1) - (A + 1) * c) / n; a2 = ((A + 1) - (A - 1) * c - r) / n;
}

double Biquad::responseDb (double rate, double hz) const
{
    const auto z = std::polar (1.0, -2.0 * pi * hz / rate);
    const auto h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}

void OnePole::lowPass (double rate, double hz)
{
    // Bilinear transform with prewarping.
    const double k = std::tan (pi * std::clamp (hz, 1.0, rate * 0.49) / rate);
    b0 = k / (1.0 + k); b1 = b0; a1 = (k - 1.0) / (k + 1.0);
}

void OnePole::highPass (double rate, double hz)
{
    const double k = std::tan (pi * std::clamp (hz, 1.0, rate * 0.49) / rate);
    b0 = 1.0 / (1.0 + k); b1 = -b0; a1 = (k - 1.0) / (k + 1.0);
}

} // namespace gigrig::fx
