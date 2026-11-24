#pragma once

#include <juce_core/juce_core.h>

#include "Pedal.h"

#include <array>
#include <atomic>
#include <vector>

namespace gigrig
{

constexpr int numLoopTracks = 6;
constexpr int numLoopOutputs = 8;       // the rig's returns
constexpr int numLoopSources = 5;
constexpr int maxLoopLanes = 2;         // separate recordings inside one track, one per input socket it listens to
constexpr int maxLoopAssigns = 16;
constexpr int numLoopBanks = 2;

/** What Tom plugs in. The RC-600's input jacks map onto these one for one. */
enum class LoopSource { vocal, acoustic, stomp, bass, electric };

juce::String loopSourceName (int source);

/** RC-600 input bits, as stored in its memory files. */
enum RcInputBits { rcMic1 = 1, rcMic2 = 2, rcInst1L = 4, rcInst1R = 8, rcInst2L = 16, rcInst2R = 32, rcRhythm = 64 };

/** "acoustic + stomp" for an RC-600 input mask. */
juce::String describeRcInputs (int rcMask);

//==============================================================================
/** One thing a switch (or an RC-600 "assign") can do to the looper. */
enum class LoopAction : juce::uint8
{
    none,
    recPlay,        // record, then play, then overdub (or record, overdub, play)
    playStop,
    stop,
    clear,
    undoRedo,       // undo or redo, while the track is running
    undoOrClear,    // undo or redo while running; clear when the track was stopped
    allStartStop,
    allClear,
    bankToggle,
    unsupported     // an RC-600 function this stage does not copy yet
};

/** A switch's three actions, as the RC-600 defines them: press, 2-second hold, double-press. */
struct SwitchFunction
{
    LoopAction push = LoopAction::none, hold = LoopAction::none, click = LoopAction::none;
    int track = -2;     // 0 to 5, -1 = the current track, -2 = no track
    int rcCode = 0;     // the number in the RC-600 memory file
};

/** Decodes a pedal function number from an RC-600 memory file. */
SwitchFunction decodeRcFunction (int rcCode) noexcept;

/** "T1 rec/play", "Undo/redo", "Bank": short enough for a switch label. */
juce::String describeFunction (const SwitchFunction&);

/** The longer version with hold and double-press: "T1 rec/play. Hold: undo, or clear when stopped. Double-press: stop". */
juce::String describeFunctionFully (const SwitchFunction&);

/** An RC-600 assign: when the source happens, do the action to the target track. */
struct LoopAssign
{
    enum class Source : juce::uint8 { off, pedal, trackRecDub, trackPlayStop };

    Source source = Source::off;
    int bank = 0;           // pedal: which bank the switch must be in
    int sw = 0;             // pedal: 0 to 8 = switches 1 to 9, 9 = CTL3, 10 = CTL4 (any bank)
    int sourceTrack = 0;    // trackRecDub / trackPlayStop
    LoopAction action = LoopAction::none;
    int targetTrack = 0;    // 0 to 5, -1 = the current track
    int rcSource = 0, rcTarget = 0;
};

/** Decodes an assign's source and target numbers from an RC-600 memory file. */
LoopAssign decodeRcAssign (int rcSource, int rcTarget) noexcept;
juce::String describeAssign (const LoopAssign&);

enum class StopMode { immediate, fade, loopEnd };

/** How a stopped track comes back in while other loops are playing (the RC-600's LOOP SYNC MODE):
    inPlace     = wherever it would be by now in the song's count (LOOP LENGTH),
    fromBarLine = from its own first bar, lined up on the nearest bar line (MEASURE),
    atOnce      = from its own first note, the instant of the press (IMMEDIATE).
    With nothing else playing every track starts from its first note at once. */
enum class ReturnMode { inPlace, fromBarLine, atOnce };

struct TrackSetup
{
    int rcInputs = 0;       // RcInputBits
    int bars = 0;           // 0 = free (the length you play), otherwise a preset number of bars
    StopMode stopMode = StopMode::immediate;
    int level = 100;        // 0 to 200, as on the RC-600
    ReturnMode returnMode = ReturnMode::inPlace;
};

/** One song's looper set-up: what an RC-600 memory holds, cut down to what this stage copies. */
struct SongSetup
{
    char name[24] = {};
    int rcMemory = 0;                       // which RC-600 memory it came from, 0 = none
    double savedBpm = 120.0;
    int beatsPerBar = 4;
    bool recThenPlay = true;                // REC ACTION: record, play, overdub (false = record, overdub, play)
    bool quantise = true;                   // QUANTIZE = MEASURE: a new track waits for the next bar
    std::array<TrackSetup, numLoopTracks> track {};
    int bank[numLoopBanks][9] = {};         // RC-600 function numbers
    std::array<LoopAssign, maxLoopAssigns> assign {};

