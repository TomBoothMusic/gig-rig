#include "LoopReplay.h"

#include <cmath>
#include <cstring>

namespace gigrig
{

LoopReplay::LoopReplay (Looper& l) : looper (l)
{
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz (10);
}

LoopReplay::~LoopReplay()
{
    stopTimer();
}

//==============================================================================
// The file. Its words are on Tom's disk: add, never rename.

juce::String LoopReplay::headerText (double rate, const juce::String& song, bool going)
{
    juce::String t;
    t << "Every looper press of this recording, so a virtual soundcheck can build the loops again. Read by the app: do not edit.\n"
      << "Times are samples from the start of the recording.\n"
      << "rate = " << juce::roundToInt (rate) << "\n"
      << "start: song = " << song << ", loops = " << (going ? "going" : "none") << "\n";
    return t;
}

juce::String LoopReplay::lineFor (const Step& s)
{
    juce::String t;
    t << s.at << ": ";

    if (s.kind == Looper::Timed::Kind::clearAll)
        t << "clear all";
    else if (s.kind == Looper::Timed::Kind::song)
        t << "song = " << s.song;
    else
        t << "switch " << (s.event.sw + 1) << " "
          << (s.event.kind == PedalEvent::Kind::up ? "up" : s.event.kind == PedalEvent::Kind::hold ? "hold" : (s.event.isDouble ? "down double" : "down"));

    return t + "\n";
}

LoopReplay::Tape LoopReplay::parse (const juce::String& text)
{
    Tape tape;

    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();

        if (line.startsWith ("rate = "))
        {
            tape.rate = line.fromFirstOccurrenceOf ("=", false, false).trim().getDoubleValue();
            continue;
        }

        if (line.startsWith ("start: song = "))
        {
            const auto rest = line.fromFirstOccurrenceOf ("start: song = ", false, false);
            tape.startSong = rest.upToLastOccurrenceOf (", loops = ", false, false).trim();
            tape.loopsGoing = rest.fromLastOccurrenceOf (", loops = ", false, false).trim() != "none";
            tape.found = true;
            continue;
        }

        const auto colon = line.indexOfChar (':');

        if (colon <= 0 || ! line.substring (0, colon).containsOnly ("0123456789"))
            continue;

        Step s;
        s.at = line.substring (0, colon).getLargeIntValue();
        const auto what = line.substring (colon + 1).trim();

        if (what == "clear all")
            s.kind = Looper::Timed::Kind::clearAll;
        else if (what.startsWith ("song = "))
        {
            s.kind = Looper::Timed::Kind::song;
            s.song = what.fromFirstOccurrenceOf ("song = ", false, false).trim();
        }
        else if (what.startsWith ("switch "))
        {
            const auto words = juce::StringArray::fromTokens (what, " ", {});

            if (words.size() < 3)
                continue;

            s.event.sw = words[1].getIntValue() - 1;

            if (s.event.sw < 0 || s.event.sw >= numPedalSwitches)
                continue;

            if (words[2] == "down")         { s.event.kind = PedalEvent::Kind::down; s.event.isDouble = words.size() > 3 && words[3] == "double"; }
            else if (words[2] == "up")      s.event.kind = PedalEvent::Kind::up;
            else if (words[2] == "hold")    s.event.kind = PedalEvent::Kind::hold;
            else                            continue;
        }
        else
            continue;       // a line from a later build

        tape.steps.push_back (s);
    }

    if (tape.rate <= 0.0)
        tape.found = false;

    std::stable_sort (tape.steps.begin(), tape.steps.end(), [] (const Step& a, const Step& b) { return a.at < b.at; });
    return tape;
}

LoopReplay::Tape LoopReplay::read (const juce::File& folder)
{
    const auto file = folder.getChildFile (fileName);
    return file.existsAsFile() ? parse (file.loadFileAsString()) : Tape();
}

//==============================================================================
// Recording.

