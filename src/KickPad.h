#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gigrig
{

/** True for the Roland SPD::ONE pads, which show up as "SPD-1K", "SPD-1P" and so on. */
bool looksLikeSpdOne (const juce::String& deviceName);

/** How loud a hit of this strength (1 to 127) plays, 0..1, for a touch setting.
    Touch 1 is a straight line; higher makes soft hits softer still. */
float kickGainForStrength (int strength, float touch) noexcept;

/** How much of the click a hit of this strength keeps, 0.15 (softest) to 1 (hardest), on top of
    being quieter: a beater that lands gently makes less of a click (Tom, 26 Nov 2026). */
float kickClickForStrength (int strength) noexcept;

/** The kick pad test (docs/kick-pad.md, 26 Nov 2026): a pad that sends a note with a strength
    over USB (Tom's SPD::ONE Kick) plays a kick sound made inside the app, louder or softer with
    how hard it was hit. Everything about each hit is counted for the report, because the point
    of the test is to learn what the pad really sends.

    No screen in here. Notes arrive on the MIDI driver's thread (or from the on-screen buttons),
    the sound is made on the audio thread, and the report is read on the message thread. */
class KickPad final
{
public:
    KickPad();

    //==============================================================================
    // The audio thread.

    void prepare (double sampleRate);

    /** Starts any hits that arrived since the last block and adds the kick to left and right
        (either may be null). With quiet true the hits are dropped: a measurement is running. */
    void process (float* left, float* right, int numSamples, bool quiet) noexcept;

    /** The piezo pad test: looks at one block of the piezo's socket for knocks, and starts a kick
        for each. Call it before process, every block; input may be null (nothing plugged in or
        no socket chosen). With quiet true knocks are ignored: a measurement is running. */
    void listen (const float* input, int numSamples, bool quiet) noexcept;

    /** For the virtual soundcheck (docs/recording.md): a hit of this strength starts with the
        next process(), exactly as one that arrived from the pad does. Before process, every block it is needed. */
    void hitNow (int strength) noexcept
    {
        if (numNow < (int) hitsNow.size())
            hitsNow[(size_t) numNow++] = juce::jlimit (1, 127, strength);
    }
    /** The pad and screen hits (not the piezo's, which come from a socket) the last process() started, for the recording's notes. */
    int numStarted() const noexcept                 { return startedCount; }
    int startedStrength (int i) const noexcept      { return startedList[(size_t) i]; }

    //==============================================================================
    // Any thread but the audio thread.

    /** Every MIDI message from every device comes through here. */
    void handleMidi (const juce::String& deviceName, const juce::MidiMessage&);

    /** A hit from the on-screen buttons (or the self-test): strength 1 to 127. */
    void screenHit (int strength);

    /** The next note from any device becomes the pad (for a pad the app does not know by name). */
    void learnNextNote()                            { learning.store (true); }
    bool isLearning() const noexcept                { return learning.load(); }

    /** The pad to listen to, from the settings file. An empty name means "find it by name". note -1 = any note. */
    void setPad (const juce::String& deviceName, int note);
    juce::String padDevice() const;
    int padNote() const;

    void setOn (bool shouldBeOn) noexcept           { on.store (shouldBeOn); }
    bool isOn() const noexcept                      { return on.load(); }
    void setLevel (float v) noexcept                { level.store (juce::jlimit (0.0f, 1.0f, v)); }
    float getLevel() const noexcept                 { return level.load(); }
    void setTouch (float v) noexcept                { touch.store (juce::jlimit (0.5f, 3.0f, v)); }
    float getTouch() const noexcept                 { return touch.load(); }
    void setClick (float v) noexcept                { click.store (juce::jlimit (0.0f, 2.0f, v)); }
    float getClick() const noexcept                 { return click.load(); }

    /** The built-in kick only: the note its body settles on (40 to 90 Hz), and how long the body rings (0.03 to 0.20 s). */
    void setPitch (float hz) noexcept               { pitch.store (juce::jlimit (40.0f, 90.0f, hz)); }
    float getPitch() const noexcept                 { return pitch.load(); }
    void setLength (float seconds) noexcept         { length.store (juce::jlimit (0.03f, 0.2f, seconds)); }
    float getLength() const noexcept                { return length.load(); }

    /** Which pair of returns the kick plays on: 0 = returns 1 and 2 ... 3 = returns 7 and 8. */
    void setReturnPair (int pair) noexcept          { returnPair.store (juce::jlimit (0, 3, pair)); }
    int getReturnPair() const noexcept              { return returnPair.load(); }

    /** The input socket the kick joins, as if the stomp box were plugged in there: the looper
        hears it on that socket and, with the mixer on, the channel listening to that socket
        plays it. -1 = straight onto the returns, never looped. */
    void setSocket (int s) noexcept                 { socket.store (juce::jlimit (-1, 15, s)); }
    int getSocket() const noexcept                  { return socket.load(); }

    //==============================================================================
    // The piezo pad test (27 Nov 2026, docs/kick-pad.md): a bare piezo disc under a plate, plugged
    // into an input socket with a guitar lead. A knock is a short spike on that socket; the app
    // measures how tall the spike is and plays the kick at that strength, as it does for the
    // SPD::ONE. In the finished board the disc would go to the controller, not to a socket.

    /** The input socket the piezo is plugged into, 0-based; -1 = no piezo. */
    void setPiezoSocket (int s) noexcept            { piezoSocket.store (juce::jlimit (-1, 15, s)); }
    int getPiezoSocket() const noexcept             { return piezoSocket.load(); }

    /** A spike has to reach this level to count as a hit (-60 to -10 dB). Anything under it is ignored. */
    void setPiezoThresholdDb (float db) noexcept    { piezoThresholdDb.store (juce::jlimit (-60.0f, -10.0f, db)); }
    float getPiezoThresholdDb() const noexcept      { return piezoThresholdDb.load(); }

    /** A spike this tall, or taller, is the hardest hit, strength 127 (-30 to 0 dB). */
    void setPiezoFullDb (float db) noexcept         { piezoFullDb.store (juce::jlimit (-30.0f, 0.0f, db)); }
    float getPiezoFullDb() const noexcept           { return piezoFullDb.load(); }

    /** The strength (0 = not a hit, else 1 to 127) of a spike of this height, 0..1 of full scale. */
    static int piezoStrength (float peak, float thresholdDb, float fullDb) noexcept;

    /** Brings the piezo's hits, counted on the audio thread, into the report. The screen calls it. */
    void collect();

    static constexpr double piezoScanMs = 2.0;      // after a spike starts, how long the app watches for its top
    static constexpr double piezoHoldMs = 40.0;     // then how long it looks away, while the plate rings

    /** A recording to play in place of the built-in kick (mono, at the file's own rate).
        An empty vector goes back to the built-in kick. */
    void setSample (std::vector<float> samples, double sampleRate, const juce::String& name);
    juce::String sampleName() const;

    /** One hit as the report shows it. */
    struct Hit
    {
        juce::int64 timeMs = 0;
        int strength = 0;
        int gapMs = -1;         // since the hit before; -1 for the first
        bool fromScreen = false;
        bool fromPiezo = false;
        float peakDb = 0.0f;    // a piezo hit: how tall its spike was
    };

    /** The most recent hits, oldest first (at most 32). */
    std::vector<Hit> recentHits() const;
    int hitCount() const;

    /** Clears the counts (not the settings or the pad). */
    void resetCounts();

    /** What the audio device is running at, for the timing lines of the report. */
    void setBlockInfo (double blockMs, double driverDelayMs);

    juce::String reportText() const;

    //==============================================================================
    static constexpr int maxRecent = 32;
    static constexpr int doubleHitMs = 30;      // two hits closer than this are one stomp: the weaker one is not played

private:
    struct Queued { int strength = 0; juce::int64 ticks = 0; };
    void queueHit (int strength, bool fromScreen, const juce::String& device, int note, int channel);

    /** One sounding kick. Two exist: the newest, and the one before it fading out underneath. */
    struct Voice
    {
        bool active = false;
        double position = 0.0;      // samples at the device rate (built-in) or at the file's rate (recording)
        float gain = 0.0f;          // from the strength of the hit and the Touch setting
        float clickAmount = 0.0f;   // how much of the click a hit this hard gets: a soft hit is duller as well as quieter
        float clickTone = 1.0f;     // one-pole coefficient: the beater of a soft hit is also darker
        float bodyHz = 55.0f, bodyDecay = 0.0f;
        int lengthSamples = 0;
        float sweepHz = 0.0f, sweepEnv = 0.0f, bend = 0.0f, bendEnv = 0.0f, bodyEnv = 0.0f, punchEnv = 0.0f;
        float overtones = 0.0f, overtone1Env = 0.0f, overtone2Env = 0.0f, squash = 0.0f;
        float slapEnv = 0.0f, slapGate = 0.0f;
        double bodyPhase = 0.0, overtone1Phase = 0.0, overtone2Phase = 0.0;
        float noiseLow = 0.0f, clickLow = 0.0f, clickLow2 = 0.0f, splitLow = 0.0f;
        juce::uint32 noiseSeed = 1;
        int fadeLeft = -1;          // samples of fade-out still to go, or -1 when not fading
        bool usesSample = false;
    };

    void startVoice (int strength) noexcept;
    std::array<int, 8> hitsNow {}, startedList {};       // audio thread
    int numNow = 0, startedCount = 0;
    float nextSample (Voice&) noexcept;

    struct Sample
    {
        std::vector<float> data;
        double rate = 48000.0;
    };

    // ---- settings, read by the audio thread ----
    std::atomic<bool> on { true }, learning { false };
    // Where a fresh install starts: the settings Tom settled on with the SPD::ONE on his studio
    // monitors (27 Nov 2026, build 103, "we'll lock that sound in"). A big PA may move them.
    std::atomic<float> level { 0.8f }, touch { 1.1f }, click { 0.86f }, pitch { 49.0f }, length { 0.155f };
    std::atomic<int> returnPair { 2 }, socket { 9 }, piezoSocket { -1 };
    std::atomic<float> piezoThresholdDb { -36.0f }, piezoFullDb { -6.0f };

    // ---- the piezo listener (audio thread) and its hits on their way to the report ----
    struct PiezoHit { int strength = 0; float peak = 0.0f; juce::int64 timeMs = 0; };
    float piezoLow = 0.0f, piezoPeak = 0.0f, piezoCut = 0.0f;
    int piezoScanLeft = 0, piezoHoldLeft = 0, piezoScanSamples = 192, piezoHoldSamples = 3840;
    juce::int64 piezoClock = 0, piezoBaseMs = 0;
    std::atomic<float> piezoIgnoredPeak { 0.0f };       // the tallest thing under the threshold since Start again
    static constexpr int piezoQueueSize = 64;
    juce::AbstractFifo piezoFifo { piezoQueueSize };
    std::array<PiezoHit, piezoQueueSize> piezoQueue {};

    // ---- hits on their way to the audio thread ----
    static constexpr int queueSize = 64;
    juce::AbstractFifo fifo { queueSize };
    std::array<Queued, queueSize> queue {};

    // ---- audio thread ----
    double rate = 48000.0;
    int fadeSamples = 240, tailSamples = 1920;
    float sweepDecay = 0.0f, bendDecay = 0.0f, punchDecay = 0.0f, overtone1Decay = 0.0f, overtone2Decay = 0.0f;
    float slapDecay = 0.0f, slapRise = 0.0f, noiseSplit = 0.0f, sampleSplit = 0.0f;
    std::array<Voice, 2> voices {};
    std::shared_ptr<const Sample> playing;      // the recording the audio thread is using now
    juce::SpinLock sampleLock;                  // held only to swap 'playing' for 'waiting'
    std::shared_ptr<const Sample> waiting;
    std::atomic<bool> sampleChanged { false };

    // ---- how long hits waited for the audio thread ----
    std::atomic<int> startedHits { 0 };
    std::atomic<double> waitSumMs { 0.0 }, waitWorstMs { 0.0 };

    // ---- everything the report counts (under the lock) ----
    mutable juce::CriticalSection lock;
    juce::String device;                // the pad in use; empty until one is found
    int note = -1, channel = 0;
    bool foundByName = false, learned = false;
    juce::String sampleLabel;
    std::vector<Hit> recent;
    int piezoHits = 0;
    float piezoSoftestDb = 0.0f, piezoHardestDb = 0.0f;
    int hits = 0, screenHits = 0, softest = 0, hardest = 0, doubles = 0, bounces = 0, lastHitStrength = 0, closestGapMs = -1;
    std::array<int, 128> strengthCounts {};
    int noteOffs = 0, otherMessages = 0, otherNotes = 0;
    juce::StringArray otherKinds;       // each different kind of other message from the pad, once
    juce::StringArray notesSeen;        // each different note the pad has sent, once
    juce::int64 lastHitMs = 0;
    double blockMs = 0.0, outputDelayMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickPad)
};

} // namespace gigrig