    juce::String title() const              { return juce::String (name).trim(); }
};

/** The songs built in for the stage 3 side-by-side test, decoded from Tom's RC-600 backup. */
int numBuiltInSongs() noexcept;
const SongSetup& builtInSong (int index) noexcept;

/** For the self-test: a Song box song by name, or one of the two self-test-only songs. */
SongSetup selfTestSong (const char* name);

/** Everything that tells you how a song is set up, in words, for the screen and the report. */
juce::String describeSong (const SongSetup&);

/** Settings that belong to the rig, not the song. */
struct LooperOptions
{
    /** false: the first loop sets the tempo (its length divided by its bars).
        true: the song's saved tempo is used, and a track with preset bars closes itself. */
    bool fixedTempo = false;

    /** With no preset bar count, the first loop is taken as 1, 2, 4, 8... bars: whichever puts the tempo in this range. */
    double tempoLow = 80.0, tempoHigh = 160.0;

    /** Which device input each source arrives on (0-based), or -1. Tom's CQ: vocal 6, acoustic and stomp 7, bass 9, electric 8. */
    std::array<int, numLoopSources> inputFor { 5, 6, 6, 8, 7 };

    /** 0: every track to returns 5+6 (the CQ's USB stereo channel).
        1: tracks that hold the vocal to returns 3+4 (ST1), the rest to 5+6.
        2: track N to return N.
        3: a track that listens to two sockets plays its first lane on 3+4 and its second on 5+6; other tracks on 5+6. */
    int outputMode = 0;

    int doubleWindowMs = 400;

    /** A press for a new track that lands up to this many beats after a bar line counts as being
        on that bar line. The looper is always listening, so what was played since the bar line is
        in the loop too. Half a beat: nobody arms the next bar that early, so a press there can only
        have been meant for the bar just begun. 0 = off. */
    double lateStartBeats = 0.5;

    /** The first loop (and any recording started with everything stopped) begins this long before
        the press, out of the same always-listening memory, so a note or stomp struck together with
        the switch keeps its attack. The loop's length is still press to press. 0 = off. */
    int firstLoopLeadMs = 30;
};

//==============================================================================
enum class TrackState : juce::uint8 { empty, armed, recording, closing, playing, overdubbing, stopping, stopped };

juce::String trackStateName (TrackState);

struct TrackView
{
    TrackState state = TrackState::empty;
    bool hasAudio = false, hasOverdub = false, undone = false, overdubUndone = false, tentative = false;
    int lanes = 1;              // 2 when the track keeps its two sources apart
    double seconds = 0.0;       // loop length
    double bars = 0.0;
    float position = 0.0f;      // 0..1 through the loop (or through the recording so far, in seconds)
    float peak = 0.0f;
};

struct LooperSnapshot
{
    std::array<TrackView, numLoopTracks> track {};
    bool running = false, tempoKnown = false;
    double bpm = 0.0, barSeconds = 0.0;
    int bank = 0, currentTrack = 0;
    int beatInBar = 0;          // 0-based
    juce::int64 bar = 0;
    double msSinceBeat = 1.0e9;
    int compensationSamples = 0;
    double sampleRate = 0.0;
    juce::int64 now = 0;
};

/** Something that happened, for the on-screen log and the automatic tests. */
struct LoopLogEntry
{
    enum class Kind : juce::uint8
    {
        songLoaded, armed, recording, loopClosed, tempoSet, playing, stopped, stopPending, overdub, overdubEnd,
        overdubCancelled, undo, redo, cleared, allStart, allStop, bank, recordingCancelled, recordingTooLong,
        unsupported, closePending, recordingLate
    };

