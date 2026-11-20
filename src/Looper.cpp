#include "Looper.h"

#include <cmath>
#include <limits>

namespace gigrig
{

namespace
{
    constexpr juce::int64 never = std::numeric_limits<juce::int64>::max() / 4;
    constexpr juce::int64 foldPerBlock = 65536;

    juce::int64 wrap (juce::int64 value, juce::int64 length) noexcept
    {
        const auto r = value % length;
        return r < 0 ? r + length : r;
    }

    bool isRecordingState (TrackState s) noexcept
    {
        return s == TrackState::armed || s == TrackState::recording || s == TrackState::closing;
    }

    juce::String trackLabel (int track)     { return "T" + juce::String (track + 1); }
}

//==============================================================================
juce::String loopSourceName (int source)
{
    static const char* const names[numLoopSources] = { "vocal", "acoustic", "stomp", "bass", "electric" };
    return source >= 0 && source < numLoopSources ? names[source] : "";
}

juce::String describeRcInputs (int rcMask)
{
    if ((rcMask & 63) == 63)
        return "every input";

    juce::StringArray parts;

    if ((rcMask & rcInst1L) != 0)   parts.add ("acoustic");
    if ((rcMask & rcInst1R) != 0)   parts.add ("stomp");
    if ((rcMask & rcInst2L) != 0)   parts.add ("bass");
    if ((rcMask & rcInst2R) != 0)   parts.add ("electric");
    if ((rcMask & rcMic1) != 0)     parts.add ("vocal");
    if ((rcMask & rcMic2) != 0)     parts.add ("mic 2");
    if ((rcMask & rcRhythm) != 0)   parts.add ("rhythm");

    return parts.isEmpty() ? "nothing" : parts.joinIntoString (" + ");
}

SwitchFunction decodeRcFunction (int code) noexcept
{
    using A = LoopAction;
    SwitchFunction f;
    f.rcCode = code;

    if (code <= 0)
        return f;

    if (code <= 182)
    {
        const bool current = code >= 157;
        const auto index = current ? code - 157 : (code - 1) % 26;
        f.track = current ? -1 : (code - 1) / 26;

        switch (index)
        {
            case 0:  f.push = A::recPlay; break;                                                    // REC/PLAY1
            case 1:  f.push = A::recPlay;  f.hold = A::undoRedo; break;                             // REC/PLAY2
            case 2:  f.push = A::recPlay;  f.hold = A::undoRedo;    f.click = A::stop; break;       // REC/PLAY3
            case 3:  f.push = A::recPlay;  f.hold = A::undoOrClear; f.click = A::stop; break;       // REC/PLAY4
            case 5:  f.push = A::playStop; f.hold = A::undoRedo; break;                             // PLAY/STOP1
            case 6:  f.push = A::playStop; f.hold = A::undoOrClear; break;                          // PLAY/STOP2
            case 7:  f.push = A::stop; break;                                                       // STOP1
            case 8:  f.push = A::stop; break;                                                       // STOP2 (double-press: tap tempo, not copied yet)
            case 9:  f.push = A::stop;     f.hold = A::clear; break;                                // STOP3
            case 10: f.push = A::stop;     f.hold = A::clear; break;                                // STOP4
            case 11: f.push = A::stop;     f.click = A::clear; break;                               // STOP5
            case 12: f.push = A::clear; break;                                                      // CLEAR
            case 14: f.push = A::undoRedo; break;                                                   // UNDO/REDO
            default: f.push = A::unsupported; break;
        }

        return f;
    }

    f.track = -2;

    switch (code)
    {
        case 186: f.push = A::allStartStop; break;
        case 187: f.push = A::allStartStop; f.hold = A::allClear; break;
        case 188: f.push = A::allStartStop; f.click = A::allClear; break;
        case 189: f.push = A::allClear; break;
        case 193: f.push = A::bankToggle; break;
        case 194: f.push = A::bankToggle; break;
        default:  f.push = A::unsupported; break;
    }

    return f;
}

juce::String describeFunction (const SwitchFunction& f)
{
    const auto who = f.track >= 0 ? trackLabel (f.track) + " " : f.track == -1 ? juce::String ("Current ") : juce::String();

    switch (f.push)
    {
        case LoopAction::none:          return "-";
        case LoopAction::recPlay:       return who + "rec/play";
        case LoopAction::playStop:      return who + "play/stop";
        case LoopAction::stop:          return who + "stop";
        case LoopAction::clear:         return who + "clear";
        case LoopAction::undoRedo:
        case LoopAction::undoOrClear:   return f.track == -1 ? juce::String ("Undo/redo") : who + "undo";
        case LoopAction::allStartStop:  return "All start/stop";
        case LoopAction::allClear:      return "All clear";
        case LoopAction::bankToggle:    return "Bank";
        case LoopAction::unsupported:   break;
    }

    if (f.rcCode >= 195 && f.rcCode <= 209)     return "Effect (later)";
    if (f.rcCode == 210 || f.rcCode == 211)     return "Song up/down (later)";
    if (f.rcCode >= 190 && f.rcCode <= 192)     return "Tempo (later)";

    return "RC-600 function " + juce::String (f.rcCode) + " (not copied yet)";
}

juce::String describeFunctionFully (const SwitchFunction& f)
{
    const auto extra = [] (LoopAction a) -> juce::String
    {
        switch (a)
        {
            case LoopAction::undoRedo:      return "undo, then redo";
            case LoopAction::undoOrClear:   return "undo, or clear if the track was stopped";
            case LoopAction::stop:          return "stop";
            case LoopAction::clear:         return "clear";
            case LoopAction::allClear:      return "clear every track";
            case LoopAction::none: case LoopAction::recPlay: case LoopAction::playStop: case LoopAction::allStartStop:
            case LoopAction::bankToggle: case LoopAction::unsupported: break;
        }

        return {};
    };

    auto text = describeFunction (f);

    if (extra (f.hold).isNotEmpty())    text << ". Hold: " << extra (f.hold);
    if (extra (f.click).isNotEmpty())   text << ". Double-press: " << extra (f.click);

    return text;
}

LoopAssign decodeRcAssign (int rcSource, int rcTarget) noexcept
{
    LoopAssign a;
    a.rcSource = rcSource;
    a.rcTarget = rcTarget;

    if (rcSource >= 0 && rcSource <= 5)             { a.source = LoopAssign::Source::trackRecDub;   a.sourceTrack = rcSource; }
    else if (rcSource >= 6 && rcSource <= 11)       { a.source = LoopAssign::Source::trackPlayStop; a.sourceTrack = rcSource - 6; }
    else if (rcSource >= 13 && rcSource <= 30)      { a.source = LoopAssign::Source::pedal; a.bank = (rcSource - 13) / 9; a.sw = (rcSource - 13) % 9; }
    else if (rcSource == 42 || rcSource == 43)      { a.source = LoopAssign::Source::pedal; a.bank = -1; a.sw = 9 + (rcSource - 42); }

    const auto kind = [] (int k)
    {
        switch (k)
        {
            case 0:  return LoopAction::recPlay;
            case 1:  return LoopAction::playStop;
            case 2:  return LoopAction::stop;
            case 3:  return LoopAction::clear;
            case 5:  return LoopAction::undoRedo;
            default: return LoopAction::unsupported;
        }
    };

    if (rcTarget >= 0 && rcTarget < 66)             { a.targetTrack = rcTarget / 11; a.action = kind (rcTarget % 11); }
    else if (rcTarget >= 66 && rcTarget < 77)       { a.targetTrack = -1; a.action = kind (rcTarget - 66); }
    else if (rcTarget == 80)                        { a.targetTrack = -2; a.action = LoopAction::allStartStop; }
    else                                            { a.targetTrack = -2; a.action = LoopAction::unsupported; }

    return a;
}

juce::String describeAssign (const LoopAssign& a)
{
    juce::String from;

    switch (a.source)
    {
        case LoopAssign::Source::off:            return {};
        case LoopAssign::Source::pedal:          from = pedalSwitchName (a.sw) + (a.bank >= 0 ? " (bank " + juce::String (a.bank + 1) + ")" : juce::String()); break;
        case LoopAssign::Source::trackRecDub:    from = trackLabel (a.sourceTrack) + " starting to record or overdub"; break;
        case LoopAssign::Source::trackPlayStop:  from = trackLabel (a.sourceTrack) + " finishing a recording or overdub"; break;
    }

    SwitchFunction f;
    f.push = a.action;
    f.track = a.targetTrack;
    f.rcCode = 0;

    return from + " also does: " + (a.action == LoopAction::unsupported ? "an effect or setting change (RC-600 target " + juce::String (a.rcTarget) + ", not copied yet)"
                                                                         : describeFunction (f));
}

juce::String describeSong (const SongSetup& song)
{
    juce::StringArray lines;

    lines.add (song.title() + (song.rcMemory > 0 ? "  (RC-600 memory " + juce::String (song.rcMemory) + ", saved tempo " + juce::String (song.savedBpm, 1) + ")" : juce::String()));
    lines.add (juce::String ("Order: ") + (song.recThenPlay ? "record, play, overdub" : "record, overdub, play")
                 + (song.quantise ? ". A new track waits for the next bar." : ". A new track starts at once."));

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& tr = song.track[(size_t) t];
        lines.add ("  " + trackLabel (t) + ": " + describeRcInputs (tr.rcInputs).paddedRight (' ', 20)
                     + (tr.bars > 0 ? juce::String (tr.bars) + (tr.bars == 1 ? " bar preset" : " bars preset") : juce::String ("free length"))
                     + (tr.stopMode == StopMode::loopEnd ? ", stops at the end of its loop" : tr.stopMode == StopMode::fade ? ", fades out" : "")
                     + (tr.level != 100 ? ", level " + juce::String (tr.level) : juce::String()));
    }

