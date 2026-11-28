#include "LoopReplay.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace gigrig
{

LoopReplay::LoopReplay (Looper& l, EffectsRack& r, KickPad& k) : looper (l), rack (r), kick (k)
{
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz (100);
}

LoopReplay::~LoopReplay()
{
    stopTimer();
}

//==============================================================================
// The file. Its words are on Tom's disk: add, never rename.

juce::String LoopReplay::headerText (double rate, const juce::String& song, bool going, const std::vector<SwitchState>& switches, float pedal)
{
    juce::String t;
    t << "Everything Tom's feet did in this recording (looper presses, kicks, effect switches, the expression pedal), so a virtual soundcheck can do it all again. Read by the app: do not edit.\n"
      << "Times are samples from the start of the recording.\n"
      << "rate = " << juce::roundToInt (rate) << "\n"
      << "start: song = " << song << ", loops = " << (going ? "going" : "none") << "\n";

    for (const auto& s : switches)
        t << "switch at start: " << (s.sw + 1) << " chain " << s.chain << " action " << s.action << " = " << (s.state != 0 ? "on" : "off") << "\n";

    if (pedal >= 0.0f)
        t << "pedal at start = " << juce::String (pedal, 2) << "\n";

    return t;
}

juce::String LoopReplay::lineFor (const Step& s)
{
    juce::String t;
    t << s.at << ": ";

    if (s.what == What::clearAll)
        t << "clear all";
    else if (s.what == What::song)
        t << "song = " << s.song;
    else if (s.what == What::kick)
        t << "kick " << juce::roundToInt (s.value);
    else if (s.what == What::expression)
        t << "pedal " << juce::String (s.value, 2);
    else if (s.what == What::fxSwitch)
        t << "effect switch " << (s.event.sw + 1) << " " << (s.event.kind == PedalEvent::Kind::down ? "down" : "up");
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

        if (line.startsWith ("switch at start: "))
        {
            // switch at start: <switch> chain <chain> action <action> = on|off
            const auto words = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf ("switch at start: ", false, false), " ", {});

            if (words.size() >= 7 && words[1] == "chain" && words[3] == "action" && words[5] == "=")
            {
                SwitchState s;
                s.sw = words[0].getIntValue() - 1;
                s.chain = words[2].getIntValue();
                s.action = words[4].getIntValue();
                s.state = words[6] == "on" ? 1 : 0;

                if (s.sw >= 0)
                    tape.switches.push_back (s);
            }

            continue;
        }

        if (line.startsWith ("pedal at start = "))
        {
            tape.pedalAtStart = juce::jlimit (0.0f, 100.0f, line.fromFirstOccurrenceOf ("=", false, false).trim().getFloatValue());
            continue;
        }

        const auto colon = line.indexOfChar (':');

        if (colon <= 0 || ! line.substring (0, colon).containsOnly ("0123456789"))
            continue;

        Step s;
        s.at = line.substring (0, colon).getLargeIntValue();
        const auto what = line.substring (colon + 1).trim();
        const auto words = juce::StringArray::fromTokens (what, " ", {});

        if (what == "clear all")
            s.what = What::clearAll;
        else if (what.startsWith ("song = "))
        {
            s.what = What::song;
            s.song = what.fromFirstOccurrenceOf ("song = ", false, false).trim();
        }
        else if (what.startsWith ("kick ") && words.size() >= 2)
        {
            s.what = What::kick;
            s.value = (float) juce::jlimit (1, 127, words[1].getIntValue());
        }
        else if (what.startsWith ("pedal ") && words.size() >= 2)
        {
            s.what = What::expression;
            s.value = juce::jlimit (0.0f, 100.0f, words[1].getFloatValue());
        }
        else if (what.startsWith ("effect switch ") && words.size() >= 4)
        {
            s.what = What::fxSwitch;
            s.event.sw = words[2].getIntValue() - 1;

            if (s.event.sw < 0 || s.event.sw >= 16)
                continue;

            if (words[3] == "down")         s.event.kind = PedalEvent::Kind::down;
            else if (words[3] == "up")      s.event.kind = PedalEvent::Kind::up;
            else                            continue;
        }
        else if (what.startsWith ("switch "))
        {
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

void LoopReplay::noteBlock (const Looper& l, const KickPad& k, bool recording, juce::int64 after, int n, double engineRate) noexcept
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

    // A kick starts on its block's first sample, played by the pad or made again by a soundcheck.
    for (int i = 0; i < k.numStarted(); ++i)
    {
        Note note;
        note.at = base;
        note.what = What::kick;
        note.value = (float) k.startedStrength (i);
        push (note);
    }

    for (int i = 0; i < l.numTapped(); ++i)
    {
        const auto& t = l.tapped (i);
        Note note;
        note.at = base + t.offset;
        note.what = t.kind == Looper::Timed::Kind::clearAll ? What::clearAll : t.kind == Looper::Timed::Kind::song ? What::song : What::pedal;
        note.event = t.event;
        std::memcpy (note.name, t.name, sizeof (note.name));
        push (note);
    }
}

void LoopReplay::noteSwitch (const PedalEvent& e)
{
    if (e.kind == PedalEvent::Kind::hold)       // news for the looper only: the effects do nothing with it
        return;

    const auto at = recordedSamples != nullptr ? recordedSamples() : (juce::int64) -1;

    if (at < 0)
        return;

    Step s;
    s.at = at;
    s.what = What::fxSwitch;
    s.event = e;
    const juce::ScopedLock guard (controlLock);
    controlNotes.push_back (s);
}

//==============================================================================
// The effect switches that flip something.

std::vector<LoopReplay::SwitchState> LoopReplay::switchesNow() const
{
    std::vector<SwitchState> list;

    if (! rack.isEnabled())
        return list;

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
    {
        const auto a = rack.pedalAssign (i);
        const int state = rack.pedalToggleState (a);

        if (state < 0)
            continue;

        bool listed = false;

        for (const auto& s : list)
            listed = listed || (s.chain == a.chain && s.action == a.action);

        if (! listed)
            list.push_back ({ a.sw, a.chain, a.action, state });
    }

    return list;
}

void LoopReplay::putSwitches (const std::vector<SwitchState>& list)
{
    if (! rack.isEnabled())
        return;

    for (const auto& s : list)
        for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        {
            const auto a = rack.pedalAssign (i);

            // Only a switch that still does the same thing as on the night is touched.
            if (a.sw != s.sw || a.chain != s.chain || a.action != s.action)
                continue;

            const int now = rack.pedalToggleState (a);

            if (now >= 0 && now != s.state)
                rack.doPedalAction (a);

            break;
        }
}

void LoopReplay::backToStart()
{
    for (size_t sw = 0; sw < heldDown.size(); ++sw)
        if (std::exchange (heldDown[sw], false))
            rack.takePedalNow ((int) sw, false, juce::Time::getMillisecondCounterHiRes());

    putSwitches (startSwitches);

    if (pedalAtStart >= 0.0f && pedalTouched)
        rack.moveExpressionPedal (pedalAtStart);
}

void LoopReplay::doControl (const Step& s, juce::int64 position)
{
    if (s.what == What::expression)
    {
        rack.moveExpressionPedal (s.value);
        pedalTouched = true;
        ++controlled;
        return;
    }

    // A tap tempo press is timed by when it was made on the night, not by when this was called.
    const bool down = s.event.kind == PedalEvent::Kind::down;
    const double late = engineRateNow > 0.0 ? (double) (position - s.at) * 1000.0 / engineRateNow : 0.0;

    if (rack.takePedalNow (s.event.sw, down, juce::Time::getMillisecondCounterHiRes() - late))
    {
        heldDown[(size_t) s.event.sw] = down;
        ++controlled;
        noteSwitch (s.event);       // a soundcheck that is itself recorded has its switches in the new recording
    }
}

//==============================================================================
// Soundcheck.

juce::String LoopReplay::arm (const juce::File& folder, double engineRate, juce::int64 passFrames, bool roundAndRound)
{
    poll();         // a recording stopped a moment ago has its last presses written first
    disarm();

    lastName = folder.getFileName();
    counts = Tape();
    const auto tape = read (folder);

    if (! tape.found)
        return status = "Nothing is made again: this recording was made before looper presses, kicks and switches were noted.";

    if (engineRate <= 0.0 || passFrames <= 0)
        return status = "Nothing is made again: no audio device is open.";

    const int presses = tape.count (What::pedal) + tape.count (What::clearAll);
    const bool loops = presses > 0 && ! tape.loopsGoing;
    const juce::String noLoops = tape.loopsGoing ? "No loops: loops were already going when this recording began, so they cannot be built again." : "";

    std::vector<Step> forAudio, forControls;

    for (auto s : tape.steps)
    {
        s.at = (juce::int64) std::llround ((double) s.at * engineRate / tape.rate);

        if (s.what == What::fxSwitch || s.what == What::expression)
            forControls.push_back (s);
        else if (loops || (s.what != What::pedal && s.what != What::clearAll))
            forAudio.push_back (s);
    }

    if (forAudio.empty() && forControls.empty() && tape.switches.empty() && tape.pedalAtStart < 0.0f)
        return status = noLoops.isNotEmpty() ? noLoops : juce::String ("Nothing to make again: no looper press, kick, effect switch or pedal move is in this recording.");

    // The first song: left alone if it is the one loaded now (so what Tom has changed in it stays), loaded if not.
    if (looper.song().title() != tape.startSong)
        if (loadSong == nullptr || ! loadSong (tape.startSong))
            return status = "Nothing is made again: the song \"" + tape.startSong + "\" this recording was made with is not in the Songs list, and another song is loaded.";

    if (loops)
        looper.clearAll();

    counts = tape;
    counts.loopsGoing = ! loops;        // from here on: "its looper presses are not made"

    // The effect switches and the pedal start where they were on the night, and go back afterwards.
    switchesBefore = switchesNow();
    pedalBefore = rack.expressionNow();
    startSwitches = tape.switches;
    pedalAtStart = tape.pedalAtStart;
    pedalTouched = pedalAtStart >= 0.0f;
    heldDown.fill (false);
    backToStart();

    controlSteps = std::move (forControls);
    controlCursor = 0;
    lastPosition = -1;
    controlled = 0;
    engineRateNow = engineRate;

    {
        const juce::SpinLock::ScopedLockType guard (lock);
        steps = std::move (forAudio);
        pass = passFrames;
        wraps = roundAndRound;
        clearsLoops = loops;
        cursor = 0;
        startSong = tape.startSong;
        made.store (0);
        kicked.store (0);
        songFifo.reset();
        armed.store (true);
    }

    juce::StringArray parts;
    const auto part = [&parts] (int n, const char* one, const char* many) { if (n > 0) parts.add (juce::String (n) + " " + (n == 1 ? one : many)); };
    part (loops ? presses : 0, "looper press", "looper presses");
    part (tape.count (What::kick), "kick", "kicks");
    part (tape.count (What::fxSwitch), "effect switch press", "effect switch presses");
    part (tape.count (What::expression), "pedal move", "pedal moves");

    status = parts.isEmpty() ? juce::String ("The effect switches start where they were in the recording.")
                             : "Made again as it plays: " + parts.joinIntoString (", ") + ", all through the sound as it is now.";

    if (noLoops.isNotEmpty())
        status << " " << noLoops;

    return status;
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

    // The switches and the pedal go back to where Tom had them.
    for (size_t sw = 0; sw < heldDown.size(); ++sw)
        if (std::exchange (heldDown[sw], false))
            rack.takePedalNow ((int) sw, false, juce::Time::getMillisecondCounterHiRes());

    putSwitches (switchesBefore);

    if (pedalTouched)
        rack.moveExpressionPedal (pedalBefore);

    pedalTouched = false;
    status = "The last soundcheck made again " + juce::String (made.load()) + " looper presses, " + juce::String (kicked.load())
               + " kicks and " + juce::String (controlled) + " effect switch presses and pedal moves.";
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

    const auto clearLoops = [&] (int offset)
    {
        if (clearsLoops && count < maxOut)
        {
            auto& t = out[count++];
            t.offset = offset;
            t.kind = Looper::Timed::Kind::clearAll;
            t.event = {};
        }
    };

    // Steps from 'from' up to (not including) 'to', placed 'shift' samples into the block.
    const auto take = [&] (juce::int64 from, juce::int64 to, int shift)
    {
        while (cursor < steps.size() && steps[cursor].at < from)
            ++cursor;

        for (; cursor < steps.size() && steps[cursor].at < to; ++cursor)
        {
            const auto& s = steps[cursor];

            if (s.what == What::song)
            {
                tellSong ((int) cursor);
                continue;
            }

            if (s.what == What::kick)       // as a hit from the pad: it starts with this block
            {
                kick.hitNow ((int) s.value);
                kicked.fetch_add (1, std::memory_order_relaxed);
                continue;
            }

            if (count >= maxOut)
                continue;

            auto& t = out[count++];
            t.offset = (int) (s.at - from) + shift;
            t.kind = s.what == What::clearAll ? Looper::Timed::Kind::clearAll : Looper::Timed::Kind::pedal;
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
        clearLoops (0);
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
    clearLoops (edge);
    tellSong (-1);
    cursor = 0;
    take (0, n - edge, edge);
    return count;
}

//==============================================================================
void LoopReplay::poll()
{
    const auto recordedNow = recordedSamples != nullptr ? recordedSamples() : (juce::int64) -1;

    // ---- what the audio thread noted while recording goes into the recording's folder ----
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
                    writingTo.replaceWithText (headerText (n.rate, juce::String::fromUTF8 (n.name).trim(), n.going, switchesNow(),
                                                           rack.expressionMoves() > 0 ? rack.expressionNow() : -1.0f));

                return;
            }

            Step s;
            s.at = n.at;
            s.what = n.what;
            s.event = n.event;
            s.value = n.value;
            s.song = juce::String::fromUTF8 (n.name).trim();
            lines << lineFor (s);
            ++written;
        };

        for (int i = 0; i < scope.blockSize1; ++i)  one (notes[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)  one (notes[(size_t) (scope.startIndex2 + i)]);

        if (lines.isNotEmpty() && writingTo != juce::File())
            writingTo.appendText (lines);
    }

    // ---- the expression pedal: looked at a hundred times a second while recording ----
    if (const int moves = rack.expressionMoves(); moves != pedalMovesSeen)
    {
        pedalMovesSeen = moves;

        if (recordedNow >= 0)
        {
            Step s;
            s.at = recordedNow;
            s.what = What::expression;
            s.value = rack.expressionNow();
            const juce::ScopedLock guard (controlLock);
            controlNotes.push_back (s);
        }
    }

    // ---- effect switch presses and pedal moves go into the same file ----
    {
        std::vector<Step> mine;

        {
            const juce::ScopedLock guard (controlLock);
            mine.swap (controlNotes);
        }

        if (! mine.empty())
        {
            const auto file = recordingFolder != nullptr ? recordingFolder().getChildFile (fileName) : juce::File();

            if (writingTo != juce::File() && writingTo == file)
            {
                juce::String lines;

                for (const auto& s : mine)
                    lines << lineFor (s);

                writingTo.appendText (lines);
                written += (int) mine.size();
            }
            else if (recordedNow >= 0)      // the recording has only just begun: its file is made on the next look
            {
                const juce::ScopedLock guard (controlLock);
                controlNotes.insert (controlNotes.begin(), mine.begin(), mine.end());
            }
        }
    }

    // ---- a soundcheck: the songs it has reached, its switches and pedal, and its end ----
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

    const auto played = soundcheckPlayed != nullptr ? soundcheckPlayed() : (juce::int64) -1;

    if (played < 0)
    {
        disarm();
        return;
    }

    if (pass <= 0)
        return;

    // The recording came round: the switches and the pedal start again where they were on the night.
    if (wraps && lastPosition >= 0 && played / pass != lastPosition / pass)
    {
        if (startSong.isNotEmpty() && looper.song().title() != startSong && loadSong != nullptr)
            loadSong (startSong);

        backToStart();
        controlCursor = 0;
    }

    lastPosition = played;
    const auto position = wraps ? played % pass : played;

    while (controlCursor < controlSteps.size() && controlSteps[controlCursor].at <= position)
        doControl (controlSteps[controlCursor++], position);
}

juce::String LoopReplay::reportText() const
{
    juce::String t;
    t << "Loops, kicks and switches in a soundcheck: ";

    if (armed.load())
        t << pressesMade() << " of " << (counts.loopsGoing ? 0 : counts.count (What::pedal) + counts.count (What::clearAll)) << " looper presses, "
          << kicksMade() << " of " << counts.count (What::kick) << " kicks and "
          << controlsMade() << " of " << (counts.count (What::fxSwitch) + counts.count (What::expression))
          << " effect switch presses and pedal moves made again so far (" << lastName << ").";
    else if (status.isNotEmpty())
        t << status << (lastName.isNotEmpty() ? " (" + lastName + ")" : juce::String());
    else
        t << "no soundcheck has been played yet.";

    t << "\nLooper presses, kicks, effect switch presses and pedal moves noted in the last recording since the app started: " << written;

    if (dropped.load() > 0)
        t << ", WARNING: " << dropped.load() << " could not be noted";

    return t + ".\n";
}

} // namespace gigrig
