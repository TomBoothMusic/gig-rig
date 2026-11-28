#include "Whammy.h"

#include <algorithm>

namespace gigrig::fx
{

namespace
{
    constexpr double guard = 2.0;       // a moving read point stays this far behind the newest sample

    struct Mode
    {
        const char* name;
        int kind;           // 0 whammy (bent sound only), 1 harmony (dry + voice), 2 detune (dry + pedal brings in the voice)
        double heel, toe;   // semitones
    };

    // The order is saved in presets as a number: only ever add to the end.
    constexpr Mode modes[] {
        { "Whammy: 1 octave up",                0,   0.0,  12.0 },
        { "Whammy: 2 octaves up",               0,   0.0,  24.0 },
        { "Whammy: 5th up",                     0,   0.0,   7.0 },
        { "Whammy: 4th up",                     0,   0.0,   5.0 },
        { "Whammy: 2nd down",                   0,   0.0,  -2.0 },
        { "Whammy: 4th down",                   0,   0.0,  -5.0 },
        { "Whammy: 5th down",                   0,   0.0,  -7.0 },
        { "Whammy: 1 octave down",              0,   0.0, -12.0 },
        { "Whammy: 2 octaves down",             0,   0.0, -24.0 },
        { "Whammy: dive bomb (3 octaves down)", 0,   0.0, -36.0 },
        { "Harmony: octave down to octave up",  1, -12.0,  12.0 },
        { "Harmony: 5th down to 4th down",      1,  -7.0,  -5.0 },
        { "Harmony: 4th down to 3rd down",      1,  -5.0,  -4.0 },
        { "Harmony: 5th up to 7th up",          1,   7.0,  10.0 },
        { "Harmony: 5th up to 6th up",          1,   7.0,   9.0 },
        { "Harmony: 4th up to 5th up",          1,   5.0,   7.0 },
        { "Harmony: 3rd up to 4th up",          1,   4.0,   5.0 },
        { "Harmony: minor 3rd up to 3rd up",    1,   3.0,   4.0 },
        { "Harmony: 2nd up to 3rd up",          1,   2.0,   4.0 },
        { "Detune: shallow",                    2,   0.10,  0.10 },
        { "Detune: deep",                       2,   0.22,  0.22 },
    };

    constexpr int numModes = (int) (sizeof (modes) / sizeof (modes[0]));

    const Mode& modeAt (int m) noexcept     { return modes[std::clamp (m, 0, numModes - 1)]; }
}

const std::vector<std::string>& Whammy::modeNames()
{
    static const std::vector<std::string> names = []
    {
        std::vector<std::string> n;
        for (const auto& m : modes)
            n.emplace_back (m.name);
        return n;
    }();
    return names;
}

double Whammy::semitones (int mode, double pedal) noexcept
{
    const auto& m = modeAt (mode);
    return m.heel + (m.toe - m.heel) * std::clamp (pedal, 0.0, 1.0);
}

Whammy::Whammy()
{
    pMode  = addChoice ("mode", "Mode", modeNames(), 0);
    // The pedal and its glide are handled per sample below, so no glide from the base class.
    pPedal = addParam ("pedal", "Pedal", "%", 0.0f, 100.0f, 0.0f, 0.0f);
    pGlide = addParam ("glide", "Glide", "ms", 20.0f, 2000.0f, 60.0f, 0.0f);
    pMix   = addParam ("mix", "Mix", "%", 0.0f, 100.0f, 100.0f);
}

void Whammy::onPrepare (double rate, int)
{
    const auto ms = [rate] (double v) { return (int) std::lround (v * 0.001 * rate); };

    decFactor = std::max (1, (int) std::lround (rate / 8000.0));
    hopMin = ms (11.0);
    hopMax = ms (28.0);
    hopMid = ms (18.0);
    winBack = ms (10.0);
    winAhead = ms (2.0);
    fineWin = ms (3.0);
    fadeMin = ms (2.0);
    fadeMax = ms (8.0);
    downTrigger = hopMax + winAhead + 4 * decFactor;

    int size = 1;
    while (size < ms (160.0))
        size <<= 1;
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    maxDelay = size - winBack - hopMax - 8 * decFactor - 16;

    int decSize = 1;
    while (decSize < size / decFactor + 4)
        decSize <<= 1;
    dec.assign ((size_t) decSize, 0.0f);
    decMask = decSize - 1;

    onReset();
}

void Whammy::onReset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    std::fill (dec.begin(), dec.end(), 0.0f);
    w = dw = 0;
    decCount = 0;
    decSum = 0.0f;
    curDelay = nextDelay = 0.0;
    fading = false;
    fadePos = restCount = 0;
    pos = value (pPedal) * 0.01;

