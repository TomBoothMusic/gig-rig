#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "GlitchWatch.h"
#include "LatencyProbe.h"
#include "Looper.h"

#include <array>
#include <atomic>
#include <vector>

namespace gigrig
{

/** The rig sends 8 signals back to the mixer ("returns"). On the CQ12T they
    travel on USB channels 9 to 16, which arrive on mixer channels 9 and 10,
    ST1, the USB stereo channel and the Bluetooth stereo channel. */
constexpr int numReturns = 8;
constexpr int numMeteredInputs = 16;

/** Which device output carries a return, or -1 if the device has too few. */
int returnToOutput (int returnIndex, int numOutputs) noexcept;

/** "Return 3 (ST1 left)": the return and the CQ channel it lands on. */
juce::String returnName (int returnIndex);

/** "input socket 10" or "inside the mixer (USB channel 15)" on the CQ;
    plain "input 3" on anything else. */
juce::String inputName (int inputIndex, bool isCQ);

/** What happens to the inputs while nothing is being measured. */
enum class PlayThrough
{
    off,        // inputs are only metered
    straight,   // input N goes back out on return N
    mixToUsb    // inputs 1 to 10 (every input socket on the CQ12T) are mixed to the CQ's USB stereo channel (returns 5 and 6)
};

struct DeviceChoice
{
    juce::String typeName, deviceName;

    bool operator== (const DeviceChoice& other) const   { return typeName == other.typeName && deviceName == other.deviceName; }
    juce::String label() const                          { return deviceName + "  (" + typeName + ")"; }
};

/** One input's timings over a run of pings. */
struct InputTiming
{
    int input = 0;
    int pingsHeard = 0;
    int medianSamples = 0, minSamples = 0, maxSamples = 0;
    float levelDb = -100.0f;
};

/** One line of the latency table: a run of pings at one buffer size. */
struct LatencyRow
{
    int bufferSize = 0;
    double sampleRate = 0.0;
    int pingsSent = 0;
    int driverSamples = 0;              // input + output latency the driver itself reports
    std::vector<InputTiming> heard;     // every input the ping came back on
    int best = -1;                      // index into heard: the answer, or -1 if not heard
    bool bestIsAnalogue = false;        // came back through a socket, not inside the mixer
    int acousticInput = -1;             // a socket that heard the ping through the air (a microphone near a speaker), set aside
    int acousticSamples = 0;            // and how long that took
    int dropouts = 0;                   // driver-reported dropouts plus late callbacks during the run
    juce::String problem;

    double ms (int samples) const       { return sampleRate > 0.0 ? samples * 1000.0 / sampleRate : 0.0; }
};

/** Boils a run of pings down to one row. Split out so it can be checked without hardware. */
LatencyRow summarisePings (const std::vector<std::vector<PingHit>>& pings, int numInputs, bool isCQ, double sampleRate = 48000.0);

/** One line of the driver's delay history: what the driver said its delay was, and when that was first seen. */
struct DriverDelayNote
{
    int rate = 0, buffer = 0, inSamples = 0, outSamples = 0;
    juce::int64 firstSeenMs = 0;

    int total() const noexcept          { return inSamples + outSamples; }
    double ms() const noexcept          { return rate > 0 ? total() * 1000.0 / rate : 0.0; }
};

/** The lowest delay the driver has reported before at this sample rate and buffer size, when the
    figure now is well above it (more than safe mode alone adds). Null when there is nothing to warn about. */
const DriverDelayNote* driverDelayGrown (const std::vector<DriverDelayNote>& history, int rate, int buffer, int totalNow);

struct DropoutStats
{
    double seconds = 0.0;       // how long the counters have been running
    int driverDropouts = 0;     // what the driver itself counted; -1 if it does not count
    int lateCallbacks = 0;      // times the computer kept the audio waiting
    double worstGapMs = 0.0;    // longest wait between two blocks of audio
    double expectedGapMs = 0.0;
    double peakLoad = 0.0;      // busiest block: share of the available time used, 0..1
};

/** The stage 1 audio engine: opens one audio device with every channel,
    passes audio through, measures round-trip latency and watches for dropouts. */
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    //==============================================================================
    // Everything in this block is for the message thread.

