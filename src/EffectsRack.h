#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "effects/Chain.h"

#include <array>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>

namespace gigrig
{

/** The effects stage inside the audio engine (built 23 Nov 2026, see docs/effects.md).

    Up to four chains. Each takes one input socket, runs it through its row of effects and
    sends the result out on a return (or a pair), so the mixer (or an amp pedal fed from a
    mixer output) hears it. The looper records the effected sound in place of a socket,
    so effects are baked into loops, as the design says.

    Also here, because they need the same audio: listening A/B against the dry sound or
    a real pedal on another socket, the level match, Capture (dry, reference and effected
    into one WAV), and the amp capture (a test sound out through an amp pedal and back,
    for making a NAM model of it). */
class EffectsRack
{
public:
    static constexpr int numChains = 4;

    enum class Listen { effects, dry, reference };

    /** Where a chain's sound goes: a return pair, one return, or nowhere. */
    struct Output
    {
        int firstReturn = 2;    // 0-based: 2 = return 3 (ST1 left on the CQ)
        bool pair = true;       // also on the next return (the same mono sound on both)
        bool operator== (const Output&) const = default;
    };

    struct Routing
    {
        bool on = false;
        int source = 9;         // 0-based input: 9 = socket 10
        Output out;
        int looperHears = -2;   // socket the looper hears it as: -2 = its own socket, -1 = not at all, else 0-based socket
        Listen listen = Listen::effects;
        int reference = 7;      // 0-based input for A/B against a real pedal (socket 8: where the board and the Dream come back)
        float trimDb = 0.0f;    // the chain's own level, set by hand or by the level match
        juce::String name;      // "Drive test", shown on screen
    };

    EffectsRack();
    ~EffectsRack();

    //==============================================================================
    // Message thread.

    double sampleRate() const noexcept                  { return rate; }

    void setEnabled (bool on) noexcept                  { enabled.store (on); }
    bool isEnabled() const noexcept                     { return enabled.load(); }

    fx::Chain& chain (int c)                            { return chains[(size_t) c].chain; }
    Routing routing (int c) const;
    void setRouting (int c, const Routing& r);
    void setListen (int c, Listen l);
    /** Effects -> dry or reference -> effects, as a pedal switch would. */
    void toggleListen (int c);

    // ---- pedal switches (an RC-600 switch taken from the looper) ----
    /** action: 0 = A/B (effects against dry or the reference), 1 = the whole chain on/off,
        2 = tap tempo (for synced delays, any chain), 3 = bend the chain's whammy while the
        switch is held (toe down on the press, heel on the release), 10 + k = slot k on/off. */
    struct PedalAssign
    {
        int sw = -1;        // pedal switch (0..8 = switches 1 to 9, 9 = CTL3, 10 = CTL4), -1 = none
        int chain = 0;
        int action = 0;
    };
    static constexpr int numPedalAssigns = 2;
    PedalAssign pedalAssign (int i) const noexcept;
    void setPedalAssign (int i, PedalAssign a) noexcept;
    /** Any thread. True if the effects use this switch (so the looper must not); presses are
        carried out on the message thread. */
    bool takePedal (int sw, bool isDown);
    /** Message thread: what a pedal press does. */
    void doPedalAction (const PedalAssign& a);
    /** Message thread: moves the Pedal of the first whammy in a chain to the toe (down) or the
        heel. The whammy's Bend time sets how fast it gets there. False if the chain has none. */
    bool bendWhammy (int chain, bool toeDown);
    /** Called on the message thread after a pedal press changed something (so the screen can follow). */
    std::function<void()> onPedalAction;
    /** A line for each switch the effects use, for the report and the screen. */
    juce::String pedalText() const;

    /** Loudest peak on an input since the last call (every input, whether a chain uses it or not). */
    float takeSocketMax (int input) noexcept            { return input >= 0 && input < 16 ? socketMax[(size_t) input].exchange (0.0f) : 0.0f; }

