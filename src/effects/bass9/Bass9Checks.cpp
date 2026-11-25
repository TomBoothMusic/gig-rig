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

    /** A clean octave down of a guitar below 2.4 kHz has next to nothing above 3 kHz, so anything
        up there is ticks. Gives the whole of it and its biggest moment, in dB against the bass itself. */
    struct Ticks { double wholeDb, biggestDb; };

    Ticks ticksIn (const std::vector<float>& out)
    {
        const double a = std::exp (-twoPi * 3000.0 / rate);
        double z[3] {}, p[3] {}, top = 0.0, all = 0.0, peak = 0.0, tick = 0.0;

        for (const float sample : out)
        {
            double v = sample;
            for (int stage = 0; stage < 3; ++stage)
            {
                const double y = a * (z[stage] + v - p[stage]);
                p[stage] = v;
                z[stage] = y;
                v = y;
            }

            top += v * v;
            all += (double) sample * sample;
            peak = std::max (peak, std::abs ((double) sample));
            tick = std::max (tick, std::abs (v));
        }

        return { 10.0 * std::log10 (std::max (top, 1.0e-30) / std::max (all, 1.0e-30)), 20.0 * std::log10 (std::max (tick, 1.0e-15) / std::max (peak, 1.0e-15)) };
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
        check (heard.wholeDb < -50.0 && heard.biggestDb < -40.0,
               "bass: a played bass line with hiss and hum comes out without ticks" + at + ": above 3 kHz "
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
        check (biggest < -50.0, "bass: a bend, a vibrato and a slide come out without ticks" + at + ": biggest "
                                + num (biggest, 1) + " dB against the bass");
    }

    // ---- two octaves down, and other sample rates ----
    {
        auto two = plain();
        two.subMix = 1.0f;
        const auto t = ticksIn (run (two, line));
        check (t.wholeDb < -50.0 && t.biggestDb < -40.0, "bass: two octaves down, the played bass line comes out without ticks: above 3 kHz "
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
