#include "Soundcheck.h"

namespace gigrig
{

namespace
{
    constexpr int readChunk = 4096;         // frames read from the files at a time
    constexpr int spare = 64;               // frames the resampler may hold back
}

Soundcheck::Soundcheck() : juce::Thread ("Virtual soundcheck")
{
    formats.registerBasicFormats();
    socketOf.fill (-1);
}

Soundcheck::~Soundcheck()
{
    stop();
}

std::vector<Soundcheck::Source> Soundcheck::sourcesIn (const juce::File& folder)
{
    std::vector<Source> found;
    juce::StringArray lines;
    lines.addLines (folder.getChildFile ("About this recording.txt").loadFileAsString());

    for (const auto& line : lines)
    {
        // "soundcheck: 05 Vocal (raw for soundcheck).wav = socket 1"
        if (! line.startsWith ("soundcheck: "))
            continue;

        const auto rest = line.fromFirstOccurrenceOf ("soundcheck: ", false, false);
        const auto name = rest.upToLastOccurrenceOf (" = socket ", false, false).trim();
        const int socket = rest.fromLastOccurrenceOf (" = socket ", false, false).trim().getIntValue() - 1;
        const auto file = folder.getChildFile (name);

        if (name.isNotEmpty() && socket >= 0 && socket < maxInputs && file.existsAsFile() && (int) found.size() < maxSockets)
            found.push_back ({ file, socket });
    }

    return found;
}

void Soundcheck::prepare (double sampleRate, int maxBlock)
{
    stop();
    rate = sampleRate;
    block = maxBlock;
}

void Soundcheck::closeFiles()
{
    readers.clear();
    resamplers.clear();
}

juce::String Soundcheck::start (const juce::File& folder, bool roundAndRound, bool ownThread)
{
    stop();

    if (rate <= 0.0 || block <= 0)
        return "No audio device is open, so there is nothing to play the recording through.";

    const auto sources = sourcesIn (folder);

    if (sources.empty())
        return "This recording has no raw files, so it cannot be played back as a soundcheck. Tick \"A raw copy of every socket\" on the Recording tab before the next recording.";

    std::vector<std::unique_ptr<juce::AudioFormatReader>> opened;
    juce::int64 shortest = 0;
    double theirRate = 0.0;

    for (const auto& s : sources)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (s.file));

        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0)
            return "The file \"" + s.file.getFileName() + "\" cannot be read.";

        if (theirRate > 0.0 && std::abs (theirRate - r->sampleRate) > 0.5)
            return "The files of this recording are not all at the same sample rate.";

        theirRate = r->sampleRate;
        shortest = opened.empty() ? r->lengthInSamples : juce::jmin (shortest, r->lengthInSamples);
        opened.push_back (std::move (r));
    }

    const int n = (int) opened.size();
    const int ringFrames = (int) rate + block * 2;          // a second in hand
    std::vector<std::vector<float>> newRing ((size_t) n, std::vector<float> ((size_t) ringFrames, 0.0f));
    std::vector<std::vector<float>> newOut ((size_t) n, std::vector<float> ((size_t) block, 0.0f));
    const double step = theirRate / rate;
    const int mostOut = (int) ((readChunk + spare) / step) + 8;

    {
        const juce::SpinLock::ScopedLockType guard (lock);
        ring.swap (newRing);
        out.swap (newOut);
        fifo.setTotalSize (ringFrames);
        fifo.reset();
        numSources = n;
        socketOf.fill (-1);
        socketMask = 0;

        for (int i = 0; i < n; ++i)
        {
            socketOf[(size_t) i] = sources[(size_t) i].socket;

            if (sources[(size_t) i].socket < 32)
                socketMask |= 1u << sources[(size_t) i].socket;
        }

        primed = false;
        gain = 0.0f;
        primeFrames = juce::jmin (ringFrames / 2, juce::jmax (block * 2, (int) (rate * 0.2)));
    }

    readers = std::move (opened);
    resamplers.clear();

    for (int i = 0; i < n; ++i)
        resamplers.push_back (std::make_unique<juce::WindowedSincInterpolator>());

    stage.assign ((size_t) n, std::vector<float> ((size_t) (readChunk + spare * 2), 0.0f));
    resampled.assign ((size_t) n, std::vector<float> ((size_t) mostOut, 0.0f));
    readBuf.setSize (1, readChunk);
    staged = 0;
    readPos = 0;
    readerDone = false;
    fileRate = theirRate;
    fileLength = shortest;
    lengthSecs = (double) shortest / theirRate;
    current = folder.getFileName();
    looping.store (roundAndRound);
    missed.store (0);
    played.store (0);
    ended.store (false);
    wantStop.store (false);
    faded.store (false);
    playing.store (true);

    if (ownThread)
        startThread();

    return {};
}

