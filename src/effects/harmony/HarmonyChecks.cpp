// The vocal harmoniser's own checks. A made-up sung vowel (pulses through three formants,
// like "ah") stands in for Tom's voice: the harmony has to land on the right note, keep the
// vowel's character, come in on time and stay quiet when there is no note to follow.

#include "Harmony.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace gigrig::fx
{

using namespace test;

namespace
{
    constexpr double twoPi = 6.283185307179586;

    double hzOf (int midiNote)      { return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0); }

    /** A sung "ah" on one note, with a little breath in it. */
    std::vector<float> vowel (int midiNote, double seconds, double rate, double level = 0.2)
    {
        const double hz = hzOf (midiNote);
        std::vector<float> x ((size_t) (seconds * rate));
        Biquad f1, f2, f3;
        f1.bandPass (rate, 730.0, 6.0);
        f2.bandPass (rate, 1090.0, 8.0);
        f3.bandPass (rate, 2440.0, 10.0);
        std::mt19937 gen (7);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        const int harmonics = (int) std::min (60.0, 9000.0 / hz);

        for (size_t i = 0; i < x.size(); ++i)
        {
            const double phase = hz * (double) i / rate;
            double pulse = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                pulse += std::cos (twoPi * h * phase) / h;
            const float v = (float) pulse + 0.02f * dist (gen);
            x[i] = f1.run (v, 0) + 0.6f * f2.run (v, 0) + 0.3f * f3.run (v, 0);
        }

        const double r = rms (x);
        const size_t fade = (size_t) (0.02 * rate);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double edge = std::min ({ 1.0, (double) i / (double) fade, (double) (x.size() - 1 - i) / (double) fade });
            x[i] = (float) (x[i] * level / r * edge);
        }
        return x;
    }

    /** The note a stretch of sound is on, in Hz (the first strong peak of how well it matches itself moved along). */
    double pitchOf (const std::vector<float>& x, double fromSeconds, double seconds, double rate)
    {
        // Every other sample (or every fourth at 96 kHz) is plenty and much quicker.
        const int skip = rate > 60000.0 ? 4 : 2;
        const double r = rate / skip;
        std::vector<double> s;
        Biquad lp;
        lp.lowPass (rate, 4000.0);
        const size_t from = (size_t) (fromSeconds * rate), to = std::min (x.size(), from + (size_t) (seconds * rate));
        for (size_t i = from; i < to; ++i)
        {
            const float v = lp.run (x[i], 0);
            if ((i - from) % (size_t) skip == 0) s.push_back (v);
        }

        const int lagMin = (int) (r / 1300.0), lagMax = (int) (r / 60.0);
        if (s.size() < (size_t) lagMax * 2)
            return 0.0;

        std::vector<double> match ((size_t) lagMax + 2, 0.0);
        double best = 0.0;
        for (int lag = lagMin; lag <= lagMax + 1; ++lag)
        {
            double both = 0.0, a = 0.0, b = 0.0;
            for (size_t i = 0; i + (size_t) lag < s.size(); ++i)
            {
                both += s[i] * s[i + (size_t) lag];
                a += s[i] * s[i];
                b += s[i + (size_t) lag] * s[i + (size_t) lag];
            }
            match[(size_t) lag] = 2.0 * both / (a + b + 1.0e-20);
            best = std::max (best, match[(size_t) lag]);
        }

        for (int lag = lagMin + 1; lag <= lagMax; ++lag)
        {
            const double a = match[(size_t) lag - 1], b = match[(size_t) lag], c = match[(size_t) lag + 1];
            if (b > 0.9 * best && b >= a && b >= c)
                return r / (lag + 0.5 * (a - c) / (a - 2.0 * b + c - 1.0e-20));
        }
        return 0.0;
    }

    double cents (double got, double want)     { return got > 0.0 && want > 0.0 ? 1200.0 * std::log2 (got / want) : 9999.0; }

    /** How loud one region is (for comparing a vowel's formants). */
    double bandLevel (std::vector<float> x, double hz, double rate)
    {
        Biquad b;
        b.bandPass (rate, hz, 2.0);
        for (auto& v : x) v = b.run (v, 0);
        return rms (x, x.size() / 3);
    }

    /** Just the harmony voice: what came out with the lead taken away (the lead passes untouched). */
    std::vector<float> harmonyOf (Effect& fx, const std::vector<float>& x)
    {
        auto y = run (fx, x);
        for (size_t i = 0; i < x.size(); ++i) y[i] -= x[i];
        return y;
    }

    const char* noteNames[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    std::string noteName (int midiNote)     { return std::string (noteNames[((midiNote % 12) + 12) % 12]) + std::to_string (midiNote / 12 - 1); }
}

