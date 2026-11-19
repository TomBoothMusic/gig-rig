#include "LatencyProbe.h"

namespace gigrig
{

namespace
{
    constexpr double pingSeconds   = 0.015;
    constexpr double listenSeconds = 0.35;    // longest round trip that can be measured, plus the ping
    constexpr float  pingPeak      = 0.25f;   // -12 dBFS
    constexpr float  silentPeak    = 1.0e-4f; // -80 dBFS: below this an input is treated as silent
    constexpr float  clearMatch    = 0.5f;
}

std::vector<float> LatencyProbe::makePing (double sampleRate)
{
    // A sweep from 1 kHz to 8 kHz. A sweep lines up with itself at exactly one
    // offset, so it can be found to the sample even with hum or noise around it.
    const auto length = juce::jmax (64, juce::roundToInt (sampleRate * pingSeconds));
    const double startHz = 1000.0, endHz = 8000.0;
    const double seconds = length / sampleRate;

    std::vector<float> samples ((size_t) length);

    for (int i = 0; i < length; ++i)
    {
        const double t = i / sampleRate;
        const double phase = juce::MathConstants<double>::twoPi
                               * (startHz * t + (endHz - startHz) * t * t / (2.0 * seconds));
        const double fade = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
        samples[(size_t) i] = (float) (pingPeak * fade * std::sin (phase));
    }

    return samples;
}

void LatencyProbe::prepare (double sampleRate, int numInputs)
{
    state.store (idle);
    ping = makePing (sampleRate);
    windowLength = juce::roundToInt (sampleRate * listenSeconds);
    recording.assign ((size_t) juce::jmax (0, numInputs), std::vector<float> ((size_t) windowLength, 0.0f));
    position = 0;
}

bool LatencyProbe::fire()
{
    if (isSounding() || recording.empty())
        return false;

    state.store (armed);
    return true;
}

void LatencyProbe::process (const float* const* inputs, int numInputs, float* pingOut, int numSamples) noexcept
{
    juce::FloatVectorOperations::clear (pingOut, numSamples);

    auto expected = (int) armed;

    if (state.compare_exchange_strong (expected, running))
        position = 0;

    if (state.load() != running)
        return;

    const auto count = juce::jmin (numSamples, windowLength - position);
    const auto pingLength = (int) ping.size();

    if (position < pingLength)
        juce::FloatVectorOperations::copy (pingOut, ping.data() + position, juce::jmin (count, pingLength - position));

    for (size_t ch = 0; ch < recording.size(); ++ch)
    {
        auto* dest = recording[ch].data() + position;

        if ((int) ch < numInputs && inputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (dest, inputs[ch], count);
        else
            juce::FloatVectorOperations::clear (dest, count);
    }

    position += count;

    if (position >= windowLength)
    {
        expected = running;
        state.compare_exchange_strong (expected, captured);
    }
}

bool LatencyProbe::findPing (const float* recorded, int length, const std::vector<float>& ping, PingHit& hit)
{
    const auto n = (int) ping.size();
    hit.delaySamples = 0;
    hit.match = 0.0f;
    hit.levelDb = -100.0f;

    if (n == 0 || length < n)
        return false;

    const auto range = juce::FloatVectorOperations::findMinAndMax (recorded, length);
    const auto peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    hit.levelDb = juce::Decibels::gainToDecibels (peak, -100.0f);

    if (peak < silentPeak)
        return false;

    double pingEnergy = 0.0;

    for (auto s : ping)
        pingEnergy += (double) s * s;

    const auto energyOf = [recorded, n] (int from)
    {
        double e = 0.0;

        for (int i = 0; i < n; ++i)
            e += (double) recorded[from + i] * recorded[from + i];

        return e;
    };

    // Slide the ping along the recording. At each offset, score how alike the
    // two are from 0 to 1, whatever the level it came back at.
    const double quietest = n * (double) silentPeak * silentPeak;
    const float* p = ping.data();
    double energy = energyOf (0);
    double best = 0.0;
    int bestOffset = 0;

    for (int offset = 0; offset + n <= length; ++offset)
    {
        if ((offset & 4095) == 4095)
            energy = energyOf (offset);   // stop rounding errors building up

        if (energy > quietest)
        {
            const float* r = recorded + offset;
            float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
            int i = 0;

            for (; i + 4 <= n; i += 4)
            {
                a += p[i] * r[i];
                b += p[i + 1] * r[i + 1];
                c += p[i + 2] * r[i + 2];
                d += p[i + 3] * r[i + 3];
            }

            for (; i < n; ++i)
                a += p[i] * r[i];

            const double score = std::abs ((double) a + b + c + d) / std::sqrt (pingEnergy * energy);

            if (score > best)
            {
                best = score;
                bestOffset = offset;
            }
        }

        if (offset + n < length)
            energy = juce::jmax (0.0, energy + (double) recorded[offset + n] * recorded[offset + n]
                                             - (double) recorded[offset] * recorded[offset]);
    }

    hit.delaySamples = bestOffset;
    hit.match = (float) best;
    return best >= clearMatch;
}

std::vector<PingHit> LatencyProbe::analyse()
{
    std::vector<PingHit> hits;

    if (state.load() != captured)
        return hits;

    for (size_t ch = 0; ch < recording.size(); ++ch)
    {
        PingHit hit;
        hit.input = (int) ch;

        if (findPing (recording[ch].data(), windowLength, ping, hit))
            hits.push_back (hit);
    }

    state.store (idle);
    return hits;
}

} // namespace gigrig
