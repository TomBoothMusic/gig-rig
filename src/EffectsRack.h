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
        switch is held (toe down on the press, heel on the release), 4 = the test take (record,
        then play, then stop; any chain), 5 = a delay throw while the switch is held (the chain's
        delay catches what is played or sung; let go and the repeats ring out), 6 = rock the chain's
        wah while the switch is held (toe on the press, heel on the release), 7 = the chain's next
        amp sound (steps round the amp models, see nextAmp), 10 + k = slot k on/off.
        These belong to the song: loading a song that names switches replaces them all (SongBook). */
    struct PedalAssign
    {
        int sw = -1;        // pedal switch (0..8 = switches 1 to 9, 9 = CTL3, 10 = CTL4), -1 = none
        int chain = 0;
        int action = 0;
    };
    static constexpr int numPedalAssigns = 6;
    static constexpr int actionPedalToe = 90;       // a held switch stands in for the expression pedal: toe down while held, heel down when let go
    PedalAssign pedalAssign (int i) const noexcept;
    void setPedalAssign (int i, PedalAssign a) noexcept;
    /** Any thread. True if the effects use this switch (so the looper must not); presses are
        carried out on the message thread. */
    bool takePedal (int sw, bool isDown, bool stillHeld = false);
    /** Message thread: what a pedal press does. */
    void doPedalAction (const PedalAssign& a);
    /** Message thread: moves the Pedal of the first whammy in a chain to the toe (down) or the
        heel. The whammy's Bend time sets how fast it gets there. False if the chain has none. */
    bool bendWhammy (int chain, bool toeDown);
    /** Message thread: the same for the first wah in a chain. Its Sweep time sets how fast it travels. */
    bool sweepWah (int chain, bool toeDown);
    /** Message thread: the delay throw. Sets the "throw" setting of the first effect in the chain
        that has one to Catching (held) or Ready (let go). False if the chain has none. */
    bool holdThrow (int chain, bool held);
    /** The harmony sings only while a switch is held: its "hold" setting goes to Singing and back to Ready.
        A harmony that is switched off is switched on by the first press. False when the chain has none. */
    bool holdHarmony (int chain, bool held);
    /** Message thread: the amp-cycling switch. Sets the first amp in the chain to the next amp
        model in the chain's list (setAmpCycle), or in the models folder when the list is empty,
        going round to the first after the last. The amp fades from one to the other by itself.
        Returns a line for the screen ("Chain 1 amp: Dream 65 - Driven"). */
    juce::String nextAmp (int chain);
    /** The amp models the amp-cycling switch steps through on a chain (empty = every model installed). */
    void setAmpCycle (int chain, const juce::StringArray& models);
    juce::StringArray ampCycle (int chain) const;
    /** The last thing nextAmp() did, for the screen and the report. */
    juce::String ampNote;

    /** Whether delays and reverbs ring out when a chain's effects are changed (another song,
        another preset), or are cut in 10 ms. On by default; a rig setting, saved with the rack. */
    void setTailsRingOut (bool shouldRing);
    bool tailsRingOut() const noexcept                  { return tails; }
    /** How many chains have an old set of effects still ringing out right now. */
    int tailsRinging() const noexcept;

    /** The presets a chain was built from, in the order they were loaded and added (empty when
        the chain was built by hand). Kept so "Keep these sounds for this song" can name the
        presets instead of copying their settings. Message thread. */
    juce::StringArray recipe (int chain) const;
    void setRecipe (int chain, const juce::StringArray& presets);
    /** The chain as it was when it was last loaded (by a song, or by Load and Add to chain), so
        "Update its presets" can tell which knobs were turned since and save only those: a
        song's own changes on top of a preset must not leak into the preset. Saved with the rack. */
    const fx::ChainModel& baseline (int chain) const;
    void setBaseline (int chain, const fx::ChainModel&);

    /** Message thread: moves the Pedal of every volume pedal in every chain to one position
        (0 heel .. 100 toe). Tom's one pedal is a blend: the electric chain's volume pedal as
        it is and the acoustic chain's reversed both follow it. Where a real expression pedal
        will arrive. Returns how many it moved. */
    int moveVolumePedals (float position);

    /** Message thread: the one expression pedal (0 heel .. 100 toe). It is the blend (every
        volume pedal follows it) until the toe switch is on; then it rocks the wah or whammy
        the toe switch engaged. A real pedal, and the Pedal sliders on screen, call this. */
    void moveExpressionPedal (float position);
    /** Message thread: the toe switch under the pedal (CTL4 on the RC-600 for now), pedal
        action 9. On: the electric goes to full and the acoustic is muted (every volume pedal
        to the toe), and the first wah or whammy in the chain is switched on and takes over the
        pedal. Off: that effect is switched off and the blend comes back where the pedal was
        left. Returns true if it is now on. */
    bool toeSwitch (int chain);
    bool toeSwitchOn() const noexcept                   { return toeOn; }

    /** Called on the message thread after a pedal press changed something (so the screen can follow). */
    std::function<void()> onPedalAction;
    /** A line for each switch the effects use, for the report and the screen. */
    juce::String pedalText() const;
    /** What the effects do with one pedal switch, in a few words ("chain 2 wah sweep while held"),
        or nothing when the looper has it: the effects are off, or no row names the switch.
        The Looper tab shows it on the switch's tile, so a switch that no longer loops says so. */
    juce::String pedalUse (int sw) const;

    /** Loudest peak on an input since the last call (every input, whether a chain uses it or not). */
    float takeSocketMax (int input) noexcept            { return input >= 0 && input < 16 ? socketMax[(size_t) input].exchange (0.0f) : 0.0f; }

    /** Frees old effect sets, and saves the rack's state if it changed. Call about 10 times a second. */
    void tick();

    /** The rack's whole state (routing and every chain) as text, for the app's own memory. */
    juce::String stateText() const;
    void loadStateText (const juce::String& text);
    static juce::File stateFile();
    void saveState();

    /** False while the self-test runs, so its pretend rigs never write over the real memory file
        (found 27 Nov 2026: a self-test run on a real machine left the test's settings behind). */
    static inline std::atomic<bool> memoryOnDisk { true };
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

    // ---- Test take: play once, then hear it round and round through the chains while turning knobs ----
    // Records the dry input of every chain that is on (before its effects). Played back, each
    // of those chains hears its part of the take in place of its socket, all in step, so a
    // whole performance (guitar and voice together, say) can be listened to hands-free. A
    // chain that was off while recording stays live, to play or sing along with the take.
    enum class TakeState { none, recording, ready, playing };
    static constexpr double maxTakeSeconds = 60.0;
    /** Starts recording. Returns an error, or an empty string. */
    juce::String startTake();
    /** Ends recording (the take is kept, with silence at either end trimmed off) or stops playback. */
    void stopTake();
    /** Plays the take round and round. Returns an error, or an empty string. */
    juce::String playTake();
    /** Ends recording, keeps the take and saves it in the takes folder. Returns a line for the screen. */
    juce::String finishTake();
    /** One press at a time: record, then (next press) keep it and play it round and round, then stop.
        What a pedal switch does. Returns a line for the screen. */
    juce::String takeStep();
    /** The last thing takeStep() or a pedal press did, for the screen. */
    juce::String takeNote;
    TakeState takeState() const noexcept;
    /** How long the take is (or how much has been recorded so far). */
    double takeSeconds() const noexcept;
    bool takeHolds (int c) const noexcept;
    juce::String takeName() const                       { return takeLabel; }
    /** A WAV with one channel per chain (silent for a chain the take does not hold). */
    juce::String saveTake (const juce::File& file);
    juce::String loadTake (const juce::File& file);
    static juce::File takeFolder();
    /** "Take 'x': 12.4 s of chains 1 and 2, playing" for the screen and the report. */
    juce::String takeText() const;

    /** How much of each audio block a chain's effects use, 0..1: on average lately, and at its busiest in the last 10 seconds. */
    struct ChainLoad { double average = 0.0, busiest = 0.0; };
    ChainLoad chainLoad (int c) const noexcept;

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
    juce::String pedalAssignText (const PedalAssign&, bool brief) const;
    bool rockPedal (int chain, const char* type, bool toeDown);
    std::atomic<double> tempoNow { 0.0 }, tappedBpm { 0.0 };
    std::atomic<bool> tempoFromLoop { false };
    juce::SpinLock tapLock;
    double lastTapMs = -1.0e9;
    std::vector<double> tapGaps;

    struct ChainSlot
    {
        fx::Chain chain;
        Routing route;                                  // message thread's copy
        juce::StringArray recipe, ampCycle;             // message thread
        fx::ChainModel baseline;                        // message thread
        std::atomic<bool> on { false };
        std::atomic<int> source { 9 }, outFirst { 2 }, looperHears { -2 }, reference { 7 }, listen { 0 };
        std::atomic<bool> outPair { true };
        std::atomic<float> trim { 1.0f };
        std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, refPeak { 0.0f };
        std::atomic<bool> guard { false };
        std::atomic<float> loadAverage { 0.0f }, loadPeak { 0.0f };
        std::array<float, 100> loadRing {};             // message thread: the busiest block in each of the last 100 ticks
        int loadRingAt = 0;
        std::atomic<double> effSum { 0.0 }, cmpSum { 0.0 };
        std::atomic<juce::int64> played { 0 };

        // audio thread
        std::vector<float> wet, heard, takeDry;
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
    bool dirty = false, tails = true;
    bool toeOn = false;             // the toe switch: the pedal works a wah or whammy, not the blend
    int toeChain = 0;
    float blendPosition = 100.0f;   // where the blend was left when the toe switch went on
    int ticksSinceChange = 0;

    // ---- capture ----
    juce::TimeSliceThread writerThread { "Effects capture writer" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<bool> capturing { false };
    std::atomic<int> captureChain { 0 };
    std::atomic<juce::int64> capturedSamples { 0 };
    juce::File lastCapture;

    // ---- test take ----
    struct Take
    {
        std::array<std::vector<float>, numChains> data;     // all the same size
        std::array<bool, numChains> has {};
        juce::int64 start = 0, length = 0;                  // the part that is played, once trimmed
        double rate = 0.0;
    };
    std::unique_ptr<Take> take;                 // only swapped under takeLock; the audio thread uses it under a try-lock
    juce::SpinLock takeLock;
    std::atomic<int> takeMode { 0 };            // 0 = idle, 1 = recording, 2 = playing
    std::atomic<juce::int64> takeRecorded { 0 }, takePos { 0 };
    juce::String takeLabel;

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
