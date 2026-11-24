#pragma once

// Shared tools for the time effects: a delay line read at fractional positions, a smooth
// low-frequency oscillator, and the note values a synced delay offers.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

/** A mono delay line. Write one sample, then read any distance back (in samples, fractional),
    with cubic interpolation so moving read points (chorus, wow and flutter) stay clean. */
class DelayLine
{
public:
    void setMaxSamples (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() noexcept                    { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void write (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** The sample from 'delay' samples ago: 0 is the one just written. */
    float read (double delay) const noexcept
    {
        delay = std::clamp (delay, 0.0, (double) (mask - 4));
        const double pos = (double) (writePos - 1) - delay;
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - (double) i);
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        // Hermite (Catmull-Rom).
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

private:
    float at (int i) const noexcept          { return buffer[(size_t) (i & mask)]; }

    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

/** A sine LFO with a phase that runs on whatever the rate does. */
struct Lfo
{
    double phase = 0.0;

    /** Advances one sample and returns -1..1. */
    double next (double hz, double rate) noexcept
    {
        phase += hz / rate;
        phase -= std::floor (phase);
        return std::sin (phase * 6.283185307179586);
    }
};

namespace sync
{
    /** Note values a synced delay offers, in beats (quarter notes). Order matches the choice list. */
    inline const std::vector<std::string>& names()
    {
        static const std::vector<std::string> n { "1/4", "Dotted 1/8", "1/8", "1/4 triplet", "1/8 triplet", "1/2", "Dotted 1/4" };
        return n;
    }

    inline double beats (int choice) noexcept
    {
        static const double b[] { 1.0, 0.75, 0.5, 2.0 / 3.0, 1.0 / 3.0, 2.0, 1.5 };
        return b[std::clamp (choice, 0, 6)];
    }

    /** The delay time in seconds: the note value at the song's tempo when following it, else the knob. */
    inline double seconds (bool follow, double bpm, int choice, double knobMs) noexcept
    {
        if (follow && bpm > 20.0)
            return beats (choice) * 60.0 / bpm;
        return knobMs * 0.001;
    }
}

} // namespace gigrig::fx
