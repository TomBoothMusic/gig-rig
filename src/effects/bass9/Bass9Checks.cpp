#include "BassMachine.h"
#include "../Registry.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <cmath>
#include <complex>
#include <functional>
#include <random>

// Checks for the bass (the app's copy of the Bass9 PRECISION sound), run by the automatic builds.

namespace gigrig::fx
{
using namespace bass9;

namespace
{
    constexpr double rate = 96000.0;
    constexpr double twoPi = 6.283185307179586;

    std::string num (double v, int places)
    {
        char t[32];
        std::snprintf (t, sizeof (t), "%.*f", places, v);
        return t;
    }

    /** Strength of one frequency in a signal (Goertzel). */
    double strength (const std::vector<float>& x, size_t from, double hz)
    {
        const double w = twoPi * hz / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = from; i < x.size(); ++i)
        {
            const double s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double n = (double) (x.size() - from);
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
    }

    double rms (const std::vector<float>& x, size_t from)
    {
        double sum = 0.0;
        for (size_t i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) (x.size() - from));
    }

    std::vector<float> run (const BassSettings& s, const std::vector<float>& in)
    {
        BassMachine m;
        m.prepare (rate);
        m.setSettings (s);
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); i += 64)
            m.process (in.data() + i, out.data() + i, (int) std::min<size_t> (64, in.size() - i));
        return out;
    }

    /** A plucked note: harmonics that die away, higher ones faster. */
    std::vector<float> pluck (double hz, double seconds, double level)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        for (int h = 1; h <= 20 && h * hz < 8000.0; ++h)
        {
            const double a = std::abs (std::sin (3.14159265 * h * 0.18)) / h;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / rate;
                x[i] += (float) (level * a * std::exp (-t * (1.0 + 0.6 * h)) * std::sin (twoPi * h * hz * t));
            }
        }
        return x;
    }


    // ---- a made-up but realistic raw guitar, to catch what plain tones do not ----
    // Stiff strings (harmonics a little sharp), every harmonic dying at its own rate and
    // beating slowly against a twin, a pick click, hiss and mains hum. The first version of
    // the bass passed every plain-tone check and still ticked about 100 times a second on this.
    struct Played { double start, hz, level, stop; };

    void addPlayed (std::vector<float>& x, const Played& n, unsigned seed)
    {
        std::mt19937 random (seed);
        std::uniform_real_distribution<double> any (0.0, 1.0);
        const size_t from = (size_t) (n.start * rate);

        for (int h = 1; h <= 40; ++h)
        {
            const double f = h * n.hz * std::sqrt (1.0 + 1.2e-4 * h * h);
            if (f > 9000.0)
                break;

            const double a = n.level * std::abs (std::sin (3.14159265 * h * 0.13)) * (std::abs (std::sin (3.14159265 * h * 0.22)) + 0.15)
                             / std::pow ((double) h, 1.1);
            const double dying = 0.7 + 0.22 * h, phase = twoPi * any (random), twin = (any (random) - 0.5) * 0.3;

            for (size_t i = from; i < x.size(); ++i)
            {
                const double t = (double) (i - from) / rate;
                double env = std::exp (-dying * t) * std::min (1.0, t / 0.0015);
                if (t > n.stop - n.start)
                    env *= std::exp (-(t - (n.stop - n.start)) / 0.012);
                if (env < 1.0e-6 && t > 0.01)
                    break;

                x[i] += (float) (a * env * (std::sin (twoPi * f * t + phase) + 0.25 * std::sin (twoPi * (f + twin) * t + 1.3 * phase)));
            }
        }

        std::normal_distribution<double> noise (0.0, 1.0);
        double click = 0.0;
        for (size_t i = from; i < std::min (x.size(), from + (size_t) (0.003 * rate)); ++i)
        {
            click += 0.2 * (noise (random) - click);
            x[i] += (float) (0.25 * n.level * click * std::exp (-(double) (i - from) / rate / 0.0008));
        }
    }

    std::vector<float> played (const std::vector<Played>& notes, double seconds, bool hissAndHum)
    {
        std::vector<float> x ((size_t) (seconds * rate), 0.0f);
        unsigned seed = 11;
        for (const auto& n : notes)
            addPlayed (x, n, seed++);

        if (hissAndHum)
        {
            std::mt19937 random (5);
            std::normal_distribution<double> hiss (0.0, 0.0004);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / rate;
                x[i] += (float) (hiss (random) + 0.0006 * (std::sin (twoPi * 50.0 * t) + 0.5 * std::sin (twoPi * 150.0 * t + 1.0)
                                                           + 0.25 * std::sin (twoPi * 250.0 * t + 2.0)));
            }
        }

        return x;
    }

    /** A bass line as a guitarist plays it on the low strings: some notes cut off by the next, some ringing over it. */
    std::vector<Played> bassLine()
    {
        const double hz[] = { 82.41, 82.41, 98.0, 110.0, 110.0, 123.47, 146.83, 164.81, 146.83, 123.47 };
        std::vector<Played> notes;
        double t = 0.2;

        for (int i = 0; i < 10; ++i)
        {
            const double length = i % 4 == 3 ? 0.6 : 0.3;
            notes.push_back ({ t, hz[i], 0.25 + 0.07 * (i % 3), t + length + (i % 3 == 2 ? 0.25 : -0.02) });
            t += length;
        }

        return notes;
    }

    /** A clean octave down of a guitar below 4.4 kHz has next to nothing above 6 kHz, so anything
        up there is ticks. Gives the whole of it and its biggest moment, in dB against the bass itself. */
    struct Ticks { double wholeDb, biggestDb; };

    Ticks ticksIn (const std::vector<float>& out)
    {
        // Three 2nd order high-passes at 6 kHz in a row.
        const double w = twoPi * 6000.0 / rate, alpha = std::sin (w) / (2.0 * 0.7071), a0 = 1.0 + alpha, cw = std::cos (w);
        const double b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
        double z1[3] {}, z2[3] {}, top = 0.0, all = 0.0, peak = 0.0, tick = 0.0;

        for (const float sample : out)
        {
            double v = sample;
            for (int stage = 0; stage < 3; ++stage)
            {
                const double y = b0 * v + z1[stage];
                z1[stage] = b1 * v - a1 * y + z2[stage];
                z2[stage] = b0 * v - a2 * y;
                v = y;
            }

            top += v * v;
            all += (double) sample * sample;
            peak = std::max (peak, std::abs ((double) sample));
            tick = std::max (tick, std::abs (v));
        }

        return { 10.0 * std::log10 (std::max (top, 1.0e-30) / std::max (all, 1.0e-30)), 20.0 * std::log10 (std::max (tick, 1.0e-15) / std::max (peak, 1.0e-15)) };
    }

    /** One strand of a sound followed through time: its level every 2 ms, in dB. The two
        running averages, each as long as one cycle of `clearOfHz`, shut out neighbours that far away. */
    std::vector<double> strandDb (const std::vector<float>& x, double hz, double clearOfHz, double fromSeconds, double toSeconds)
    {
        const size_t span = (size_t) std::lround (rate / clearOfHz);
        std::vector<std::complex<double>> first (x.size()), second (x.size());
        std::complex<double> sum;

        for (size_t i = 0; i < x.size(); ++i)
        {
            first[i] = (double) x[i] * std::polar (1.0, -twoPi * hz * (double) i / rate);
            sum += first[i];
            if (i >= span)  sum -= first[i - span];
            second[i] = sum / (double) span;
        }

        std::vector<double> db;
        sum = {};
        for (size_t i = 0; i < x.size(); ++i)
        {
            sum += second[i];
            if (i >= span)  sum -= second[i - span];
            const double t = (double) i / rate;
            if (t >= fromSeconds && t < toSeconds && i % 192 == 0)
                db.push_back (20.0 * std::log10 (2.0 * std::abs (sum) / (double) span + 1.0e-12));
        }

        return db;
    }

    /** Of what comes out between 30 and 1300 Hz, how much is NOT at the harmonics of the note an
        octave down, in dB against what is. A clean octave down gives about -40 here (what is
        left is the measuring window itself); the first version gave as little as -13. */
    double offNoteDb (const std::vector<float>& out, double noteHz, double fromSeconds)
    {
        const size_t n = 32768;
        std::vector<std::complex<double>> a (n);
        const size_t from = (size_t) (fromSeconds * rate);
        for (size_t i = 0; i < n; ++i)
            a[i] = (from + i < out.size() ? out[from + i] : 0.0f) * (0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n));

        for (size_t i = 1, j = 0; i < n; ++i)       // plain radix-2 FFT
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const std::complex<double> turn = std::polar (1.0, -twoPi / (double) len);
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0);
                for (size_t j = 0; j < len / 2; ++j, w *= turn)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                }
            }
        }

        const double binHz = rate / (double) n, f0 = 0.5 * noteHz;
        double on = 0.0, off = 0.0;

        for (size_t bin = (size_t) (30.0 / binHz); bin < (size_t) (1300.0 / binHz); ++bin)
        {
            const double f = (double) bin * binHz, h = std::round (f / f0);
            const double harmonic = h * f0 * std::sqrt (1.0 + 1.2e-4 * h * h);
            (h >= 1.0 && std::abs (f - harmonic) < 3.0 * binHz + 0.003 * f ? on : off) += std::norm (a[bin]);
        }

        return 10.0 * std::log10 (std::max (off, 1.0e-30) / std::max (on, 1.0e-30));
    }

    /** Plain settings, so a check measures the octave itself and not the voicing. */
    BassSettings plain()
    {
        BassSettings s;
        s.compress = 0.0f;
        s.drive = 0.0f;
        s.gateDb = -90.0f;
        s.lowDb = s.lowMidDb = s.midDb = 0.0f;
        s.tone = 1.0f;
        s.pluck = 0.0f;
        s.brightDb = 0.0f;
        return s;
    }
}

