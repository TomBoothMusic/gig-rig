#pragma once

#include "Effect.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gigrig::fx
{

/** What a chain holds, as plain data: the message thread's copy, and what presets save. */
struct SlotModel
{
    std::string type;                                           // registry id
    bool on = true;
    std::vector<std::pair<std::string, float>> params;          // id -> value (missing ones keep defaults)
    std::vector<std::pair<std::string, std::string>> texts;     // text settings

    float param (const std::string& id, float fallback) const;
    void setParam (const std::string& id, float value);
    std::string text (const std::string& key) const;
    void setText (const std::string& key, const std::string& value);
};

struct ChainModel
{
    std::vector<SlotModel> slots;

    /** A small text file anyone can read:  slot=ts / on=1 / drive=5 ... one line each.
        Lines starting with # are notes. */
    std::string toText() const;
    /** Unknown effect types are left out (listed in skipped); unknown settings are ignored. */
    static ChainModel fromText (const std::string& text, std::vector<std::string>* skipped = nullptr);
};

/** A row of effects, one after another, running on the audio thread.

    Turning a slot on or off fades it in or out over 10 ms. Any change to the row itself
    (adding, removing, reordering, changing a type) builds a fresh set of effects on the
    message thread and the audio thread fades from the old set to the new one over 10 ms,
    so nothing ever clicks and the audio thread never waits or allocates. */
class Chain
{
public:
    static constexpr int maxSlots = 8;

    Chain();
    ~Chain();

    // ---- message thread ----
    /** Only while audio is stopped (or in the self-test between blocks). Builds the effects at once. */
    void prepare (double sampleRate, int maxBlockSize);
    void setModel (const ChainModel& m);
    const ChainModel& model() const noexcept        { return current; }
    void setSlotOn (int slot, bool on);
    void setParam (int slot, int index, float value);
    void setText (int slot, const std::string& key, const std::string& value);
    /** The effect in a slot as it will run (null if none). Message thread: for reading its settings. */
    Effect* effect (int slot) const;
    /** Frees effect sets the audio thread has finished with. Call often (a timer). */
    void collectGarbage();
    /** Delay through the slots that are on. */
    int latencySamples() const;

    // ---- audio thread ----
    void process (float* buffer, int numSamples) noexcept;
    void reset() noexcept;

private:
    struct Slot
    {
        std::unique_ptr<Effect> fx;
        std::atomic<bool> on { true };
        float mix = 1.0f;
    };

    struct Built
    {
        std::vector<std::unique_ptr<Slot>> slots;
    };

    Built* build (const ChainModel& m) const;
    void submit (Built* b);
    void runBuilt (Built& b, float* buffer, int numSamples) noexcept;
    void freeAll();

    ChainModel current;
    double rate = 48000.0;
    int maxBlock = 512;

    // Handed between threads. Message thread owns pending until the audio thread takes it,
    // and retired once the audio thread lets go of it.
    std::atomic<Built*> pending { nullptr }, retired { nullptr };
    Built* latest = nullptr;        // newest set handed over (message thread's view)
    Built* live = nullptr;          // audio thread
    Built* fading = nullptr;        // audio thread: the old set during a change-over
    int fadePos = 0, fadeLength = 480;

    std::vector<float> wet, old;
};

} // namespace gigrig::fx
