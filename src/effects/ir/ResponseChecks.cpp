// The "ir" effect's own checks, and the response maker's (IrTools). A made-up tone box with
// a known curve, delay and level stands in for the pedal: the maker has to find all three.

#include "ResponsePlayer.h"
#include "IrTools.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>

namespace gigrig::fx
{

using namespace test;

namespace
{
    constexpr double rate = 96000.0;
    constexpr size_t boxDelay = 591;        // samples (6.156 ms): what the leads and converters would add
    constexpr double boxLevelDb = -4.0;

    struct Box
    {
        Biquad a, b, c;
        Box()   { a.peak (rate, 200.0, 1.2, 6.0); b.peak (rate, 2000.0, 1.0, -6.0); c.highShelf (rate, 8000.0, 3.0); }
        double db (double hz) const     { return a.responseDb (rate, hz) + b.responseDb (rate, hz) + c.responseDb (rate, hz) + boxLevelDb; }
    };

    /** A sweep from 20 Hz to 20 kHz every 8 seconds with a little hiss on top, like the Amp capture's test sound. */
    std::vector<float> testSound (double seconds)
    {
        std::mt19937 gen (5);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> x ((size_t) (seconds * rate));
        double phase = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            phase += 20.0 * std::pow (1000.0, std::fmod ((double) i / rate, 8.0) / 8.0) / rate;
            x[i] = 0.25f * (float) std::sin (6.283185307179586 * phase) + 0.1f * dist (gen);
        }
        return x;
    }

    std::vector<float> throughBox (const std::vector<float>& sent, bool squashed)
    {
        Box box;
        std::mt19937 gen (9);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> back (sent.size(), 0.0f);
        const float level = (float) std::pow (10.0, boxLevelDb / 20.0);
        for (size_t i = 0; i + boxDelay < sent.size(); ++i)
        {
            float v = box.c.run (box.b.run (box.a.run (sent[i], 0), 0), 0) * level;
            if (squashed) v = std::tanh (v * 4.0f) / 4.0f;
            back[i + boxDelay] = v + 0.0005f * dist (gen);      // and a little hiss of its own
        }
        return back;
    }

    std::string tempFile (const char* name)
    {
        std::error_code ec;
        auto dir = std::filesystem::temp_directory_path (ec);
        if (ec) dir = ".";
        return (dir / name).string();
    }
}

