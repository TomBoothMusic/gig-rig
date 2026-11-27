#include "Tuner.h"

#include <algorithm>
#include <cmath>

namespace gigrig
{

Tuner::Tuner()
{
    ring.assign ((size_t) fifo.getTotalSize(), 0.0f);
    recent.assign ((size_t) window, 0.0f);
}

juce::File Tuner::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("tuner.txt");
}

juce::String Tuner::noteName (int midi)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = juce::jlimit (0, 127, midi);
    return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
}

void Tuner::nearestNote (double hz, double referenceHz, int& midi, double& cents) noexcept
{
    const double exact = 69.0 + 12.0 * std::log2 (juce::jmax (1.0, hz) / referenceHz);
    midi = (int) std::floor (exact + 0.5);
    cents = (exact - (double) midi) * 100.0;
}

//==============================================================================
void Tuner::setOn (bool shouldBeOn)
{
    if (on.load() == shouldBeOn)
        return;

    if (shouldBeOn)
    {
        // Start from nothing: whatever was heard last time is forgotten.
        flush.fetch_add (1);
        filled = 0;
        lastCount = 0;
        silentTicks = 0;
        shown = {};
        ++timesOn;
    }

    on.store (shouldBeOn);

    if (! shouldBeOn)
        shown = {};
}

void Tuner::setSocket (int s)
{
    s = juce::jlimit (-1, maxSockets - 1, s);

    if (wanted.exchange (s) != s)
    {
        dirty = true;
        filled = 0;
        lastCount = 0;
    }
}

void Tuner::setReference (double hz)
{
    hz = juce::jlimit (minReference, maxReference, hz);

    if (std::abs (hz - refHz) > 1.0e-6)
    {
        refHz = hz;
        dirty = true;
    }
}

//==============================================================================
void Tuner::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    factor = juce::jmax (1, (int) std::floor (rate / 24000.0 + 0.5));
    thinRate.store (rate / (double) factor);
    held = 0;
    sum = 0.0f;
    current = -1;
    quietFor = 0;
    flush.fetch_add (1);
}

