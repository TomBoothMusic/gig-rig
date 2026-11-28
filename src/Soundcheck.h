#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace gigrig
{

/** Virtual soundcheck (built 28 Nov 2026, docs/recording.md, "Virtual soundcheck"): plays the
    raw files of a recording back into the input sockets they were recorded from, so the
    effects, the looper and the mixer all work on them exactly as they would on Tom playing,
    and every change he makes is heard as it would be live.

    Only raw files can be played (Tom, 28 Nov 2026: "that would have to use a raw recording
    that can then be run through my current effects and mixer state"). Which file belongs to
    which socket is read from the "soundcheck:" lines of the recording's
    "About this recording.txt". A socket with no file stays live, so Tom can play along.

    Threads: a background thread reads the files, brings them to the interface's sample rate
    and fills a queue. The audio thread only empties the queue; it never touches a file or the
    heap. A drive that cannot keep up gives a moment of silence on those sockets, counted in
    the report, never a wait.

    **Not playing, feed() reads one flag and hands back the inputs it was given: no sample
    changes** (a check). */
class Soundcheck final : private juce::Thread
{
public:
    static constexpr int maxSockets = 16;       // files played at once
    static constexpr int maxInputs = 64;        // the widest interface feed() will stand in for

    struct Source
    {
        juce::File file;
        int socket = -1;                        // 0-based input socket
    };

    Soundcheck();
    ~Soundcheck() override;

    //==============================================================================
    // Message thread.

    /** The raw files of a recording and the sockets they came from (empty: it cannot be played). */
    static std::vector<Source> sourcesIn (const juce::File& recordingFolder);

    /** The interface (re)opened. Stops anything playing. */
    void prepare (double sampleRate, int maxBlock);

    /** Starts playing a recording from its start. Returns an error, or an empty string. */
    juce::String start (const juce::File& recordingFolder, bool roundAndRound = true, bool ownThread = true,
                        const std::function<void()>& beforePlaying = {});
    /** Fades out and hands the sockets back to what is plugged into them. */
    void stop();

    bool isPlaying() const noexcept             { return playing.load(); }
    /** The recording that is playing (its folder's name), or the one that played last. */
    juce::String name() const                   { return current; }
    double positionSeconds() const noexcept;
    double lengthSeconds() const noexcept       { return lengthSecs; }
    bool isLooping() const noexcept             { return looping.load(); }
    /** One pass of the recording, in samples at the interface's rate. */
    juce::int64 passFrames() const noexcept     { return fileRate > 0.0 ? (juce::int64) std::llround ((double) fileLength * rate / fileRate) : 0; }
    /** The sockets being stood in for, as a bit for each (bit 0 = socket 1). */
    unsigned sockets() const noexcept           { return playing.load() ? socketMask : 0u; }
    /** Blocks that had to be silent because the drive did not keep up. */
    int missedBlocks() const noexcept           { return missed.load(); }
    juce::String reportText() const;

    /** For the self-test (started with ownThread false): fills the queue now, as the background thread would have in real time. */
    void fillNow()                              { while (pump()) {} }
    /** For the self-test: waits until the queue holds enough to start. */
    bool waitUntilReady (int milliseconds);

    //==============================================================================
    // Audio thread.

    /** Hands back the inputs with the recording's sockets swapped for the recording. */
    const float* const* feed (const float* const* inputs, int numIns, int numSamples) noexcept;
    /** After feed(), while playing: how much of the recording had been played before this block
        (every pass counted on), or -1 if this block played none of it. */
    juce::int64 blockStart() const noexcept     { return blockAt; }
    /** Any thread: how much has been played so far (every pass counted on), or -1 when nothing is playing. */
    juce::int64 playedNow() const noexcept      { return playing.load() ? played.load() : -1; }

private:
    void run() override;
    bool pump();
    void closeFiles();

    double rate = 0.0, fileRate = 48000.0, lengthSecs = 0.0;
    int block = 0;
    juce::String current;
    unsigned socketMask = 0;

    juce::AudioFormatManager formats;
    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;      // reader thread, between start and stop
    std::vector<std::unique_ptr<juce::WindowedSincInterpolator>> resamplers;
    std::vector<std::vector<float>> stage, resampled;                   // reader thread
    juce::AudioBuffer<float> readBuf;
    int staged = 0;
    juce::int64 readPos = 0, fileLength = 0;
    bool readerDone = false;

    // ---- the queue: one ring per file, filled together, emptied together ----
    juce::SpinLock lock;                        // held by the message thread only while starting and stopping
    juce::AbstractFifo fifo { 16 };
    std::vector<std::vector<float>> ring;
    std::vector<std::vector<float>> out;        // audio thread: this block, for each file
    std::array<int, maxSockets> socketOf {};
    int numSources = 0;
    std::array<const float*, maxInputs> swapped {};

    std::atomic<bool> playing { false }, looping { true }, wantStop { false }, ended { false }, faded { false };
    std::atomic<int> missed { 0 };
    std::atomic<juce::int64> played { 0 };
    bool primed = false;                        // audio thread
    juce::int64 blockAt = -1;                   // audio thread
    float gain = 0.0f;                          // audio thread: the fade in and out
    int primeFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Soundcheck)
};

} // namespace gigrig
