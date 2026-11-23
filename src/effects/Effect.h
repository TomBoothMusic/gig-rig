#pragma once

// The one interface every effect in the rig follows. Read CLAUDE.md, "Adding an effect",
// and docs/effects.md before changing anything here: songs and presets refer to effects
// and their settings by their ids, so ids must never change once released.

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace gigrig::fx
{

/** Chains carry at most this many channels (1 today; 2 once stereo effects arrive). */
constexpr int maxChannels = 2;

/** One knob or switch of an effect. */
struct ParamInfo
{
    enum class Kind { continuous, toggle, choice };

    std::string id;         // stable name used in presets and songs: never rename, add a new one instead
    std::string label;      // what the screen shows
    std::string unit;       // "dB", "ms", "%", "" ...
    float min = 0.0f, max = 1.0f, def = 0.5f;
    Kind kind = Kind::continuous;
    std::vector<std::string> choices;   // for Kind::choice: value 0 = choices[0] and so on
    float smoothingMs = 80.0f;          // a knob swept end to end glides there over this long (0 = at once)

    /** "3.5 dB", "On", "Built-in": the value as the screen should show it. */
    std::string format (float value) const;
};

/** An effect: a sound processor with named settings.

    Threads:
      - Message thread: construction, setParam / getParam, setText, prepare().
      - Audio thread: process() and reset() only. process() must not allocate memory,
        take locks, touch files or the network, or log.

    Settings are handed over lock-free: setParam() stores the new target, and process()
    glides each value there at a steady pace (smoothingMs for the whole range) in steps of at most 16 samples, so moving a
    knob never clicks. Effects read the gliding value with value(). */
class Effect
{
public:
    virtual ~Effect() = default;

    /** The effect's stable type id ("ts", "speaker"...), as in the registry. */
    virtual std::string typeId() const = 0;

    const std::vector<ParamInfo>& params() const noexcept   { return infos; }
    int paramIndex (const std::string& id) const noexcept;

    // ---- message thread ----
    void setParam (int index, float value) noexcept;
    float getParam (int index) const noexcept;
    bool setParam (const std::string& id, float value) noexcept;

    /** Settings that are text, such as a file to load. Keys are stable like param ids. */
    virtual std::vector<std::string> textKeys() const       { return {}; }
    virtual void setText (const std::string& /*key*/, const std::string& /*value*/) {}
    virtual std::string getText (const std::string& /*key*/) const { return {}; }
    /** A short line on how the text settings went ("loaded cab.wav, 1.2 s"), for the screen and report. */
    virtual std::string status() const                      { return {}; }

    /** Before audio starts, and again whenever the rate or block size changes. May allocate. */
    void prepare (double sampleRate, int maxBlockSize);

    // ---- audio thread ----
    /** Clears delay lines and filter memory (the knobs keep their values). */
    void reset() noexcept;

    /** Processes numChannels (1 or 2) channels in place. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Delay the effect adds, in samples at the prepared rate. Must be the truth: the self-test checks it. */
    virtual int latencySamples() const noexcept             { return 0; }

    /** Most of one audio block this effect may use, in percent, at 96 kHz / 64 samples on the
        build machine. The self-test fails the build above it. */
    virtual double cpuBudgetPercent() const noexcept        { return 5.0; }

    /** True for effects meant to make sound on their own (a tail, a tone). */
    virtual bool makesSoundFromSilence() const noexcept     { return false; }

    double sampleRate() const noexcept                      { return rate; }

protected:
    /** Call from the constructor, once per setting, in the order they show on screen. */
    int addParam (ParamInfo info);
    int addParam (std::string id, std::string label, std::string unit, float min, float max, float def, float smoothingMs = 80.0f);
    int addToggle (std::string id, std::string label, bool def);
    int addChoice (std::string id, std::string label, std::vector<std::string> choices, int def);

    /** The current (gliding) value. Audio thread. */
    float value (int index) const noexcept                  { return smooth[(size_t) index].current; }
    /** True if any setting moved since the last processChunk. */
    bool anyChanged() const noexcept                        { return changedFlag; }

    virtual void onPrepare (double sampleRate, int maxBlockSize) = 0;
    virtual void onReset() noexcept = 0;
    /** At most 16 samples at a time. */
    virtual void processChunk (float* const* channels, int numChannels, int numSamples) noexcept = 0;

    static constexpr int chunk = 16;

private:
    struct Smooth
    {
        float current = 0.0f;
        float coeff = 1.0f;     // most it moves per chunk
    };

    std::vector<ParamInfo> infos;
    std::unique_ptr<std::atomic<float>[]> targets;
    std::vector<Smooth> smooth;
    double rate = 48000.0;
    bool changedFlag = true;
    bool prepared = false;
};

//==============================================================================
// Small tools for effects. Inline so they cost nothing.

inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept    { return g > 1.0e-9f ? 20.0f * std::log10 (g) : -180.0f; }

/** A biquad (RBJ cookbook), transposed direct form II, in double for low-frequency accuracy. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, maxChannels> z1 {}, z2 {};

    float run (float x, int ch) noexcept
    {
        const double y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void clear() noexcept   { z1 = {}; z2 = {}; }

    void lowPass   (double rate, double hz, double q = 0.7071);
    void highPass  (double rate, double hz, double q = 0.7071);
    void bandPass  (double rate, double hz, double q);          // 0 dB peak
    void peak      (double rate, double hz, double q, double db);
    void lowShelf  (double rate, double hz, double db, double slope = 1.0);
    void highShelf (double rate, double hz, double db, double slope = 1.0);
    void flat()    { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    /** Gain in dB at a frequency, for checks. */
    double responseDb (double rate, double hz) const;
};

/** First-order filters (6 dB per octave), as in most pedal circuits. */
struct OnePole
{
    double b0 = 1, b1 = 0, a1 = 0;
    std::array<double, maxChannels> x1 {}, y1 {};

    float run (float x, int ch) noexcept
    {
        const double y = b0 * x + b1 * x1[(size_t) ch] - a1 * y1[(size_t) ch];
        x1[(size_t) ch] = x;
        y1[(size_t) ch] = y;
        return (float) y;
    }

    void clear() noexcept   { x1 = {}; y1 = {}; }
    void lowPass  (double rate, double hz);
    void highPass (double rate, double hz);
};

} // namespace gigrig::fx