void LoopReplay::noteBlock (const Looper& l, bool recording, juce::int64 after, int n, double engineRate) noexcept
{
    if (! recording)
    {
        wasRecording = false;
        return;
    }

    const auto base = after - n;

    if (base < 0)
        return;

    const auto push = [this] (const Note& note)
    {
        const auto scope = noteFifo.write (1);

        if (scope.blockSize1 > 0)        notes[(size_t) scope.startIndex1] = note;
        else if (scope.blockSize2 > 0)   notes[(size_t) scope.startIndex2] = note;
        else                             dropped.fetch_add (1, std::memory_order_relaxed);
    };

    const bool first = ! wasRecording || base == 0;
    wasRecording = true;

    if (first)
    {
        Note s;
        s.start = true;
        s.rate = engineRate;
        std::memcpy (s.name, l.songNameNow(), sizeof (s.name));

        // Loops that were going before the recording began cannot be built again from it. A press
        // in the recording's own first block does not count: it is in the list.
        s.going = l.holdsAnything() && l.numTapped() == 0;
        push (s);
    }

    for (int i = 0; i < l.numTapped(); ++i)
    {
        const auto& t = l.tapped (i);
        Note note;
        note.at = base + t.offset;
        note.kind = t.kind;
        note.event = t.event;
        std::memcpy (note.name, t.name, sizeof (note.name));
        push (note);
    }
}

//==============================================================================
// Soundcheck.

juce::String LoopReplay::arm (const juce::File& folder, double engineRate, juce::int64 passFrames, bool roundAndRound)
{
    poll();         // a recording stopped a moment ago has its last presses written first
    disarm();

    lastName = folder.getFileName();
    tapePresses = 0;
    const auto tape = read (folder);

    if (! tape.found)
        return status = "No loops: this recording was made before looper presses were noted.";

    for (const auto& s : tape.steps)
        if (s.kind != Looper::Timed::Kind::song)
            ++tapePresses;

    if (tape.loopsGoing)
        return status = "No loops: loops were already going when this recording began, so they cannot be built again.";

    if (tapePresses == 0)
        return status = "No loops: the looper was not used in this recording.";

    if (engineRate <= 0.0 || passFrames <= 0)
        return status = "No loops: no audio device is open.";

    // The first song: left alone if it is the one loaded now (so what Tom has changed in it stays), loaded if not.
    if (looper.song().title() != tape.startSong)
        if (loadSong == nullptr || ! loadSong (tape.startSong))
            return status = "No loops: the song \"" + tape.startSong + "\" this recording was made with is not in the Songs list, and another song is loaded.";

    looper.clearAll();

    {
        const juce::SpinLock::ScopedLockType guard (lock);
        steps = tape.steps;

        for (auto& s : steps)
            s.at = (juce::int64) std::llround ((double) s.at * engineRate / tape.rate);

        pass = passFrames;
        wraps = roundAndRound;
        cursor = 0;
        startSong = tape.startSong;
        made.store (0);
        songFifo.reset();
        armed.store (true);
    }

    return status = "Loops: " + juce::String (tapePresses) + " looper presses will be made again, so the loops are built through the sound as it is now.";
}

void LoopReplay::disarm()
{
    if (! armed.load())
        return;

    {
        const juce::SpinLock::ScopedLockType guard (lock);
        armed.store (false);
    }

    // The loops the soundcheck built are nobody's playing: they go when it stops.
    if (made.load() > 0)
        looper.clearAll();

    status = "The last soundcheck made " + juce::String (made.load()) + " of its " + juce::String (tapePresses) + " looper presses again.";
}