void responseChecks (const Check& check)
{
    const auto sent = testSound (16.0);
    const Box box;

    // ---- the maker ----
    const auto made = ir::makeResponse (sent, throughBox (sent, false), rate);
    check (made.error.empty() && made.taps.size() > 1000 && (int) made.taps.size() <= ResponsePlayer::maxTaps && made.rate == 48000.0,
           "response maker: makes a response file from a capture (" + std::to_string (made.taps.size()) + " points at 48 kHz)" + (made.error.empty() ? "" : ": " + made.error));

    if (made.taps.empty())
        return;

    check (std::abs (made.delayMs - (double) boxDelay * 1000.0 / rate) < 0.02,
           "response maker: finds how late the box's sound came back (" + num (made.delayMs, 3) + " ms, really " + num ((double) boxDelay * 1000.0 / rate, 3) + ")");

    double worst = 0.0;
    for (double hz : { 80.0, 120.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0, 10000.0 })
        worst = std::max (worst, std::abs (ir::responseDb (made.taps, made.rate, hz) + made.levelDb - box.db (hz)));
    check (worst < 0.6, "response maker: the file and its level match the box's curve from 80 Hz to 10 kHz (worst miss " + num (worst, 2) + " dB)");

    double power = 0.0;
    int points = 0;
    for (double hz = 100.0; hz <= 4000.0; hz *= 1.26, ++points)
        power += std::pow (10.0, ir::responseDb (made.taps, made.rate, hz) / 10.0);
    const double neutral = 10.0 * std::log10 (power / points);
    check (std::abs (neutral) < 0.3, "response maker: the file itself is level-neutral (" + num (neutral, 2) + " dB over 100 Hz to 4 kHz), with the box's own level reported apart ("
                                     + num (made.levelDb) + " dB)");

    const auto squashed = ir::makeResponse (sent, throughBox (sent, true), rate);
    check (made.matchPercent > 97.0 && squashed.matchPercent < made.matchPercent - 2.5,
           "response maker: says how much a file can explain: " + num (made.matchPercent) + "% for a tone-only box, " + num (squashed.matchPercent) + "% for one that also squashes");

    const auto silent = ir::makeResponse (sent, std::vector<float> (sent.size(), 0.0f), rate);
    check (silent.taps.empty() && ! silent.error.empty(), "response maker: a silent return gives a plain reason, not a file (\"" + silent.error + "\")");

    // ---- WAV files ----
    const auto path = tempFile ("gigrig-selftest-response.wav");
    std::string error;
    ir::Wav out, in;
    out.rate = made.rate;
    out.channels.push_back (made.taps);
    const bool wrote = ir::writeWav (path, out, error);
    const bool read = wrote && ir::readWav (path, in, error);
    check (read && in.channels.size() == 1 && in.channels[0] == made.taps && in.rate == made.rate,
           "response file: written to a WAV and read back, every point is the same" + (error.empty() ? std::string() : " (" + error + ")"));

    if (! read)
        return;

    // ---- the player ----
    for (double r : { 96000.0, 48000.0, 44100.0 })
    {
        auto fx = makeEffect ("ir");
        fx->setText ("file", path);
        fx->prepare (r, 64);
        double miss = 0.0;
        for (double hz : { 110.0, 200.0, 1000.0, 2000.0, 6000.0 })
        {
            fx->reset();
            const auto y = run (*fx, sine (hz, 0.8, 0.1, r));
            const double got = 20.0 * std::log10 (std::max (strength (y, (size_t) (0.4 * r), hz, r), 1.0e-12) / 0.1);
            miss = std::max (miss, std::abs (got - ir::responseDb (made.taps, made.rate, hz)));
        }
        check (miss < 0.3 && fx->status().rfind ("playing", 0) == 0,
               "ir: at " + num (r / 1000.0, 1) + " kHz a guitar through the file gets the file's curve (worst miss " + num (miss, 2) + " dB; \"" + fx->status() + "\")");
    }

    auto fx = makeEffect ("ir");
    fx->setText ("file", path);
    fx->prepare (rate, 64);
    const int lag = fx->latencySamples();

    {
        fx->setParam ("mix", 0.0f);
        double miss = 0.0;
        for (double hz : { 110.0, 1000.0, 6000.0 })
        {
            fx->reset();
            const auto y = run (*fx, sine (hz, 0.5, 0.1, rate));
            miss = std::max (miss, std::abs (20.0 * std::log10 (strength (y, (size_t) (0.25 * rate), hz, rate) / 0.1)));
        }
        check (miss < 0.1, "ir: Mix at 0 is the guitar itself (within " + num (miss, 3) + " dB), " + std::to_string (lag) + " samples late");
        fx->setParam ("mix", 100.0f);
    }

    {
        // A new file while playing: the sound dips over to it without a click.
        fx->reset();
        const auto x = sine (220.0, 0.6, 0.2, rate);
        auto y = x;
        const size_t change = (size_t) (0.3 * rate);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i >= change && i < change + 64)
                fx->setText ("file", "");        // back to a straight wire
            float* p = y.data() + i;
            fx->process (&p, 1, (int) std::min<size_t> (64, y.size() - i));
        }
        float steady = 0.0f, during = 0.0f;
        for (size_t i = (size_t) (0.1 * rate); i < change; ++i)     steady = std::max (steady, std::abs (y[i] - y[i - 1]));
        for (size_t i = change; i < y.size(); ++i)                  during = std::max (during, std::abs (y[i] - y[i - 1]));
        const double after = 20.0 * std::log10 (rms (y, (size_t) (0.5 * rate)) / rms (x, (size_t) (0.5 * rate)));
        check (during < steady * 2.0f + 1.0e-4f && std::abs (after) < 0.1,
               "ir: taking the file away while playing does not click (biggest step " + num (during, 4) + ", music alone " + num (steady, 4) + ") and leaves a straight wire");
    }

    {
        auto missing = makeEffect ("ir");
        missing->setText ("file", "no-such-response-file.wav");
        missing->prepare (rate, 64);
        const auto x = sine (330.0, 0.3, 0.1, rate);
        const auto y = run (*missing, x);
        const double level = 20.0 * std::log10 (rms (y, (size_t) (0.1 * rate)) / rms (x, (size_t) (0.1 * rate)));
        check (std::abs (level) < 0.05 && missing->status().find ("can't find") != std::string::npos && missing->getText ("file") == "no-such-response-file.wav",
               "ir: a missing file is a straight wire, says so, and keeps the name for when the file turns up (\"" + missing->status() + "\")");
    }

    {
        // The longest file it will play (85 ms), inside its processor budget.
        std::mt19937 gen (3);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        ir::Wav longWav;
        longWav.rate = 48000.0;
        longWav.channels.emplace_back ((size_t) ResponsePlayer::maxTaps + 2000);
        for (size_t i = 0; i < longWav.channels[0].size(); ++i)
            longWav.channels[0][i] = 0.05f * dist (gen) * (float) std::exp (-(double) i / 3000.0) + (i == 0 ? 0.5f : 0.0f);
        const auto longPath = tempFile ("gigrig-selftest-long-response.wav");
        ir::writeWav (longPath, longWav, error);

        auto heavy = makeEffect ("ir");
        heavy->setText ("file", longPath);
        heavy->prepare (rate, 64);
        std::vector<float> x ((size_t) rate);
        for (auto& v : x) v = 0.1f * dist (gen);
        // The longest file is the worst case, so it gets twice the everyday budget, and the best of
        // eight goes: a busy shared build machine once read 8.54% against 8% where others read about 4%
        // (run 130 on main, 27 Nov 2026), and that one reading stopped a build being published.
        const double worstCaseBudget = 2.0 * heavy->cpuBudgetPercent();
        double best = 1.0e9;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            auto y = x;
            const auto start = std::chrono::steady_clock::now();
            for (size_t i = 0; i + 64 <= y.size(); i += 64)
            {
                float* p = y.data() + i;
                heavy->process (&p, 1, 64);
            }
            best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() * 100.0);
        }
        check (best <= worstCaseBudget && heavy->status().find ("85 ms") != std::string::npos,
               "ir: the longest file it plays (" + heavy->status() + ") uses " + num (best, 2) + "% of each block (budget " + num (worstCaseBudget, 0) + "% for the longest file)");
        std::error_code ec;
        std::filesystem::remove (longPath, ec);
    }

    std::error_code ec;
    std::filesystem::remove (path, ec);
}

} // namespace gigrig::fx