void Soundcheck::stop()
{
    if (playing.load())
    {
        // Ask the audio thread to fade out, and give it a moment to (it may not be running at all).
        wantStop.store (true);

        for (int i = 0; i < 40 && ! faded.load() && playing.load(); ++i)
            juce::Thread::sleep (5);
    }

    {
        const juce::SpinLock::ScopedLockType guard (lock);
        playing.store (false);
    }

    stopThread (4000);
    closeFiles();
}

double Soundcheck::positionSeconds() const noexcept
{
    if (rate <= 0.0 || lengthSecs <= 0.0)
        return 0.0;

    const double secs = (double) played.load (std::memory_order_relaxed) / rate;
    return looping.load() ? std::fmod (secs, lengthSecs) : juce::jmin (secs, lengthSecs);
}

bool Soundcheck::waitUntilReady (int milliseconds)
{
    for (int waited = 0; waited < milliseconds; waited += 2)
    {
        if (fifo.getNumReady() >= primeFrames || readerDone)
            return true;

        juce::Thread::sleep (2);
    }

    return false;
}

juce::String Soundcheck::reportText() const
{
    juce::String t;
    t << "VIRTUAL SOUNDCHECK\n";

    if (! playing.load())
    {
        t << "Not playing." << (current.isNotEmpty() ? " Last played: " + current + "." : juce::String()) << "\n";
    }
    else
    {
        juce::StringArray s;

        for (int i = 0; i < numSources; ++i)
            s.add (juce::String (socketOf[(size_t) i] + 1));

        t << "Playing " << current << " into socket" << (s.size() == 1 ? " " : "s ") << s.joinIntoString (", ")
          << ", " << (looping.load() ? "round and round" : "once") << ". At " << juce::String (positionSeconds(), 1) << " of "
          << juce::String (lengthSecs, 1) << " seconds. Files at " << juce::String (fileRate / 1000.0, 1) << " kHz, played at "
          << juce::String (rate / 1000.0, 1) << " kHz.\n";
    }

    if (missed.load() > 0)
        t << "SOUNDCHECK WARNING: the drive did not keep up " << missed.load() << " times (a moment of silence each time).\n";

    return t;
}

//==============================================================================
void Soundcheck::run()
{
    while (! threadShouldExit())
        if (! pump())
            wait (4);
}

