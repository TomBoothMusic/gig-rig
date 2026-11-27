#include "BreakMusic.h"
#include "Mixer.h"

#if JUCE_LINUX
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace gigrig
{

namespace
{
    constexpr int readChunk = 2048;                 // frames read from a file at a time
    constexpr double queueSeconds = 2.0;
    constexpr double phoneRate = 44100.0;           // what every receiver is told to send
    constexpr double phoneHeldSeconds = 0.3;        // how much of the phone's sound is kept in hand, to ride out its bursts
    constexpr double stopFadeSeconds = 0.4;
    constexpr double levelGlideSeconds = 0.05;
    constexpr double hearSeconds = 0.15;            // sound on an input has to last this long to count
    constexpr int analogueSockets = 10;             // the CQ12T's input sockets; its USB channels above them carry its own outputs

    const char* const fileEndings = "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff;*.m4a;*.wma";

    float gainFor (float db) noexcept       { return db <= BreakMusic::minLevelDb ? 0.0f : std::pow (10.0f, db / 20.0f); }

    juce::String clock (double seconds)
    {
        const auto s = (juce::int64) juce::jmax (0.0, seconds);
        return s >= 3600 ? juce::String (s / 3600) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2)
                         : juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    float peakOf (const float* x, int n) noexcept
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    }
}

//==============================================================================
void BreakMusic::Resampler::reset() noexcept
{
    for (auto& h : hist)
        h.fill (0.0f);

    phase = 0.0;
}

int BreakMusic::Resampler::push (const float* inL, const float* inR, int numIn, double step, float* toL, float* toR, int maxOut) noexcept
{
    int out = 0;
    const float* const in[2] { inL, inR };
    float* const to[2] { toL, toR };

    for (int i = 0; i < numIn; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& h = hist[(size_t) ch];
            h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = in[ch][i];
        }

        // The newest four samples are in: everything that falls between the middle two can now be worked out.
        while (phase < 1.0 && out < maxOut)
        {
            const auto t = (float) phase;

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& h = hist[(size_t) ch];
                const float a = -0.5f * h[0] + 1.5f * h[1] - 1.5f * h[2] + 0.5f * h[3];
                const float b = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
                const float c = -0.5f * h[0] + 0.5f * h[2];
                to[ch][out] = ((a * t + b) * t + c) * t + h[1];
            }

            ++out;
            phase += step;
        }

        phase -= 1.0;

        if (phase < 0.0)        // only if the caller's buffer was too small: never go backwards
            phase = 0.0;
    }

    return out;
}

//==============================================================================
BreakMusic::BreakMusic() : juce::Thread ("Break music reader")
{
    formats.registerBasicFormats();
    root = defaultFolder();
    pipe = defaultPipe();
    setSettings (set);
    levelGain.store (gainFor (level));
    prepare (48000.0, 1024);
}

BreakMusic::~BreakMusic()
{
    stopThread (2000);

   #if JUCE_LINUX
    if (pipeFd >= 0)
        ::close (pipeFd);
   #endif
}

juce::File BreakMusic::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Break music");
}

juce::File BreakMusic::defaultPipe()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("gigrig").getChildFile ("break-music.pipe");
}

bool BreakMusic::phoneSupported() noexcept
{
   #if JUCE_LINUX
    return true;
   #else
    return false;
   #endif
}

juce::File BreakMusic::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("break-music.txt");
}