    for (int b = 0; b < numLoopBanks; ++b)
    {
        lines.add ("  Bank " + juce::String (b + 1) + ":");

        for (int sw = 0; sw < 9; ++sw)
            lines.add ("    " + pedalSwitchName (sw) + ": " + describeFunctionFully (decodeRcFunction (song.bank[b][sw])));
    }

    juce::StringArray linked;

    for (const auto& a : song.assign)
        if (a.source != LoopAssign::Source::off)
            linked.add ("    " + describeAssign (a));

    if (! linked.isEmpty())
    {
        lines.add ("  Linked actions:");
        lines.addArray (linked);
    }

    return lines.joinIntoString ("\n") + "\n";
}

juce::String trackStateName (TrackState s)
{
    switch (s)
    {
        case TrackState::empty:        return "empty";
        case TrackState::armed:        return "waiting for the bar";
        case TrackState::recording:    return "RECORDING";
        case TrackState::closing:      return "recording to the bar";
        case TrackState::playing:      return "playing";
        case TrackState::overdubbing:  return "OVERDUBBING";
        case TrackState::stopping:     return "stopping";
        case TrackState::stopped:      return "stopped";
    }

    return {};
}

juce::String describeLogEntry (const LoopLogEntry& e, double sampleRate)
{
    using K = LoopLogEntry::Kind;
    const auto seconds = sampleRate > 0.0 ? (double) e.when / sampleRate : 0.0;
    const auto whole = (int) seconds;
    const auto stamp = juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + "."
                         + juce::String ((int) ((seconds - whole) * 1000.0)).paddedLeft ('0', 3) + "  ";
    const auto who = e.track >= 0 ? trackLabel (e.track) + " " : juce::String();
    const auto length = [&] { return juce::String (sampleRate > 0.0 ? (double) e.a / sampleRate : 0.0, 3) + " s"; };

    switch (e.kind)
    {
        case K::songLoaded:         return stamp + "Song loaded; every track cleared";
        case K::armed:              return stamp + who + "waiting for the next bar to record";
        case K::recording:          return stamp + who + "recording";
        case K::closePending:       return stamp + who + "will finish recording at " + juce::String (e.b, 0) + (e.b == 1.0 ? " bar" : " bars");
        case K::loopClosed:         return stamp + who + "loop closed: " + length() + " (" + juce::String (e.a) + " samples)"
                                             + (e.b > 0.0 ? ", " + juce::String (e.b, 0) + (e.b == 1.0 ? " bar" : " bars") : juce::String());
        case K::tempoSet:           return stamp + "Tempo set by " + who.trim() + ": " + juce::String (e.b, 1) + " BPM, one bar = " + length();
        case K::playing:            return stamp + who + "playing";
        case K::stopped:            return stamp + who + "stopped";
        case K::stopPending:        return stamp + who + (e.a == 1 ? "fading out" : "will stop at the end of its loop");
        case K::overdub:            return stamp + who + "overdubbing";
        case K::overdubEnd:         return stamp + who + "overdub finished; playing";
        case K::overdubCancelled:   return stamp + who + "overdub taken back (the press turned into a hold or double-press)";
        case K::undo:               return stamp + who + (e.a == 1 ? "undo: the whole recording is hidden" : "undo: the last overdub is hidden");
        case K::redo:               return stamp + who + (e.a == 1 ? "redo: the recording is back" : "redo: the last overdub is back");
        case K::cleared:            return stamp + who + "cleared";
        case K::allStart:           return stamp + "All start";
        case K::allStop:            return stamp + "All stop";
        case K::bank:               return stamp + "Bank " + juce::String (e.a + 1);
        case K::recordingCancelled: return stamp + who + "recording cancelled";
        case K::recordingTooLong:   return stamp + who + "reached the longest loop this build allows and was closed";
        case K::unsupported:        return stamp + "That switch has an RC-600 function this build does not copy yet (number " + juce::String (e.a) + ")";
    }

    return stamp;
}

//==============================================================================
Looper::Looper()
{
    activeSong = shownSong = builtInSong (0);
    rebuildRouting();
}

