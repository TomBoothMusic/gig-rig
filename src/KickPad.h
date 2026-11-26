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

    /** The built-in kick only: the note its body settles on (40 to 90 Hz), and how long the body rings (0.05 to 0.40 s). */
    void setPitch (float hz) noexcept               { pitch.store (juce::jlimit (40.0f, 90.0f, hz)); }
    float getPitch() const noexcept                 { return pitch.load(); }
    void setLength (float seconds) noexcept         { length.store (juce::jlimit (0.05f, 0.4f, seconds)); }
    float getLength() const noexcept                { return length.load(); }

    /** Which pair of returns the kick plays on: 0 = returns 1 and 2 ... 3 = returns 7 and 8. */
    void setReturnPair (int pair) noexcept          { returnPair.store (juce::jlimit (0, 3, pair)); }
    int getReturnPair() const noexcept              { return returnPair.load(); }

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
    static constexpr int doubleHitMs = 30;      // two hits closer than this are counted as a possible double trigger

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
        float clickTone = 1.0f;     // one-pole coefficient: the click of a soft hit is also darker
        float bodyHz = 55.0f, bodyDecay = 0.0f;
        int lengthSamples = 0;
        float sweepHz = 0.0f, sweepEnv = 0.0f, bodyEnv = 0.0f, clickEnv = 0.0f, noiseEnv = 0.0f;
        double bodyPhase = 0.0, clickPhase = 0.0;
        float noiseLow = 0.0f, clickLow = 0.0f, splitLow = 0.0f;
        juce::uint32 noiseSeed = 1;
        int fadeLeft = -1;          // samples of fade-out still to go, or -1 when not fading
        bool usesSample = false;
    };

    void startVoice (int strength) noexcept;
    float nextSample (Voice&) noexcept;

    struct Sample
    {
        std::vector<float> data;
        double rate = 48000.0;
    };

    // ---- settings, read by the audio thread ----
    std::atomic<bool> on { true }, learning { false };
    std::atomic<float> level { 0.8f }, touch { 1.5f }, click { 1.0f }, pitch { 55.0f }, length { 0.17f };
    std::atomic<int> returnPair { 2 };

    // ---- hits on their way to the audio thread ----
    static constexpr int queueSize = 64;
    juce::AbstractFifo fifo { queueSize };
    std::array<Queued, queueSize> queue {};

    // ---- audio thread ----
    double rate = 48000.0;
    int fadeSamples = 240, tailSamples = 1920;
    float sweepDecay = 0.0f, clickDecay = 0.0f, noiseDecay = 0.0f, noiseSplit = 0.0f, sampleSplit = 0.0f;
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
    int hits = 0, screenHits = 0, softest = 0, hardest = 0, doubles = 0, closestGapMs = -1;
    std::array<int, 128> strengthCounts {};
    int noteOffs = 0, otherMessages = 0, otherNotes = 0;
    juce::StringArray otherKinds;       // each different kind of other message from the pad, once
    juce::StringArray notesSeen;        // each different note the pad has sent, once
    juce::int64 lastHitMs = 0;
    double blockMs = 0.0, outputDelayMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickPad)
};

} // namespace gigrig
