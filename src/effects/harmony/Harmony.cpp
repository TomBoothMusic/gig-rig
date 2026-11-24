#include "Harmony.h"

#include <algorithm>
#include <cstdio>

namespace gigrig::fx
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int scale[7] { 0, 2, 4, 5, 7, 9, 11 };                           // the major scale, in semitones
    constexpr int voiceSteps[11] { -7, -5, -4, -3, -2, 0, 2, 3, 4, 5, 7 };     // scale steps for each Voice choice
    constexpr int unisonVoice = 5;
    constexpr double unisonCents = 9.0;         // a unison is a touch sharp, so it sounds like a second singer

    float hermite (const std::vector<float>& b, int msk, double pos) noexcept
    {
        const auto i = (long long) std::floor (pos);
        const float f = (float) (pos - (double) i);
        const float y0 = b[(size_t) ((i - 1) & msk)], y1 = b[(size_t) (i & msk)], y2 = b[(size_t) ((i + 1) & msk)], y3 = b[(size_t) ((i + 2) & msk)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
}

const std::vector<std::string>& Harmony::keyNames()
{
    static const std::vector<std::string> names { "C / Am", "Db / Bbm", "D / Bm", "Eb / Cm", "E / C#m", "F / Dm",
                                                  "F# / D#m", "G / Em", "Ab / Fm", "A / F#m", "Bb / Gm", "B / G#m" };
    return names;
}

const std::vector<std::string>& Harmony::voiceNames()
{
    static const std::vector<std::string> names { "Octave below", "6th below", "5th below", "4th below", "3rd below", "Unison (double)",
                                                  "3rd above", "4th above", "5th above", "6th above", "Octave above" };
    return names;
}

int Harmony::interval (int key, int voice, int midiNote) noexcept
{
    const int steps = voiceSteps[std::clamp (voice, 0, 10)];
    const int pc = (((midiNote - key) % 12) + 12) % 12;       // the note's place counted from the key note

    // Which note of the scale it is. A note between two scale notes counts as the one below it.
    int degree = 0;
    for (int d = 0; d < 7; ++d)
        if (scale[d] <= pc)
            degree = d;

    const int target = degree + steps;
    const int octave = (target >= 0 ? target : target - 6) / 7;
    const int wrapped = target - octave * 7;
    return scale[wrapped] + 12 * octave - scale[degree];
}

Harmony::Harmony()
{
    // Ids never change: they are saved in presets and songs.
    pKey   = addChoice ("key",   "Key",   keyNames(), 0);
    pVoice = addChoice ("voice", "Voice", voiceNames(), 6);
    pLevel = addParam ("level",  "Harmony level", "dB", -30.0f, 6.0f, -6.0f);
}

std::string Harmony::status() const
{
    static const char* names[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int n = shownNote.load (std::memory_order_relaxed);

    if (n < 0)
        return "no note heard just now";

    const int h = n + shownInterval.load (std::memory_order_relaxed);
    char text[96];
    std::snprintf (text, sizeof (text), "hears %s%d, sings %s%d", names[n % 12], n / 12 - 1, names[((h % 12) + 12) % 12], h / 12 - 1);
    return text;
}

//==============================================================================
void Harmony::onPrepare (double rate, int)
{
    bridge.prepare (rate);
    inner = bridge.innerRate (rate);

    // The tracker listens at about 12 kHz to what is below 1.5 kHz.
    decimate = std::max (1, (int) std::lround (inner / 12000.0));
    const double slow = inner / decimate;
    low1.lowPass (inner, 1500.0, 0.5412);
    low2.lowPass (inner, 1500.0, 1.3066);
    high.highPass (inner, 70.0);
    lagMin = std::max (4, (int) std::floor (slow / highestHz));
    lagMax = std::min (window / 2 - 2, (int) std::ceil (slow / lowestHz) + 1);
    nsdf.assign ((size_t) lagMax + 2, 0.0f);

    in.assign (bufferSize, 0.0f);
    energy.assign (bufferSize, 0.0f);
    ola.assign (bufferSize, 0.0f);

    maxPeriod = inner / lowestHz;
    restPeriod = inner * 0.005;
    delay = (int) std::ceil (harmonyDelaySeconds() * inner);
    energyHigh.highPass (inner, 200.0);
    energyCoeff = 1.0 - std::exp (-1.0 / (0.0007 * inner));
    onReset();
}

void Harmony::onReset() noexcept
{
    bridge.clear();
    low1.clear(); low2.clear(); high.clear();
    ring.fill (0.0f);
    snap.fill (0.0f);
    ringPos = sinceUpdate = decimateCount = 0;
    lagCursor = 0;
    period = restPeriod;
    voiced = false;
    pendingHz = 0.0;
    unvoicedRuns = 0;

    std::fill (in.begin(), in.end(), 0.0f);
    std::fill (energy.begin(), energy.end(), 0.0f);
    std::fill (ola.begin(), ola.end(), 0.0f);
    marks = {};
    markCount = 0;
    now = 0;
    nextMark = 0.0;
    nextGrain = 0.0;
    ratioNow = 1.0f;
    grainGain = 0.0f;
    energyHigh.clear();
    energyNow = 0.0;
    shownNote.store (-1, std::memory_order_relaxed);
}

//==============================================================================
// The pitch tracker: the "normalised square difference" of McLeod and Wyvill ("A smarter
// way to find pitch", 2005). For every candidate cycle length it asks how alike the sound
// is to itself that much later (1 = identical), then takes the first really good match.

void Harmony::trackerFeed (float x) noexcept
{
    const float v = high.run (low2.run (low1.run (x, 0), 0), 0);

    if (++decimateCount < decimate)
        return;

    decimateCount = 0;
    ring[(size_t) ringPos] = v;
    ringPos = (ringPos + 1) % window;

    // A fresh look every 64 slow samples (about 5 ms), once the last one is finished.
    if (++sinceUpdate >= 64 && lagCursor == 0)
    {
        sinceUpdate = 0;
        double power = 0.0;
        for (int i = 0; i < window; ++i)
        {
            snap[(size_t) i] = ring[(size_t) ((ringPos + i) % window)];
            power += (double) snap[(size_t) i] * snap[(size_t) i];
        }
        snapPower = power;
        lagCursor = 1;
    }
}

void Harmony::trackerFinish() noexcept
{
    // Too quiet to have a note (about -60 dB).
    const bool loud = snapPower / window > 1.0e-6;

    // The first peak that is nearly as good as the best one: that is one cycle (a later,
    // slightly better peak would be two or three cycles, an octave too low).
    float best = 0.0f;
    for (int lag = lagMin; lag <= lagMax; ++lag)
        best = std::max (best, nsdf[(size_t) lag]);

    double found = 0.0;
    float clarity = 0.0f;

    if (loud && best > 0.5f)
    {
        // Skip the first downhill run from "identical at no shift at all".
        int lag = 1;
        while (lag < lagMax && nsdf[(size_t) lag] > 0.0f) ++lag;
        lag = std::max (lag, lagMin);

        for (; lag < lagMax; ++lag)
        {
            const float a = nsdf[(size_t) lag - 1], b = nsdf[(size_t) lag], c = nsdf[(size_t) lag + 1];
            if (b >= best * 0.9f && b >= a && b > c)
            {
                // The top of a curve through the three points nearest the peak.
                const float bend = a - 2.0f * b + c;
                const float shift = std::abs (bend) > 1.0e-9f ? 0.5f * (a - c) / bend : 0.0f;
                found = (double) lag + std::clamp (shift, -0.5f, 0.5f);
                clarity = b;
                break;
            }
        }
    }

    const double slow = inner / decimate;
    const bool good = found > 0.0 && clarity > (voiced ? 0.6f : 0.8f);

    if (! good)
    {
        // A couple of doubtful looks in a row before letting go of the note.
        if (++unvoicedRuns >= 3)
        {
            voiced = false;
            shownNote.store (-1, std::memory_order_relaxed);
        }
        return;
    }

    unvoicedRuns = 0;
    const double hz = slow / found;

    // A leap of more than four semitones from a held note has to show up twice running:
    // a single odd look is usually the tracker slipping an octave on a consonant.
    if (voiced)
    {
        const double held = inner / period;
        const double leap = std::abs (12.0 * std::log2 (hz / held));
        if (leap > 4.0 && ! (pendingHz > 0.0 && std::abs (12.0 * std::log2 (hz / pendingHz)) < 1.0))
        {
            pendingHz = hz;
            return;
        }
    }

    pendingHz = 0.0;
    period = std::clamp (inner / hz, inner / highestHz, maxPeriod);
    voiced = true;

    // Which note that is: it has to move most of the way to the next note before the note changes.
    const double exact = 69.0 + 12.0 * std::log2 (hz / 440.0);
    if (std::abs (exact - (double) note) > 0.65)
        note = (int) std::lround (exact);

    shownNote.store (std::clamp (note, 0, 127), std::memory_order_relaxed);
}

//==============================================================================
// Marks: one per cycle of the voice, each placed on the cycle's burst of energy (the
// moment the vocal cords snap shut), so every grain is cut around the same part of the wave.

void Harmony::placeMarks() noexcept
{
    const double t = voiced ? period : restPeriod;
    const double reach = voiced ? 0.3 * t : 0.0;

    // Fell far behind (after a reset, or a big change of period): start again from now.
    if (nextMark < (double) now - 3.0 * maxPeriod)
        nextMark = (double) now;

    while ((double) now >= nextMark + reach)
    {
        double pos = nextMark;

        if (voiced)
        {
            // The strongest moment near where the next cycle is expected, leaning towards the expected place.
            const auto from = (long long) std::ceil (nextMark - reach), to = (long long) std::floor (nextMark + reach);
            double top = -1.0;
            for (long long k = from; k <= to; ++k)
            {
                const double lean = 1.0 - 0.35 * std::abs ((double) k - nextMark) / reach;
                const double e = energy[(size_t) (k & mask)] * lean;
                if (e > top) { top = e; pos = (double) k; }
            }
        }

        auto& m = marks[(size_t) (markCount % numMarks)];
        m.pos = pos;
        m.period = t;
        m.voiced = voiced;
        ++markCount;
        nextMark = pos + t;
    }
}

void Harmony::writeGrain (double centre, double source, double grainPeriod, float gain) noexcept
{
    // Two cycles long under a smooth (Hann) window; grains one cycle apart add up to the voice itself.
    const auto first = (long long) std::ceil (centre - grainPeriod), last = (long long) std::floor (centre + grainPeriod);
    const double shift = source - centre;

    for (long long k = first; k <= last; ++k)
    {
        const double u = ((double) k - centre) / grainPeriod;
        const float w = (float) (0.5 + 0.5 * std::cos (pi * u));
        ola[(size_t) (k & mask)] += gain * w * hermite (in, mask, (double) k + shift);
    }
}

void Harmony::runInner (float* x, int m) noexcept
{
    const int key = (int) std::lround (value (pKey));
    const int voice = (int) std::lround (value (pVoice));

    for (int i = 0; i < m; ++i)
    {
        const float s = x[i];
        in[(size_t) (now & mask)] = s;

        const float h = energyHigh.run (s, 0);
        energyNow += energyCoeff * ((double) h * h - energyNow);
        if (energyNow < 1.0e-30) energyNow = 0.0;
        energy[(size_t) (now & mask)] = (float) energyNow;

        // ---- the tracker: fed every sample, and one candidate cycle length worked out per sample ----
        trackerFeed (s);

        if (lagCursor > 0)
        {
            const int lag = lagCursor;
            double match = 0.0, sizes = 0.0;
            const int count = window - lag;
            for (int j = 0; j < count; ++j)
            {
                const float a = snap[(size_t) j], b = snap[(size_t) (j + lag)];
                match += a * b;
                sizes += a * a + b * b;
            }
            nsdf[(size_t) lag] = sizes > 1.0e-20 ? (float) (2.0 * match / sizes) : 0.0f;

            if (++lagCursor > lagMax)
            {
                lagCursor = 0;
                nsdf[0] = 1.0f;
                trackerFinish();
            }
        }

        // ---- how far to move the pitch ----
        float ratioTarget = 1.0f;
        if (voiced)
        {
            const int semis = interval (key, voice, note);
            shownInterval.store (semis, std::memory_order_relaxed);
            ratioTarget = (float) std::pow (2.0, ((double) semis + (voice == unisonVoice ? unisonCents * 0.01 : 0.0)) / 12.0);
        }
        ratioNow += (ratioTarget - ratioNow) * (float) (1.0 / (0.012 * inner));

        placeMarks();

        // ---- grains: written just before the harmony's read point reaches where they start ----
        const double readAt = (double) (now - delay);
        if (nextGrain < readAt)
            nextGrain = readAt + restPeriod;

        for (int guard = 0; guard < 8; ++guard)
        {
            // The newest mark at or before this grain's place.
            const Mark* mark = nullptr;
            for (int k = 1; k <= std::min (markCount, numMarks); ++k)
            {
                const auto& c = marks[(size_t) ((markCount - k) % numMarks)];
                if (c.pos <= nextGrain) { mark = &c; break; }
            }

            const double t = mark != nullptr ? mark->period : restPeriod;
            if (nextGrain - t > readAt + 1.0)
                break;

            const bool sung = mark != nullptr && mark->voiced;
            const float ratio = sung ? ratioNow : 1.0f;
            // Closer grains add up louder: keep the harmony as loud as the lead. Breaths and
            // consonants come through quietly, so an "s" is not doubled.
            const float target = sung ? 1.0f / std::sqrt (ratio) : 0.3f;
            grainGain += 0.5f * (target - grainGain);

            if (mark != nullptr)
                writeGrain (nextGrain, mark->pos, t, grainGain);

            nextGrain += t / ratio;
        }

        // ---- the harmony, read 'delay' behind, and its place in the buffer wiped for next time ----
        const auto at = (size_t) ((now - delay) & mask);
        x[i] = ola[at];
        ola[at] = 0.0f;
        ++now;
    }
}

void Harmony::processChunk (float* const* ch, int numCh, int n) noexcept
{
    std::array<float, HalfRate::maxChunk> voiceCopy {};
    std::copy (ch[0], ch[0] + n, voiceCopy.begin());
    bridge.process (voiceCopy.data(), n, [this] (float* x, int m) { runInner (x, m); });

    const float level = dbToGain (value (pLevel));
    for (int i = 0; i < n; ++i)
        ch[0][i] += level * voiceCopy[(size_t) i];

    for (int c = 1; c < numCh; ++c)
        std::copy (ch[0], ch[0] + n, ch[c]);
}

} // namespace gigrig::fx
