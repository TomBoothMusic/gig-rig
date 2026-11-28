#include "Tuner.h"

#include <algorithm>
#include <cmath>

namespace gigrig
{

Tuner::Tuner()
{
    ring.assign ((size_t) fifo.getTotalSize(), 0.0f);
    recent.assign ((size_t) kept, 0.0f);
    marks.reserve (256);
}

juce::String Tuner::silenceWord (Silence s)
{
    return s == Silence::guitars ? "guitars" : s == Silence::mainMix ? "main" : "nothing";
}

bool Tuner::silenceFromWord (const juce::String& word, Silence& s)
{
    const auto w = word.trim().toLowerCase();

    if (w == "guitars")         s = Silence::guitars;
    else if (w == "main")       s = Silence::mainMix;
    else if (w == "nothing")    s = Silence::nothing;
    else                        return false;

    return true;
}

bool Tuner::looksLikeGuitar (const juce::String& channelName)
{
    const auto n = channelName.toLowerCase();

    if (n.contains ("drum") || n.contains ("kick") || n.contains ("vocal") || n.contains ("mic"))
        return false;

    for (const char* word : { "guitar", "gtr", "electric", "acoustic", "bass", "uke", "mandolin", "banjo" })
        if (n.contains (word))
            return true;

    return false;
}

bool Tuner::goesSilent (const juce::String& channelName) const
{
    const auto n = channelName.trim().toLowerCase();

    if (silentByHand.contains (n))  return true;
    if (liveByHand.contains (n))    return false;

    return looksLikeGuitar (n);
}

void Tuner::setGoesSilent (const juce::String& channelName, bool silent)
{
    const auto n = channelName.trim().toLowerCase();

    if (n.isEmpty() || goesSilent (n) == silent)
        return;

    silentByHand.removeString (n);
    liveByHand.removeString (n);

    // Only a choice that differs from what the name says needs remembering.
    if (looksLikeGuitar (n) != silent)
        (silent ? silentByHand : liveByHand).add (n);

    dirty = true;
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
        total = 0;
        sinceNote = 0;
        lastCount = 0;
        silentSamples = 0;
        idleTicks = 0;
        shown = {};
        restStrobe();
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
        restStrobe();
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
    const auto among = candidates.load (std::memory_order_relaxed);     // the guitars' sockets, so a voice never takes the tuner; 0 = any socket

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

        if (among != 0 && current >= 0 && (among & (1u << current)) == 0)
            current = -1;

        for (int s = 0; s < count; ++s)
            if (among != 0 && (among & (1u << s)) == 0)
                continue;
            else if (const float p = peakAt (s); p > most)
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

double Tuner::phaseAgainst (const float* x, int n, double sampleRate, double hz, juce::int64 endsAt, double& size)
{
    size = 0.0;

    if (n < 16 || sampleRate <= 0.0 || hz <= 0.0)
        return 0.0;

    // The sound times a steady wave of exactly `hz`, added up under a smooth window. What is left
    // is how far the sound's own wave sits ahead of the steady one. The steady wave is counted
    // from the very first sample of the stream, so answers from different moments can be compared.
    const double turn = hz / sampleRate;                                // cycles per sample
    const double first = std::fmod (turn * (double) (endsAt - n), 1.0);
    const double step = juce::MathConstants<double>::twoPi * turn;
    const double stepCos = std::cos (step), stepSin = std::sin (step);
    double c = std::cos (juce::MathConstants<double>::twoPi * first), sn = std::sin (juce::MathConstants<double>::twoPi * first);
    const double winStep = juce::MathConstants<double>::twoPi / (double) n;
    const double winCos = std::cos (winStep), winSin = std::sin (winStep);
    double wc = std::cos (0.5 * winStep), ws = std::sin (0.5 * winStep);    // the window's own wave, half a sample in
    double re = 0.0, im = 0.0, weight = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * wc;
        const double v = (double) x[i] * w;
        re += v * c;
        im -= v * sn;
        weight += w;

        const double c2 = c * stepCos - sn * stepSin;
        sn = sn * stepCos + c * stepSin;
        c = c2;
        const double w2 = wc * winCos - ws * winSin;
        ws = ws * winCos + wc * winSin;
        wc = w2;
    }

    size = weight > 0.0 ? 2.0 * std::sqrt (re * re + im * im) / weight : 0.0;
    double phase = std::atan2 (im, re) / juce::MathConstants<double>::twoPi;
    return phase < 0.0 ? phase + 1.0 : phase;
}

void Tuner::restStrobe()
{
    strobeMidi = -1;
    strobePartial = 1;
    strobeBase = 0.0;
    clearNow = false;
    marks.clear();
    shown.strobing = false;
    shown.strength = 0.0f;
}

void Tuner::strobeStep (double steadyHz, int newSamples)
{
    const double fs = thinRate.load();
    int midi = 0;
    double roughCents = 0.0;
    nearestNote (steadyHz, refHz, midi, roughCents);
    const double note = refHz * std::pow (2.0, (double) (midi - 69) / 12.0);

    // Eight cycles of the note, or a sixteenth of a second if that is longer: long enough that the
    // overtones do not disturb it, short enough to follow the tuning peg.
    int n = juce::jlimit (1536, kept, (int) (8.0 * fs / note));

    if (filled < n)
        n = filled;

    if ((double) n < 4.0 * fs / note || n < 512)
    {
        shown.strobing = false;
        return;
    }

    const float* x = recent.data() + (kept - n);
    double size = 0.0;

    // After a gap (a fresh pluck is all noise for a moment) the count of whole cycles is lost: begin again.
    if (midi != strobeMidi || (double) (total - strobeAt) > 0.15 * fs)
    {
        // A new note. A string whose own note is almost missing under its first overtone (a low
        // bass string through a small pickup) is followed by that overtone instead.
        double size1 = 0.0, size2 = 0.0;
        phaseAgainst (x, n, fs, note, total, size1);
        phaseAgainst (x, n, fs, 2.0 * note, total, size2);
        strobePartial = size1 < 0.03 * size2 && 2.0 * note < 0.4 * fs ? 2 : 1;
        strobeMidi = midi;
        strobeRaw = phaseAgainst (x, n, fs, note * (double) strobePartial, total, size);
        strobeTurn = strobeRaw;
        strobeAt = total;

        // The picture carries on from where it was, so a new note or a fresh pluck never makes it jump.
        const double was = shown.strobe - std::floor (shown.strobe);
        strobeBase = was - strobeTurn / (double) strobePartial;
        marks.clear();
        marks.push_back ({ total, strobeTurn });
    }
    else if (newSamples > 0)
    {
        const double raw = phaseAgainst (x, n, fs, note * (double) strobePartial, total, size);

        // The phase is only known within one cycle. The note worked out the other way says about
        // how far it should have moved, and the nearest whole fit to that is the truth.
        const double want = strobeTurn + (double) strobePartial * (steadyHz - note) * (double) (total - strobeAt) / fs;
        double gap = raw - (want - std::floor (want));
        gap -= std::floor (gap + 0.5);
        strobeTurn = want + gap;
        strobeRaw = raw;
        strobeAt = total;

        // Marks from the last 0.4 seconds: the slope from the oldest to now is how far out the note is.
        const auto oldest = total - (juce::int64) (0.4 * fs);
        size_t drop = 0;

        while (drop + 1 < marks.size() && marks[drop].at < oldest)
            ++drop;

        marks.erase (marks.begin(), marks.begin() + (std::ptrdiff_t) drop);

        if (marks.size() >= 250)
            marks.erase (marks.begin() + 1);    // never grows on the heap after the first note

        marks.push_back ({ total, strobeTurn });
    }
    else
        return;

    shown.midi = midi;
    shown.name = noteName (midi);
    shown.hz = steadyHz;
    shown.cents = roughCents;

    const auto& old = marks.front();
    const double span = (double) (total - old.at) / fs;

    if (span >= 0.12)
    {
        const double fine = note + (strobeTurn - old.turn) / ((double) strobePartial * span);
        const double fineCents = fine > 0.0 ? 1200.0 * std::log2 (fine / note) : roughCents;

        if (std::abs (fineCents - roughCents) > 6.0)
        {
            // The two ways of measuring disagree (a peg turned fast, or a noisy start): begin again from here.
            marks.clear();
            marks.push_back ({ total, strobeTurn });
        }
        else
        {
            shown.hz = fine;
            shown.cents = fineCents;
        }
    }

    shown.strobing = true;
    shown.strobe = strobeTurn / (double) strobePartial + strobeBase;
    shown.strength = (float) juce::jlimit (0.0, 1.0, (20.0 * std::log10 (juce::jmax (1.0e-6, size)) + 60.0) / 40.0);
}

void Tuner::tick()
{
    if (beforeTick != nullptr)
        beforeTick();

    if (dirty)
    {
        dirty = false;
        saveState();
    }

    if (! on.load())
        return;

    // Bring in what has arrived; keep the newest `kept` samples.
    int brought = 0;

    for (;;)
    {
        const int ready = fifo.getNumReady();

        if (ready <= 0)
            break;

        const int take = juce::jmin (ready, kept);
        const auto scope = fifo.read (take);
        std::move (recent.begin() + take, recent.end(), recent.begin());
        auto to = recent.end() - take;
        to = std::copy (ring.begin() + scope.startIndex1, ring.begin() + scope.startIndex1 + scope.blockSize1, to);
        std::copy (ring.begin() + scope.startIndex2, ring.begin() + scope.startIndex2 + scope.blockSize2, to);
        filled = juce::jmin (kept, filled + take);
        brought += take;
    }

    total += brought;
    sinceNote += brought;
    shown.socket = listening.load();
    shown.level = levelNow.exchange (0.0f);

    const auto rest = [this]
    {
        // The note has gone: the needle rests.
        const auto socketNow = shown.socket;
        const auto levelWas = shown.level;
        shown = {};
        shown.socket = socketNow;
        shown.level = levelWas;
        lastCount = 0;
        restStrobe();
    };

    if (brought == 0)
    {
        // No sound is arriving at all (the audio has stopped): after a couple of seconds, rest.
        if (++idleTicks > 40 && shown.heard)
            rest();

        return;
    }

    idleTicks = 0;
    const double fs = thinRate.load();

    // The note itself is worked out at most 30 times a second; the strobe follows every tick.
    if ((double) sinceNote >= fs / 30.0)
    {
        const int since = sinceNote;
        sinceNote = 0;
        double clarity = 0.0, hz = 0.0;
        const float* newest = recent.data() + (kept - window);

        // A note needs the whole window (about a sixth of a second) and some level: under -50 dB is hiss.
        float peak = 0.0f;

        for (int i = window - window / 4; i < window; ++i)
            peak = juce::jmax (peak, std::abs (newest[i]));

        if (filled >= window && peak > 0.003f)
            hz = pitchOf (newest, window, fs, clarity);

        // The note has to be there in the sound, itself or its first overtone. What is read from
        // the rumble left when a string is damped has neither, and is no note.
        if (hz > 0.0 && clarity >= 0.9)
        {
            int midi = 0;
            double cents = 0.0, size1 = 0.0, size2 = 0.0, power = 0.0;
            nearestNote (hz, refHz, midi, cents);
            const int n = juce::jmin (filled, juce::jlimit (1536, kept, (int) (8.0 * fs / hz)));
            const float* x = recent.data() + (kept - n);
            phaseAgainst (x, n, fs, hz, total, size1);
            phaseAgainst (x, n, fs, 2.0 * hz, total, size2);

            for (int i = 0; i < n; ++i)
                power += (double) x[i] * x[i];

            if (juce::jmax (size1, size2) < 0.03 * std::sqrt (2.0 * power / (double) n))
                hz = 0.0;
        }

        clearNow = hz > 0.0 && clarity >= 0.9;

        if (clearNow)
        {
            // A different note starts the steadying again.
            if (lastCount > 0 && std::abs (std::log2 (hz / last[(size_t) ((lastCount - 1) % 3)])) > 0.04)
                lastCount = 0;

            last[(size_t) (lastCount % 3)] = hz;
            ++lastCount;
            ++readings;
            silentSamples = 0;

            // The middle of the last three, so one odd figure never shows.
            std::array<double, 3> sorted = last;
            const int have = juce::jmin (3, lastCount);
            std::sort (sorted.begin(), sorted.begin() + have);
            steady = have == 3 ? sorted[1] : sorted[0];
            shown.heard = true;
        }
        else if ((silentSamples += since) > (int) (0.42 * fs))
            rest();
    }

    if (shown.heard)
    {
        // While the note is unclear (a fresh pluck) the picture holds still rather than thrash.
        if (clearNow)
            strobeStep (steady, brought);

        if (! shown.strobing)
        {
            shown.hz = steady;
            nearestNote (steady, refHz, shown.midi, shown.cents);
            shown.name = noteName (shown.midi);
        }
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
      << ". A = " << juce::String (refHz, 1) << " Hz. "
      << (silence() == Silence::guitars ? juce::String ("The guitar channels go silent in the main mix while it is on (mixer on only); voices and the stomp stay live. ")
                                              + (monitorsKeepGuitars() ? "The monitors keep the guitars." : "The monitors lose the guitars too.")
          : silence() == Silence::mainMix ? "The whole main mix goes silent while it is on (mixer on only)." : "Nothing goes silent while it is on.")
      << (fills ? " It fills the screen." : " It shows in the header strip.") << "\n";

    if (silentByHand.size() + liveByHand.size() > 0)
        t << "  Chosen by hand: silent " << (silentByHand.isEmpty() ? juce::String ("none") : silentByHand.joinIntoString (", "))
          << "; live " << (liveByHand.isEmpty() ? juce::String ("none") : liveByHand.joinIntoString (", ")) << ".\n";

    t << "  Switched on " << timesOn << (timesOn == 1 ? " time" : " times") << " since the app started, " << readings << " readings taken.\n";

    if (isOn())
        t << "  " << statusLine() << (shown.strobing ? " Strobe locked." : "") << "\n";

    return t;
}

juce::String Tuner::stateText() const
{
    juce::String t ("# GigRig tuner. Safe to delete.\n[tuner]\n");
    t << "socket=" << socket() << "\n";
    t << "reference=" << juce::String (refHz, 1) << "\n";
    t << "mute main=" << (mutes() ? 1 : 0) << "\n";                 // read by builds before 28 Nov 2026
    t << "silence=" << silenceWord (silence()) << "\n";
    t << "guitars in monitors=" << (monitorsKeepGuitars() ? 1 : 0) << "\n";
    t << "full screen=" << (fills ? 1 : 0) << "\n";

    if (! silentByHand.isEmpty())
        t << "silent channels=" << silentByHand.joinIntoString ("|") << "\n";
    if (! liveByHand.isEmpty())
        t << "live channels=" << liveByHand.joinIntoString ("|") << "\n";

    return t;
}

void Tuner::loadStateText (const juce::String& text)
{
    bool worded = false;
    const auto names = [] (const juce::String& value)
    {
        juce::StringArray list;
        list.addTokens (value.toLowerCase(), "|", {});
        list.trim();
        list.removeEmptyStrings();
        return list;
    };

    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();

        if (line.startsWithChar ('#') || ! line.containsChar ('='))
            continue;

        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim(), value = line.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "socket")            setSocket (value.getIntValue());
        else if (key == "reference")    setReference (value.getDoubleValue());
        else if (key == "mute main")    { if (! worded) quiet.store ((int) (value.getIntValue() != 0 ? Silence::guitars : Silence::nothing)); }
        else if (key == "silence")      { Silence w; if (silenceFromWord (value, w)) { quiet.store ((int) w); worded = true; } }
        else if (key == "full screen")  fills = value.getIntValue() != 0;
        else if (key == "guitars in monitors")  monitorsKeep.store (value.getIntValue() != 0);
        else if (key == "silent channels")  silentByHand = names (value);
        else if (key == "live channels")    liveByHand = names (value);
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
