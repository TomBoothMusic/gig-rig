#include "AcousticBody.h"

#include <algorithm>

namespace gigrig::fx
{

namespace
{
    // The curve at Body 10: where, how wide, and how many dB.
    struct Band { double hz, q, db; };
    constexpr Band curve[5] {
        { 110.0,  1.3,  5.0 },      // the air inside the box
        { 220.0,  1.5,  4.0 },      // the top
        { 1200.0, 0.9, -4.5 },      // quack
        { 2800.0, 1.2, -3.5 },      // quack, upper part
        { 9500.0, 0.7,  2.0 },      // air
    };

    constexpr double squeezeThresholdDb = -38.0;    // on the 2 kHz region alone
    constexpr double squeezeMostDb = 6.0;           // at Body 10
}

AcousticBody::AcousticBody()
{
    pBody  = addParam ("body",  "Body",  "",   0.0f, 10.0f, 5.0f);
    pPhase = addToggle ("phase", "Phase inv", false);
    pLevel = addParam ("level", "Level", "dB", -15.0f, 15.0f, 0.0f);
}

void AcousticBody::onPrepare (double rate, int)
{
    for (int b = 0; b < numBands; ++b)
        bands[(size_t) b].bandPass (rate, curve[b].hz, curve[b].q);
    quack.bandPass (rate, 2000.0, 0.8);
    quackOut.bandPass (rate, 2000.0, 0.8);
    attack = 1.0 - std::exp (-1.0 / (0.001 * rate));
    release = 1.0 - std::exp (-1.0 / (0.080 * rate));
    glide = (float) (1.0 - std::exp (-1.0 / (0.0003 * rate)));
    onReset();
}

void AcousticBody::onReset() noexcept
{
    for (auto& b : bands) b.clear();
    quack.clear(); quackOut.clear();
    env = 0.0;
    gainNow = 1.0f;
    polarity = value (pPhase) >= 0.5f ? -1.0f : 1.0f;
    setMeter (0.0f);
}

void AcousticBody::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const float amount = value (pBody) * 0.1f;
    std::array<float, numBands> mix {};
    for (int b = 0; b < numBands; ++b)
        mix[(size_t) b] = dbToGain ((float) curve[b].db * amount) - 1.0f;

    const float level = dbToGain (value (pLevel));
    const float polarityTarget = value (pPhase) >= 0.5f ? -1.0f : 1.0f;
    const float polarityStep = (float) (2.0 / (0.010 * sampleRate()));      // through zero in 10 ms: a dip, not a click
    const double threshold = std::pow (10.0, squeezeThresholdDb / 20.0);
    float deepest = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];
        float y = x;

        for (int b = 0; b < numBands; ++b)
            y += mix[(size_t) b] * bands[(size_t) b].run (x, 0);

        // The squeeze: how loud the 2 kHz region is decides how far that region alone is turned down.
        const float q = quack.run (x, 0);
        const double mag = std::abs ((double) q);
        env += (mag > env ? attack : release) * (mag - env);
        float target = 1.0f;
        if (env > threshold)
        {
            // Half of what goes over the threshold is taken back (2:1), up to the most allowed.
            const double overDb = 20.0 * std::log10 (env / threshold);
            target = dbToGain ((float) -std::min (overDb * 0.5, squeezeMostDb) * amount);
        }
        gainNow += glide * (target - gainNow);
        deepest = std::min (deepest, gainNow);
        // Taken out of the sound as it stands after the curve, so the two never add up to a hole.
        y -= (1.0f - gainNow) * quackOut.run (y, 0);

        polarity += std::clamp (polarityTarget - polarity, -polarityStep, polarityStep);
        y *= level * polarity;

        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }

    if (env < 1.0e-20) env = 0.0;
    setMeter (gainToDb (deepest));
}

} // namespace gigrig::fx