void Looper::prepare (double sampleRate, double maxLoopSeconds)
{
    rate = sampleRate;
    capacity = (juce::int64) (sampleRate * maxLoopSeconds);

    ringSize = 1;

    while (ringSize < (juce::int64) (sampleRate * 3.5))
        ringSize <<= 1;

    ringMask = ringSize - 1;
    fadeSamples = juce::jmax (1, (int) (sampleRate * 0.005));
    crossfade   = juce::jmax (1, (int) (sampleRate * 0.008));
    startRamp   = juce::jmax (1, (int) (sampleRate * 0.002));
    stopRamp    = juce::jmax (1, (int) (sampleRate * 0.005));

    for (auto& tr : tracks)
    {
        for (auto& lane : tr.lanes)
        {
            if ((juce::int64) lane.base.size() != capacity)
            {
                lane.base.assign ((size_t) capacity, 0.0f);
                lane.dub.assign ((size_t) capacity, 0.0f);
            }

            lane.ring.assign ((size_t) ringSize, 0.0f);
            lane.pre.assign ((size_t) crossfade, 0.0f);
        }
    }

    now = 0;
    resetAll (false);

    // What is left in the buffers from before is never played, but an overdub must not start on top of it.
    for (auto& tr : tracks)
        tr.dubDirty = true;

    messageFifo.reset();
    publish();
}

void Looper::setSong (const SongSetup& s)
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    pendingSong = shownSong = s;
    songPending = true;
    anyPending.store (true);
}

void Looper::setOptions (const LooperOptions& o)
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    pendingOptions = shownOptions = o;
    optionsPending = true;
    anyPending.store (true);
}

SongSetup Looper::song() const
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    return shownSong;
}

LooperOptions Looper::options() const
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    return shownOptions;
}

void Looper::pedal (const PedalEvent& event)
{
    const juce::SpinLock::ScopedLockType sl (producerLock);
    const auto scope = messageFifo.write (1);

    if (scope.blockSize1 > 0)
        messages[(size_t) scope.startIndex1] = { Message::Type::pedal, event };
    else if (scope.blockSize2 > 0)
        messages[(size_t) scope.startIndex2] = { Message::Type::pedal, event };
}

void Looper::clearAll()
{
    const juce::SpinLock::ScopedLockType sl (producerLock);
    const auto scope = messageFifo.write (1);

    if (scope.blockSize1 > 0)
        messages[(size_t) scope.startIndex1] = { Message::Type::clearAll, {} };
    else if (scope.blockSize2 > 0)
        messages[(size_t) scope.startIndex2] = { Message::Type::clearAll, {} };
}

LooperSnapshot Looper::snapshot() const
{
    const juce::SpinLock::ScopedLockType sl (snapshotLock);
    return shared;
}

int Looper::takeLog (LoopLogEntry* entries, int maxEntries)
{
    const auto scope = logFifo.read (juce::jmin (maxEntries, logFifo.getNumReady()));
    int count = 0;

    for (int i = 0; i < scope.blockSize1; ++i)  entries[count++] = logEntries[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)  entries[count++] = logEntries[(size_t) (scope.startIndex2 + i)];

    return count;
}

void Looper::log (LoopLogEntry::Kind kind, int track, juce::int64 a, double b) noexcept
{
    const auto scope = logFifo.write (1);
    const auto index = scope.blockSize1 > 0 ? scope.startIndex1 : scope.blockSize2 > 0 ? scope.startIndex2 : -1;

    if (index >= 0)
        logEntries[(size_t) index] = { kind, track, now, a, b };
}

//==============================================================================
void Looper::rebuildRouting() noexcept
{
    static const int sourceBits[numLoopSources] = { rcMic1, rcInst1L, rcInst1R, rcInst2L, rcInst2R };

    for (int t = 0; t < numLoopTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        const auto& setup = activeSong.track[(size_t) t];

        // One lane for each input socket the track listens to, in the order vocal, acoustic,
        // stomp, bass, electric. Sources on the same socket (acoustic and stomp on the CQ) share
        // a lane, and anything past the last lane joins it.
        juce::uint32 laneInputs[maxLoopLanes] = {};
        bool laneIsVocal[maxLoopLanes] = {};
        int lanes = 0;

        for (int s = 0; s < numLoopSources; ++s)
        {
            const auto input = activeOptions.inputFor[(size_t) s];

            if ((setup.rcInputs & sourceBits[s]) == 0 || input < 0 || input >= 32)
                continue;

            const auto bit = 1u << input;
            int lane = -1;

            for (int l = 0; l < lanes; ++l)
                if ((laneInputs[l] & bit) != 0)
                    lane = l;

            if (lane < 0)
                lane = lanes < maxLoopLanes ? lanes++ : maxLoopLanes - 1;

            laneInputs[lane] |= bit;
            laneIsVocal[lane] = laneIsVocal[lane] || s == (int) LoopSource::vocal;
        }

        tr.numLanes = juce::jmax (1, lanes);

        for (int l = 0; l < maxLoopLanes; ++l)
        {
            auto& lane = tr.lanes[(size_t) l];
            lane.inputMask = l < tr.numLanes ? laneInputs[l] : 0u;

            switch (activeOptions.outputMode)
            {
                case 1:  lane.outputMask = laneIsVocal[l] ? 0b00001100u : 0b00110000u; break;
                case 2:  lane.outputMask = 1u << t; break;
                case 3:  lane.outputMask = tr.numLanes > 1 && l == 0 ? 0b00001100u : 0b00110000u; break;
                default: lane.outputMask = 0b00110000u; break;
            }
        }

        tr.level = (float) juce::jlimit (0, 200, setup.level) * 0.01f;
    }

    for (int b = 0; b < numLoopBanks; ++b)
        for (int sw = 0; sw < 9; ++sw)
            functions[b][sw] = decodeRcFunction (activeSong.bank[b][sw]);
}

void Looper::resetAll (bool logIt) noexcept
{
    for (auto& tr : tracks)
    {
        const auto dirty = tr.dubDirty || tr.hasDub;

        tr.state = TrackState::empty;
        tr.length = tr.phase = 0;
        tr.hasBase = tr.baseUndone = tr.hasDub = tr.dubUndone = false;
        tr.dubDirty = dirty;
        tr.gain = tr.gainTarget = 0.0f;
        tr.peak = 0.0f;
        tr.recActive = tr.dubActive = tr.tentative = tr.folding = tr.fadingOut = false;
        tr.recEndAt = tr.stopAt = -1;
    }

    for (auto& p : presses)
        p = {};

    running = false;
    origin = now;
    bank = 0;
    currentTrack = 0;
    assignCount = 0;
    bar = 0;

    if (activeOptions.fixedTempo && rate > 0.0 && activeSong.savedBpm > 0.0)
        bar = juce::jmax<juce::int64> (1, juce::roundToInt (rate * 60.0 * activeSong.beatsPerBar / activeSong.savedBpm));

    rebuildRouting();

    if (logIt)
        log (LoopLogEntry::Kind::songLoaded, -1);
}

