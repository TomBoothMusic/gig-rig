#include "MixerBlocks.h"

#include <algorithm>
#include <cmath>

namespace gigrig::fx
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    double warp (double rate, double hz)    { return std::tan (pi * std::clamp (hz, 5.0, rate * 0.45) / rate); }

    /** "20 Hz" on a low cut and "20 kHz" on a high cut mean off: at the ends of their knobs
        the cuts move right out of what can be heard. */
    double lowCutHz (double knob)           { return knob * std::clamp (0.25 + 0.075 * (knob - 20.0), 0.25, 1.0); }
    double highCutHz (double knob)          { return knob * (1.0 + std::max (0.0, (knob - 18000.0) / 2000.0)); }

    double coeffFor (double seconds, double rate) noexcept
    {
        return 1.0 - std::exp (-1.0 / std::max (1.0, seconds * rate));
    }
}

void MixSvf::highPass (double rate, double hz, double q) noexcept
{
    const double k = 1.0 / q;
    set (warp (rate, hz), k);
    m0 = 1.0; m1 = -k; m2 = -1.0;
}

void MixSvf::lowPass (double rate, double hz, double q) noexcept
{
    set (warp (rate, hz), 1.0 / q);
    m0 = 0.0; m1 = 0.0; m2 = 1.0;
}

void MixSvf::bell (double rate, double hz, double q, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / (q * a);
    set (warp (rate, hz), k);
    m0 = 1.0; m1 = k * (a * a - 1.0); m2 = 0.0;
}

void MixSvf::lowShelf (double rate, double hz, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / 0.7071;
    set (warp (rate, hz) / std::sqrt (a), k);
    m0 = 1.0; m1 = k * (a - 1.0); m2 = a * a - 1.0;
}

void MixSvf::highShelf (double rate, double hz, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / 0.7071;
    set (warp (rate, hz) * std::sqrt (a), k);
    m0 = a * a; m1 = k * (1.0 - a) * a; m2 = 1.0 - a * a;
}

//==============================================================================
MixEq::MixEq()
{
    // Ids never change: they are saved in the mixer's presets.
    pLowCut  = addParam ("lowcut",  "Low cut",       "Hz", 20.0f, 400.0f, 20.0f);
    pLsf     = addParam ("lsf",     "Low shelf at",  "Hz", 40.0f, 400.0f, 100.0f);
    pLsg     = addParam ("lsg",     "Low shelf",     "dB", -15.0f, 15.0f, 0.0f);

    static const struct { const char* f; const char* g; const char* q; const char* name; float lo, hi, def; } bands[numBells] {
        { "b1f", "b1g", "b1q", "Band 1", 60.0f, 1000.0f, 250.0f },
        { "b2f", "b2g", "b2q", "Band 2", 150.0f, 3000.0f, 800.0f },
        { "b3f", "b3g", "b3q", "Band 3", 500.0f, 8000.0f, 2500.0f },
        { "b4f", "b4g", "b4q", "Band 4", 1500.0f, 16000.0f, 6000.0f } };

    for (int b = 0; b < numBells; ++b)
    {
        const auto& band = bands[b];
        pF[(size_t) b] = addParam (band.f, std::string (band.name) + " at",    "Hz", band.lo, band.hi, band.def);
        pG[(size_t) b] = addParam (band.g, band.name,                          "dB", -15.0f, 15.0f, 0.0f);
        pQ[(size_t) b] = addParam (band.q, std::string (band.name) + " width (high = narrow)", "", 0.3f, 10.0f, 1.0f);
    }

    pHsf     = addParam ("hsf",     "High shelf at", "Hz", 2000.0f, 16000.0f, 8000.0f);
    pHsg     = addParam ("hsg",     "High shelf",    "dB", -15.0f, 15.0f, 0.0f);
    pHighCut = addParam ("highcut", "High cut",      "Hz", 2000.0f, 20000.0f, 20000.0f);
}

void MixEq::onPrepare (double, int)     { onReset(); }

void MixEq::onReset() noexcept
{
    lowCut.clear(); lowShelf.clear(); highShelf.clear(); highCut.clear();
    for (auto& b : bell) b.clear();
}

