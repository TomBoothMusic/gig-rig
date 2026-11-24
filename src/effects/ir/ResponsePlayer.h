#pragma once

#include "../Effect.h"
#include "../HalfRate.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace gigrig::fx
{

/** "ir": plays the sound through a response file (an "impulse response", IR): a short WAV
    that holds everything a tone-only box does. Made for a capture of Tom's TC BodyRez (the
    Amp capture tab can make the file by itself, see docs/effects.md), and just as good for
    a microphone-in-front-of-the-guitar response or a speaker cabinet.

    Text setting "file": the name of a WAV in the responses folder (or a full path). The
    first 85 ms of the file are used (4096 points at 48 kHz); a body or cabinet response is
    much shorter than that. Mix blends the dry sound with the response (100% = the response
    alone); Level is the output level.

    How it works: every output sample is the last 4096 input samples each multiplied by one
    point of the file and added up, done at 48 kHz (inside 96 kHz it goes down and back up
    like the amp model, which costs a few samples of delay, reported truthfully). With no
    file chosen it is a straight wire.

    Loading happens on the message thread; the audio thread only swaps pointers, with the
    sound dipped for a few milliseconds around the swap so a new file never clicks. */
class ResponsePlayer final : public Effect
{
public:
    static constexpr int maxTaps = 4096;

    ResponsePlayer();
    ~ResponsePlayer() override;

    std::string typeId() const override                 { return "ir"; }
    int latencySamples() const noexcept override        { return bridge.latencySamples(); }
    double cpuBudgetPercent() const noexcept override   { return 8.0; }

    std::vector<std::string> textKeys() const override  { return { "file" }; }
    void setText (const std::string& key, const std::string& value) override;
    std::string getText (const std::string& key) const override;
    std::string status() const override;

    /** Where file names are looked up (Documents/GigRig/Responses). Set once at start. */
    static void setFolder (const std::string& folder);
    static std::string folder();

private:
    struct Loaded
    {
        std::vector<float> taps;    // at the inner rate
        std::string name;
    };

    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    /** Reads the file and makes the points for the rate the convolution runs at. */
    std::unique_ptr<Loaded> load (const std::string& nameOrPath, double innerRate, std::string& error) const;
    void runInner (float* x, int m) noexcept;

    int pMix, pLevel;
    std::string fileName, loadStatus = "no response file chosen (a straight wire)";
    double innerRate = 48000.0;
    bool isPrepared = false;

    std::unique_ptr<Loaded> current;            // audio thread's own (set before audio starts, or swapped in below)
    std::atomic<Loaded*> pending { nullptr };   // message thread -> audio thread
    std::atomic<Loaded*> retired { nullptr };   // audio thread -> message thread (deleted there)
    float dip = 1.0f;                           // the wet sound's own fader around a swap
    bool swapping = false;

    HalfRate bridge;
    std::vector<float> history;                 // the last maxTaps inner samples, written twice so a read never wraps
    int writeAt = 0;
};

} // namespace gigrig::fx