void Looper::applyPending() noexcept
{
    if (! anyPending.load (std::memory_order_relaxed))
        return;

    const juce::SpinLock::ScopedTryLockType sl (configLock);

    if (! sl.isLocked())
        return;

    const bool newSong = songPending;

    if (optionsPending)
        activeOptions = pendingOptions;

    if (newSong)
        activeSong = pendingSong;

    songPending = optionsPending = false;
    anyPending.store (false);

    bool anything = false;

    for (const auto& tr : tracks)
        anything = anything || tr.hasBase || tr.state != TrackState::empty;

    // A change to how the tempo is found only makes sense on a clean slate.
    if (newSong || ! anything)
        resetAll (newSong);
    else
        rebuildRouting();
}

//==============================================================================
bool Looper::isActive (const Track& tr) const noexcept
{
    return tr.state != TrackState::empty && tr.state != TrackState::stopped;
}

juce::int64 Looper::positionOf (const Track& tr, juce::int64 time) const noexcept
{
    return tr.length > 0 ? wrap (time - origin - tr.phase, tr.length) : 0;
}

juce::int64 Looper::nextBar (juce::int64 atOrAfter) const noexcept
{
    if (bar <= 0)
        return atOrAfter;

    const auto into = wrap (atOrAfter - origin, bar);
    return into == 0 ? atOrAfter : atOrAfter + (bar - into);
}

void Looper::setBar (juce::int64 samples) noexcept
{
    bar = samples;
}

void Looper::updateGain (Track& tr) noexcept
{
    const bool sounding = (tr.state == TrackState::playing || tr.state == TrackState::overdubbing || tr.state == TrackState::stopping)
                            && tr.hasBase && ! tr.baseUndone && ! tr.fadingOut;
    tr.gainTarget = sounding ? 1.0f : 0.0f;
}

void Looper::updateTransport() noexcept
{
    bool active = false, anything = false;

    for (const auto& tr : tracks)
    {
        active = active || isActive (tr);
        anything = anything || tr.hasBase || tr.state != TrackState::empty;
    }

    running = active;

    // With every track cleared the next first loop sets a new tempo.
    if (! anything && ! activeOptions.fixedTempo)
        bar = 0;
}

//==============================================================================
void Looper::handle (const Message& m) noexcept
{
    if (m.type == Message::Type::clearAll)
    {
        clearEverything();
        return;
    }

    const auto sw = m.event.sw;

    if (sw < 0 || sw >= numPedalSwitches)
        return;

    switch (m.event.kind)
    {
        case PedalEvent::Kind::down:  handleDown (sw, m.event.isDouble); break;
        case PedalEvent::Kind::hold:  handleHold (sw); break;
        case PedalEvent::Kind::up:    handleUp (sw); break;
    }

    drainAssigns();
}

void Looper::handleDown (int sw, bool isDouble) noexcept
{
    auto& p = presses[(size_t) sw];
    const auto fn = sw < 9 ? functions[bank][sw] : SwitchFunction();
    const auto track = fn.track == -1 ? currentTrack : fn.track;
    const auto bankBefore = bank;

    p.fn = fn;
    p.track = track;
    p.down = true;
    p.stateBefore = track >= 0 ? tracks[(size_t) track].state : TrackState::empty;

    const bool useClick = isDouble && fn.click != LoopAction::none;
    auto action = useClick ? fn.click : fn.push;

    if (useClick && track >= 0)
    {
        // The first press of the pair already acted. Take back what it should not have done.
        auto& tr = tracks[(size_t) track];

        if (tr.tentative && tr.tentativeSwitch == sw)
        {
            cancelTentative (track, true);
        }
        else if (isRecordingState (tr.state) && tr.recStartedBySwitch == sw && now - tr.recPressedAt < (juce::int64) (rate * 0.6))
        {
            cancelRecording (track, true);
            action = LoopAction::none;
        }
    }

    if (action == LoopAction::unsupported)
        log (LoopLogEntry::Kind::unsupported, -1, fn.rcCode);

    perform (action, track, sw, fn.hold != LoopAction::none || fn.click != LoopAction::none);
    fireAssigns (LoopAssign::Source::pedal, bankBefore, sw);
}

void Looper::handleHold (int sw) noexcept
{
    const auto& p = presses[(size_t) sw];
    const auto track = p.track;

    if (p.fn.hold == LoopAction::none)
        return;

    if (track >= 0 && tracks[(size_t) track].tentative && tracks[(size_t) track].tentativeSwitch == sw)
        cancelTentative (track, false);

    // Held on the very press that started a first recording (or set one waiting for the bar):
    // on the RC-600 a hold undoes the last recording, so the take is thrown away.
    if (track >= 0 && (p.fn.hold == LoopAction::undoRedo || p.fn.hold == LoopAction::undoOrClear))
    {
        auto& tr = tracks[(size_t) track];

        if (p.stateBefore == TrackState::empty && tr.recStartedBySwitch == sw
              && (tr.state == TrackState::armed || tr.state == TrackState::recording))
        {
            cancelRecording (track, true);
            return;
        }
    }

    const auto before = p.stateBefore;
    const bool wasRunning = before == TrackState::playing || before == TrackState::overdubbing || before == TrackState::recording
                              || before == TrackState::closing || before == TrackState::stopping;

    switch (p.fn.hold)
    {
        case LoopAction::undoRedo:
            if (track >= 0 && wasRunning)
                undo (track);
            break;

        case LoopAction::undoOrClear:
            if (track >= 0 && before == TrackState::stopped)
                clearTrack (track, true);
            else if (track >= 0 && wasRunning)
                undo (track);
            break;

        case LoopAction::clear:
            if (track >= 0)
                clearTrack (track, true);
            break;

        case LoopAction::allClear:
            clearEverything();
            break;

        case LoopAction::none: case LoopAction::recPlay: case LoopAction::playStop: case LoopAction::stop:
        case LoopAction::allStartStop: case LoopAction::bankToggle: case LoopAction::unsupported:
            break;
    }
}

void Looper::handleUp (int sw) noexcept
{
    presses[(size_t) sw].down = false;

    for (auto& tr : tracks)
        if (tr.tentative && tr.tentativeSwitch == sw)
            tr.tentativeReleased = true;    // fireTimed() confirms it once the double-press window has passed
}

void Looper::perform (LoopAction action, int track, int sw, bool allowTentative) noexcept
{
    const bool hasTrack = track >= 0 && track < numLoopTracks;

    if (hasTrack && tracks[(size_t) track].tentative && action != LoopAction::none)
        confirmTentative (track);

    switch (action)
    {
        case LoopAction::recPlay:
            if (hasTrack)
            {
                currentTrack = track;
                recPlay (track, sw, allowTentative);
            }
            break;

        case LoopAction::playStop:
            if (hasTrack)
            {
                currentTrack = track;
                auto& tr = tracks[(size_t) track];

                if (isActive (tr))
                    stopTrack (track, false);
                else if (tr.hasBase && ! tr.baseUndone)
                    startPlaying (track);
            }
            break;

        case LoopAction::stop:
            if (hasTrack)
            {
                currentTrack = track;
                stopTrack (track, false);
            }
            break;

        case LoopAction::clear:
            if (hasTrack)
                clearTrack (track, true);
            break;

        case LoopAction::undoRedo:
        case LoopAction::undoOrClear:
            if (hasTrack)
                undo (track);
            break;

        case LoopAction::allStartStop:  allStartStop(); break;
        case LoopAction::allClear:      clearEverything(); break;

        case LoopAction::bankToggle:
            bank = (bank + 1) % numLoopBanks;
            log (LoopLogEntry::Kind::bank, -1, bank);
            break;

        case LoopAction::none:
        case LoopAction::unsupported:
            break;
    }
}

