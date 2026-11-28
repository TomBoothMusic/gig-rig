#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace gigrig
{

/** The tuner (built 27 Nov 2026; the guide is docs/tuner.md, the design doc has it on switch 8:
    "TUNER: mutes main, in-ears live").

    While it is on it listens to one input socket as it arrives, before any effect, and says
    which note is being played and how far out it is in cents (hundredths of a semitone).
    With the mixer on, the guitar channels go silent in the main mix while it is on (Tom, 28 Nov
    2026: the microphone and the stomp box stay live, so he can talk to the room while he
    tunes; the monitors keep the guitars unless that is unticked); the
    older choice of the whole main mix going silent is still there. With the mixer off it
    changes no sound at all.

    The strobe (28 Nov 2026, the full-screen tuner in src/StrobeTuner.*): once the note is known,
    the sound is compared with that note's exact frequency, wave against wave, as a strobe tuner's
    spinning disc does. How far the two have slipped apart, in cycles, is Reading::strobe: it
    stands still when the string is in tune and drifts faster the further out it is. The cents
    shown come from the same drift, so the figure and the picture always agree.

    Threads: the audio thread only copies the socket into a queue (thinned to about 24 kHz).
    The message thread empties the queue and works out the note; nothing here uses a lock or
    the heap on the audio thread. Off, the audio thread does nothing but read one flag. */
class Tuner
{
public:
    Tuner();

    //==============================================================================
    // Message thread.

    void setOn (bool shouldBeOn);
    bool isOn() const noexcept                          { return on.load(); }

    /** The 0-based input socket to listen to, or -1 for "whichever is being played". */
    void setSocket (int s);
    int socket() const noexcept                         { return wanted.load(); }

    static constexpr double minReference = 430.0, maxReference = 450.0;
    /** What A above middle C is tuned to, 440 unless somebody changes it. */
    void setReference (double hz);
    double reference() const noexcept                   { return refHz; }

    /** What goes silent while the tuner is on (mixer on only). */
    enum class Silence { nothing, guitars, mainMix };
    void setSilence (Silence s)                         { if (quiet.exchange ((int) s) != (int) s) dirty = true; }
    Silence silence() const noexcept                    { return (Silence) quiet.load(); }
    /** The words used in the memory file and by the remote screen: "nothing", "guitars", "main". */
    static juce::String silenceWord (Silence);
    static bool silenceFromWord (const juce::String&, Silence&);

    /** The older yes or no (builds before 28 Nov 2026 and their remote pages): yes is "the guitars". */
    void setMutes (bool m)                              { if (m != mutes()) setSilence (m ? Silence::guitars : Silence::nothing); }
    bool mutes() const noexcept                         { return silence() != Silence::nothing; }

    /** Whether a mixer channel with this name goes silent under Silence::guitars. Guitars and
        basses are known by their names ("Electric", "Acoustic", "Bass", "Guitar 2"); anything
        else (a voice, the stomp, keys) stays live. A choice made by hand wins over the name. */
    bool goesSilent (const juce::String& channelName) const;
    void setGoesSilent (const juce::String& channelName, bool silent);
    static bool looksLikeGuitar (const juce::String& channelName);

    /** Under Silence::guitars: whether the monitors still carry the guitars (Tom, 19 Nov 2026: the
        in-ears stay live so he can hear the guitar while he tunes). Without it the guitars are
        silent everywhere, as with a tuner pedal at the front of a pedalboard. */
    void setMonitorsKeepGuitars (bool k)                { if (monitorsKeep.exchange (k) != k) dirty = true; }
    bool monitorsKeepGuitars() const noexcept           { return monitorsKeep.load (std::memory_order_relaxed); }

    /** Whether the rig's own screen fills with the strobe tuner while the tuner is on. */
    void setFillsScreen (bool f)                        { if (fills != f) { fills = f; dirty = true; } }
    bool fillsScreen() const noexcept                   { return fills; }

    /** Message thread: called at the start of every tick. The engine uses it to tell the mixer
        which channels go silent and the tuner which sockets are guitars. */
    std::function<void()> beforeTick;
    /** The sockets "whichever input is being played" may choose from, one bit each; 0 = any. */
    void setCandidates (juce::uint32 mask) noexcept     { candidates.store (mask); }

    struct Reading
    {
        bool heard = false;         // a steady note is being played
        double hz = 0.0;
        int midi = 0;               // the nearest note (69 = the A above middle C)
        double cents = 0.0;         // how far from it, -50 to 50; minus is flat
        juce::String name;          // "E2", "F#3"
        float level = 0.0f;         // how loud the socket is, 0 to 1
        int socket = -1;            // the socket it is listening to now, 0-based
        bool strobing = false;      // the strobe has locked on to the note
        double strobe = 0.0;        // cycles the sound has slipped against the note's exact frequency since the note began; grows while sharp, shrinks while flat
        float strength = 0.0f;      // how strongly the note is there, 0 to 1 (the strobe's brightness)
    };

    /** Works out the note from what has arrived. Call about 20 times a second. */
    void tick();
    Reading reading() const                             { return shown; }

    /** "E2" for 40. */
    static juce::String noteName (int midi);
    /** The note and the cents for a frequency. */
    static void nearestNote (double hz, double referenceHz, int& midi, double& cents) noexcept;

    /** One line: "Tuner: A2, 3 cents flat (109.8 Hz), input 1". */
    juce::String statusLine() const;
    juce::String reportText() const;

    juce::String stateText() const;
    void loadStateText (const juce::String&);
    static juce::File stateFile();
    void saveState();
    /** False while the self-test runs, so it never writes over the real memory file. */
    static inline std::atomic<bool> memoryOnDisk { true };

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate);
    void hear (const float* const* inputs, int numIns, int numSamples) noexcept;
    /** What should be silent in this block: nothing while the tuner is off. */
    Silence silenceNow() const noexcept                 { return on.load (std::memory_order_relaxed) ? (Silence) quiet.load (std::memory_order_relaxed) : Silence::nothing; }

    //==============================================================================
    /** The frequency of a steady note in `x` (samples at `rate`), or 0. `clarity` is how sure
        it is, 0 to 1. Public so the self-test can check it. */
    static double pitchOf (const float* x, int n, double rate, double& clarity);

    /** The strobe's measurement: how the last `n` samples of `x` line up with a steady wave of
        `hz`. `endsAt` is how many samples the whole stream has had by the end of `x`, so one
        call's answer can be compared with the next. Returns the phase in cycles (0 to 1) and
        sets `size` to how strongly that frequency is there (the amplitude of a sine of it). */
    static double phaseAgainst (const float* x, int n, double rate, double hz, juce::int64 endsAt, double& size);

private:
    static constexpr int window = 4096;         // what the note is worked out from
    static constexpr int kept = 8192;           // what is kept: the strobe wants eight cycles of a low bass string
    static constexpr int maxSockets = 32;

    void restStrobe();
    void strobeStep (double steadyHz, int newSamples);

    std::atomic<bool> on { false }, monitorsKeep { true };
    std::atomic<int> quiet { (int) Silence::guitars };
    std::atomic<juce::uint32> candidates { 0 };
    std::atomic<int> wanted { -1 }, listening { -1 };
    std::atomic<float> levelNow { 0.0f };
    std::atomic<double> thinRate { 24000.0 };
    std::atomic<int> flush { 0 };

    juce::AbstractFifo fifo { 16384 };
    std::vector<float> ring;

    // ---- audio thread ----
    int factor = 4, held = 0, flushSeen = 0, current = -1, quietFor = 0;
    float sum = 0.0f;
    double rate = 96000.0;

    // ---- message thread ----
    std::vector<float> recent;          // the last `kept` thinned samples
    int filled = 0;
    juce::int64 total = 0;              // thinned samples brought in since the tuner was switched on
    int sinceNote = 0, silentSamples = 0, idleTicks = 0;
    std::array<double, 3> last {};      // the last three notes heard, for a steady figure
    int lastCount = 0;
    double steady = 0.0;                // the note as last worked out (the middle of the last three)
    bool fills = true;
    juce::StringArray silentByHand, liveByHand;     // channel names, lower case

    // The strobe. `turn` is the unwrapped phase of the partial it follows, in cycles.
    struct Mark { juce::int64 at; double turn; };
    int strobeMidi = -1, strobePartial = 1;
    double strobeTurn = 0.0, strobeRaw = 0.0, strobeBase = 0.0;
    bool clearNow = false;              // the last look found a clear note
    juce::int64 strobeAt = 0;
    std::vector<Mark> marks;
    double refHz = 440.0;
    Reading shown;
    bool dirty = false;
    int readings = 0, timesOn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tuner)
};

} // namespace gigrig