void harmonyChecks (const Check& check)
{
    // ---- which note the harmony sings ----
    {
        // In C, a 3rd above each note of the scale: C-E, D-F, E-G, F-A, G-B, A-C, B-D.
        const int notes[7] { 60, 62, 64, 65, 67, 69, 71 }, thirds[7] { 4, 3, 3, 4, 4, 3, 3 };
        bool ok = true;
        for (int k = 0; k < 7; ++k)
            ok = ok && Harmony::interval (0, 6, notes[k]) == thirds[k] && Harmony::interval (0, 6, notes[k] - 24) == thirds[k];
        check (ok, "harmony: a 3rd above stays in the key (in C: C-E, D-F, E-G, F-A, G-B, A-C, B-D)");

        // The same shape moved to another key: in A, A-C#, B-D, C#-E.
        check (Harmony::interval (9, 6, 69) == 4 && Harmony::interval (9, 6, 71) == 3 && Harmony::interval (9, 6, 73) == 3,
               "harmony: the key moves the whole pattern (in A: A-C#, B-D, C#-E)");

        bool others = Harmony::interval (0, 10, 60) == 12 && Harmony::interval (0, 0, 64) == -12 && Harmony::interval (0, 5, 67) == 0
                   && Harmony::interval (0, 8, 60) == 7 && Harmony::interval (0, 8, 71) == 6          // B up a 5th in C is F: a flat 5th
                   && Harmony::interval (0, 2, 60) == -7 && Harmony::interval (0, 4, 60) == -3 && Harmony::interval (0, 4, 64) == -4
                   && Harmony::interval (0, 9, 60) == 9 && Harmony::interval (0, 9, 64) == 8 && Harmony::interval (0, 7, 65) == 6
                   && Harmony::interval (0, 3, 60) == -5 && Harmony::interval (0, 1, 60) == -8;
        check (others, "harmony: octaves, unison, 4ths, 5ths and 6ths above and below all land on notes of the key");

        // A note outside the key still gets a sensible harmony (a 3rd is always 3 or 4 semitones).
        bool sane = true;
        for (int key = 0; key < 12; ++key)
            for (int n = 36; n < 96; ++n)
            {
                const int up = Harmony::interval (key, 6, n), down = Harmony::interval (key, 4, n);
                sane = sane && (up == 3 || up == 4 || up == 2 || up == 5) && (down <= -2 && down >= -5);
            }
        check (sane, "harmony: notes outside the key still get a close harmony in every key");
    }

    // ---- sung notes ----
    struct Case { int note, key, voice, semitones; };

    for (double rate : { 96000.0, 48000.0 })
    {
        const std::vector<Case> cases = rate > 60000.0
            ? std::vector<Case> { { 48, 0, 6, 4 }, { 50, 0, 6, 3 }, { 57, 0, 6, 3 }, { 60, 0, 10, 12 }, { 64, 0, 0, -12 }, { 69, 9, 2, -7 }, { 76, 0, 6, 3 }, { 45, 0, 4, -4 } }
            : std::vector<Case> { { 55, 0, 6, 4 }, { 62, 2, 8, 7 } };

        double worstCents = 0.0, worstLevel = 0.0, worstTilt = 0.0;
        std::string detail;

        for (const auto& c : cases)
        {
            auto fx = makeEffect ("harmony");
            fx->setParam ("key", (float) c.key);
            fx->setParam ("voice", (float) c.voice);
            fx->setParam ("level", 0.0f);
            fx->prepare (rate, 64);

            const auto x = vowel (c.note, 1.2, rate);
            const auto h = harmonyOf (*fx, x);
            const double got = pitchOf (h, 0.5, 0.5, rate), want = hzOf (c.note + c.semitones);
            const std::vector<float> xs (x.begin() + (long) (0.5 * rate), x.begin() + (long) (1.0 * rate)), hs (h.begin() + (long) (0.5 * rate), h.begin() + (long) (1.0 * rate));
            const double level = 20.0 * std::log10 (std::max (rms (hs), 1.0e-9) / rms (xs));
            // "ah" has its first formant at 730 Hz and its third at 2440: the harmony should keep both where they are.
            const double tilt = 20.0 * std::log10 (bandLevel (hs, 2440.0, rate) / bandLevel (hs, 730.0, rate))
                              - 20.0 * std::log10 (bandLevel (xs, 2440.0, rate) / bandLevel (xs, 730.0, rate));

            worstCents = std::max (worstCents, std::abs (cents (got, want)));
            worstLevel = std::max (worstLevel, std::abs (level));
            worstTilt = std::max (worstTilt, std::abs (tilt));
            detail += (detail.empty() ? "" : ", ") + noteName (c.note) + ">" + noteName (c.note + c.semitones);
        }

        const auto tag = "harmony at " + num (rate / 1000.0, 0) + " kHz: ";
        check (worstCents < 15.0, tag + "sung notes get the right harmony note (" + detail + "; furthest out " + num (worstCents, 0) + " hundredths of a semitone)");
        check (worstLevel < 3.0, tag + "with Harmony level at 0 dB the harmony is as loud as the lead (within " + num (worstLevel) + " dB)");
        check (worstTilt < 5.0, tag + "the harmony keeps the voice's own character: the vowel's formants stay put, even an octave away (within " + num (worstTilt) + " dB)");
    }

    constexpr double rate = 96000.0;

    {
        // The lead passes untouched and at once; the harmony follows about 24 ms behind.
        auto fx = makeEffect ("harmony");
        fx->setParam ("level", 0.0f);
        fx->prepare (rate, 64);
        std::vector<float> click ((size_t) (0.2 * rate), 0.0f);
        click[1000] = 0.5f;
        const auto y = run (*fx, click);
        check (y[1000] == 0.5f && y[999] == 0.0f && fx->latencySamples() == 0, "harmony: the lead voice passes straight through, untouched and with no delay");

        fx->reset();
        const auto x = vowel (57, 0.6, rate);
        const auto h = harmonyOf (*fx, x);
        size_t start = 0;
        while (start < h.size() && std::abs (h[start]) < 0.02f) ++start;
        const double ms = (double) start * 1000.0 / rate, said = Harmony::harmonyDelaySeconds() * 1000.0;
        check (ms > said - 2.0 && ms < said + 25.0, "harmony: the harmony voice comes in " + num (ms, 0) + " ms after the lead starts (it sings " + num (said, 0) + " ms behind)");
        check (fx->status() == "hears A3, sings C4", "harmony: its screen line says what it is doing (\"" + fx->status() + "\")");
    }

    {
        // Two notes in a row, and the key changed while a note is held.
        auto fx = makeEffect ("harmony");
        fx->setParam ("level", 0.0f);
        fx->prepare (rate, 64);
        auto x = vowel (60, 0.8, rate);
        const auto second = vowel (62, 0.8, rate);
        x.insert (x.end(), second.begin(), second.end());
        const auto h = harmonyOf (*fx, x);
        const double first = pitchOf (h, 0.3, 0.4, rate), next = pitchOf (h, 1.1, 0.4, rate);
        check (std::abs (cents (first, hzOf (64))) < 15.0 && std::abs (cents (next, hzOf (65))) < 15.0,
               "harmony: C then D with a 3rd above gives E then F (" + num (first) + " then " + num (next) + " Hz)");

        fx->reset();
        const auto held = vowel (60, 1.6, rate);
        auto y = held;
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i >= (size_t) (0.8 * rate) && i < (size_t) (0.8 * rate) + 64)
                fx->setParam ("key", 3.0f);     // C to Eb: the 3rd above C becomes Eb instead of E
            float* p = y.data() + i;
            fx->process (&p, 1, (int) std::min<size_t> (64, y.size() - i));
        }
        float stepLead = 0.0f, stepHarmony = 0.0f;
        for (size_t i = 1; i < y.size(); ++i)
        {
            stepLead = std::max (stepLead, std::abs (held[i] - held[i - 1]));
            stepHarmony = std::max (stepHarmony, std::abs ((y[i] - held[i]) - (y[i - 1] - held[i - 1])));
        }
        for (size_t i = 0; i < y.size(); ++i) y[i] -= held[i];
        const double before = pitchOf (y, 0.3, 0.4, rate), after = pitchOf (y, 1.1, 0.4, rate);
        check (std::abs (cents (before, hzOf (64))) < 15.0 && std::abs (cents (after, hzOf (63))) < 15.0 && stepHarmony < stepLead * 2.0f,
               "harmony: changing the key from C to Eb while a C is held moves the harmony from E to Eb without a click (" + num (before) + " then " + num (after)
               + " Hz; biggest step " + num (stepHarmony, 3) + ", the lead's own " + num (stepLead, 3) + ")");
    }

    {
        // No note to follow (a breath, an "s", applause): the harmony stays out of the way.
        auto fx = makeEffect ("harmony");
        fx->setParam ("level", 0.0f);
        fx->prepare (rate, 64);
        std::mt19937 gen (1);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> x ((size_t) (2.0 * rate));
        for (auto& v : x) v = 0.1f * dist (gen);
        const auto h = harmonyOf (*fx, x);
        const double level = 20.0 * std::log10 (std::max (rms (h, (size_t) rate), 1.0e-9) / rms (x, (size_t) rate));
        check (level < -8.0, "harmony: with only hiss to hear (no note), the harmony voice stays " + num (-level) + " dB under it");
    }
}

} // namespace gigrig::fx