void Looper::fireAssigns (LoopAssign::Source source, int bankOrZero, int key) noexcept
{
    for (const auto& a : activeSong.assign)
    {
        if (a.source != source || a.action == LoopAction::none || a.action == LoopAction::unsupported)
            continue;

        const bool match = source == LoopAssign::Source::pedal ? a.sw == key && (a.bank < 0 || a.bank == bankOrZero)
                                                               : a.sourceTrack == key;

        if (match && assignCount < (int) assignQueue.size())
            assignQueue[(size_t) assignCount++] = { a.action, a.targetTrack };
    }
}

void Looper::drainAssigns() noexcept
{
    if (drainingAssigns)
        return;

    drainingAssigns = true;

    // Linked actions can set off more linked actions; the queue's size is the limit.
    for (int i = 0; i < assignCount; ++i)
    {
        const auto item = assignQueue[(size_t) i];
        perform (item.action, item.track == -1 ? currentTrack : item.track, -1, false);
    }

    assignCount = 0;
    drainingAssigns = false;
}

//==============================================================================
void Looper::recPlay (int track, int sw, bool allowTentative) noexcept
{
    auto& tr = tracks[(size_t) track];
    const auto next = activeSong.recThenPlay ? TrackState::playing : TrackState::overdubbing;

    switch (tr.state)
    {
        case TrackState::empty:
            startRecording (track, sw);
            break;

        case TrackState::armed:
            cancelRecording (track, true);
            break;

        case TrackState::recording:
        case TrackState::closing:
            pressToClose (track, next);
            break;

        case TrackState::playing:
            if (! tr.hasBase || tr.baseUndone)
            {
                startRecording (track, sw);
            }
            else if (allowTentative)
            {
                // Sound like an overdub and look like one, but write nothing until the foot
                // has shown it is not the start of a hold or a double-press.
                tr.tentative = true;
                tr.tentativeReleased = false;
                tr.tentativeSwitch = sw;
                tr.tentativeAt = now;
                tr.state = TrackState::overdubbing;
                log (LoopLogEntry::Kind::overdub, track);
            }
            else
            {
                beginOverdub (track, now);
                log (LoopLogEntry::Kind::overdub, track);
            }
            break;

        case TrackState::overdubbing:
            endOverdub (track);
            break;

        case TrackState::stopping:
            tr.stopAt = -1;
            tr.fadingOut = false;
            tr.state = TrackState::playing;
            tr.gainUp = 1.0f / (float) startRamp;
            updateGain (tr);
            log (LoopLogEntry::Kind::playing, track);
            break;

        case TrackState::stopped:
            if (! tr.hasBase || tr.baseUndone)
                startRecording (track, sw);
            else
                startPlaying (track);
            break;
    }
}

void Looper::startRecording (int track, int sw) noexcept
{
    auto& tr = tracks[(size_t) track];
    const auto& setup = activeSong.track[(size_t) track];

    tr.dubDirty = tr.dubDirty || tr.hasDub;
    tr.length = tr.phase = 0;
    tr.hasBase = tr.baseUndone = tr.hasDub = tr.dubUndone = false;
    tr.dubActive = tr.tentative = tr.folding = tr.fadingOut = false;
    tr.gain = tr.gainTarget = 0.0f;
    tr.stopAt = -1;

    if (! running)
    {
        origin = now;
        running = true;
        tr.recStart = now;
    }
    else
    {
        tr.recStart = bar > 0 && activeSong.quantise ? nextBar (now) : now;
    }

    tr.recActive = true;
    tr.preCaptured = false;
    tr.recLength = 0;
    tr.recWrittenTo = now;
    tr.recPressedAt = now;
    tr.recComp = juce::jmin (compensation.load (std::memory_order_relaxed), (int) (ringSize / 4));
    tr.recStartedBySwitch = sw;
    tr.afterClose = activeSong.recThenPlay ? TrackState::playing : TrackState::overdubbing;

    if (bar > 0 && setup.bars > 0 && setup.bars * bar <= capacity)
    {
        tr.recEndAt = tr.recStart + setup.bars * bar;
        tr.recForcedEnd = false;
    }
    else
    {
        tr.recEndAt = tr.recStart + capacity;
        tr.recForcedEnd = true;
    }

    if (tr.recStart > now)
    {
        tr.state = TrackState::armed;
        log (LoopLogEntry::Kind::armed, track);
    }
    else
    {
        tr.state = TrackState::recording;
        log (LoopLogEntry::Kind::recording, track);
        fireAssigns (LoopAssign::Source::trackRecDub, 0, track);
    }
}

void Looper::pressToClose (int track, TrackState next) noexcept
{
    auto& tr = tracks[(size_t) track];
    tr.afterClose = next;

    if (tr.state == TrackState::closing)
        return;

    if (tr.state == TrackState::armed)
    {
        cancelRecording (track, true);
        return;
    }

    const auto elapsed = now - tr.recStart;

    if (! tr.recForcedEnd)
    {
        // A preset number of bars: the track closes itself; the press only says what happens next.
        tr.state = TrackState::closing;
        log (LoopLogEntry::Kind::closePending, track, 0, (double) (tr.recEndAt - tr.recStart) / (double) bar);
        return;
    }

    if (bar <= 0)
    {
        // The first loop: its length, divided by its bars, is the length of a bar from now on.
        const auto& setup = activeSong.track[(size_t) track];
        juce::int64 bars = setup.bars;

        if (elapsed < (juce::int64) (rate * 0.25))
        {
            cancelRecording (track, true);
            return;
        }

        if (bars <= 0 && activeSong.rcMemory > 0 && activeSong.savedBpm > 0.0)
        {
            // The song's saved tempo is the guide: the loop is however many bars bring it closest.
            const auto barsAtSaved = (double) elapsed * activeSong.savedBpm / (activeSong.beatsPerBar * 60.0 * rate);
            bars = juce::jlimit<juce::int64> (1, 64, (juce::int64) std::llround (barsAtSaved));
        }
        else if (bars <= 0)
        {
            bars = 1;
            const auto tempoFor = [&] (juce::int64 n) { return (double) n * activeSong.beatsPerBar * 60.0 * rate / (double) elapsed; };

            while (bars < 64 && tempoFor (bars) < activeOptions.tempoLow)
                bars *= 2;
        }

        const auto oneBar = elapsed / bars;

        if (oneBar < 64)
        {
            cancelRecording (track, true);
            return;
        }

        setBar (oneBar);
        log (LoopLogEntry::Kind::tempoSet, track, oneBar, activeSong.beatsPerBar * 60.0 * rate / (double) oneBar);
        closeRecording (track, bars * oneBar);
        return;
    }

    auto bars = juce::jmax<juce::int64> (1, (elapsed + bar / 2) / bar);

    while (bars > 0 && bars * bar > capacity)
        --bars;

    if (bars <= 0)
    {
        cancelRecording (track, true);
        return;
    }

    const auto target = bars * bar;

    if (target <= elapsed)
    {
        closeRecording (track, target);     // pressed a little late: the loop ends on the bar just gone
    }
    else
    {
        tr.recEndAt = tr.recStart + target; // pressed a little early: keep recording to the bar
        tr.recForcedEnd = false;
        tr.state = TrackState::closing;
        log (LoopLogEntry::Kind::closePending, track, 0, (double) bars);
    }
}

