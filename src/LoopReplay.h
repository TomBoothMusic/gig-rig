#pragma once

#include "EffectsRack.h"
#include "KickPad.h"
#include "Looper.h"

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace gigrig
{

/** What Tom's feet did, made again in a virtual soundcheck (built 28 Nov 2026, docs/recording.md,
    "Loops, kicks and switches in a soundcheck").

    Tom, 28 Nov 2026: "I'd love to be able to play the soundcheck back with loops but tweak the
    processing according to the venue." A loop that was saved as sound already has its processing
    in it, so the loops are not saved at all: **while a recording is made, everything his feet do
    is noted** in the recording's "Looper presses.txt" (every looper press and every kick from the
    pad to the sample; effect switch presses and expression pedal moves to a hundredth of a
    second; the song that was loaded), and **while that recording plays as a soundcheck, the same
    things are done again at the same moments**. The looper builds every loop afresh out of the
    raw playback, the kick pad plays its kicks, the effect switches switch, all through whatever
    the effects and the mixer are set to now.

    Threads: the audio thread only copies presses and kicks into a queue (recording) or out of a
    list made before playback began (soundcheck). The message thread writes the file, loads songs,
    and presses the effect switches and moves the pedal, through the doors a foot uses.

    Nothing recording and no soundcheck playing, the audio thread reads two flags and does nothing. */
class LoopReplay final : private juce::Timer
{
public:
    /** The file in a recording's folder. On Tom's disk: never rename it or reword its lines. */
    static constexpr const char* fileName = "Looper presses.txt";

    enum class What : juce::uint8 { pedal, clearAll, song, kick, fxSwitch, expression };

    struct Step
    {
        juce::int64 at = 0;             // samples from the start of the recording, at the tape's rate
        What what = What::pedal;
        PedalEvent event;               // pedal (a looper press) and fxSwitch (a press the effects took)
        juce::String song;              // song
        float value = 0.0f;             // kick: its strength, 1 to 127. expression: the pedal, 0 heel to 100 toe
    };

    /** Where a switch that flips something stood when the recording began. */
    struct SwitchState
    {
        int sw = -1, chain = 0, action = 0, state = 0;
    };

    struct Tape
    {
        bool found = false;
        double rate = 0.0;
        juce::String startSong;
        bool loopsGoing = false;        // loops were already going when the recording began: they cannot be built again
        std::vector<SwitchState> switches;
        float pedalAtStart = -1.0f;     // -1 = the pedal had never been moved
        std::vector<Step> steps;

        int count (What w) const noexcept
        {
            int n = 0;
            for (const auto& s : steps) n += s.what == w ? 1 : 0;
            return n;
        }
    };

    LoopReplay (Looper&, EffectsRack&, KickPad&);
    ~LoopReplay() override;

    //==============================================================================
    // Message thread.

    static juce::String headerText (double rate, const juce::String& startSong, bool loopsGoing, const std::vector<SwitchState>& switches, float pedalAtStart);
    static juce::String lineFor (const Step&);
    static Tape parse (const juce::String& text);
    static Tape read (const juce::File& recordingFolder);

    /** The folder of the recording being made now. */
    std::function<juce::File()> recordingFolder;
    /** How many samples of the recording being made have been recorded, or -1 when nothing is recording. */
    std::function<juce::int64()> recordedSamples;
    /** Loads a song by name as a program change would (looper layout and sounds). False: there is no such song. */
    std::function<bool (const juce::String&)> loadSong;
    /** How much of the soundcheck has been played (every pass counted on), or -1 when none is playing. */
    std::function<juce::int64()> soundcheckPlayed;

    /** Any thread but the audio thread: the effects took this press (EffectsRack::takePedal said
        so). Noted if a recording is being made. */
    void noteSwitch (const PedalEvent&);

    /** A soundcheck of this recording is about to start. Gets everything ready, loads its first
        song if another is loaded, clears the looper and puts the switches where they were.
        Returns what will happen, in words. */
    juce::String arm (const juce::File& recordingFolder, double engineRate, juce::int64 passFrames, bool roundAndRound);
    /** Nothing more is made again. The loops the soundcheck built are cleared and the switches
        and the pedal go back to where they were before it began. */
    void disarm();

    bool isArmed() const noexcept                   { return armed.load(); }
    /** Looper presses and kicks made again so far (the audio thread's part). */
    int pressesMade() const noexcept                { return made.load(); }
    int kicksMade() const noexcept                  { return kicked.load(); }
    /** Effect switch presses and pedal moves made again so far (the message thread's part). */
    int controlsMade() const noexcept               { return controlled; }
    /** One line for the screens: what this soundcheck is doing again, or why it is doing nothing. */
    juce::String statusLine() const                 { return status; }
    juce::String reportText() const;

    /** Writes what was noted to the file, loads songs the soundcheck has reached, presses its
        switches and moves its pedal. Runs a hundred times a second by itself; the self-test calls it. */
    void poll();

    //==============================================================================
    // Audio thread.

    /** After the looper, the kick pad and the recorder have done a block: notes what they acted on. */
    void noteBlock (const Looper&, const KickPad&, bool recording, juce::int64 recordedAfterBlock, int numSamples, double engineRate) noexcept;

    /** Before the kick pad and the looper: what a soundcheck does in this block (blockStart is how
        much of it had been played before the block, or -1). Looper presses go in 'out' with their
        samples and their number is returned; kicks are handed straight to the kick pad. */
    int pressesFor (juce::int64 blockStart, int numSamples, Looper::Timed* out, int maxOut) noexcept;

private:
    void timerCallback() override                   { poll(); }
    std::vector<SwitchState> switchesNow() const;
    void putSwitches (const std::vector<SwitchState>&);
    void doControl (const Step&, juce::int64 position);
    void backToStart();

    Looper& looper;
    EffectsRack& rack;
    KickPad& kick;

    // ---- recording: audio thread to message thread ----
    struct Note
    {
        juce::int64 at = 0;
        What what = What::pedal;
        bool start = false, going = false;
        double rate = 0.0;
        PedalEvent event;
        float value = 0.0f;
        char name[24] = {};
    };

    juce::AbstractFifo noteFifo { 2048 };
    std::array<Note, 2048> notes {};
    bool wasRecording = false;                      // audio thread
    std::atomic<int> dropped { 0 };
    juce::File writingTo;                           // message thread
    int written = 0;
    juce::CriticalSection controlLock;              // effect switch presses arrive on the MIDI thread
    std::vector<Step> controlNotes;
    int pedalMovesSeen = 0;

    // ---- soundcheck: made on the message thread before 'armed', read by the audio thread ----
    juce::SpinLock lock;                            // held by the message thread only while arming and disarming
    std::vector<Step> steps;                        // looper presses, songs and kicks; times already at the engine's rate
    bool clearsLoops = false;                       // the tape has looper presses: the loops go when it comes round
    double engineRateNow = 0.0;
    juce::int64 pass = 0;
    bool wraps = true;
    size_t cursor = 0;                              // audio thread
    std::atomic<bool> armed { false };
    std::atomic<int> made { 0 }, kicked { 0 };

    juce::AbstractFifo songFifo { 64 };             // audio thread to message thread: a step's index, or -1 for the first song again
    std::array<int, 64> songSteps {};

    // The message thread's part of a soundcheck: effect switches and the pedal.
    std::vector<Step> controlSteps;
    size_t controlCursor = 0;
    juce::int64 lastPosition = -1;
    int controlled = 0;
    std::vector<SwitchState> startSwitches, switchesBefore;
    float pedalAtStart = -1.0f, pedalBefore = -1.0f;
    bool pedalTouched = false;
    std::array<bool, 16> heldDown {};               // effect switches the soundcheck is holding down

    juce::String startSong, status, lastName;
    Tape counts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopReplay)
};

} // namespace gigrig