juce::StringArray BreakMusic::mixes() const
{
    juce::StringArray names;

    for (const auto& f : root.findChildFiles (juce::File::findFiles, false, fileEndings))
        if (! f.getFileName().startsWithChar ('.'))
            names.addIfNotAlreadyThere (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

juce::File BreakMusic::fileFor (const juce::String& name) const
{
    auto found = root.findChildFiles (juce::File::findFiles, false, fileEndings);
    found.sort();

    for (const auto& f : found)
        if (f.getFileNameWithoutExtension() == name)
            return f;

    return {};
}

//==============================================================================
juce::String BreakMusic::play (const juce::String& name, bool fromTheStart)
{
    lastError.clear();

    if (name == phoneName())
    {
        if (! phoneSupported())
            return lastError = "The phone's sound can only be played on the rig computer (Linux), not on this one.";

        rememberPlace();
        const juce::ScopedLock l (sourceLock);
        reader.reset();
        resampler.reset();
        pipeHeld = 0;
        fillAverage = phoneHeldSeconds;
        sourceRate.store (phoneRate);
        lengthSecs.store (0.0);
        phone.store (true);
    }
    else
    {
        const auto file = fileFor (name);

        if (! file.existsAsFile())
            return lastError = "There is no mix called '" + name + "' in " + root.getFullPathName() + ".";

        std::unique_ptr<juce::AudioFormatReader> next (formats.createReaderFor (file));

        if (next == nullptr || next->lengthInSamples <= 0 || next->sampleRate <= 0.0)
            return lastError = "'" + file.getFileName() + "' could not be read as music. WAV, FLAC and MP3 files work.";

        rememberPlace();
        double from = 0.0;

        if (! fromTheStart)
            if (const auto it = places.find (name); it != places.end())
                from = it->second;

        const juce::ScopedLock l (sourceLock);
        auto start = (juce::int64) (from * next->sampleRate);

        if (start < 0 || start >= next->lengthInSamples)
            start = 0;

        sourceRate.store (next->sampleRate);
        lengthSecs.store ((double) next->lengthInSamples / next->sampleRate);
        reader = std::move (next);
        readPos = start;
        shownPos.store (start);
        resampler.reset();
        phone.store (false);
    }

    current = name;
    flush.fetch_add (1);
    up.store (true);
    playing.store (true);
    dirty = true;

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::normal);

    notify();
    return {};
}

void BreakMusic::stop()
{
    rememberPlace();
    playing.store (false);
    dirty = true;
}

void BreakMusic::rememberPlace()
{
    if (current.isNotEmpty() && ! phone.load() && lengthSecs.load() > 0.0)
        places[current] = positionSeconds();
}

double BreakMusic::positionSeconds() const noexcept
{
    const auto r = sourceRate.load();
    const auto length = lengthSecs.load();

    if (r <= 0.0 || length <= 0.0)
        return 0.0;

    // What has been read, less what is still waiting in the queue.
    auto at = (double) shownPos.load() / r - (double) fifo.getNumReady() / juce::jmax (1.0, rate);

    while (at < 0.0)
        at += length;

    return std::fmod (at, length);
}

void BreakMusic::setLevelDb (float db)
{
    level = juce::jlimit (minLevelDb, maxLevelDb, db);
    levelGain.store (gainFor (level));
    dirty = true;
}

void BreakMusic::bringUp()
{
    up.store (true);
}

void BreakMusic::fadeDown()
{
    up.store (false);
}

void BreakMusic::setSettings (const Settings& s)
{
    set = s;
    set.fadeDownSeconds = juce::jlimit (0.1, 30.0, s.fadeDownSeconds);
    set.fadeUpSeconds = juce::jlimit (0.1, 30.0, s.fadeUpSeconds);
    set.soundDb = juce::jlimit (-60.0f, 0.0f, s.soundDb);
    set.returnPair = juce::jlimit (0, 3, s.returnPair);
    downStep.store (1.0 / (set.fadeDownSeconds * rate));
    upStep.store (1.0 / (set.fadeUpSeconds * rate));
    hearOn.store (set.onSound);
    hearGain.store (std::pow (10.0f, set.soundDb / 20.0f));
    pairNow.store (set.returnPair);
    dirty = true;
}

void BreakMusic::startedPlaying (Started kind) noexcept
{
    const bool counts = kind == Started::footswitch ? set.onSwitch : kind == Started::song ? set.onSong : hearOn.load (std::memory_order_relaxed);

    if (counts && playing.load (std::memory_order_relaxed) && up.exchange (false))
    {
        byPlaying.fetch_add (1);
        lastStart.store ((int) kind);
    }
}

void BreakMusic::tick()
{
    if (playing.load() && ! phone.load() && ++ticksSincePlace >= 100)
    {
        ticksSincePlace = 0;
        rememberPlace();
        dirty = true;
    }

    if (dirty && ++ticksSinceChange >= 20)
    {
        dirty = false;
        ticksSinceChange = 0;
        saveState();
    }
}

//==============================================================================
juce::String BreakMusic::stateText() const
{
    juce::String t ("# GigRig break music: the app's own memory of the Break music tab. Safe to delete (it starts afresh).\n");
    t << "[break music]\n";
    t << "mix=" << current << "\n";
    t << "level=" << juce::String (level, 1) << "\n";
    t << "fadedown=" << juce::String (set.fadeDownSeconds, 1) << "\n";
    t << "fadeup=" << juce::String (set.fadeUpSeconds, 1) << "\n";
    t << "onswitch=" << (set.onSwitch ? 1 : 0) << "\n";
    t << "onsong=" << (set.onSong ? 1 : 0) << "\n";
    t << "onsound=" << (set.onSound ? 1 : 0) << "\n";
    t << "sounddb=" << juce::String (set.soundDb, 1) << "\n";
    t << "returnpair=" << (set.returnPair + 1) << "\n";
    t << "[places]\n";

    for (const auto& [name, seconds] : places)
        t << name << "=" << juce::String (seconds, 1) << "\n";

    return t;
}

void BreakMusic::loadStateText (const juce::String& text)
{
    juce::String section;
    auto s = set;

    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();

        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;

        if (line.startsWithChar ('['))
        {
            section = line.removeCharacters ("[]").trim();
            continue;
        }

        // A mix's name may hold an equals sign; the figure after the last one is its place.
        const auto key = section == "places" ? line.upToLastOccurrenceOf ("=", false, false).trim() : line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = section == "places" ? line.fromLastOccurrenceOf ("=", false, false).trim() : line.fromFirstOccurrenceOf ("=", false, false).trim();

        if (section == "places")
        {
            if (key.isNotEmpty())
                places[key] = juce::jmax (0.0, value.getDoubleValue());
        }
        else if (section == "break music")
        {
            if (key == "mix")               current = value;
            else if (key == "level")        setLevelDb (value.getFloatValue());
            else if (key == "fadedown")     s.fadeDownSeconds = value.getDoubleValue();
            else if (key == "fadeup")       s.fadeUpSeconds = value.getDoubleValue();
            else if (key == "onswitch")     s.onSwitch = value.getIntValue() != 0;
            else if (key == "onsong")       s.onSong = value.getIntValue() != 0;
            else if (key == "onsound")      s.onSound = value.getIntValue() != 0;
            else if (key == "sounddb")      s.soundDb = value.getFloatValue();
            else if (key == "returnpair")   s.returnPair = value.getIntValue() - 1;
        }
    }

    setSettings (s);
    dirty = false;
}

