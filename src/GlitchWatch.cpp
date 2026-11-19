#include "GlitchWatch.h"

#include <cmath>

namespace gigrig
{

namespace
{
    constexpr double skipSeconds = 0.3;         // let the tone make its first trip round before judging it
    constexpr double measureSeconds = 0.5;
    constexpr double quietestTone = 0.003;      // about -50 dBFS: any quieter and hiss would look like glitches
    constexpr double cleanEnough = 0.02;        // background must be under 2% of the tone to trust an input
    constexpr double spikeSize = 0.06;          // a break: what is left over jumps past 6% of the tone
    constexpr double holdOffSeconds = 0.1;      // one break often has two edges: count it once
    constexpr double sameGlitchSeconds = 1.0;   // breaks closer together than this are one glitch
    constexpr double lostBelow = 0.25;          // tone under a quarter of its level counts as missing
    constexpr double relistenAfterSeconds = 2.0;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

void GlitchWatch::prepare (double sampleRate, int numInputs, int analogueInputs)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    inputs = juce::jlimit (0, maxInputs, numInputs);
    sockets = juce::jlimit (0, inputs, analogueInputs);

    stepA = twoPi * noteA / rate;
    stepB = twoPi * noteB / rate;
    coeffA = 2.0 * std::cos (stepA);
    coeffB = 2.0 * std::cos (stepB);