    const auto& m = modeAt ((int) std::lround (value (pMode)));
    const float mix = value (pMix) * 0.01f;
    const float amount = m.kind == 2 ? mix * (float) pos : mix;
    dryGain = m.kind == 0 ? 1.0f - mix : 1.0f - 0.29f * amount;
    wetGain = m.kind == 0 ? mix : 0.71f * amount;
}

/** The sound from 'delay' samples before the newest one (0 = the newest), between samples
    by a smooth curve. */
float Whammy::readAt (double delay) const noexcept
{
    const double p = (double) (w - 1) - delay;
    const auto i = (std::int64_t) std::floor (p);
    const auto f = (float) (p - (double) i);

    if (f <= 0.0f)
        return buf[(size_t) (i & mask)];

    const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)],
                y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

/** How far the read point should hop (in samples, hopMin..hopMax) so that it lands on sound
    that looks like what it is playing now: the hop that best lines the waves up. 'earlier'
    hops back in time (pitching up), otherwise forward (pitching down). */
double Whammy::findHop (double delay, bool earlier) const noexcept
{
    // The stretch of sound around the read point: mostly what it has just played, and a
    // little of what it is about to (as far as that has arrived).
    const std::int64_t at = (w - 1) - (std::int64_t) std::llround (delay);
    const std::int64_t end = std::min<std::int64_t> (w - 1, at + winAhead);
    const int sign = earlier ? -1 : 1;

    // First a quick look on the thinned-out copy...
    const int d = decFactor;
    const auto endD = end / d - 1;
    const int winD = (winBack + winAhead) / d;
    const int loD = (hopMin + d - 1) / d, hiD = hopMax / d;

    if (! earlier && (endD + hiD) * d + d > w)      // not enough newer sound to hop into yet
        return (double) hopMin;

    float energyA = 0.0f;
    for (int k = 0; k < winD; ++k)
    {
        const float a = dec[(size_t) ((endD - k) & decMask)];
        energyA += a * a;
    }

    if (energyA < 1.0e-9f * (float) winD)
        return (double) hopMid;                     // silence: nothing to line up

    constexpr int maxLags = 256;
    float scores[maxLags];
    const int numLags = std::min (maxLags, hiD - loD + 1);
    float best = -2.0f;

    for (int j = 0; j < numLags; ++j)
    {
        float dot = 0.0f, energyB = 0.0f;
        const auto shift = endD + sign * (loD + j);

        for (int k = 0; k < winD; ++k)
        {
            const float a = dec[(size_t) ((endD - k) & decMask)];
            const float b = dec[(size_t) ((shift - k) & decMask)];
            dot += a * b;
            energyB += b * b;
        }

        scores[j] = dot / std::sqrt (energyA * energyB + 1.0e-20f);
        best = std::max (best, scores[j]);
    }

    // The shortest hop that lines up nearly as well as the best one (a shorter hop keeps the
    // bent sound closer behind the playing).
    int bestD = loD;

    for (int j = 0; j < numLags; ++j)
    {
        const bool peak = (j == 0 || scores[j] >= scores[j - 1]) && (j == numLags - 1 || scores[j] >= scores[j + 1]);

        if (peak && scores[j] >= best - 0.03f)
        {
            bestD = loD + j;
            break;
        }
    }

    // ...then to the exact sample on the real thing, and a fraction of a sample beyond that
    // (high notes need it).
    constexpr int maxFine = 96;
    float fine[maxFine];
    const int firstLag = std::max (hopMin, bestD * d - d);
    int count = 0;

    for (int lag = firstLag; lag <= std::min (hopMax, bestD * d + d) && count < maxFine; ++lag)
    {
        if (! earlier && end + lag > w - 1)
            break;

        float dot = 0.0f, energyB = 0.0f, energyF = 0.0f;
        const auto shift = end + sign * lag;

        for (int k = 0; k < fineWin; ++k)
        {
            const float a = buf[(size_t) ((end - k) & mask)];
            const float b = buf[(size_t) ((shift - k) & mask)];
            dot += a * b;
            energyB += b * b;
            energyF += a * a;
        }

        fine[count++] = dot / std::sqrt (energyF * energyB + 1.0e-20f);
    }

    if (count == 0)
        return (double) (bestD * d);

    int top = 0;
    for (int j = 1; j < count; ++j)
        if (fine[j] > fine[top])
            top = j;

    double hop = (double) (firstLag + top);

    if (top > 0 && top < count - 1)
    {
        const double curve = (double) fine[top - 1] - 2.0 * fine[top] + fine[top + 1];
        if (curve < -1.0e-9)
            hop += std::clamp (0.5 * ((double) fine[top - 1] - fine[top + 1]) / curve, -0.5, 0.5);
    }

    return hop;
}