void BreakMusic::saveState()
{
    if (! memoryOnDisk.load())
        return;

    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (stateText());
}

juce::String BreakMusic::statusLine() const
{
    if (current.isEmpty())
        return "Nothing chosen yet.";

    if (! playing.load())
        return "Stopped. Last played: '" + current + "'.";

    juce::String t ("Playing '" + current + "'");

    if (! phone.load())
        t << ": " << clock (positionSeconds()) << " of " << clock (lengthSecs.load());

    const auto f = fadeNow();
    t << (up.load() ? (f >= 0.999f ? ", up" : ", coming up") : (f <= 0.001f ? ", faded down (still playing, silent)" : ", fading down"));
    t << ", level " << juce::String (level, 1) << " dB";
    return t;
}

juce::String BreakMusic::reportText() const
{
    static const char* const starts[3] { "a footswitch", "a song loading", "sound on an input" };
    const auto list = mixes();
    juce::String t ("Break music\n");
    t << "  Folder: " << root.getFullPathName() << " (" << list.size() << (list.size() == 1 ? " mix" : " mixes") << (list.isEmpty() ? juce::String() : ": " + list.joinIntoString (", ")) << ")\n";
    t << "  Now: " << statusLine() << "\n";
    t << "  Fades down in " << juce::String (set.fadeDownSeconds, 1) << " s when: "
      << (set.onSwitch ? "a footswitch is pressed" : "")
      << (set.onSong ? juce::String (set.onSwitch ? ", " : "") + "a song loads" : juce::String())
      << (set.onSound ? juce::String (set.onSwitch || set.onSong ? ", " : "") + "an input is louder than " + juce::String (set.soundDb, 0) + " dB" : juce::String())
      << (! set.onSwitch && ! set.onSong && ! set.onSound ? "never by itself" : "")
      << ". Comes up in " << juce::String (set.fadeUpSeconds, 1) << " s, by hand only.\n";
    t << "  Faded down by playing: " << byPlaying.load() << (byPlaying.load() == 1 ? " time" : " times");

    if (const int last = lastStart.load(); last >= 0 && last < 3)
        t << " (last: " << starts[last] << ")";

    t << ". Times the music ran dry: " << underruns.load() << ".\n";
    t << "  With the mixer off it plays on returns " << (set.returnPair * 2 + 1) << " and " << (set.returnPair * 2 + 2) << ".\n";

    if (phoneSupported())
        t << "  Phone: reads " << pipe.getFullPathName() << (pipe.exists() ? "" : " (not there yet: linux/break-music.sh makes it)")
          << ", " << juce::String ((double) phoneFrames.load() / phoneRate, 1) << " s of sound received.\n";

    if (lastError.isNotEmpty())
        t << "  Last problem: " << lastError << "\n";

    return t;
}