void Looper::closeRecording (int track, juce::int64 length) noexcept
{
    auto& tr = tracks[(size_t) track];

    tr.length = tr.recLength = length;
    tr.hasBase = true;
    tr.baseUndone = tr.hasDub = tr.dubUndone = false;
    tr.phase = wrap (tr.recStart - origin, length);
    tr.recEndAt = -1;

    log (LoopLogEntry::Kind::loopClosed, track, length, bar > 0 ? (double) length / (double) bar : 0.0);

    const auto next = tr.afterClose;
    tr.state = next == TrackState::stopped ? TrackState::stopped : TrackState::playing;
    tr.gainUp = 1.0f / (float) startRamp;
    tr.gainDown = 1.0f / (float) stopRamp;
    updateGain (tr);

    if (next == TrackState::stopped)
        log (LoopLogEntry::Kind::stopped, track);
    else
        log (LoopLogEntry::Kind::playing, track);

    fireAssigns (LoopAssign::Source::trackPlayStop, 0, track);

    if (next == TrackState::overdubbing)
    {
        beginOverdub (track, now);
        log (LoopLogEntry::Kind::overdub, track);
    }

    updateTransport();
}

void Looper::cancelRecording (int track, bool logIt) noexcept
{
    auto& tr = tracks[(size_t) track];

    tr.recActive = false;
    tr.recEndAt = -1;
    tr.length = 0;
    tr.hasBase = false;
    tr.state = TrackState::empty;
    updateGain (tr);

    if (logIt)
        log (LoopLogEntry::Kind::recordingCancelled, track);

    updateTransport();
}

void Looper::startPlaying (int track) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (! running)
    {
        origin = now;       // everything stopped: start again from the top
        running = true;
    }

    tr.state = TrackState::playing;
    tr.stopAt = -1;
    tr.fadingOut = false;
    tr.gainUp = 1.0f / (float) startRamp;
    updateGain (tr);
    log (LoopLogEntry::Kind::playing, track);
}

void Looper::stopTrack (int track, bool immediately) noexcept
{
    auto& tr = tracks[(size_t) track];

    switch (tr.state)
    {
        case TrackState::empty:
        case TrackState::stopped:
            return;

        case TrackState::armed:
            cancelRecording (track, true);
            return;

        case TrackState::recording:
        case TrackState::closing:
            pressToClose (track, TrackState::stopped);
            return;

        case TrackState::stopping:
            finishStop (track);     // a second stop press does not wait
            return;

        case TrackState::playing:
        case TrackState::overdubbing:
            break;
    }

    if (tr.tentative)
        cancelTentative (track, false);

    if (tr.state == TrackState::overdubbing)
        endOverdub (track);

    const auto mode = immediately ? StopMode::immediate : activeSong.track[(size_t) track].stopMode;

    if (mode == StopMode::loopEnd && tr.length > 0)
    {
        const auto remaining = tr.length - positionOf (tr, now);
        tr.stopAt = now + juce::jmax<juce::int64> (0, remaining - stopRamp);
        tr.state = TrackState::stopping;
        log (LoopLogEntry::Kind::stopPending, track, 0);
    }
    else if (mode == StopMode::fade)
    {
        const auto fade = bar > 0 ? bar : (juce::int64) rate;
        tr.gainDown = 1.0f / (float) fade;
        tr.fadingOut = true;
        tr.stopAt = now + fade;
        tr.state = TrackState::stopping;
        updateGain (tr);
        log (LoopLogEntry::Kind::stopPending, track, 1);
    }
    else
    {
        tr.gainDown = 1.0f / (float) stopRamp;
        finishStop (track);
    }
}

void Looper::finishStop (int track) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (! tr.fadingOut)
        tr.gainDown = 1.0f / (float) stopRamp;

    tr.state = TrackState::stopped;
    tr.stopAt = -1;
    tr.fadingOut = false;
    updateGain (tr);
    log (LoopLogEntry::Kind::stopped, track);
    updateTransport();
}

//==============================================================================
void Looper::beginOverdub (int track, juce::int64 pressedAt) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (tr.length <= 0)
        return;

    finishFold (tr);

    tr.dubComp = juce::jmin (compensation.load (std::memory_order_relaxed), (int) (ringSize / 4));
    tr.gateOn = juce::jmax (pressedAt + tr.dubComp / 2, now - ringSize / 2);

    if (tr.hasDub || tr.dubDirty)
    {
        // The last overdub becomes part of the loop for good (or is dropped, if it was undone),
        // which empties the overdub layer for this one. Enough is done now to stay ahead of
        // both the playback and the writing; the rest follows over the next few blocks.
        tr.foldDiscard = ! tr.hasDub || tr.dubUndone;
        tr.folding = true;
        tr.foldNext = wrap (tr.gateOn - tr.dubComp - origin - tr.phase, tr.length);
        tr.foldRemaining = tr.length;
        fold (tr, juce::jmax<juce::int64> (32768, (now - tr.gateOn) + tr.dubComp + currentBlock + 8192));
    }

    tr.hasDub = true;
    tr.dubUndone = false;
    tr.dubDirty = true;
    tr.dubActive = true;
    tr.gateOff = never;
    tr.dubWrittenTo = tr.gateOn;
    tr.state = TrackState::overdubbing;
    updateGain (tr);
    fireAssigns (LoopAssign::Source::trackRecDub, 0, track);
}

void Looper::endOverdub (int track) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (tr.tentative)
    {
        confirmTentative (track);
    }

    tr.gateOff = now + tr.dubComp / 2;
    tr.state = TrackState::playing;
    log (LoopLogEntry::Kind::overdubEnd, track);
    fireAssigns (LoopAssign::Source::trackPlayStop, 0, track);
}

void Looper::confirmTentative (int track) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (! tr.tentative)
        return;

    tr.tentative = false;
    beginOverdub (track, tr.tentativeAt);
}

void Looper::cancelTentative (int track, bool logIt) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (! tr.tentative)
        return;

    tr.tentative = false;
    tr.state = TrackState::playing;

    if (logIt)
        log (LoopLogEntry::Kind::overdubCancelled, track);
}

