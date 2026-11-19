#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

namespace gigrig
{

/** Where one ping was heard coming back in. */
struct PingHit
{
    int input = 0;            // input channel, counting from 0
    int delaySamples = 0;     // round trip: samples from sending the ping to hearing it
    float match = 0.0f;       // 0..1, how closely what came back matches the ping
    float levelDb = -100.0f;  // peak level of that input while listening
};

/** Measures round-trip latency: plays a short chirp out, records every input
    while it does, then finds how many samples later the chirp came back.

    The audio thread only plays and records (process). Finding the chirp is
    the slow part and happens on the message thread (analyse). */
class LatencyProbe
{
public:
    /** Call before audio starts, or while it is stopped. */
    void prepare (double sampleRate, int numInputs);

    /** Message thread: send one ping. False if one is still in the air. */
    bool fire();

    /** Message thread: true once the listening window after a ping is full. */
    bool captureReady() const noexcept     { return state.load() == captured; }

    /** True from fire() until the listening window is full. */
    bool isSounding() const noexcept       { const auto s = state.load(); return s == armed || s == running; }

    /** Message thread, after captureReady(): every input the ping came back on.
        The probe is ready for the next fire() afterwards. */
    std::vector<PingHit> analyse();

    /** Message thread: give up on a ping in the air. */
    void cancel() noexcept                 { state.store (idle); }

    /** Audio thread. Fills pingOut with the ping's samples for this block
        (zeros when no ping is playing) and records the inputs. */
    void process (const float* const* inputs, int numInputs, float* pingOut, int numSamples) noexcept;

    /** The chirp: a short sweep, quiet at both ends, peaking at -12 dBFS. */
    static std::vector<float> makePing (double sampleRate);

    /** Finds the ping in one recorded channel. Fills in hit.delaySamples,
        hit.match and hit.levelDb; returns true if the ping is clearly there. */
    static bool findPing (const float* recorded, int length, const std::vector<float>& ping, PingHit& hit);

private:
    enum State { idle, armed, running, captured };

    std::atomic<int> state { idle };
    std::vector<float> ping;
    std::vector<std::vector<float>> recording;   // one buffer per input
    int windowLength = 0;
    int position = 0;                            // audio thread only
};

} // namespace gigrig
