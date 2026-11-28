#include "ChannelEq.h"

#include <algorithm>

namespace gigrig::fx
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    double warp (double rate, double hz)    { return std::tan (pi * std::clamp (hz, 5.0, rate * 0.45) / rate); }
}

void ChannelEq::Svf::highPass (double rate, double hz, double q) noexcept
{
    const double k = 1.0 / q;
    set (warp (rate, hz), k);
    m0 = 1.0; m1 = -k; m2 = -1.0;
}

void ChannelEq::Svf::lowPass (double rate, double hz, double q) noexcept
{
    set (warp (rate, hz), 1.0 / q);
    m0 = 0.0; m1 = 0.0; m2 = 1.0;
}

void ChannelEq::Svf::bell (double rate, double hz, double q, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / (q * a);
    set (warp (rate, hz), k);
    m0 = 1.0; m1 = k * (a * a - 1.0); m2 = 0.0;
}

void ChannelEq::Svf::lowShelf (double rate, double hz, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / 0.7071;
    set (warp (rate, hz) / std::sqrt (a), k);
    m0 = 1.0; m1 = k * (a - 1.0); m2 = a * a - 1.0;
}

void ChannelEq::Svf::highShelf (double rate, double hz, double db) noexcept
{
    const double a = std::pow (10.0, db / 40.0);
    const double k = 1.0 / 0.7071;
    set (warp (rate, hz) * std::sqrt (a), k);
    m0 = a * a; m1 = k * (1.0 - a) * a; m2 = 1.0 - a * a;
}

ChannelEq::ChannelEq()
{
    // Ids never change: they are saved in presets and songs.
    pLowCut  = addParam ("lowcut",  "HPF",              "Hz", 20.0f, 400.0f, 20.0f);
    pHighCut = addParam ("highcut", "LPF",              "Hz", 2000.0f, 20000.0f, 20000.0f);
    pLow     = addParam ("low",     "LF 120 Hz",        "dB", -15.0f, 15.0f, 0.0f);
    pHigh    = addParam ("high",    "HF 7 kHz",         "dB", -15.0f, 15.0f, 0.0f);
    pLmf     = addParam ("lmf",     "LMF Freq",         "Hz", 100.0f, 1500.0f, 300.0f);
    pLmg     = addParam ("lmg",     "LMF Gain",         "dB", -15.0f, 15.0f, 0.0f);
    pHmf     = addParam ("hmf",     "HMF Freq",         "Hz", 800.0f, 8000.0f, 3000.0f);
    pHmg     = addParam ("hmg",     "HMF Gain",         "dB", -15.0f, 15.0f, 0.0f);
    pWidth   = addParam ("width",   "Mid Q",            "",   0.0f, 10.0f, 5.0f);
    pLevel   = addParam ("level",   "Level",            "dB", -15.0f, 15.0f, 0.0f);
}

void ChannelEq::onPrepare (double, int)
{
    onReset();
}

void ChannelEq::onReset() noexcept
{
    for (auto* f : { &lowCut, &lowShelf, &lowMid, &highMid, &highShelf, &highCut })
        f->clear();
}

void ChannelEq::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
    {
        const double rate = sampleRate();
        // Width 0 is a narrow notch (Q 6), 10 a broad tilt (Q 0.4).
        const double q = 6.0 * std::pow (0.4 / 6.0, value (pWidth) / 10.0);
        // At the ends of their knobs the two cuts move right out of what can be heard, so
        // "20 Hz" and "20 kHz" mean off.
        const double lc = value (pLowCut), hc = value (pHighCut);
        lowCut.highPass (rate, lc * std::clamp (0.25 + 0.075 * (lc - 20.0), 0.25, 1.0), 0.7071);
        lowShelf.lowShelf (rate, 120.0, value (pLow));
        lowMid.bell (rate, value (pLmf), q, value (pLmg));
        highMid.bell (rate, value (pHmf), q, value (pHmg));
        highShelf.highShelf (rate, 7000.0, value (pHigh));
        highCut.lowPass (rate, hc * (1.0 + std::max (0.0, (hc - 18000.0) / 2000.0)), 0.7071);
    }

    const float level = dbToGain (value (pLevel));

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            x = lowCut.run (x, c);
            x = lowShelf.run (x, c);
            x = lowMid.run (x, c);
            x = highMid.run (x, c);
            x = highShelf.run (x, c);
            x = highCut.run (x, c);
            ch[c][i] = x * level;
        }
}

} // namespace gigrig::fx
