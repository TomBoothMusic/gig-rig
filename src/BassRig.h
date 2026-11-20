#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "BassMachine.h"

#include <atomic>
#include <memory>

namespace gigrig
{

/** The Bass9 test bench inside the engine: the app's own bass made from the Bass9's
    dry output, the real Bass9 passed through beside it, a switch between the two on
    one pair of returns, level figures for matching them, and a recorder that captures
    the dry guitar and both basses together for tuning afterwards. */
class BassRig
{
public:
    /** What goes out on the bass returns. */
    enum class Listen { off, real, app };

    BassRig();
    ~BassRig();

    //==============================================================================
    // Message thread (the pedal's thread is fine for setListen / toggle).

    void setSettings (const BassSettings& s);
    BassSettings getSettings() const;

    void setListen (Listen l) noexcept              { listenMode.store ((int) l); }
    Listen listen() const noexcept                  { return (Listen) listenMode.load(); }

    /** Real -> app -> real. Off stays off. */
    void toggle() noexcept;

    /** Indexes from 0: input 9 is socket 10 on the CQ. */
    void setDryInput (int index) noexcept           { dryIn.store (index); }
    int dryInput() const noexcept                   { return dryIn.load(); }
    void setRealInput (int index) noexcept          { realIn.store (index); }
    int realInput() const noexcept                  { return realIn.load(); }

    /** The first return of the stereo pair the bass goes out on (the same mono bass on both). */
    void setOutputPair (int firstReturn) noexcept   { outPair.store (firstReturn); }
    int outputPair() const noexcept                 { return outPair.load(); }

    /** Average levels while the guitar was being played, since the last call. */
    struct Levels
    {
        double appDb = -100.0, realDb = -100.0;
        double playedSeconds = 0.0;
    };
    Levels takeLevels() noexcept;

    /** Peaks since the last call, 0..1: dry guitar, real Bass9, app bass. */
    float takePeak (int which) noexcept             { return peaks[(size_t) which].exchange (0.0f); }

    /** Starts recording dry guitar, real Bass9 and app bass to a 3-channel WAV in the folder,
        with the bass settings beside it in a .txt. Returns an error, or an empty string. */
    juce::String startCapture (const juce::File& folder);
    void stopCapture();
    bool isCapturing() const noexcept               { return capturing.load(); }
    double captureSeconds() const noexcept;
    juce::File lastCaptureFile() const              { return lastFile; }

    /** Longest capture before it stops by itself. */
    static constexpr double maxCaptureSeconds = 180.0;

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate, int maxBlock);

    /** Adds the chosen bass to the two outputs (either may be null). */
    void process (const float* const* inputs, int numIns, float* outA, float* outB, int numSamples) noexcept;

private:
    BassMachine machine;
    juce::SpinLock settingsLock;
    BassSettings shared;
    std::atomic<bool> settingsChanged { true };

    std::atomic<int> listenMode { (int) Listen::off };
    std::atomic<int> dryIn { 9 }, realIn { 8 }, outPair { 2 };

    std::atomic<double> appSum { 0.0 }, realSum { 0.0 };
    std::atomic<juce::int64> playedSamples { 0 };
    std::array<std::atomic<float>, 3> peaks {};

    std::vector<float> appBuffer, silence;
    double rate = 48000.0;
    double dryEnv = 0.0, envCoeff = 0.0;

    // ---- capture ----
    juce::TimeSliceThread writerThread { "Bass capture writer" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<bool> capturing { false };
    std::atomic<juce::int64> capturedSamples { 0 };
    juce::File lastFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassRig)
};

} // namespace gigrig