bool Soundcheck::pump()
{
    if (readerDone || readers.empty() || rate <= 0.0)
        return false;

    const double step = fileRate / rate;
    const bool same = std::abs (step - 1.0) < 1.0e-9;
    const int n = (int) readers.size();

    if (fifo.getFreeSpace() < (int) ((readChunk + spare) / step) + 8)
        return false;

    // ---- read: the end of the recording runs straight into its start when it goes round ----
    const int room = (int) stage[0].size() - staged;
    int want = juce::jmin (readChunk, room);
    int got = 0;
    bool atEnd = false;

    while (got < want)
    {
        const auto left = fileLength - readPos;

        if (left <= 0)
        {
            if (! looping.load()) { atEnd = true; break; }
            readPos = 0;
            continue;
        }

        const int now = (int) juce::jmin ((juce::int64) (want - got), left);

        for (int i = 0; i < n; ++i)
        {
            readers[(size_t) i]->read (&readBuf, 0, now, readPos, true, false);
            juce::FloatVectorOperations::copy (stage[(size_t) i].data() + staged + got, readBuf.getReadPointer (0), now);
        }

        got += now;
        readPos += now;
    }

    staged += got;

    // ---- bring it to the interface's rate ----
    int made = 0;

    if (same)
    {
        made = staged;

        for (int i = 0; i < n; ++i)
            juce::FloatVectorOperations::copy (resampled[(size_t) i].data(), stage[(size_t) i].data(), made);

        staged = 0;
    }
    else
    {
        // At the end of a recording played once, the last few frames are pushed out with silence behind them.
        if (atEnd)
        {
            for (int i = 0; i < n; ++i)
                std::fill (stage[(size_t) i].begin() + staged, stage[(size_t) i].begin() + juce::jmin ((int) stage[(size_t) i].size(), staged + spare), 0.0f);

            staged = juce::jmin ((int) stage[0].size(), staged + spare);
        }

        made = juce::jmax (0, (int) ((double) (staged - spare) / step));
        made = juce::jmin (made, (int) resampled[0].size());
        int used = 0;

        if (made > 0)
            for (int i = 0; i < n; ++i)
                used = resamplers[(size_t) i]->process (step, stage[(size_t) i].data(), resampled[(size_t) i].data(), made);

        used = juce::jlimit (0, staged, used);

        for (int i = 0; i < n; ++i)
            std::copy (stage[(size_t) i].begin() + used, stage[(size_t) i].begin() + staged, stage[(size_t) i].begin());

        staged -= used;
    }

    // ---- queue it ----
    if (made > 0)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (made, start1, size1, start2, size2);

        for (int i = 0; i < n; ++i)
        {
            if (size1 > 0) juce::FloatVectorOperations::copy (ring[(size_t) i].data() + start1, resampled[(size_t) i].data(), size1);
            if (size2 > 0) juce::FloatVectorOperations::copy (ring[(size_t) i].data() + start2, resampled[(size_t) i].data() + size1, size2);
        }

        fifo.finishedWrite (size1 + size2);
    }

    if (atEnd)
    {
        readerDone = true;
        ended.store (true);
    }

    return made > 0 || got > 0;
}

//==============================================================================
const float* const* Soundcheck::feed (const float* const* inputs, int numIns, int n) noexcept
{
    if (! playing.load (std::memory_order_relaxed))
        return inputs;

    const juce::SpinLock::ScopedTryLockType guard (lock);

    if (! guard.isLocked() || ! playing.load (std::memory_order_relaxed) || numIns > maxInputs || n <= 0 || n > block || numSources <= 0)
        return inputs;

    for (int i = 0; i < numIns; ++i)
        swapped[(size_t) i] = inputs[i];

    const int ready = fifo.getNumReady();
    const bool stopping = wantStop.load (std::memory_order_relaxed);
    const bool over = ended.load (std::memory_order_relaxed);

    if (! primed && (ready >= primeFrames || (over && ready > 0)))
        primed = true;

    int take = 0;

    if (primed)
    {
        if (ready >= n)             take = n;
        else if (over)              take = ready;       // the last block of a recording played once
        else                        missed.fetch_add (1, std::memory_order_relaxed);
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;

    if (take > 0)
        fifo.prepareToRead (take, start1, size1, start2, size2);

    const float step = 1.0f / (float) juce::jmax (1.0, rate * 0.01);       // a 10 ms fade in and out
    const float target = stopping ? 0.0f : 1.0f;
    float g = gain;

    for (int i = 0; i < numSources; ++i)
    {
        auto* o = out[(size_t) i].data();

        if (size1 > 0) juce::FloatVectorOperations::copy (o, ring[(size_t) i].data() + start1, size1);
        if (size2 > 0) juce::FloatVectorOperations::copy (o + size1, ring[(size_t) i].data() + start2, size2);
        if (take < n)  juce::FloatVectorOperations::clear (o + take, n - take);

        g = gain;

        if (g < 1.0f || target < 1.0f)
            for (int k = 0; k < n; ++k)
            {
                g = target > g ? juce::jmin (target, g + step) : juce::jmax (target, g - step);
                o[k] *= g;
            }

        const int socket = socketOf[(size_t) i];

        if (socket >= 0 && socket < numIns)
            swapped[(size_t) socket] = o;
    }

    gain = g;

    if (take > 0)
    {
        fifo.finishedRead (take);
        played.fetch_add (take, std::memory_order_relaxed);
    }

    if (stopping && gain <= 0.0f)
        faded.store (true, std::memory_order_relaxed);

    // A recording played once has run out: the sockets go back to what is plugged in.
    if (over && primed && fifo.getNumReady() == 0 && ! stopping)
        playing.store (false, std::memory_order_relaxed);

    return swapped.data();
}

} // namespace gigrig