void Tuner::hear (const float* const* inputs, int numIns, int numSamples) noexcept
{
    if (! on.load (std::memory_order_relaxed))
        return;

    if (const int f = flush.load (std::memory_order_relaxed); f != flushSeen)
    {
        flushSeen = f;
        held = 0;
        sum = 0.0f;
        current = -1;
        quietFor = 0;
    }

    const int want = wanted.load (std::memory_order_relaxed);
    const int count = juce::jmin (numIns, maxSockets);

    auto peakAt = [&] (int s) -> float
    {
        if (s < 0 || s >= count || inputs[s] == nullptr)
            return 0.0f;

        const auto range = juce::FloatVectorOperations::findMinAndMax (inputs[s], numSamples);
        return juce::jmax (-range.getStart(), range.getEnd());
    };

    if (want >= 0)
        current = want;
    else
    {
        // Whichever socket is being played: stay on one until it has been quiet for a third of a
        // second, or another is much louder (so a note dying away is not left for a quieter socket's hiss).
        int loudest = -1;
        float most = 0.003f;

        for (int s = 0; s < count; ++s)
            if (const float p = peakAt (s); p > most)
            {
                most = p;
                loudest = s;
            }

        const float mine = peakAt (current);
        quietFor = mine < 0.003f ? quietFor + numSamples : 0;

        if (loudest >= 0 && loudest != current && (current < 0 || (double) quietFor > rate * 0.3 || most > mine * 8.0f))
        {
            current = loudest;
            quietFor = 0;
            held = 0;
            sum = 0.0f;
        }
    }

    listening.store (current, std::memory_order_relaxed);

    if (current < 0 || current >= numIns || inputs[current] == nullptr)
        return;

    const float* in = inputs[current];
    const float p = peakAt (current);

    if (p > levelNow.load (std::memory_order_relaxed))
        levelNow.store (p, std::memory_order_relaxed);

    // Thin it to about 24 kHz by averaging (plenty for a note's pitch) and queue it.
    float chunk[256];
    int made = 0;
    const float scale = 1.0f / (float) factor;

    auto send = [&]
    {
        const auto scope = fifo.write (made);       // a full queue (nobody is reading) just drops the sound

        if (scope.blockSize1 > 0)
            std::copy (chunk, chunk + scope.blockSize1, ring.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (chunk + scope.blockSize1, chunk + scope.blockSize1 + scope.blockSize2, ring.begin() + scope.startIndex2);

        made = 0;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        sum += in[i];

        if (++held >= factor)
        {
            chunk[made++] = sum * scale;
            sum = 0.0f;
            held = 0;

            if (made == 256)
                send();
        }
    }

    if (made > 0)
        send();
}

//==============================================================================
double Tuner::pitchOf (const float* x, int n, double sampleRate, double& clarity)
{
    clarity = 0.0;

    if (n < 256 || sampleRate <= 0.0)
        return 0.0;

    // The McLeod pitch method: how alike the sound is to itself moved along by each number of
    // samples, scaled so that a perfect match is 1 whatever the loudness.
    const int maxLag = juce::jmin (n / 2, (int) (sampleRate / 27.0));
    const int minLag = juce::jmax (2, (int) (sampleRate / 1500.0));

    if (maxLag <= minLag + 2)
        return 0.0;

    std::vector<float> v ((size_t) n);
    double mean = 0.0;

    for (int i = 0; i < n; ++i)
        mean += x[i];

    mean /= (double) n;
    double energy = 0.0;

    for (int i = 0; i < n; ++i)
    {
        v[(size_t) i] = x[i] - (float) mean;
        energy += (double) v[(size_t) i] * v[(size_t) i];
    }

    if (energy < 1.0e-9 * (double) n)
        return 0.0;

    std::vector<double> nsdf ((size_t) maxLag + 1);
    double m = 2.0 * energy;
    nsdf[0] = 1.0;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        const double a = v[(size_t) (lag - 1)], b = v[(size_t) (n - lag)];
        m -= a * a + b * b;

        const float* p = v.data();
        const float* q = v.data() + lag;
        const int len = n - lag;
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        int i = 0;

        for (; i + 4 <= len; i += 4)
        {
            s0 += p[i] * q[i];
            s1 += p[i + 1] * q[i + 1];
            s2 += p[i + 2] * q[i + 2];
            s3 += p[i + 3] * q[i + 3];
        }

        for (; i < len; ++i)
            s0 += p[i] * q[i];

        nsdf[(size_t) lag] = m > 1.0e-12 ? 2.0 * (double) (s0 + s1 + s2 + s3) / m : 0.0;
    }

    // The highest point of each hump after the first dip below zero.
    struct Top { double lag, value; };
    std::vector<Top> tops;
    int lag = 1;

    while (lag < maxLag && nsdf[(size_t) lag] > 0.0)
        ++lag;

    double highest = 0.0;

    while (lag < maxLag)
    {
        while (lag < maxLag && nsdf[(size_t) lag] <= 0.0)
            ++lag;

        int best = -1;

        while (lag < maxLag && nsdf[(size_t) lag] > 0.0)
        {
            if (best < 0 || nsdf[(size_t) lag] > nsdf[(size_t) best])
                best = lag;

            ++lag;
        }

        // A hump cut off by the end of the range has no top of its own.
        if (best >= minLag && best > 0 && best < maxLag && nsdf[(size_t) best] >= nsdf[(size_t) (best - 1)] && nsdf[(size_t) best] >= nsdf[(size_t) (best + 1)])
        {
            const double y0 = nsdf[(size_t) (best - 1)], y1 = nsdf[(size_t) best], y2 = nsdf[(size_t) (best + 1)];
            const double bend = y0 - 2.0 * y1 + y2;
            const double shift = std::abs (bend) > 1.0e-12 ? 0.5 * (y0 - y2) / bend : 0.0;
            const Top t { (double) best + shift, y1 - 0.25 * (y0 - y2) * shift };
            tops.push_back (t);
            highest = juce::jmax (highest, t.value);
        }
    }

    // The first hump that is nearly as high as the highest is one cycle of the note (a later
    // one is two or three cycles, which would read an octave or more low).
    for (const auto& t : tops)
        if (t.value >= 0.9 * highest)
        {
            clarity = juce::jlimit (0.0, 1.0, t.value);
            return sampleRate / t.lag;
        }

    return 0.0;
}

