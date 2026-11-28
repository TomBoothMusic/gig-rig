#pragma once

// The live frequency picture behind the EQ curve (an "RTA", built 28 Nov 2026 for the phone and
// tablet screen's EQ page; docs/mixer.md, "The EQ curve and its live picture").
//
// Two parts, plain C++ with no JUCE in them:
//
//   SpectrumTap    the audio thread copies the sound being looked at into a ring of samples.
//                  That copy is ALL the audio thread ever does for the picture: it reads the
//                  sound and never writes to it, so the picture cannot change a sample.
//   spectrumBands  the message thread takes the newest stretch from the ring and works out how
//                  loud each sixth of an octave is, from 20 Hz to 20 kHz.
//
// Checked by the mixer's part of the self-test (src/MixerSelfTest.cpp).

#include <atomic>
#include <cstdint>
#include <vector>

namespace gigrig
{

class SpectrumTap
{
public:
    static constexpr int ringSize = 1 << 16;    // 0.68 s at 96 kHz

    SpectrumTap() : ring ((size_t) ringSize, 0.0f) {}

    /** Audio thread. Adds n samples: a alone, or the middle of a and b when b is given. */
    void write (const float* a, const float* b, int n) noexcept
    {
        uint32_t at = head.load (std::memory_order_relaxed);

        if (b == nullptr)
            for (int i = 0; i < n; ++i)
                ring[(size_t) ((at + (uint32_t) i) & mask)] = a[i];
        else
            for (int i = 0; i < n; ++i)
                ring[(size_t) ((at + (uint32_t) i) & mask)] = 0.5f * (a[i] + b[i]);

        head.store (at + (uint32_t) n, std::memory_order_release);
    }

    /** Message thread. Copies the newest `count` samples, oldest first. The writer is always
        ahead of what is read here, so the two never touch the same sample unless the writer goes
        all the way round during the copy; then the copy is simply made again. */
    void latest (float* dest, int count) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t end = head.load (std::memory_order_acquire);
            const uint32_t start = end - (uint32_t) count;

            for (int i = 0; i < count; ++i)
                dest[i] = ring[(size_t) ((start + (uint32_t) i) & mask)];

            if (head.load (std::memory_order_acquire) - end < (uint32_t) (ringSize - count))
                return;
        }
    }

    /** How many samples have gone in since the start (it wraps round after hours; only differences matter). */
    uint32_t written() const noexcept           { return head.load (std::memory_order_relaxed); }

private:
    static constexpr uint32_t mask = (uint32_t) ringSize - 1;
    std::vector<float> ring;
    std::atomic<uint32_t> head { 0 };
};

/** The bands of the picture: band i is centred on 20 Hz x 2^(i/6), so 61 of them reach 20.3 kHz. */
constexpr int spectrumBandCount = 61;
constexpr double spectrumLowHz = 20.0;
constexpr int spectrumBandsPerOctave = 6;
double spectrumBandHz (int band) noexcept;

/** How many samples spectrumBands wants at this sample rate (a power of two, about a third of a second). */
int spectrumWindow (double sampleRate) noexcept;

/** How loud each band is, in dB, where a sine at full scale reads 0 dB in its band and steady
    pink noise reads the same in every band (so the picture is level for a full, even sound, the
    way a mixer's RTA is). Silence reads `floorDb`. `samples` holds spectrumWindow (rate) of them.
    Below about 80 Hz a band is narrower than the analysis can separate, so neighbouring bands
    share what they see there: a single low note still reads its true level. */
std::vector<float> spectrumBands (const float* samples, double sampleRate, float floorDb = -100.0f);

} // namespace gigrig
