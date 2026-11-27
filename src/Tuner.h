#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>

namespace gigrig
{

/** The tuner (built 27 Nov 2026; the guide is docs/tuner.md, the design doc has it on switch 8:
    "TUNER: mutes main, in-ears live").

    While it is on it listens to one input socket as it arrives, before any effect, and says
    which note is being played and how far out it is in cents (hundredths of a semitone).
    With the mixer on, the main mix goes silent while it is on (the monitors stay live, and so
    does the break music); with the mixer off it changes no sound at all.

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

    /** Whether the mixer's main mix goes silent while the tuner is on. */
    void setMutes (bool m)                              { if (muting.exchange (m) != m) dirty = true; }
    bool mutes() const noexcept                         { return muting.load(); }

    struct Reading
    {
        bool heard = false;         // a steady note is being played
        double hz = 0.0;
        int midi = 0;               // the nearest note (69 = the A above middle C)
        double cents = 0.0;         // how far from it, -50 to 50; minus is flat
        juce::String name;          // "E2", "F#3"
        float level = 0.0f;         // how loud the socket is, 0 to 1
        int socket = -1;            // the socket it is listening to now, 0-based
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
    /** True while the main mix should be silent. */
    bool wantsSilence() const noexcept                  { return on.load (std::memory_order_relaxed) && muting.load (std::memory_order_relaxed); }

    //==============================================================================
    /** The frequency of a steady note in `x` (samples at `rate`), or 0. `clarity` is how sure
        it is, 0 to 1. Public so the self-test can check it. */
    static double pitchOf (const float* x, int n, double rate, double& clarity);

private:
    static constexpr int window = 4096;
    static constexpr int maxSockets = 32;

    std::atomic<bool> on { false }, muting { true };
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
    std::vector<float> recent;          // the last `window` thinned samples
    int filled = 0;
    std::array<double, 3> last {};      // the last three notes heard, for a steady figure
    int lastCount = 0, silentTicks = 0;
    double refHz = 440.0;
    Reading shown;
    bool dirty = false;
    int readings = 0, timesOn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tuner)
};

} // namespace gigrig