int LoopReplay::pressesFor (juce::int64 blockStart, int n, Looper::Timed* out, int maxOut) noexcept
{
    if (! armed.load (std::memory_order_relaxed) || blockStart < 0 || n <= 0)
        return 0;

    const juce::SpinLock::ScopedTryLockType guard (lock);

    if (! guard.isLocked() || ! armed.load (std::memory_order_relaxed) || pass <= 0)
        return 0;

    int count = 0;

    const auto tellSong = [this] (int index)
    {
        const auto scope = songFifo.write (1);

        if (scope.blockSize1 > 0)        songSteps[(size_t) scope.startIndex1] = index;
        else if (scope.blockSize2 > 0)   songSteps[(size_t) scope.startIndex2] = index;
    };

    // Steps from 'from' up to (not including) 'to', placed 'shift' samples into the block.
    const auto take = [&] (juce::int64 from, juce::int64 to, int shift)
    {
        while (cursor < steps.size() && steps[cursor].at < from)
            ++cursor;

        for (; cursor < steps.size() && steps[cursor].at < to; ++cursor)
        {
            const auto& s = steps[cursor];

            if (s.kind == Looper::Timed::Kind::song)
            {
                tellSong ((int) cursor);
                continue;
            }

            if (count >= maxOut)
                continue;

            auto& t = out[count++];
            t.offset = (int) (s.at - from) + shift;
            t.kind = s.kind;
            t.event = s.event;
            made.fetch_add (1, std::memory_order_relaxed);
        }
    };

    if (! wraps)
    {
        take (blockStart, blockStart + n, 0);
        return count;
    }

    const auto position = blockStart % pass;

    if (blockStart == 0)
        cursor = 0;

    // The recording came round to its start exactly at this block: the loops of the last pass go first.
    if (position == 0 && blockStart > 0)
    {
        if (count < maxOut)
        {
            auto& t = out[count++];
            t.offset = 0;
            t.kind = Looper::Timed::Kind::clearAll;
            t.event = {};
        }

        tellSong (-1);
        cursor = 0;
    }

    if (position + n <= pass)
    {
        take (position, position + n, 0);
        return count;
    }

    // The recording comes round to its start inside this block: the loops of the last pass go, then it all begins again.
    const int edge = (int) (pass - position);
    take (position, pass, 0);

    if (count < maxOut)
    {
        auto& t = out[count++];
        t.offset = edge;
        t.kind = Looper::Timed::Kind::clearAll;
        t.event = {};
    }

    tellSong (-1);
    cursor = 0;
    take (0, n - edge, edge);
    return count;
}

//==============================================================================
void LoopReplay::poll()
{
    // ---- presses noted while recording go into the recording's folder ----
    {
        juce::String lines;
        const auto scope = noteFifo.read (noteFifo.getNumReady());

        const auto one = [&] (const Note& n)
        {
            if (n.start)
            {
                if (lines.isNotEmpty() && writingTo != juce::File())
                    writingTo.appendText (lines);

                lines.clear();
                writingTo = recordingFolder != nullptr ? recordingFolder().getChildFile (fileName) : juce::File();
                written = 0;

                if (writingTo != juce::File())
                    writingTo.replaceWithText (headerText (n.rate, juce::String::fromUTF8 (n.name).trim(), n.going));

                return;
            }

            Step s;
            s.at = n.at;
            s.kind = n.kind;
            s.event = n.event;
            s.song = juce::String::fromUTF8 (n.name).trim();
            lines << lineFor (s);
            ++written;
        };

        for (int i = 0; i < scope.blockSize1; ++i)  one (notes[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)  one (notes[(size_t) (scope.startIndex2 + i)]);

        if (lines.isNotEmpty() && writingTo != juce::File())
            writingTo.appendText (lines);
    }

    // ---- a soundcheck: the songs it has reached, and its end ----
    if (! armed.load())
        return;

    {
        const auto scope = songFifo.read (songFifo.getNumReady());

        const auto one = [this] (int index)
        {
            const auto name = index >= 0 && index < (int) steps.size() ? steps[(size_t) index].song : startSong;

            if (name.isNotEmpty() && looper.song().title() != name && loadSong != nullptr)
                loadSong (name);
        };

        for (int i = 0; i < scope.blockSize1; ++i)  one (songSteps[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)  one (songSteps[(size_t) (scope.startIndex2 + i)]);
    }

    if (soundcheckPlaying != nullptr && ! soundcheckPlaying())
        disarm();
}

juce::String LoopReplay::reportText() const
{
    juce::String t;
    t << "Loops in a soundcheck: ";

    if (armed.load())
        t << pressesMade() << " of " << tapePresses << " looper presses made again so far (" << lastName << ").";
    else if (status.isNotEmpty())
        t << status << (lastName.isNotEmpty() ? " (" + lastName + ")" : juce::String());
    else
        t << "no soundcheck has been played yet.";

    t << "\nLooper presses noted in recordings since the app started: " << written << " in the last recording";

    if (dropped.load() > 0)
        t << ", WARNING: " << dropped.load() << " could not be noted";

    return t + ".\n";
}

} // namespace gigrig