void Looper::undo (int track) noexcept
{
    auto& tr = tracks[(size_t) track];

    if (tr.tentative)
        cancelTentative (track, false);

    if (isRecordingState (tr.state))
    {
        cancelRecording (track, true);
        return;
    }

    if (tr.state == TrackState::overdubbing)
        endOverdub (track);

    if (tr.hasDub)
    {
        tr.dubUndone = ! tr.dubUndone;
        log (tr.dubUndone ? LoopLogEntry::Kind::undo : LoopLogEntry::Kind::redo, track, 0);
    }
    else if (tr.hasBase)
    {
        tr.baseUndone = ! tr.baseUndone;
        tr.gainUp = 1.0f / (float) startRamp;
        tr.gainDown = 1.0f / (float) stopRamp;
        updateGain (tr);
        log (tr.baseUndone ? LoopLogEntry::Kind::undo : LoopLogEntry::Kind::redo, track, 1);
    }
}

void Looper::clearTrack (int track, bool logIt) noexcept
{
    auto& tr = tracks[(size_t) track];
    const bool hadSomething = tr.hasBase || tr.state != TrackState::empty;

    tr.dubDirty = tr.dubDirty || tr.hasDub;
    tr.state = TrackState::empty;
    tr.length = tr.phase = 0;
    tr.hasBase = tr.baseUndone = tr.hasDub = tr.dubUndone = false;
    tr.recActive = tr.dubActive = tr.tentative = tr.folding = tr.fadingOut = false;
    tr.recEndAt = tr.stopAt = -1;
    tr.gain = tr.gainTarget = 0.0f;

    if (logIt && hadSomething)
        log (LoopLogEntry::Kind::cleared, track);

    updateTransport();
}

void Looper::clearEverything() noexcept
{
    for (int t = 0; t < numLoopTracks; ++t)
        clearTrack (t, true);
}

void Looper::allStartStop() noexcept
{
    if (running)
    {
        log (LoopLogEntry::Kind::allStop, -1);

        for (int t = 0; t < numLoopTracks; ++t)
            if (isActive (tracks[(size_t) t]))
                stopTrack (t, true);

        return;
    }

    bool any = false;

    for (const auto& tr : tracks)
        any = any || (tr.hasBase && ! tr.baseUndone);

    if (! any)
        return;

    log (LoopLogEntry::Kind::allStart, -1);

    for (int t = 0; t < numLoopTracks; ++t)
        if (tracks[(size_t) t].hasBase && ! tracks[(size_t) t].baseUndone)
            startPlaying (t);
}

//==============================================================================
void Looper::fireTimed() noexcept
{
    const auto window = (juce::int64) (rate * activeOptions.doubleWindowMs * 0.001);

    for (int t = 0; t < numLoopTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];

        if (tr.state == TrackState::armed && tr.recStart <= now)
        {
            tr.state = TrackState::recording;
            log (LoopLogEntry::Kind::recording, t);
            fireAssigns (LoopAssign::Source::trackRecDub, 0, t);
        }

        if ((tr.state == TrackState::recording || tr.state == TrackState::closing) && tr.recEndAt >= 0 && tr.recEndAt <= now)
        {
            if (tr.recForcedEnd)
            {
                log (LoopLogEntry::Kind::recordingTooLong, t);
                pressToClose (t, tr.afterClose);
            }
            else
            {
                closeRecording (t, tr.recEndAt - tr.recStart);
            }
        }

        if (tr.state == TrackState::stopping && tr.stopAt >= 0 && tr.stopAt <= now)
            finishStop (t);

        if (tr.tentative && tr.tentativeReleased && now >= tr.tentativeAt + window)
            confirmTentative (t);
    }

    drainAssigns();
}

juce::int64 Looper::nextTimed (juce::int64 after, juce::int64 limit) const noexcept
{
    const auto window = (juce::int64) (rate * activeOptions.doubleWindowMs * 0.001);
    auto next = limit;

    const auto consider = [&] (juce::int64 time)
    {
        if (time > after && time < next)
            next = time;
    };

    for (const auto& tr : tracks)
    {
        if (tr.state == TrackState::armed)
            consider (tr.recStart);

        if ((tr.state == TrackState::recording || tr.state == TrackState::closing) && tr.recEndAt >= 0)
            consider (tr.recEndAt);

        if (tr.state == TrackState::stopping && tr.stopAt >= 0)
            consider (tr.stopAt);

        if (tr.tentative && tr.tentativeReleased)
            consider (tr.tentativeAt + window);
    }

    return next;
}

void Looper::render (juce::int64 from, juce::int64 to, float* const* outputs, int offset) noexcept
{
    const auto count = (int) (to - from);

    for (auto& tr : tracks)
    {
        if (tr.length <= 0 || ! tr.hasBase || (tr.gain <= 0.0f && tr.gainTarget <= 0.0f))
            continue;

        const bool withDub = tr.hasDub && ! tr.dubUndone;
        const auto startIndex = positionOf (tr, from);
        const auto startGain = tr.gain;
        auto endGain = startGain;
        auto peak = tr.peak;

        // Every lane is read from the same place with the same fade, so the lanes of a track stay locked together.
        for (int l = 0; l < tr.numLanes; ++l)
        {
            const auto& lane = tr.lanes[(size_t) l];
            const auto* base = lane.base.data();
            const auto* dub = lane.dub.data();
            auto index = startIndex;
            auto gain = startGain;

            float* outs[numLoopOutputs];
            int numOuts = 0;

            for (int r = 0; r < numLoopOutputs; ++r)
                if ((lane.outputMask & (1u << r)) != 0 && outputs[r] != nullptr)
                    outs[numOuts++] = outputs[r] + offset;

            for (int i = 0; i < count; ++i)
            {
                if (gain < tr.gainTarget)       gain = juce::jmin (tr.gainTarget, gain + tr.gainUp);
                else if (gain > tr.gainTarget)  gain = juce::jmax (tr.gainTarget, gain - tr.gainDown);

                const auto sample = (withDub ? base[index] + dub[index] : base[index]) * gain * tr.level;

                for (int o = 0; o < numOuts; ++o)
                    outs[o][i] += sample;

                peak = juce::jmax (peak, std::abs (sample));

                if (++index >= tr.length)
                    index = 0;
            }

            endGain = gain;
        }

        tr.gain = endGain;
        tr.peak = peak;
    }
}

void Looper::fold (Track& tr, juce::int64 count) noexcept
{
    if (! tr.folding || tr.length <= 0)
    {
        tr.folding = false;
        return;
    }

    count = juce::jmin (count, tr.foldRemaining);
    tr.foldRemaining -= count;

    while (count > 0)
    {
        if (tr.foldNext >= tr.length)
            tr.foldNext = 0;

        const auto chunk = (int) juce::jmin (count, tr.length - tr.foldNext);

        for (auto& lane : tr.lanes)
        {
            auto* dub = lane.dub.data() + tr.foldNext;

            if (! tr.foldDiscard)
                juce::FloatVectorOperations::add (lane.base.data() + tr.foldNext, dub, chunk);

            juce::FloatVectorOperations::clear (dub, chunk);
        }

        tr.foldNext += chunk;
        count -= chunk;
    }

    if (tr.foldRemaining <= 0)
        tr.folding = false;
}

void Looper::finishFold (Track& tr) noexcept
{
    if (tr.folding)
        fold (tr, tr.foldRemaining);
}

