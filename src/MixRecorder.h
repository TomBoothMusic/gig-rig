#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "effects/Oversampler.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gigrig
{

/** Records the mixer's tracks to WAV files, one file per track, in a folder of their own
    (design doc, "Multitrack recording").

    Files are 24-bit. When the app runs at 88.2 or 96 kHz they are written at half that
    rate (48 kHz from 96), as Tom decided on 20 Nov 2026 to match his other recordings and
    halve the size; at 44.1 or 48 kHz they are written as they are.

    The audio thread only copies into a queue; a background thread writes to the drive. */
class MixRecorder
{
public:
    static constexpr int maxTracks = 12;

    struct Track
    {
        int slot = 0;               // which write() slot feeds it
        juce::String name;          // becomes the file name
        int channels = 1;           // 1 or 2
    };

    MixRecorder();
    ~MixRecorder();

    // ---- message thread ----
    /** Makes 'folder' and one WAV in it per track. Returns an error, or an empty string. */
    juce::String start (const juce::File& folder, const std::vector<Track>& tracks, double engineRate, int maxBlock);
    void stop();
    bool isRecording() const noexcept           { return recording.load(); }
    double seconds() const noexcept;
    double fileRate() const noexcept            { return writtenRate; }
    juce::File folder() const                   { return lastFolder; }
    juce::StringArray fileNames() const         { return lastFiles; }
    /** Samples that could not be queued because the drive was too slow (0 = a clean recording). */
    juce::int64 lostSamples() const noexcept    { return lost.load(); }
    juce::String statusText() const;

    // ---- audio thread ----
    /** True if this slot is being recorded now (so the caller need not prepare its audio otherwise). */
    bool wants (int slot) const noexcept        { return recording.load (std::memory_order_relaxed) && slot >= 0 && slot < maxTracks && active[(size_t) slot].load (std::memory_order_relaxed); }
    void write (int slot, const float* left, const float* right, int numSamples) noexcept;
    /** Once per block, after the writes. */
    void endBlock (int numSamples) noexcept;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        int channels = 1;
        std::array<fx::HalfBand, 2> down;
        std::array<float, 2> carry {};
        bool haveCarry = false;
        std::array<std::vector<float>, 2> half;
    };

    juce::TimeSliceThread thread { "Mixer recorder" };
    juce::SpinLock lock;                        // held by the message thread only while starting and stopping
    std::array<Slot, maxTracks> slots;
    std::array<std::atomic<bool>, maxTracks> active {};
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> samples { 0 }, lost { 0 };
    double engineRateNow = 48000.0, writtenRate = 48000.0;
    int factor = 1;
    juce::File lastFolder;
    juce::StringArray lastFiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixRecorder)
};

} // namespace gigrig