    /** Frees old effect sets, and saves the rack's state if it changed. Call about 10 times a second. */
    void tick();

    /** The rack's whole state (routing and every chain) as text, for the app's own memory. */
    juce::String stateText() const;
    void loadStateText (const juce::String& text);
    static juce::File stateFile();
    void saveState();
    void markChanged() noexcept                         { dirty = true; }

    // ---- meters and figures ----
    float takeInputPeak (int c) noexcept                { return chains[(size_t) c].inPeak.exchange (0.0f); }
    float takeOutputPeak (int c) noexcept               { return chains[(size_t) c].outPeak.exchange (0.0f); }
    float takeReferencePeak (int c) noexcept            { return chains[(size_t) c].refPeak.exchange (0.0f); }
    bool guardTripped (int c) const noexcept            { return chains[(size_t) c].guard.load(); }
    void clearGuard (int c) noexcept                    { chains[(size_t) c].guard.store (false); }
    /** Biggest share of the audio block the rack used since the last call, 0..1. */
    double takeLoad() noexcept                          { return peakLoad.exchange (0.0); }

    struct Levels
    {
        double effectsDb = -100.0, compareDb = -100.0;  // compare = the reference if one is set, else the dry
        double playedSeconds = 0.0;
    };
    /** Average levels while the guitar was played, since the last call. */
    Levels takeLevels (int c) noexcept;

    // ---- Capture: dry, reference and effected of one chain ----
    juce::String startCapture (int c, const juce::File& folder);
    void stopCapture();
    bool isCapturing() const noexcept                   { return capturing.load(); }
    double captureSeconds() const noexcept;
    juce::File lastCaptureFile() const                  { return lastCapture; }
    static constexpr double maxCaptureSeconds = 180.0;

    // ---- Amp capture: a test sound out through an amp pedal and back ----
    struct AmpCaptureSetup
    {
        Output out { 2, true };     // the return(s) that feed the amp
        int input = 7;              // 0-based socket the amp comes back on
        float levelDb = -12.0f;     // the test sound's loudest peak
        juce::String name;          // "Dream 65 A edge of breakup"
        juce::File soundFile;       // empty = the app's own test sound
    };
    /** Starts playing the test sound and recording. Returns an error, or an empty string. */
    juce::String startAmpCapture (const AmpCaptureSetup& setup, const juce::File& folder, double latencyMs, const juce::String& latencySource);
    void stopAmpCapture();
    bool isAmpCapturing() const noexcept                { return ampPlaying.load(); }
    /** 0..1 through the test sound. */
    double ampCaptureProgress() const noexcept;
    double ampCaptureSeconds() const noexcept;
    juce::File lastAmpCaptureFile() const               { return lastAmpCapture; }
    float takeAmpReturnPeak() noexcept                  { return ampReturnPeak.exchange (0.0f); }
    /** True once the test sound has finished by itself (it then stops recording on the next tick). */
    bool ampCaptureFinished() const noexcept            { return ampDone.load(); }

    /** The app's own test sound at a rate: blips to line up, sweeps, noise and plucked chords at many levels. */
    static std::vector<float> makeTestSound (double rate);

    /** The effects part of the report. */
    juce::String reportText() const;

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate, int maxBlock, int numIns);

    /** Runs every chain. Afterwards looperInputs() holds the inputs the looper should hear. */
    void process (const float* const* inputs, int numIns, int numSamples) noexcept;
    const float* const* looperInputs (const float* const* inputs, int numIns) const noexcept
    {
        return (int) feed.size() >= numIns && numIns > 0 ? feed.data() : inputs;
    }

    /** Adds the chains' sound (and the amp capture's test sound) to the returns. outputFor(r) gives return r's buffer or null. */
    template <typename OutputFor>
    void addOutputs (OutputFor&& outputFor, int numSamples) noexcept
    {
        if (! running)
            return;

        for (int r = 0; r < 8; ++r)
            if (auto* out = outputFor (r))
                if (returnUsed[(size_t) r])
                    juce::FloatVectorOperations::add (out, returnMix.data() + (size_t) r * (size_t) blockCapacity, numSamples);
    }