void Looper::runWriters (juce::int64 blockEnd) noexcept
{
    for (auto& tr : tracks)
    {
        // ---- a recording: linear, shifted earlier by the round trip ----
        if (tr.recActive)
        {
            const auto zero = tr.recStart + tr.recComp;     // when the sound for the loop's first sample arrives

            if (! tr.preCaptured && blockEnd >= zero)
            {
                // The moment just before the loop starts, kept for the crossfade at the loop point.
                for (auto& lane : tr.lanes)
                    for (int i = 0; i < crossfade; ++i)
                        lane.pre[(size_t) i] = lane.ring[(size_t) ((zero - crossfade + i) & ringMask)];

                tr.preCaptured = true;
            }

            const auto limit = tr.recLength > 0 ? tr.recLength : capacity;
            const auto from = juce::jmax (tr.recWrittenTo, zero);
            const auto to = juce::jmin (blockEnd, zero + limit);

            for (auto& lane : tr.lanes)
            {
                auto* base = lane.base.data();
                const auto* ring = lane.ring.data();

                for (auto time = from; time < to; ++time)
                    base[time - zero] = ring[time & ringMask];
            }

            tr.recWrittenTo = juce::jmax (tr.recWrittenTo, to);

            if (tr.recLength > 0 && blockEnd >= zero + tr.recLength)
            {
                // The loop is complete. Blend its last few milliseconds into what was heard just
                // before it began, so the jump back to the start is seamless.
                if (tr.preCaptured && tr.recLength > crossfade * 4)
                {
                    for (auto& lane : tr.lanes)
                    {
                        for (int i = 0; i < crossfade; ++i)
                        {
                            const auto a = ((float) i + 0.5f) / (float) crossfade;
                            auto& sample = lane.base[(size_t) (tr.recLength - crossfade + i)];
                            sample = sample * (1.0f - a) + lane.pre[(size_t) i] * a;
                        }
                    }
                }

                tr.recActive = false;
            }
        }

        // ---- an overdub: added round the loop, with a short fade at each end ----
        if (tr.dubActive && tr.length > 0)
        {
            const auto closed = tr.gateOff >= never ? never : tr.gateOff + fadeSamples;
            const auto to = juce::jmin (blockEnd, closed);
            const auto startIndex = wrap (tr.dubWrittenTo - tr.dubComp - origin - tr.phase, tr.length);
            const auto fadeStep = 1.0f / (float) fadeSamples;

            for (auto& lane : tr.lanes)
            {
                auto* dub = lane.dub.data();
                const auto* ring = lane.ring.data();
                auto index = startIndex;

                for (auto time = tr.dubWrittenTo; time < to; ++time)
                {
                    auto gate = juce::jmin (1.0f, (float) (time - tr.gateOn) * fadeStep);

                    if (time >= tr.gateOff)
                        gate *= juce::jmax (0.0f, 1.0f - (float) (time - tr.gateOff) * fadeStep);

                    dub[index] += ring[time & ringMask] * gate;

                    if (++index >= tr.length)
                        index = 0;
                }
            }

            tr.dubWrittenTo = juce::jmax (tr.dubWrittenTo, to);

            if (blockEnd >= closed)
                tr.dubActive = false;
        }

        if (tr.folding)
            fold (tr, foldPerBlock);
    }
}

void Looper::publish() noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (snapshotLock);

    if (! sl.isLocked())
        return;

    auto& s = shared;
    s.running = running;
    s.tempoKnown = bar > 0;
    s.sampleRate = rate;
    s.now = now;
    s.bank = bank;
    s.currentTrack = currentTrack;
    s.compensationSamples = compensation.load (std::memory_order_relaxed);
    s.barSeconds = bar > 0 && rate > 0.0 ? (double) bar / rate : 0.0;
    s.bpm = bar > 0 ? activeSong.beatsPerBar * 60.0 * rate / (double) bar : 0.0;

    if (bar > 0)
    {
        const auto since = now - origin;
        const auto inBar = wrap (since, bar);
        const auto beat = (double) bar / activeSong.beatsPerBar;
        s.beatInBar = juce::jlimit (0, activeSong.beatsPerBar - 1, (int) ((double) inBar / beat));
        s.msSinceBeat = ((double) inBar - s.beatInBar * beat) * 1000.0 / rate;
        s.bar = since >= 0 ? since / bar : 0;
    }
    else
    {
        s.beatInBar = 0;
        s.bar = 0;
        s.msSinceBeat = 1.0e9;
    }

    for (int t = 0; t < numLoopTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        auto& v = s.track[(size_t) t];

        v.state = tr.state;
        v.hasAudio = tr.hasBase;
        v.hasOverdub = tr.hasDub;
        v.undone = tr.baseUndone;
        v.overdubUndone = tr.hasDub && tr.dubUndone;
        v.tentative = tr.tentative;
        v.lanes = tr.numLanes;
        v.peak = juce::jmax (tr.peak, v.peak * 0.85f);
        tr.peak = 0.0f;

        if (tr.state == TrackState::recording || tr.state == TrackState::closing)
        {
            v.seconds = (double) (now - tr.recStart) / rate;
            v.bars = bar > 0 ? (double) (now - tr.recStart) / (double) bar : 0.0;
            v.position = 0.0f;
        }
        else
        {
            v.seconds = (double) tr.length / rate;
            v.bars = bar > 0 ? (double) tr.length / (double) bar : 0.0;
            v.position = tr.length > 0 ? (float) ((double) positionOf (tr, now) / (double) tr.length) : 0.0f;
        }
    }
}

//==============================================================================
void Looper::process (const float* const* inputs, int numInputs, float* const* outputs, int numSamples) noexcept
{
    if (rate <= 0.0 || capacity <= 0 || numSamples <= 0)
        return;

    currentBlock = numSamples;
    applyPending();

    // Every track keeps the last few seconds of its own inputs, mixed. Recordings and overdubs
    // are copied out of this, which is what lets them reach back in time.
    for (auto& tr : tracks)
    {
        for (auto& lane : tr.lanes)
        {
            auto* ring = lane.ring.data();
            auto position = now & ringMask;

            for (int i = 0; i < numSamples; ++i)
            {
                ring[position] = 0.0f;
                position = (position + 1) & ringMask;
            }

            for (int ch = 0; ch < juce::jmin (numInputs, 32); ++ch)
            {
                if ((lane.inputMask & (1u << ch)) == 0 || inputs[ch] == nullptr)
                    continue;

                position = now & ringMask;

                for (int i = 0; i < numSamples; ++i)
                {
                    ring[position] += inputs[ch][i];
                    position = (position + 1) & ringMask;
                }
            }
        }
    }

    fireTimed();

    {
        const auto scope = messageFifo.read (messageFifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i)  handle (messages[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)  handle (messages[(size_t) (scope.startIndex2 + i)]);
    }

    const auto end = now + numSamples;
    int offset = 0;

    while (now < end)
    {
        fireTimed();
        const auto next = nextTimed (now, end);
        render (now, next, outputs, offset);
        offset += (int) (next - now);
        now = next;
    }

    runWriters (end);
    publish();
}

} // namespace gigrig
