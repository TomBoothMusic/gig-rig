#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>

namespace gigrig
{

/** What the glitch check has found so far. */
struct GlitchStats
{
    enum class State { off, listening, locked, notHeard };

    State state = State::off;
    int input = -1;                 // the input the tone is being heard on
    bool analogue = false;          // through a socket (a cable), not the copy inside the mixer
    int glitches = 0;               // glitches since the count was last reset: breaks less than a second apart count as one
    int breaks = 0;                 // every separate break, however close together
    double seconds = 0.0;           // how long the tone has been watched
    double lostSeconds = 0.0;       // how long the tone was missing altogether
    float levelDb = -100.0f;        // how loud the tone came back
    float backgroundPercent = 0.0f; // everything that is not the tone, as a share of it
    std::vector<double> times;      // when the first few glitches started, in seconds
    std::vector<int> breaksEach;    // and how many breaks each was made of
};

/** Sends a steady two-note tone and listens for it coming back.

    Both notes are known exactly, so everything that is not those two notes can
    be cancelled out sample by sample. What is left is close to silence until
    the sound breaks: a dropped, repeated or silent stretch of audio shows up
    as a spike, whatever caused it. That catches glitches in the driver or the
    USB link that the computer's own timing never sees.

    process() is for the audio thread; everything else is for the message thread. */
class GlitchWatch
{
public:
    static constexpr int maxInputs = 16;
    static constexpr int maxTimes = 24;
    static constexpr double noteA = 1052.0, noteB = 1836.0;   // chosen so no likely glitch length hides in both
    static constexpr float noteLevel = 0.05f;                 // each note, about -26 dBFS

    /** analogueInputs: how many of the first inputs are sockets (the rest are copies from inside the mixer). */
    void prepare (double sampleRate, int numInputs, int analogueInputs);

    void setOn (bool shouldBeOn) noexcept;
    bool isOn() const noexcept                  { return on.load(); }
    void resetCount() noexcept;

    /** Writes the tone to toneOut and listens on every input. */
    void process (const float* const* inputs, int numInputs, float* toneOut, int numSamples) noexcept;

    GlitchStats stats() const;

private:
    struct Listener
    {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double sumSquares = 0.0, residualPeak = 0.0;

        double residual (double x, double coeffA, double coeffB) noexcept
        {
            const auto y = x - coeffA * x1 + x2;
            x2 = x1;  x1 = x;
            const auto e = y - coeffB * y1 + y2;
            y2 = y1;  y1 = y;
            return e;
        }
    };

    void beginListening() noexcept;
    void chooseInput() noexcept;

    double rate = 48000.0;
    int inputs = 0, sockets = 0;
    double coeffA = 0.0, coeffB = 0.0, stepA = 0.0, stepB = 0.0;

    std::atomic<bool> on { false }, restartRequested { false }, countResetRequested { false };

    // ---- audio thread only ----
    std::array<Listener, maxInputs> listeners;
    double phaseA = 0.0, phaseB = 0.0, fade = 0.0;
    int listenSamples = 0, measuredSamples = 0;
    bool lockedNow = false;
    int chosen = -1;
    double reference = 0.0, envelope = 0.0;
    int holdOff = 0, lostRun = 0;
    juce::int64 watchedSamples = 0, lostSamples = 0, lastBreakAt = -1;

    // ---- written by the audio thread, read by the screen ----
    std::atomic<int> state { (int) GlitchStats::State::off };
    std::atomic<int> shownInput { -1 };
    std::atomic<int> glitchCount { 0 }, breakCount { 0 };
    std::atomic<double> shownSeconds { 0.0 }, shownLostSeconds { 0.0 };
    std::atomic<float> shownLevelDb { -100.0f }, shownBackground { 0.0f };
    std::array<std::atomic<float>, maxTimes> glitchTimes {};
    std::array<std::atomic<int>, maxTimes> glitchBreaks {};
};

} // namespace gigrig