    Kind kind = Kind::songLoaded;
    int track = -1;
    juce::int64 when = 0;       // sample time
    juce::int64 a = 0;          // length in samples, bank, RC code...
    double b = 0.0;             // bars, bpm...
};

juce::String describeLogEntry (const LoopLogEntry&, double sampleRate);

//==============================================================================
/** The looper core. It copies how Tom's RC-600 behaves with LOOP SYNC on:

    - the first loop sets the length of a bar (and so the tempo);
    - every later track is a whole number of bars and stays lined up with the bar;
    - a new track waits for the next bar to start, and its end snaps to the nearest bar;
    - each recording is shifted earlier by the round-trip delay, so it plays back
      exactly where it was played.

    process() runs on the audio thread and never waits, allocates or locks. */
class Looper
{
public:
    Looper();

    /** Call with audio stopped. Allocates the loop memory (two buffers for each lane of each track). */
    void prepare (double sampleRate, double maxLoopSeconds);

    /** Any thread but the audio thread. A new song clears every loop. */
    void setSong (const SongSetup&);
    void setOptions (const LooperOptions&);
    SongSetup song() const;
    LooperOptions options() const;

    /** How many samples later a sound comes back out than it went in (the round trip). */
    void setCompensation (int samples) noexcept     { compensation.store (juce::jmax (0, samples)); }
    int getCompensation() const noexcept            { return compensation.load(); }

    /** A foot on a switch. Any thread but the audio thread. */
    void pedal (const PedalEvent&);

    /** Clear every track, from the screen. */
    void clearAll();

    /** The audio thread. `outputs` holds numLoopOutputs pointers (null where a return does not exist); loops are added to them. */
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numSamples) noexcept;

    LooperSnapshot snapshot() const;
    /** The tempo now, for the effects (0 = no loop has set it yet). Audio thread only. */
    double tempoNow() const noexcept    { return bar > 0 && rate > 0.0 ? activeSong.beatsPerBar * 60.0 * rate / (double) bar : 0.0; }

    /** Log entries since the last call. */
    int takeLog (LoopLogEntry* entries, int maxEntries);

    double maxLoopSeconds() const noexcept          { return rate > 0.0 ? (double) capacity / rate : 0.0; }

private:
    /** One source's recording inside a track. A track that listens to two input sockets keeps
        a lane for each, so each can come back out on its own return and be mixed on its own.
        The lanes share everything else (length, state, switch presses), so they cannot drift apart. */
    struct Lane
    {
        std::vector<float> base, dub, ring, pre;
        juce::uint32 inputMask = 0, outputMask = 0;
    };

    struct Track
    {
        std::array<Lane, maxLoopLanes> lanes;
        int numLanes = 1;

        TrackState state = TrackState::empty;
        juce::int64 length = 0, phase = 0;
        bool hasBase = false, baseUndone = false, hasDub = false, dubUndone = false, dubDirty = false;

        float gain = 0.0f, gainTarget = 0.0f, gainUp = 1.0f, gainDown = 1.0f;
        float peak = 0.0f;

        // recording
        bool recActive = false, preCaptured = false, recForcedEnd = false;
        juce::int64 recStart = 0, recLength = 0, recWrittenTo = 0, recEndAt = -1, recPressedAt = 0, recLead = 0;
        int recComp = 0;
        TrackState afterClose = TrackState::playing;
        int recStartedBySwitch = -1;

        // overdubbing
        bool dubActive = false;
        juce::int64 gateOn = 0, gateOff = 0, dubWrittenTo = 0;
        int dubComp = 0;

        // an overdub that a press has started but a hold or double-press may still take back
        bool tentative = false, tentativeReleased = false;
        int tentativeSwitch = -1;
        juce::int64 tentativeAt = 0;

        // folding an old overdub into the base a piece at a time
        bool folding = false, foldDiscard = false;
        juce::int64 foldNext = 0, foldRemaining = 0;

        juce::int64 stopAt = -1, playStartedAt = -1;
        bool fadingOut = false;

        float level = 1.0f;
    };

    struct Press
    {
        SwitchFunction fn;
        int track = -2;
        TrackState stateBefore = TrackState::empty;
        bool down = false;
    };