void Whammy::startFade (double toDelay, int length) noexcept
{
    nextDelay = toDelay;
    fadeLen = std::max (1, length);
    fadePos = 0;
    fading = true;
}

void Whammy::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const int modeIndex = (int) std::lround (value (pMode));
    const auto& m = modeAt (modeIndex);
    const double target = value (pPedal) * 0.01;
    const double slew = 1.0 / std::max (1.0, value (pGlide) * 0.001 * rate);     // heel to toe in "Bend time"
    const float mix = value (pMix) * 0.01f;
    const float gainStep = (float) (1.0 - std::exp (-1.0 / (0.005 * rate)));
    const double span = m.toe - m.heel;

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];

        buf[(size_t) (w & mask)] = x;
        ++w;
        decSum += x;
        if (++decCount >= decFactor)
        {
            dec[(size_t) (dw & decMask)] = decSum / (float) decFactor;
            ++dw;
            decSum = 0.0f;
            decCount = 0;
        }

        pos += std::clamp (target - pos, -slew, slew);

        const double semis = m.heel + span * pos;
        const bool moving = std::abs (semis) > 1.0e-4;
        const double ratio = moving ? std::exp2 (semis / 12.0) : 1.0;
        const double step = 1.0 - ratio;            // how the read point's delay changes each sample

        if (moving)
        {
            curDelay = std::clamp (curDelay + step, guard, (double) maxDelay);
            if (fading)
                nextDelay = std::clamp (nextDelay + step, guard, (double) maxDelay);
            restCount = 0;
        }

        float wet;

        if (fading)
        {
            const float g = 0.5f - 0.5f * std::cos (3.14159265f * (float) (fadePos + 1) / (float) fadeLen);
            wet = readAt (curDelay) * (1.0f - g) + readAt (nextDelay) * g;

            if (++fadePos >= fadeLen)
            {
                curDelay = nextDelay;
                fading = false;
            }
        }
        else
        {
            wet = readAt (curDelay);

            if (! moving)
            {
                // Back at true pitch: after a moment, fade over to the guitar with no delay.
                if (curDelay > 0.0 && ++restCount > fadeMax)
                    startFade (0.0, fadeMax);
            }
            else
            {
                const double speed = std::abs (step);
                const int fade = (int) std::clamp (0.4 * (double) hopMid / std::max (speed, 1.0e-6), (double) fadeMin, (double) fadeMax);

                if (ratio > 1.0 && curDelay <= guard + 1.3 * fade * speed + 1.0)
                {
                    const double hop = findHop (curDelay, true);
                    startFade (curDelay + hop, std::min (fade, (int) std::clamp (0.4 * hop / speed, (double) fadeMin, (double) fadeMax)));
                }
                else if (ratio < 1.0 && curDelay >= (double) downTrigger)
                {
                    const double hop = findHop (curDelay, false);
                    startFade (curDelay - hop, (int) std::clamp (0.4 * hop / speed, (double) fadeMin, (double) fadeMax));
                }
            }
        }

        const float amount = m.kind == 2 ? mix * (float) pos : mix;
        const float dryTarget = m.kind == 0 ? 1.0f - mix : 1.0f - 0.29f * amount;
        const float wetTarget = m.kind == 0 ? mix : 0.71f * amount;
        dryGain += (dryTarget - dryGain) * gainStep;
        wetGain += (wetTarget - wetGain) * gainStep;

        const float y = x * dryGain + wet * wetGain;
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }
}

} // namespace gigrig::fx
