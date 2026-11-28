#pragma once

#include "Looper.h"

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace gigrig
{

/** Loops in a virtual soundcheck (built 28 Nov 2026, docs/recording.md, "Loops in a soundcheck").

    Tom, 28 Nov 2026: "I'd love to be able to play the soundcheck back with loops but tweak the
    processing according to the venue." A loop that was saved as sound already has its processing
    in it, so the loops are not saved at all: **while a recording is made, every looper press is
    noted to the sample** in the recording's "Looper presses.txt", with the song that was loaded;
    **while that recording plays as a soundcheck, the same presses are made again at the same
    samples**, so the looper builds every loop afresh out of the raw playback, through whatever
    the effects and the mixer are set to now.

    Threads: the audio thread only copies presses into a queue (recording) or out of a list made
    before playback began (soundcheck). The message thread writes the file and loads songs.

    Nothing recording and no soundcheck playing, the audio thread reads two flags and does nothing. */
class LoopReplay final : private juce::Timer
{
public:
    /** The file in a recording's folder. On Tom's disk: never rename it or reword its lines. */
    static constexpr const char* fileName = "Looper presses.txt";

    struct Step
    {
        juce::int64 at = 0;             // samples from the start of the recording, at the tape's rate
        Looper::Timed::Kind kind = Looper::Timed::Kind::pedal;
        PedalEvent event;
        juce::String song;              // Kind::song only
    };

    struct Tape
    {
        bool found = false;
        double rate = 0.0;
        juce::String startSong;
        bool loopsGoing = false;        // loops were already going when the recording began: it cannot be replayed
        std::vector<Step> steps;
    };

    explicit LoopReplay (Looper&);
    ~LoopReplay() override;

    //==============================================================================
    // Message thread.

    static juce::String headerText (double rate, const juce::String& startSong, bool loopsGoing);
    static juce::String lineFor (const Step&);
    static Tape parse (const juce::String& text);
    static Tape read (const juce::File& recordingFolder);

    /** The folder of the recording being made now. */
    std::function<juce::File()> recordingFolder;
    /** Loads a song by name as a program change would (looper layout and sounds). False: there is no such song. */
    std::function<bool (const juce::String&)> loadSong;
    /** True while the soundcheck is playing. */
    std::function<bool()> soundcheckPlaying;

    /** A soundcheck of this recording is about to start. Gets its presses ready, loads its first
        song if another is loaded, and clears the looper. Returns what will happen, in words. */
    juce::String arm (const juce::File& recordingFolder, double engineRate, juce::int64 passFrames, bool roundAndRound);
    /** No more presses (the soundcheck's own loops are cleared if it made any). */
    void disarm();

    bool isArmed() const noexcept                   { return armed.load(); }
    int pressesInTape() const noexcept              { return tapePresses; }
    int pressesMade() const noexcept                { return made.load(); }
    /** One line for the screens: what the loops of this soundcheck are doing, or why there are none. */
    juce::String statusLine() const                 { return status; }
    juce::String reportText() const;

    /** Writes noted presses to the file, loads songs the soundcheck has reached. Runs ten times a
        second by itself; the self-test calls it. */
    void poll();

    //==============================================================================
    // Audio thread.

    /** After the looper and the recorder have done a block: notes what the looper acted on. */
    void noteBlock (const Looper&, bool recording, juce::int64 recordedAfterBlock, int numSamples, double engineRate) noexcept;

    /** Before the looper: the presses of a soundcheck that fall in this block (blockStart is how
        much of the soundcheck had been played before it, or -1). Returns how many were put in 'out'. */
    int pressesFor (juce::int64 blockStart, int numSamples, Looper::Timed* out, int maxOut) noexcept;

private:
    void timerCallback() override                   { poll(); }

    Looper& looper;

    // ---- recording: audio thread to message thread ----
    struct Note
    {
        juce::int64 at = 0;
        Looper::Timed::Kind kind = Looper::Timed::Kind::pedal;
        bool start = false, going = false;
        double rate = 0.0;
        PedalEvent event;
        char name[24] = {};
    };

    juce::AbstractFifo noteFifo { 2048 };
    std::array<Note, 2048> notes {};
    bool wasRecording = false;                      // audio thread
    std::atomic<int> dropped { 0 };
    juce::File writingTo;                           // message thread
    int written = 0;

    // ---- soundcheck: made on the message thread before 'armed', read by the audio thread ----
    juce::SpinLock lock;                            // held by the message thread only while arming and disarming
    std::vector<Step> steps;                        // times already at the engine's rate
    juce::int64 pass = 0;
    bool wraps = true;
    size_t cursor = 0;                              // audio thread
    std::atomic<bool> armed { false };
    std::atomic<int> made { 0 };

    juce::AbstractFifo songFifo { 64 };             // audio thread to message thread: a step's index, or -1 for the first song again
    std::array<int, 64> songSteps {};

    juce::String startSong, status, lastName;
    int tapePresses = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopReplay)
};

} // namespace gigrig