    /** The song's tempo for effects that follow it, such as a synced delay (0 = not known yet).
        Audio thread, before process(). */
    void setTempo (double looperBpm) noexcept
    {
        const double bpm = looperBpm > 0.0 ? looperBpm : tappedBpm.load (std::memory_order_relaxed);
        tempoFromLoop.store (looperBpm > 0.0, std::memory_order_relaxed);
        tempoNow.store (bpm, std::memory_order_relaxed);
        for (auto& c : chains)
            c.chain.setTempo (bpm);
    }

    /** Tap tempo, for synced delays when no loop has set the tempo. Any thread except audio:
        a press at 'ms' (Time::getMillisecondCounterHiRes). Two taps set it; up to four are
        averaged; a gap over 2 seconds starts again. */
    void tap (double ms);
    /** The tempo the effects use now, and where it came from (for the screen and report). */
    double effectsTempo() const noexcept                { return tempoNow.load(); }
    juce::String tempoText() const;

    /** Silences the rack (measuring, glitch check): nothing out, and the looper hears the raw inputs. */
    void bypassBlock (const float* const* inputs, int numIns, int numSamples) noexcept;

private:
    std::atomic<double> tempoNow { 0.0 }, tappedBpm { 0.0 };
    std::atomic<bool> tempoFromLoop { false };
    juce::SpinLock tapLock;
    double lastTapMs = -1.0e9;
    std::vector<double> tapGaps;

    struct ChainSlot
    {
        fx::Chain chain;
        Routing route;                                  // message thread's copy
        std::atomic<bool> on { false };
        std::atomic<int> source { 9 }, outFirst { 2 }, looperHears { -2 }, reference { 7 }, listen { 0 };
        std::atomic<bool> outPair { true };
        std::atomic<float> trim { 1.0f };
        std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, refPeak { 0.0f };
        std::atomic<bool> guard { false };
        std::atomic<double> effSum { 0.0 }, cmpSum { 0.0 };
        std::atomic<juce::int64> played { 0 };

        // audio thread
        std::vector<float> wet, heard;
        float trimNow = 1.0f;
        float mixEffects = 1.0f, mixDry = 0.0f, mixRef = 0.0f;
        double env = 0.0;
        int hot = 0;
        bool wasOn = false;
    };

    void pushRouting (int c);

    std::array<ChainSlot, numChains> chains;
    std::atomic<bool> enabled { false };
    bool running = false;          // audio thread: this block produced output
    double rate = 48000.0;
    int blockCapacity = 0;
    std::vector<float> silence, returnMix;
    std::array<bool, 8> returnUsed {};
    std::vector<const float*> feed;
    std::atomic<double> peakLoad { 0.0 };
    std::array<std::atomic<float>, 16> socketMax {};
    std::array<std::atomic<int>, numPedalAssigns> assignSw, assignChain, assignAction;
    double envCoeff = 0.0;
    bool dirty = false;
    int ticksSinceChange = 0;

    // ---- capture ----
    juce::TimeSliceThread writerThread { "Effects capture writer" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<bool> capturing { false };
    std::atomic<int> captureChain { 0 };
    std::atomic<juce::int64> capturedSamples { 0 };
    juce::File lastCapture;

    // ---- amp capture ----
    std::vector<float> testSound;               // only swapped while not playing
    std::atomic<bool> ampPlaying { false }, ampDone { false };
    std::atomic<juce::int64> ampPosition { 0 };
    std::atomic<int> ampOutFirst { 2 }, ampInput { 7 };
    std::atomic<bool> ampOutPair { true };
    std::atomic<float> ampGain { 0.25f };
    std::atomic<float> ampReturnPeak { 0.0f };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> ampWriter;
    juce::SpinLock ampWriterLock;
    juce::File lastAmpCapture;
    juce::String ampSetupText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsRack)
};

} // namespace gigrig