    phaseA = phaseB = fade = 0.0;
    glitchCount.store (0);
    breakCount.store (0);
    watchedSamples = lostSamples = 0;
    lastBreakAt = -1;
    shownSeconds.store (0.0);
    shownLostSeconds.store (0.0);
    beginListening();
    state.store ((int) (on.load() ? GlitchStats::State::listening : GlitchStats::State::off));
}

void GlitchWatch::setOn (bool shouldBeOn) noexcept
{
    if (shouldBeOn == on.load())
        return;

    if (shouldBeOn)
    {
        // Nothing is listening while it is off, so the last run's result can be cleared from here.
        glitchCount.store (0);
    breakCount.store (0);
        shownSeconds.store (0.0);
        shownLostSeconds.store (0.0);
        restartRequested.store (true);
        countResetRequested.store (true);
        state.store ((int) GlitchStats::State::listening);
    }
    else
    {
        state.store ((int) GlitchStats::State::off);
    }

    on.store (shouldBeOn);
}

void GlitchWatch::resetCount() noexcept
{
    if (on.load())
    {
        countResetRequested.store (true);
        return;
    }

    glitchCount.store (0);
    breakCount.store (0);      // stopped: clear what the last run found
    shownSeconds.store (0.0);
    shownLostSeconds.store (0.0);
}

void GlitchWatch::beginListening() noexcept
{
    for (auto& listener : listeners)
        listener = {};

    listenSamples = measuredSamples = 0;
    lockedNow = false;
    chosen = -1;
    holdOff = lostRun = 0;
}

void GlitchWatch::chooseInput() noexcept
{
    int best = -1;
    double bestLevel = 0.0;
    bool bestIsSocket = false;

    for (int ch = 0; ch < inputs; ++ch)
    {
        const auto& listener = listeners[(size_t) ch];
        const auto level = std::sqrt (listener.sumSquares / juce::jmax (1, measuredSamples));
        const bool socket = ch < sockets;

        if (level < quietestTone || listener.residualPeak > cleanEnough * level)
            continue;

        // A socket beats the copy inside the mixer: the cable covers the whole trip.
        if (best < 0 || (socket && ! bestIsSocket) || (socket == bestIsSocket && level > bestLevel))
        {
            best = ch;
            bestLevel = level;
            bestIsSocket = socket;
        }
    }

    if (best < 0)
    {
        state.store ((int) GlitchStats::State::notHeard);
        beginListening();   // keep trying: the cable may be plugged in any moment
        return;
    }

    const auto& listener = listeners[(size_t) best];
    chosen = best;
    reference = bestLevel;
    envelope = bestLevel * bestLevel;
    lockedNow = true;
    holdOff = lostRun = 0;

    shownInput.store (best);
    shownLevelDb.store ((float) (20.0 * std::log10 (bestLevel)));
    shownBackground.store ((float) (100.0 * listener.residualPeak / bestLevel));
    state.store ((int) GlitchStats::State::locked);
}

void GlitchWatch::process (const float* const* in, int numInputs, float* toneOut, int numSamples) noexcept
{
    if (restartRequested.exchange (false))
    {
        fade = 0.0;
        beginListening();
    }

    if (countResetRequested.exchange (false))
    {
        glitchCount.store (0);
        breakCount.store (0);
        watchedSamples = lostSamples = 0;
        lastBreakAt = -1;
    }

    // ---- the tone ----
    const auto fadeStep = 1.0 / (0.01 * rate);

    for (int i = 0; i < numSamples; ++i)
    {
        toneOut[i] = (float) (fade * noteLevel * (std::sin (phaseA) + std::sin (phaseB)));
        phaseA += stepA;
        phaseB += stepB;
        fade = juce::jmin (1.0, fade + fadeStep);
    }

    phaseA = std::fmod (phaseA, twoPi);
    phaseB = std::fmod (phaseB, twoPi);

    const auto channels = juce::jmin (inputs, numInputs);

    // ---- still finding which input the tone comes back on ----
    if (! lockedNow)
    {
        const auto skip = (int) (skipSeconds * rate);
        const bool measuring = listenSamples >= skip;

        for (int ch = 0; ch < channels; ++ch)
        {
            if (in[ch] == nullptr)
                continue;

            auto& listener = listeners[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = (double) in[ch][i];
                const auto e = std::abs (listener.residual (x, coeffA, coeffB));

                if (measuring)
                {
                    listener.sumSquares += x * x;
                    listener.residualPeak = juce::jmax (listener.residualPeak, e);
                }
            }
        }

        listenSamples += numSamples;

        if (measuring)
            measuredSamples += numSamples;

        if (measuredSamples >= (int) (measureSeconds * rate))
            chooseInput();

        return;
    }

    // ---- watching the tone for breaks ----
    if (chosen >= channels || in[chosen] == nullptr)
        return;

    auto& listener = listeners[(size_t) chosen];
    const auto limit = spikeSize * reference;
    const auto smoothing = 1.0 / (0.005 * rate);
    const auto lostLevel = lostBelow * lostBelow * reference * reference;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = (double) in[chosen][i];
        const auto e = std::abs (listener.residual (x, coeffA, coeffB));
        envelope += smoothing * (x * x - envelope);

        if (holdOff > 0)
        {
            --holdOff;
        }
        else if (e > limit)
        {
            const auto now = watchedSamples + i;
            auto count = glitchCount.load (std::memory_order_relaxed);

            // A run of breaks close together is one glitch (or one burst of some other sound).
            if (lastBreakAt < 0 || now - lastBreakAt > (juce::int64) (sameGlitchSeconds * rate))
            {
                if (count < maxTimes)
                {
                    glitchTimes[(size_t) count].store ((float) ((double) now / rate), std::memory_order_relaxed);
                    glitchBreaks[(size_t) count].store (0, std::memory_order_relaxed);
                }

                glitchCount.store (++count, std::memory_order_relaxed);
            }

            if (count >= 1 && count <= maxTimes)
                glitchBreaks[(size_t) (count - 1)].fetch_add (1, std::memory_order_relaxed);

            breakCount.fetch_add (1, std::memory_order_relaxed);
            lastBreakAt = now;
            holdOff = (int) (holdOffSeconds * rate);
        }

        if (envelope < lostLevel)
        {
            ++lostSamples;
            ++lostRun;
        }
        else
        {
            lostRun = 0;
        }
    }

    watchedSamples += numSamples;
    shownSeconds.store ((double) watchedSamples / rate, std::memory_order_relaxed);
    shownLostSeconds.store ((double) lostSamples / rate, std::memory_order_relaxed);

    // Gone for good (a fader pulled down, a cable pulled out): go back to looking for it.
    if (lostRun > (int) (relistenAfterSeconds * rate))
    {
        state.store ((int) GlitchStats::State::listening);
        beginListening();
    }
}

GlitchStats GlitchWatch::stats() const
{
    GlitchStats stats;
    stats.state = on.load() ? (GlitchStats::State) state.load() : GlitchStats::State::off;
    stats.input = shownInput.load();
    stats.analogue = stats.input >= 0 && stats.input < sockets;
    stats.glitches = glitchCount.load();
    stats.breaks = breakCount.load();
    stats.seconds = shownSeconds.load();
    stats.lostSeconds = shownLostSeconds.load();
    stats.levelDb = shownLevelDb.load();
    stats.backgroundPercent = shownBackground.load();

    for (int i = 0; i < juce::jmin (stats.glitches, maxTimes); ++i)
    {
        stats.times.push_back ((double) glitchTimes[(size_t) i].load());
        stats.breaksEach.push_back (glitchBreaks[(size_t) i].load());
    }

    return stats;
}

} // namespace gigrig