void MixEq::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
    {
        const double rate = sampleRate();
        lowCut.highPass (rate, lowCutHz (value (pLowCut)), 0.7071);
        lowShelf.lowShelf (rate, value (pLsf), value (pLsg));
        for (int b = 0; b < numBells; ++b)
            bell[(size_t) b].bell (rate, value (pF[(size_t) b]), value (pQ[(size_t) b]), value (pG[(size_t) b]));
        highShelf.highShelf (rate, value (pHsf), value (pHsg));
        highCut.lowPass (rate, highCutHz (value (pHighCut)), 0.7071);
    }

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            x = lowCut.run (x, c);
            x = lowShelf.run (x, c);
            for (auto& b : bell)
                x = b.run (x, c);
            x = highShelf.run (x, c);
            ch[c][i] = highCut.run (x, c);
        }
}

//==============================================================================
MixComp::MixComp()
{
    pThreshold = addParam ("threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f);
    pRatio     = addParam ("ratio",     "Ratio",     ":1", 1.0f, 20.0f, 3.0f);
    pAttack    = addParam ("attack",    "Attack",    "ms", 0.1f, 100.0f, 10.0f);
    pRelease   = addParam ("release",   "Release",   "ms", 20.0f, 1000.0f, 150.0f);
    pKnee      = addParam ("knee",      "Knee (0 = hard)", "dB", 0.0f, 12.0f, 6.0f);
    pMakeup    = addParam ("makeup",    "Make-up gain", "dB", 0.0f, 24.0f, 0.0f);
}

void MixComp::onPrepare (double, int)   { onReset(); }

void MixComp::onReset() noexcept
{
    env = 0.0;
    gr = 0.0;
    setMeter (0.0f);
}

void MixComp::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double threshold = value (pThreshold);
    const double slope = 1.0 - 1.0 / std::max (1.0, (double) value (pRatio));
    const double knee = value (pKnee);
    const double attackC = coeffFor (value (pAttack) * 0.001, rate);
    const double releaseC = coeffFor (value (pRelease) * 0.001, rate);
    const double ripple = coeffFor (0.002, rate);       // the detector's own smoothing, so bass notes do not wobble the gain
    const double makeup = value (pMakeup);
    double deepest = 0.0;

    for (int i = 0; i < n; ++i)
    {
        double level = 0.0;
        for (int c = 0; c < numCh; ++c)
            level = std::max (level, (double) std::abs (ch[c][i]));

        env = level > env ? level : env + ripple * (level - env);
        const double levelDb = env > 1.0e-9 ? 20.0 * std::log10 (env) : -180.0;

        const double over = levelDb - threshold;
        double target = 0.0;
        if (knee <= 0.01 || over >= knee * 0.5)     target = std::max (0.0, over) * slope;
        else if (over > -knee * 0.5)                target = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);

        gr += (target > gr ? attackC : releaseC) * (target - gr);
        if (gr < 1.0e-9) gr = 0.0;
        deepest = std::max (deepest, gr);

        const float g = dbToGain ((float) (makeup - gr));
        for (int c = 0; c < numCh; ++c)
            ch[c][i] *= g;
    }

    setMeter ((float) -deepest);
}

//==============================================================================
Notches::Notches()
{
    static const float starts[count] { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f };

    for (int k = 0; k < count; ++k)
    {
        const auto number = std::to_string (k + 1);
        pF[(size_t) k]   = addParam ("n" + number + "f", "Notch " + number + " at", "Hz", 40.0f, 12000.0f, starts[k]);
        pCut[(size_t) k] = addParam ("n" + number + "g", "Notch " + number,         "dB", -24.0f, 0.0f, 0.0f);
    }

    pWidth = addParam ("width", "Width (narrow up)", "", 2.0f, 30.0f, 10.0f);
}

void Notches::onPrepare (double, int)   { onReset(); }

void Notches::onReset() noexcept
{
    for (auto& f : notch) f.clear();
}

void Notches::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
        for (int k = 0; k < count; ++k)
            notch[(size_t) k].bell (sampleRate(), value (pF[(size_t) k]), value (pWidth), value (pCut[(size_t) k]));

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            for (auto& f : notch)
                x = f.run (x, c);
            ch[c][i] = x;
        }
}

