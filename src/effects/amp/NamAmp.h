#pragma once

#include "../Effect.h"
#include "../Oversampler.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace nam { class DSP; }

namespace gigrig::fx
{

/** "amp": plays a Neural Amp Modeler model (a .nam file), such as the ones trained from
    the Amp capture tab (Tom's Dream '65 settings). The model is the amp and its cab together.

    How it works: the model runs at the rate it was trained at (48 kHz). When the app runs
    at 96 kHz, the sound is taken down to 48 kHz and back up around the model with the same
    half-band filters the drives use, which costs a few samples of delay.

    Settings: input (how hard the model is driven; 0 dB = as hard as during the capture,
    when socket 10's gain is unchanged), output, and the text setting "model": the file name
    of a .nam file in the models folder (or a full path). Changing model fades across
    over 20 ms, so it never clicks.

    Loading happens on the message thread; the audio thread only swaps pointers. */
class NamAmp final : public Effect
{
public:
    NamAmp();
    ~NamAmp() override;

    std::string typeId() const override             { return "amp"; }
    int latencySamples() const noexcept override    { return latency; }
    double cpuBudgetPercent() const noexcept override { return 45.0; }

    std::vector<std::string> textKeys() const override { return { "model" }; }
    void setText (const std::string& key, const std::string& value) override;
    std::string getText (const std::string& key) const override;
    std::string status() const override;

    /** Where model file names are looked up (Documents/GigRig/Amp models). Set once at start. */
    static void setModelFolder (const std::string& folder);
    static std::string modelFolder();
    /** The model a new amp starts with when it is given none (the first of Tom's captures). */
    static void setDefaultModel (const std::string& nameOrPath);

private:
    struct Model;

    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    std::unique_ptr<Model> load (const std::string& nameOrPath, std::string& error) const;
    void takeOutTheBin();
    void runModel (Model& m, const float* in, float* out, int n) noexcept;

    int pInput, pOutput;
    std::string modelName, loadStatus = "no model chosen";

    std::unique_ptr<Model> current;             // audio thread's own (set before audio starts, or swapped in below)
    std::atomic<Model*> pending { nullptr };    // message thread -> audio thread
    std::atomic<Model*> retired { nullptr };    // audio thread -> message thread (deleted there)
    Model* fadingOut = nullptr;                 // audio thread
    int fadeLeft = 0, fadeLength = 1;

    int factor = 1;                             // 2 at 88.2/96 kHz: the model runs at half the rate
    HalfBand down, up;
    float carry = 0.0f;                         // a sample waiting for its pair when the factor is 2
    bool haveCarry = false;
    int hereCount = 0;                          // upsampled samples waiting to go out
    std::vector<float> inner, innerOut, oldOut, here;
    OnePole dcBlock;
    int latency = 0;
};

} // namespace gigrig::fx