    /** Devices that have both inputs and outputs. Rescans the drivers. */
    juce::Array<DeviceChoice> listDevices();

    /** Opens a device with all its channels at 48 kHz (bufferSize 0 = the driver's own choice).
        Returns an error message, or an empty string if audio is running. */
    juce::String open (const DeviceChoice& choice, int bufferSize = 0);
    void close();

    /** A pretend device for the self-test: the test calls processBlock itself. */
    void openSimulated (double sampleRate, int bufferSize, int numInputs, int numOutputs);

    bool isOpen() const noexcept                { return device != nullptr || simulated; }
    DeviceChoice currentChoice() const          { return choice; }
    bool isCQ() const noexcept                  { return deviceIsCQ; }
    double sampleRate() const noexcept          { return currentSampleRate; }
    int bufferSize() const noexcept             { return currentBufferSize; }
    int numInputs() const noexcept              { return currentInputs; }
    int numOutputs() const noexcept             { return currentOutputs; }
    int driverLatencySamples() const;
    juce::Array<int> availableBufferSizes() const;
    juce::String lastError() const              { return errorText; }
    bool hasControlPanel() const;
    void showControlPanel();

    /** Sends a run of pings at each buffer size in turn (an empty list means the
        current size only), then goes back to the size it started on. */
    void measure (std::vector<int> bufferSizes);
    bool isMeasuring() const noexcept           { return job.active; }
    juce::String measuringStatus() const;
    const std::vector<LatencyRow>& latencyRows() const noexcept  { return rows; }

    /** Call about 20 times a second: moves a measurement along. */
    void tick();

    void setPingReturns (int returnMask) noexcept   { pingMask.store (returnMask); }
    int pingReturns() const noexcept                { return pingMask.load(); }

    void setPlayThrough (PlayThrough mode) noexcept;
    PlayThrough playThrough() const noexcept        { return (PlayThrough) playMode.load(); }
    bool feedbackGuardTripped() const noexcept      { return guardTripped.load(); }

    /** Steady test tone on one return (0 to 7), or -1 for none. */
    void setToneReturn (int returnIndex) noexcept   { toneReturn.store (returnIndex); }
    int toneReturnIndex() const noexcept            { return toneReturn.load(); }

    /** The glitch check: a steady tone on the ping returns, and a count of every
        break in it as it comes back. Play-through and the return tone are silent while it runs. */
    void setGlitchCheck (bool shouldBeOn) noexcept;
    bool glitchCheckOn() const noexcept             { return glitch.isOn(); }

    /** Called (message thread) each time a glitch check starts. */
    std::function<void()> onGlitchCheckStarted;
    GlitchStats glitchStats() const                 { return glitch.stats(); }

    DropoutStats dropoutStats() const;
    void resetDropoutStats() noexcept;

    /** Highest level since the last call, 0..1. Inputs first, then the returns. */
    float takeInputPeak (int input) noexcept        { return inputPeaks[(size_t) input].exchange (0.0f); }
    float takeReturnPeak (int ret) noexcept         { return returnPeaks[(size_t) ret].exchange (0.0f); }

    /** The stage 1 part of the report Tom pastes back: the device and the
        latency table, then the play-through and dropout counts. */
    juce::String latencyText() const;
    juce::String dropoutText() const;
    juce::String reportText() const                 { return latencyText() + "\n" + dropoutText() + (extraReport != nullptr ? "\n" + extraReport() : juce::String()); }

    /** Later stages (the pedal, the looper) add their own part to the report through this. */
    std::function<juce::String()> extraReport;

    //==============================================================================
    /** The looper. It runs whenever a device is open, except during a latency
        measurement or a glitch check. Its loops come back on the returns. */
    Looper& looper() noexcept                       { return loopCore; }