void Tuner::tick()
{
    if (dirty)
    {
        dirty = false;
        saveState();
    }

    if (! on.load())
        return;

    // Bring in what has arrived; keep the newest `window` samples.
    for (;;)
    {
        const int ready = fifo.getNumReady();

        if (ready <= 0)
            break;

        const int take = juce::jmin (ready, window);
        const auto scope = fifo.read (take);
        std::move (recent.begin() + take, recent.end(), recent.begin());
        auto to = recent.end() - take;
        to = std::copy (ring.begin() + scope.startIndex1, ring.begin() + scope.startIndex1 + scope.blockSize1, to);
        std::copy (ring.begin() + scope.startIndex2, ring.begin() + scope.startIndex2 + scope.blockSize2, to);
        filled = juce::jmin (window, filled + take);
    }

    shown.socket = listening.load();
    shown.level = levelNow.exchange (0.0f);

    double clarity = 0.0, hz = 0.0;

    // A note needs the whole window (about a sixth of a second) and some level: under -50 dB is hiss.
    float peak = 0.0f;

    for (int i = window - window / 4; i < window; ++i)
        peak = juce::jmax (peak, std::abs (recent[(size_t) i]));

    if (filled >= window && peak > 0.003f)
        hz = pitchOf (recent.data(), window, thinRate.load(), clarity);

    if (hz > 0.0 && clarity >= 0.9)
    {
        // A different note starts the steadying again.
        if (lastCount > 0 && std::abs (std::log2 (hz / last[(size_t) ((lastCount - 1) % 3)])) > 0.04)
            lastCount = 0;

        last[(size_t) (lastCount % 3)] = hz;
        ++lastCount;
        ++readings;
        silentTicks = 0;

        // The middle of the last three, so one odd figure never shows.
        std::array<double, 3> sorted = last;
        const int have = juce::jmin (3, lastCount);
        std::sort (sorted.begin(), sorted.begin() + have);
        const double steady = have == 3 ? sorted[1] : sorted[0];

        shown.heard = true;
        shown.hz = steady;
        nearestNote (steady, refHz, shown.midi, shown.cents);
        shown.name = noteName (shown.midi);
    }
    else if (++silentTicks > 8)
    {
        // The note has gone: the needle rests.
        const auto socketNow = shown.socket;
        const auto levelWas = shown.level;
        shown = {};
        shown.socket = socketNow;
        shown.level = levelWas;
        lastCount = 0;
    }
}

//==============================================================================
juce::String Tuner::statusLine() const
{
    if (! isOn())
        return "The tuner is off.";

    const auto where = shown.socket >= 0 ? "input " + juce::String (shown.socket + 1) : juce::String ("no input yet");

    if (! shown.heard)
        return "Tuner: play one string (" + where + ").";

    const int c = (int) std::floor (std::abs (shown.cents) + 0.5);
    const auto how = c == 0 ? juce::String ("in tune") : juce::String (c) + (c == 1 ? " cent " : " cents ") + (shown.cents < 0.0 ? "flat" : "sharp");
    return "Tuner: " + shown.name + ", " + how + " (" + juce::String (shown.hz, 1) + " Hz), " + where;
}

juce::String Tuner::reportText() const
{
    juce::String t ("TUNER\n");
    t << "  " << (isOn() ? "On" : "Off") << ". Listening to " << (socket() < 0 ? juce::String ("whichever input is being played") : "input " + juce::String (socket() + 1))
      << ". A = " << juce::String (refHz, 1) << " Hz. " << (mutes() ? "The main mix goes silent while it is on (mixer on only)." : "It does not silence the main mix.") << "\n";
    t << "  Switched on " << timesOn << (timesOn == 1 ? " time" : " times") << " since the app started, " << readings << " readings taken.\n";

    if (isOn())
        t << "  " << statusLine() << "\n";

    return t;
}

juce::String Tuner::stateText() const
{
    juce::String t ("# GigRig tuner. Safe to delete.\n[tuner]\n");
    t << "socket=" << socket() << "\n";
    t << "reference=" << juce::String (refHz, 1) << "\n";
    t << "mute main=" << (mutes() ? 1 : 0) << "\n";
    return t;
}

void Tuner::loadStateText (const juce::String& text)
{
    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();

        if (line.startsWithChar ('#') || ! line.containsChar ('='))
            continue;

        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim(), value = line.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "socket")            setSocket (value.getIntValue());
        else if (key == "reference")    setReference (value.getDoubleValue());
        else if (key == "mute main")    muting.store (value.getIntValue() != 0);
    }

    dirty = false;
}

void Tuner::saveState()
{
    if (! memoryOnDisk.load())
        return;

    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (stateText());
}

} // namespace gigrig