void bass9Checks (const Check& check)
{
    // ---- presets as text ----
    {
        BassSettings s;
        s.subMix = 0.3f;
        s.tone = 0.25f;
        s.levelDb = -4.5f;
        const auto back = BassSettings::fromText (s.toText());
        check (back == s, "bass: a preset written as text reads back the same");

        const auto odd = BassSettings::fromText ("# a comment\ntone = 0.4\nsubMix=7\nnonsense=3\nlevelDb=abc\n");
        check (std::abs (odd.tone - 0.4f) < 1.0e-6f && odd.subMix == 1.0f && odd.levelDb == BassSettings().levelDb,
               "bass: preset text tolerates spaces, comments, unknown names and bad numbers, and keeps values in range");
    }

    // ---- one octave down ----
    {
        std::vector<float> in ((size_t) rate);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = (float) (0.3 * std::sin (twoPi * 110.0 * (double) i / rate));

        const auto out = run (plain(), in);
        const auto from = (size_t) (0.4 * rate);
        const double sub = strength (out, from, 55.0), same = strength (out, from, 110.0), lower = strength (out, from, 27.5);
        check (sub > 10.0 * same && sub > 10.0 * lower, "bass: 110 Hz in gives 55 Hz out (55: " + num (sub, 4)
                                                        + ", 110: " + num (same, 4) + ", 27.5: " + num (lower, 4) + ")");

        auto two = plain();
        two.subMix = 1.0f;
        const auto out2 = run (two, in);
        const double s2 = strength (out2, from, 27.5), s1 = strength (out2, from, 55.0);
        check (s2 > 5.0 * s1, "bass: sub mix at 10 gives two octaves down (27.5: " + num (s2, 4) + ", 55: " + num (s1, 4) + ")");

        const double ratio = rms (out, from) / rms (in, from);
        check (ratio > 0.7 && ratio < 1.4, "bass: comes out about as loud as the guitar went in (ratio " + num (ratio, 2) + ")");
    }

    // ---- same level every time, whatever came before the note ----
    // Without the bands being lined up, a note's level depends on chance: up to 4 times quieter.
    for (double hz : { 82.4, 196.0, 440.0 })
    {
        std::mt19937 random (7);
        std::normal_distribution<float> hiss (0.0f, 0.001f);
        double low = 1.0e9, high = 0.0;

        for (int trial = 0; trial < 6; ++trial)
        {
            std::vector<float> in ((size_t) (0.9 * rate));
            const size_t start = (size_t) ((0.2 + 0.013 * trial) * rate);
            for (size_t i = 0; i < in.size(); ++i)
            {
                in[i] = hiss (random);
                if (i >= start)
                {
                    const double t = (double) (i - start) / rate;
                    in[i] += (float) (0.3 * std::sin (twoPi * hz * t + trial) * std::min (1.0, t / 0.005));
                }
            }

            const auto level = rms (run (plain(), in), (size_t) (0.65 * rate));
            low = std::min (low, level);
            high = std::max (high, level);
        }

        const double spreadDb = 20.0 * std::log10 (high / std::max (low, 1.0e-12));
        check (spreadDb < 1.5, "bass: a " + num (hz, 1) + " Hz note comes out at the same level every time (spread "
                               + num (spreadDb, 2) + " dB)");
    }

    // ---- every pitch at the same level ----
    {
        double low = 1.0e9, high = -1.0e9;
        for (double hz = 82.0; hz < 1600.0; hz *= 1.0595 * 1.0293)      // every semitone and a half
        {
            std::vector<float> in ((size_t) (0.5 * rate));
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = (float) (0.2 * std::sin (twoPi * hz * (double) i / rate));

            const auto from = (size_t) (0.35 * rate);
            const double db = 20.0 * std::log10 (rms (run (plain(), in), from) / rms (in, from));
            low = std::min (low, db);
            high = std::max (high, db);
        }

        check (high - low < 1.0, "bass: every pitch from the low E up comes out at the same level (" + num (low, 2) + " to " + num (high, 2) + " dB)");
    }

    // ---- a real guitar, not a test tone: no ticks, no static ----
    // Tom, 25 Nov 2026: "clicky artifacts the whole time" and "static noise". Plain tones never showed it.
    const auto lineNotes = bassLine();
    const auto line = played (lineNotes, lineNotes.back().stop + 0.5, true);
    const double singles[] = { 82.41, 110.0, 146.83, 196.0 };
    std::vector<std::vector<float>> single;
    for (double hz : singles)
        single.push_back (played ({ { 0.1, hz, 0.3, 0.9 } }, 1.0, true));

    for (float tracking : { 0.0f, 0.6f, 1.0f })
    {
        auto s = plain();
        s.tracking = tracking;
        const std::string at = " (Tracking speed " + num (tracking, 1) + ")";

        const auto heard = ticksIn (run (s, line));
        check (heard.wholeDb < -72.0 && heard.biggestDb < -62.0,
               "bass: a played bass line with hiss and hum comes out without ticks" + at + ": above 6 kHz "
               + num (heard.wholeDb, 1) + " dB, biggest tick " + num (heard.biggestDb, 1) + " dB against the bass");

        double worst = -200.0;
        for (size_t i = 0; i < single.size(); ++i)
            worst = std::max (worst, offNoteDb (run (s, single[i]), singles[i], 0.25));

        check (worst < -30.0, "bass: a single played note comes out as the note an octave down and nothing else" + at
                              + ": the rest is " + num (worst, 1) + " dB down at worst");

        // Bends, vibrato and slides move a tone from filter to filter.
        const auto moving = [] (const std::function<double (double)>& hzAt, double seconds)
        {
            std::vector<float> x ((size_t) (seconds * rate));
            double phase = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / rate;
                phase += hzAt (t) / rate;
                double sum = 0.0;
                for (int h = 1; h <= 8; ++h)
                    sum += std::sin (twoPi * h * phase + h) / h;
                x[i] = (float) (0.15 * sum * std::min (1.0, t / 0.005));
            }
            return x;
        };

        const auto bend = ticksIn (run (s, moving ([] (double t) { return 110.0 * std::pow (2.0, (1.0 / 12.0) * (1.0 - std::cos (twoPi * t / 0.8))); }, 1.7)));
        const auto vibrato = ticksIn (run (s, moving ([] (double t) { return 146.83 * std::pow (2.0, 0.4 / 12.0 * std::sin (twoPi * 6.0 * t)); }, 1.2)));
        const auto slide = ticksIn (run (s, moving ([] (double t) { return 82.41 * std::pow (2.0, std::clamp ((t - 0.3) / 0.4, 0.0, 1.0)); }, 1.2)));
        const double biggest = std::max ({ bend.biggestDb, vibrato.biggestDb, slide.biggestDb });
        check (biggest < -78.0, "bass: a bend, a vibrato and a slide come out without ticks" + at + ": biggest "
                                + num (biggest, 1) + " dB against the bass");
    }

    // ---- two octaves down, and other sample rates ----
    {
        auto two = plain();
        two.subMix = 1.0f;
        const auto t = ticksIn (run (two, line));
        check (t.wholeDb < -88.0 && t.biggestDb < -74.0, "bass: two octaves down, the played bass line comes out without ticks: above 6 kHz "
                                                         + num (t.wholeDb, 1) + " dB, biggest tick " + num (t.biggestDb, 1) + " dB");

        std::string levels;
        bool same = true;
        for (double other : { 44100.0, 48000.0, 192000.0 })
        {
            BassMachine m;
            m.prepare (other);
            m.setSettings (plain());
            std::vector<float> in ((size_t) (0.5 * other)), out (in.size());
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = (float) (0.2 * std::sin (twoPi * 110.0 * (double) i / other));
            for (size_t i = 0; i < in.size(); i += 64)
                m.process (in.data() + i, out.data() + i, (int) std::min<size_t> (64, in.size() - i));

            const auto from = (size_t) (0.35 * other);
            const double db = 20.0 * std::log10 (rms (out, from) / rms (in, from));
            same = same && std::abs (db) < 1.0;
            levels += (levels.empty() ? "" : ", ") + num (db, 2);
        }

        check (same, "bass: sounds at the same level at 44.1, 48 and 192 kHz (" + levels + " dB)");
    }

    // ---- no warble: every harmonic of a dying note comes out at its own level and steady ----
    // Build 77 let the strong low harmonics of a note upset the bands that held its upper
    // harmonics: those came out 6 to 20 dB down and wobbling by 3 to 9 dB, which Tom heard as
    // a warble (25 Nov 2026). A real string's harmonics are a little sharp and each dies at
    // its own rate, so that is what is played here.
    for (float tracking : { 0.0f, 0.6f, 1.0f })
    {
        const double hz = 82.41, start = 0.1, from = 0.3, to = 0.9;
        std::vector<float> in ((size_t) (1.0 * rate), 0.0f);
        struct Part { double hz, level, dying; };
        std::vector<Part> parts;

        for (int h = 1; h <= 26; ++h)
        {
            const Part p { h * hz * std::sqrt (1.0 + 1.2e-4 * h * h), 0.25 / h, 0.7 + 0.22 * h };
            parts.push_back (p);
            for (size_t i = (size_t) (start * rate); i < in.size(); ++i)
            {
                const double t = (double) i / rate - start;
                in[i] += (float) (p.level * std::exp (-p.dying * t) * std::min (1.0, t / 0.0015) * std::sin (twoPi * p.hz * t + 1.7 * h));
            }
        }

        auto s = plain();
        s.tracking = tracking;
        const auto out = run (s, in);
        double worstLevel = 0.0, worstWobble = 0.0;

        for (int h = 4; h <= 16; ++h)
        {
            const auto& p = parts[(size_t) (h - 1)];
            const auto db = strandDb (out, 0.5 * p.hz, 0.5 * hz, from, to);

            // A harmonic dying evenly is a straight line in dB: how far off the line it strays is the wobble.
            const double n = (double) db.size();
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (size_t i = 0; i < db.size(); ++i)
            {
                sx += (double) i;  sy += db[i];  sxx += (double) i * (double) i;  sxy += (double) i * db[i];
            }
            const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), base = (sy - slope * sx) / n;
            double off = 0.0;
            for (size_t i = 0; i < db.size(); ++i)
                off += std::pow (db[i] - (base + slope * (double) i), 2.0);

            const double middle = 0.5 * (from + to) - start;
            const double wanted = 20.0 * std::log10 (p.level) - 8.686 * p.dying * middle;
            const double level = base + slope * 0.5 * (n - 1.0) - wanted;
            if (std::abs (level) > std::abs (worstLevel))
                worstLevel = level;
            worstWobble = std::max (worstWobble, std::sqrt (off / n));
        }

        check (std::abs (worstLevel) < 4.0 && worstWobble < 1.5,
               "bass: the harmonics of a dying low E come out at their own level and steady, no warble (Tracking speed "
               + num (tracking, 1) + "): furthest off " + num (worstLevel, 1) + " dB, biggest wobble " + num (worstWobble, 2) + " dB");
    }

    // ---- drive and compression work the same on a quiet guitar as on a hot one ----
    // They used to work against a fixed level near full scale. Tom's guitar arrived 28 dB
    // below that, and neither did anything he could hear.
    {
        const auto third = [] (double level, float drive)
        {
            std::vector<float> in ((size_t) (1.0 * rate));
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = (float) (level * std::sin (twoPi * 110.0 * (double) i / rate));

            auto s = plain();
            s.drive = drive;
            const auto out = run (s, in);
            const auto from = (size_t) (0.5 * rate);
            return 20.0 * std::log10 (strength (out, from, 165.0) / strength (out, from, 55.0));
        };

        const double clean = third (0.3, 0.0f), hot = third (0.3, 0.6f), quiet = third (0.003, 0.6f);
        check (clean < -50.0 && hot > -30.0 && std::abs (hot - quiet) < 1.0,
               "bass: Drive adds the same grit to a quiet guitar as to a hot one (third harmonic: " + num (clean, 1)
               + " dB with Drive off, " + num (hot, 1) + " dB at 0.6 on a hot guitar, " + num (quiet, 1) + " dB on one 40 dB quieter)");

        // A loud note then one 12 dB softer: compression brings them closer, by the same amount at any level.
        const auto apart = [] (double level, float compress)
        {
            std::vector<float> in ((size_t) (2.0 * rate));
            for (size_t i = 0; i < in.size(); ++i)
            {
                const double t = (double) i / rate;
                in[i] = (float) (level * (t < 1.0 ? 1.0 : 0.25) * std::sin (twoPi * 110.0 * t));
            }

            auto s = plain();
            s.compress = compress;
            const auto out = run (s, in);
            const std::vector<float> loud (out.begin() + (long) (0.6 * rate), out.begin() + (long) (1.0 * rate));
            const std::vector<float> soft (out.begin() + (long) (1.6 * rate), out.begin() + (long) (2.0 * rate));
            return 20.0 * std::log10 (rms (loud, 0) / rms (soft, 0));
        };

        const double none = apart (0.3, 0.0f), squeezed = apart (0.3, 1.0f), squeezedQuiet = apart (0.003, 1.0f);
        check (std::abs (none - 12.0) < 0.5 && squeezed < 6.0 && std::abs (squeezed - squeezedQuiet) < 0.5,
               "bass: Compression evens out a quiet guitar the same as a hot one (two notes 12 dB apart come out " + num (none, 1)
               + " dB apart with it off, " + num (squeezed, 1) + " dB at 1, and " + num (squeezedQuiet, 1) + " dB on a guitar 40 dB quieter)");
    }

    // ---- the pluck: a click on the front of each note, and none of the guitar after it ----
    {
        const std::vector<Played> notes { { 0.2, 110.0, 0.3, 0.9 }, { 0.9, 146.83, 0.3, 1.6 } };
        const auto in = played (notes, 1.8, true);
        auto with = plain(), without = plain();
        with.pluck = 0.5f;
        const auto a = run (with, in), b = run (without, in);

        // What the pluck added, 2 ms at a time.
        std::vector<double> added;
        for (size_t i = 0; i + 192 <= a.size(); i += 192)
        {
            double sum = 0.0;
            for (size_t k = i; k < i + 192; ++k)
                sum += std::pow ((double) a[k] - b[k], 2.0);
            added.push_back (std::sqrt (sum / 192.0));
        }

        const auto most = [&added] (double fromSeconds, double toSeconds)
        {
            double m = 0.0;
            for (size_t i = (size_t) (fromSeconds / 0.002); i < std::min (added.size(), (size_t) (toSeconds / 0.002)); ++i)
                m = std::max (m, added[i]);
            return m;
        };

        const double first = most (0.2, 0.215), second = most (0.9, 0.915);
        const double between = std::max (most (0.26, 0.9), most (0.96, 1.8)), bass = rms (b, (size_t) (0.3 * rate));
        const double clickDb = 20.0 * std::log10 (std::min (first, second) / bass), restDb = 20.0 * std::log10 (between / std::min (first, second) + 1.0e-12);
        check (clickDb > -20.0 && clickDb < 6.0 && restDb < -30.0,
               "bass: the pluck puts a click on the front of each note and nothing of the guitar after it (click " + num (clickDb, 1)
               + " dB against the bass, the rest of the note " + num (restDb, 1) + " dB below the click)");

        // Hiss and hum alone must not open it.
        const auto hiss = played ({}, 1.0, true);
        const auto c = run (with, hiss), d = run (without, hiss);
        double extra = 0.0;
        for (size_t i = (size_t) (0.2 * rate); i < c.size(); ++i)
            extra += std::pow ((double) c[i] - d[i], 2.0);
        const double hissDb = 20.0 * std::log10 (std::sqrt (extra / (0.8 * rate)) / rms (hiss, (size_t) (0.2 * rate)) + 1.0e-12);
        check (hissDb < -20.0, "bass: hiss and hum alone do not open the pluck (it adds " + num (hissDb, 1) + " dB against the hiss going in)");
    }

    // ---- tone knob ----
    {
        const auto in = pluck (110.0, 1.0, 0.3);
        auto bright = plain(), dark = plain();
        bright.tone = 1.0f;
        dark.tone = 0.0f;
        const auto a = run (bright, in), b = run (dark, in);
        const double highA = strength (a, 4800, 825.0) + strength (a, 4800, 1100.0);
        const double highB = strength (b, 4800, 825.0) + strength (b, 4800, 1100.0);
        check (highA > 1.0e-5 && highB < 0.3 * highA, "bass: tone knob down takes the top off (825 and 1100 Hz: "
                                                      + num (highA, 5) + " up, " + num (highB, 5) + " down)");
    }

    // ---- gate, and nothing silly with silly input ----
    {
        std::vector<float> quiet ((size_t) (0.5 * rate));
        std::mt19937 random (3);
        std::normal_distribution<float> hiss (0.0f, 0.0002f);
        for (auto& v : quiet)
            v = hiss (random);

        const auto out = run (BassSettings(), quiet);
        check (rms (out, (size_t) (0.2 * rate)) < 1.0e-5, "bass: the gate keeps hiss from coming out as rumble");

        std::vector<float> loud ((size_t) (0.5 * rate), 0.0f);
        for (size_t i = 0; i < loud.size(); ++i)
            loud[i] = i % 200 < 100 ? 1.0f : -1.0f;
        loud[1000] = 1.0e6f;

        auto hot = BassSettings();
        hot.levelDb = 24.0f;
        hot.drive = 1.0f;
        bool finite = true;
        for (auto v : run (hot, loud))
            finite = finite && std::isfinite (v);
        check (finite, "bass: a huge input spike gives no broken numbers");
    }
}

} // namespace gigrig::fx