    /** Where the looper's timing correction comes from. */
    struct LoopTiming
    {
        int samples = 0;            // what the looper is told
        int baseSamples = 0;        // before the trim
        juce::String source;        // "the latency test at this buffer size", "the driver's own figure"...
        bool measured = false;      // false: only the driver's own figure, not checked by a latency test
    };

    /** Works the correction out again and hands it to the looper. Message thread. */
    LoopTiming updateLoopTiming();

    /** Empty, or a sentence saying the driver's delay is well above what it was before at this
        same setting. Message thread. */
    juce::String driverDelayWarning();
    void setLoopTrimMs (double ms)                  { loopTrimMs = ms; updateLoopTiming(); }
    double getLoopTrimMs() const noexcept           { return loopTrimMs; }

    //==============================================================================
    /** The audio thread's work. Public so the self-test can drive it. */
    void processBlock (const float* const* inputs, int numIns, float* const* outputs, int numOuts, int numSamples) noexcept;

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String& message) override;

    void prepare (double rate, int block, int ins, int outs);
    juce::String reopen (int newBufferSize);
    int dropoutTotal() const;
    void startRow();
    void finishRow();
    void endJob();

    juce::OwnedArray<juce::AudioIODeviceType> types;
    std::unique_ptr<juce::AudioIODevice> device;
    DeviceChoice choice;
    bool simulated = false;
    bool deviceIsCQ = false;
    double currentSampleRate = 0.0;
    int currentBufferSize = 0, currentInputs = 0, currentOutputs = 0;
    juce::String errorText;

    // ---- shared with the audio thread ----
    LatencyProbe probe;
    GlitchWatch glitch;
    std::atomic<int> pingMask { 0b00110000 };   // returns 5 and 6: the CQ's USB stereo channel
    std::atomic<int> playMode { (int) PlayThrough::off };
    std::atomic<int> toneReturn { -1 };
    std::atomic<bool> measuring { false };
    std::atomic<bool> guardTripped { false };
    std::array<std::atomic<float>, numMeteredInputs> inputPeaks {};
    std::array<std::atomic<float>, numReturns> returnPeaks {};
    std::atomic<int> lateCallbacks { 0 };
    std::atomic<double> worstGapSeconds { 0.0 }, peakLoad { 0.0 };
    std::atomic<bool> statsResetRequested { true };

    // ---- audio thread only ----
    std::vector<float> scratch, mix;
    juce::int64 lastCallbackTicks = 0;
    int samplesSinceReset = 0;
    double tonePhase = 0.0;
    int hotSamples = 0;

    // ---- a measurement in progress (message thread) ----
    struct Job
    {
        enum class Phase { settle, listening, gap };

        bool active = false;
        std::vector<int> sizes;
        size_t index = 0;
        Phase phase = Phase::settle;
        int ticks = 0;
        int originalSize = 0;
        std::vector<std::vector<PingHit>> pings;
        int dropoutsAtStart = 0;
        juce::String rowProblem;
    };

    Job job;
    std::vector<LatencyRow> rows;
    Looper loopCore;
    double loopTrimMs = 0.0;

    // Measured corrections from this and earlier runs, kept in a small file so the latency
    // test need not be repeated every time the app is opened. One line per setting:
    // sample rate, buffer size, the driver's own latency figure (it changes with safe mode), samples.
    struct RememberedTiming { int rate, buffer, driver, samples; };
    std::vector<RememberedTiming> remembered;
    bool rememberedLoaded = false;
    void loadRemembered();
    void remember (int samples);

    // Every different delay figure the driver has given, with the date it was first seen,
    // so a delay that has crept up since an earlier day shows up without anyone reading numbers out.
    std::vector<DriverDelayNote> driverHistory;
    bool driverHistoryLoaded = false;
    void noteDriverDelay();
    juce::Time statsSince;
    int driverDropoutsAtReset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace gigrig