//==============================================================================
void BreakMusic::prepare (double sampleRate, int maxBlock)
{
    const juce::ScopedLock l (sourceLock);
    rate = deviceRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    capacity = juce::jmax (16, maxBlock);
    outL.assign ((size_t) capacity, 0.0f);
    outR.assign ((size_t) capacity, 0.0f);

    const auto size = (int) (queueSeconds * rate) + capacity;
    fifoL.assign ((size_t) size, 0.0f);
    fifoR.assign ((size_t) size, 0.0f);
    fifo.setTotalSize (size);
    readBuf.setSize (2, readChunk);
    const auto most = Resampler::mostOut (readChunk, 8000.0 / 192000.0);
    resL.assign ((size_t) most, 0.0f);
    resR.assign ((size_t) most, 0.0f);
    pipeBytes.assign ((size_t) readChunk * 4 + 4, 0);
    resampler.reset();
    primed = false;
    gainNow = levelGain.load();
    hotSamples = 0;
    setSettings (set);      // the fade steps depend on the rate
}

void BreakMusic::queue (const float* l, const float* r, int n) noexcept
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);

    if (size1 > 0)
    {
        juce::FloatVectorOperations::copy (fifoL.data() + start1, l, size1);
        juce::FloatVectorOperations::copy (fifoR.data() + start1, r, size1);
    }

    if (size2 > 0)
    {
        juce::FloatVectorOperations::copy (fifoL.data() + start2, l + size1, size2);
        juce::FloatVectorOperations::copy (fifoR.data() + start2, r + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
}

bool BreakMusic::pumpFile()
{
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const double step = reader->sampleRate / deviceRate;

    if (fifo.getFreeSpace() < Resampler::mostOut (readChunk, step) || (int) resL.size() < Resampler::mostOut (readChunk, step))
        return false;

    // Round and round: the end of the file runs straight into its start.
    for (int done = 0; done < readChunk;)
    {
        const auto n = (int) juce::jmin ((juce::int64) (readChunk - done), reader->lengthInSamples - readPos);
        reader->read (&readBuf, done, n, readPos, true, true);
        readPos += n;
        done += n;

        if (readPos >= reader->lengthInSamples)
            readPos = 0;
    }

    const int made = resampler.push (readBuf.getReadPointer (0), readBuf.getReadPointer (1), readChunk, step, resL.data(), resR.data(), (int) resL.size());
    queue (resL.data(), resR.data(), made);
    shownPos.store (readPos);
    return true;
}

bool BreakMusic::pumpPhone()
{
   #if JUCE_LINUX
    if (! playing.load())
    {
        if (pipeFd >= 0)
        {
            ::close (pipeFd);       // the receiver sees the pipe close and waits for the next Play
            pipeFd = -1;
        }

        return false;
    }

    if (pipeFd < 0)
    {
        const auto path = pipe.getFullPathName();

        if (! pipe.exists())
        {
            pipe.getParentDirectory().createDirectory();
            ::mkfifo (path.toRawUTF8(), 0660);
        }

        pipeFd = ::open (path.toRawUTF8(), O_RDONLY | O_NONBLOCK);

        if (pipeFd < 0)
            return false;

        pipeHeld = 0;
        resampler.reset();
    }

    const auto got = ::read (pipeFd, pipeBytes.data() + pipeHeld, pipeBytes.size() - 4 - pipeHeld);

    if (got <= 0)
        return false;       // nobody is sending, or nothing has arrived yet

    const auto bytes = pipeHeld + (size_t) got;
    const int frames = (int) (bytes / 4);
    auto* l = readBuf.getWritePointer (0);
    auto* r = readBuf.getWritePointer (1);

    for (int i = 0; i < frames; ++i)
    {
        const auto* b = (const unsigned char*) pipeBytes.data() + (size_t) i * 4;
        l[i] = (float) (juce::int16) (b[0] | (b[1] << 8)) / 32768.0f;
        r[i] = (float) (juce::int16) (b[2] | (b[3] << 8)) / 32768.0f;
    }

    pipeHeld = bytes - (size_t) frames * 4;
    std::memmove (pipeBytes.data(), pipeBytes.data() + (size_t) frames * 4, pipeHeld);
    phoneFrames.fetch_add (frames);

    // The phone's clock and the interface's are never quite the same speed. The queue's
    // average fill says which is ahead; the speed is nudged (half a percent at most) to hold it.
    const double fill = (double) fifo.getNumReady() / deviceRate;

    if (fill >= phoneHeldSeconds * 0.5)     // while the queue first fills there is nothing to learn from it
        fillAverage += (fill - fillAverage) * 0.02;

    const double nudge = juce::jlimit (-0.005, 0.005, (fillAverage - phoneHeldSeconds) / phoneHeldSeconds * 0.005);
    const double step = phoneRate / deviceRate * (1.0 + nudge);
    const int made = resampler.push (l, r, frames, step, resL.data(), resR.data(), (int) resL.size());

    if (fifo.getFreeSpace() >= made)       // a full queue means nothing is listening: let this piece go
        queue (resL.data(), resR.data(), made);

    return true;
   #else
    return false;
   #endif
}

bool BreakMusic::pump()
{
    return phone.load() ? pumpPhone() : pumpFile();
}

void BreakMusic::fillNow()
{
    const juce::ScopedLock l (sourceLock);

    while (pump()) {}
}

void BreakMusic::run()
{
    while (! threadShouldExit())
    {
        bool did = false;

        {
            const juce::ScopedLock l (sourceLock);
            did = pump();
        }

        if (! did)
            wait (phone.load() && playing.load() ? 5 : 15);
    }
}

//==============================================================================
bool BreakMusic::render (int n) noexcept
{
    if (n <= 0 || n > capacity)
        return false;

    // Another mix was started: whatever is still queued from the old one is thrown away.
    if (const int f = flush.load (std::memory_order_relaxed); f != flushSeen)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
        fifo.finishedRead (size1 + size2);
        flushSeen = f;
        primed = false;
    }

    const bool play = playing.load (std::memory_order_relaxed);

    if (! play && fade <= 0.0)
    {
        fadeShown.store (0.0f, std::memory_order_relaxed);
        return false;
    }

    const bool wantUp = play && up.load (std::memory_order_relaxed);
    const double fadeFrom = fade;
    const double step = wantUp ? upStep.load (std::memory_order_relaxed)
                               : -(play ? downStep.load (std::memory_order_relaxed) : 1.0 / (stopFadeSeconds * rate));
    fade = juce::jlimit (0.0, 1.0, fade + step * (double) n);
    fadeShown.store ((float) fade, std::memory_order_relaxed);

    // The music keeps moving while it is faded down, so the queue is emptied either way.
    const int ready = fifo.getNumReady();

    if (! primed)
        primed = ready >= (phone.load (std::memory_order_relaxed) ? juce::jmax (n, (int) (phoneHeldSeconds * rate)) : n);

    if (primed && ready < n)
    {
        primed = false;

        if (fadeFrom > 0.0)
            underruns.fetch_add (1, std::memory_order_relaxed);
    }

    if (! primed)
        return false;

    int start1, size1, start2, size2;
    fifo.prepareToRead (n, start1, size1, start2, size2);

    if (size1 > 0)
    {
        juce::FloatVectorOperations::copy (outL.data(), fifoL.data() + start1, size1);
        juce::FloatVectorOperations::copy (outR.data(), fifoR.data() + start1, size1);
    }

    if (size2 > 0)
    {
        juce::FloatVectorOperations::copy (outL.data() + size1, fifoL.data() + start2, size2);
        juce::FloatVectorOperations::copy (outR.data() + size1, fifoR.data() + start2, size2);
    }

    fifo.finishedRead (size1 + size2);

    const float target = levelGain.load (std::memory_order_relaxed);

    if (fadeFrom <= 0.0 && fade <= 0.0)
    {
        gainNow = target;       // silent: a level change needs no glide
        return false;
    }

    const float most = (float) (1.0 / (levelGlideSeconds * rate));
    const double fadeInc = (fade - fadeFrom) / (double) n;

    for (int i = 0; i < n; ++i)
    {
        gainNow += juce::jlimit (-most, most, target - gainNow);
        const auto f = (float) (fadeFrom + fadeInc * (double) (i + 1));
        const float g = gainNow * f * f;        // squared: an even fade to the ear
        outL[(size_t) i] *= g;
        outR[(size_t) i] *= g;
    }

    const float p = juce::jmax (peakOf (outL.data(), n), peakOf (outR.data(), n));

    if (p > peak.load (std::memory_order_relaxed))
        peak.store (p, std::memory_order_relaxed);

    return true;
}