//==============================================================================
Limiter::Limiter()
{
    pCeiling = addParam ("ceiling", "Ceiling", "dB", -24.0f, 0.0f, -1.0f);
    pRelease = addParam ("release", "Release", "ms", 20.0f, 1000.0f, 120.0f);
}

void Limiter::onPrepare (double, int)   { onReset(); }

void Limiter::onReset() noexcept
{
    gain = 1.0;
    setMeter (0.0f);
}

void Limiter::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double ceiling = dbToGain (value (pCeiling));
    const double releaseC = coeffFor (value (pRelease) * 0.001, sampleRate());
    double lowest = 1.0;

    for (int i = 0; i < n; ++i)
    {
        double peak = 0.0;
        for (int c = 0; c < numCh; ++c)
            peak = std::max (peak, (double) std::abs (ch[c][i]));

        // Letting go is gradual; turning down is at once, so not one sample gets past the ceiling.
        gain += releaseC * (1.0 - gain);
        if (peak * gain > ceiling)
            gain = ceiling / peak;

        lowest = std::min (lowest, gain);

        for (int c = 0; c < numCh; ++c)
            ch[c][i] = (float) (ch[c][i] * gain);
    }

    setMeter (gainToDb ((float) lowest));
}

//==============================================================================
LineDelay::LineDelay()
{
    pMs = addParam ("ms", "Delay", "ms", 0.0f, (float) maxMs, 0.0f, 0.0f);      // fades on its own
}

int LineDelay::latencySamples() const noexcept
{
    return (int) std::lround ((double) getParam (pMs) * 0.001 * sampleRate());
}

void LineDelay::onPrepare (double rate, int)
{
    size = (int) (rate * (maxMs + 20.0) * 0.001) + 8;
    for (auto& l : line)
        l.assign ((size_t) size, 0.0f);
    fadeStep = (float) (1.0 / std::max (1.0, 0.020 * rate));
    primed = false;
    onReset();
}

void LineDelay::onReset() noexcept
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    writeAt = 0;
    fading = false;
    fade = 0.0f;
    delayNow = delayNext = std::clamp ((int) std::lround ((double) value (pMs) * 0.001 * sampleRate()), 0, std::max (0, size - 1));
    primed = true;
}

void LineDelay::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const int target = std::clamp ((int) std::lround ((double) value (pMs) * 0.001 * sampleRate()), 0, size - 1);

    if (! fading && target != delayNow)
    {
        delayNext = target;
        fading = true;
        fade = 0.0f;
    }

    for (int i = 0; i < n; ++i)
    {
        int a = writeAt - delayNow;     if (a < 0) a += size;
        int b = writeAt - delayNext;    if (b < 0) b += size;

        for (int c = 0; c < numCh; ++c)
        {
            auto& l = line[(size_t) c];
            l[(size_t) writeAt] = ch[c][i];
            const float old = l[(size_t) a];
            ch[c][i] = fading ? old + (l[(size_t) b] - old) * fade : old;
        }

        if (fading)
        {
            fade += fadeStep;
            if (fade >= 1.0f)
            {
                delayNow = delayNext;
                fading = false;
            }
        }

        if (++writeAt >= size)
            writeAt = 0;
    }
}

//==============================================================================
WetFilter::WetFilter()
{
    pLowCut  = addParam ("lowcut",  "Low cut",  "Hz", 20.0f, 1000.0f, 20.0f);
    pHighCut = addParam ("highcut", "High cut", "Hz", 1000.0f, 20000.0f, 20000.0f);
}

void WetFilter::onPrepare (double, int) { onReset(); }

void WetFilter::onReset() noexcept
{
    lowCut.clear();
    highCut.clear();
}

void WetFilter::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
    {
        lowCut.highPass (sampleRate(), lowCutHz (value (pLowCut)), 0.7071);
        highCut.lowPass (sampleRate(), highCutHz (value (pHighCut)), 0.7071);
    }

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = highCut.run (lowCut.run (ch[c][i], c), c);
}

} // namespace gigrig::fx