    struct Message
    {
        enum class Type { pedal, clearAll } type = Type::pedal;
        PedalEvent event;
    };

    // ---- audio thread ----
    void applyPending() noexcept;
    void resetAll (bool logIt) noexcept;
    void rebuildRouting() noexcept;
    void handle (const Message&) noexcept;
    void handleDown (int sw, bool isDouble) noexcept;
    void handleHold (int sw) noexcept;
    void handleUp (int sw) noexcept;
    void perform (LoopAction, int track, int sw, bool allowTentative) noexcept;
    void fireAssigns (LoopAssign::Source, int bankOrZero, int swOrTrack, int edge = 0) noexcept;
    void finishOtherRecordings (int track) noexcept;
    void drainAssigns() noexcept;

    void recPlay (int track, int sw, bool allowTentative) noexcept;
    void startRecording (int track, int sw) noexcept;
    void pressToClose (int track, TrackState next) noexcept;
    void closeRecording (int track, juce::int64 length) noexcept;
    void cancelRecording (int track, bool logIt) noexcept;
    void startPlaying (int track) noexcept;
    void placeReturningTracks() noexcept;
    void stopTrack (int track, bool immediately) noexcept;
    void finishStop (int track) noexcept;
    void beginOverdub (int track, juce::int64 pressedAt) noexcept;
    void endOverdub (int track) noexcept;
    void confirmTentative (int track) noexcept;
    void cancelTentative (int track, bool logIt) noexcept;
    void undo (int track) noexcept;
    void clearTrack (int track, bool logIt) noexcept;
    void clearEverything() noexcept;
    void allStartStop() noexcept;

    void fireTimed() noexcept;
    juce::int64 nextTimed (juce::int64 after, juce::int64 limit) const noexcept;
    void render (juce::int64 from, juce::int64 to, float* const* outputs, int offset) noexcept;
    void runWriters (juce::int64 blockEnd) noexcept;
    void fold (Track&, juce::int64 count) noexcept;
    void finishFold (Track&) noexcept;
    void updateGain (Track&) noexcept;
    void updateTransport() noexcept;
    void setBar (juce::int64 samples) noexcept;
    juce::int64 nextBar (juce::int64 atOrAfter) const noexcept;
    juce::int64 positionOf (const Track&, juce::int64 time) const noexcept;
    bool isActive (const Track&) const noexcept;
    void log (LoopLogEntry::Kind, int track, juce::int64 a = 0, double b = 0.0) noexcept;
    void publish() noexcept;

    double rate = 0.0;
    juce::int64 capacity = 0, ringSize = 0, ringMask = 0;
    int fadeSamples = 0, crossfade = 0, startRamp = 0, stopRamp = 0;

    std::array<Track, numLoopTracks> tracks;
    std::array<Press, numPedalSwitches> presses {};
    SwitchFunction functions[numLoopBanks][9];

    SongSetup activeSong;
    LooperOptions activeOptions;

    juce::int64 now = 0, origin = 0, bar = 0;   // bar = samples in one bar, 0 = not known yet
    bool running = false;
    int bank = 0, currentTrack = 0, currentBlock = 0;
    int lastRecTrack = -1, recTrackBefore = -1;     // the track of the last recording or overdub: what Undo/redo acts on
    bool runningBefore = false;                     // was anything going just before this press

    struct PendingAssign { LoopAction action; int track; int edge; };     // edge: 0 = a switch, 1 = the source track began to play, 2 = it stopped
    std::array<PendingAssign, 64> assignQueue {};
    int assignCount = 0;
    bool drainingAssigns = false;

    // ---- shared ----
    std::atomic<int> compensation { 0 };

    juce::SpinLock producerLock;
    juce::AbstractFifo messageFifo { 256 };
    std::array<Message, 256> messages {};

    mutable juce::SpinLock configLock;
    SongSetup pendingSong, shownSong;
    LooperOptions pendingOptions, shownOptions;
    bool songPending = false, optionsPending = false;
    std::atomic<bool> anyPending { false };

    mutable juce::SpinLock snapshotLock;
    LooperSnapshot shared;

    juce::AbstractFifo logFifo { 512 };
    std::array<LoopLogEntry, 512> logEntries {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Looper)
};

} // namespace gigrig