void BreakMusic::hear (const float* const* inputs, int numIns, int n) noexcept
{
    if (! hearOn.load (std::memory_order_relaxed) || ! playing.load (std::memory_order_relaxed) || ! up.load (std::memory_order_relaxed))
    {
        hotSamples = 0;
        return;
    }

    float loudest = 0.0f;

    for (int s = 0; s < juce::jmin (numIns, analogueSockets); ++s)
        if (inputs[s] != nullptr)
            loudest = juce::jmax (loudest, peakOf (inputs[s], n));

    hotSamples = loudest >= hearGain.load (std::memory_order_relaxed) ? hotSamples + n : 0;

    if ((double) hotSamples >= hearSeconds * rate)
    {
        hotSamples = 0;
        startedPlaying (Started::sound);
    }
}

//==============================================================================
bool VenueKnob::onBreakMusic()
{
    // Tom has started playing: the knob goes back to its resting job by itself.
    if (const int fades = music.fadedByPlaying(); fades != fadesSeen)
    {
        fadesSeen = fades;
        breakJob = false;
    }

    return breakJob;
}

juce::String VenueKnob::hold (double nowMs)
{
    breakJob = ! onBreakMusic();
    return screen (nowMs);
}

juce::String VenueKnob::turn (int steps, double nowMs)
{
    if (! onBreakMusic())
        return mixer.turnVenue (steps, nowMs);

    if (! music.isPlaying())
        return "No break music is playing. Push to start " + (music.mixName().isEmpty() ? juce::String ("a mix (choose one on the Break music tab first)") : "'" + music.mixName() + "'") + ".";

    music.setLevelDb (music.levelDb() + (float) steps);

    if (steps > 0)
        music.bringUp();        // turning it up is asking to hear it

    return screen (nowMs);
}

juce::String VenueKnob::push (double nowMs)
{
    if (! onBreakMusic())
        return mixer.pushVenue (nowMs);

    if (! music.isPlaying())
    {
        if (music.mixName().isEmpty())
            return "Choose a mix on the Break music tab first.";

        if (const auto error = music.play (music.mixName()); error.isNotEmpty())
            return error;
    }
    else if (music.isUp())
        music.fadeDown();
    else
        music.bringUp();

    return screen (nowMs);
}

juce::String VenueKnob::screen (double nowMs)
{
    if (! onBreakMusic())
        return mixer.venueScreen (nowMs);

    return "BREAK MUSIC knob. " + music.statusLine() + ". Turn = level, push = up or down, hold = back to PA/venue.";
}

} // namespace gigrig
