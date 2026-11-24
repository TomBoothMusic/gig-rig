#include "SpringReverb.h"

namespace gigrig::fx
{

SpringReverb::SpringReverb()
{
    pReverb = addParam ("reverb", "Reverb", "",  0.0f, 10.0f, 3.0f);
    pDecay  = addParam ("decay",  "Decay",  "",  0.0f, 10.0f, 5.0f);
    pTone   = addParam ("tone",   "Tone",   "",  0.0f, 10.0f, 5.0f);
    pDwell  = addParam ("dwell",  "Dwell",  "",  0.0f, 10.0f, 5.0f);
}

void SpringReverb::onPrepare (double rate, int)
{
    const double lengthsMs[numSprings] { 33.7, 41.3 };
    for (int s = 0; s < numSprings; ++s)
    {
        springs[(size_t) s].length = lengthsMs[s] * 0.001 * rate;
        springs[(size_t) s].line.setMaxSamples ((int) springs[(size_t) s].length + 64);
    }
    drive.highPass (rate, 320.0);       // the tank is driven thin, as in the amp
    outHigh.highPass (rate, 120.0);
    lastTone = -1.0f;
    onReset();
}

void SpringReverb::onReset() noexcept
{
    for (auto& s : springs)
    {
        s.ap.fill (0.0f);
        s.line.clear();
        s.low.clear();
        s.last = 0.0f;
    }
    drive.clear(); outHigh.clear();
    wobble.phase = 0.0;
}

void SpringReverb::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();

    if (value (pTone) != lastTone)
    {
        lastTone = value (pTone);
        for (auto& s : springs)
            s.low.lowPass (rate, 2500.0 * std::pow (2.4, lastTone / 10.0));   // 2.5 to 6 kHz
    }

    const double t60 = 0.6 + value (pDecay) * 0.34;             // 0.6 to 4 seconds
    const float amount = value (pReverb) * 0.1f;
    const float in = 0.6f + value (pDwell) * 0.25f;
    // The all-pass coefficient sets how strong the drip is (and depends on the rate, so it
    // sounds the same at 48 and 96 kHz).
    const float a = (float) std::clamp (0.62 * std::pow (96000.0 / rate, 0.5), 0.3, 0.8);
    std::array<float, numSprings> gain {};
    for (int k = 0; k < numSprings; ++k)
        gain[(size_t) k] = (float) std::pow (10.0, -3.0 * (springs[(size_t) k].length / rate) / t60);

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];
        float d = drive.run (x, 0) * in;
        d = std::tanh (d * 1.5f) / 1.5f;                          // the drive transformer running out of room
        const double wob = 1.0 + 0.0008 * wobble.next (0.9, rate);  // springs never sit quite still

        float wet = 0.0f;
        for (int k = 0; k < numSprings; ++k)
        {
            auto& s = springs[(size_t) k];
            float v = d + gain[(size_t) k] * s.last;

            // The dispersion: first-order all-passes in a row.
            for (auto& z : s.ap)
            {
                const float y = -a * v + z;
                z = v + a * y;
                v = y;
            }

            s.line.write (v);
            float back = s.low.run (s.line.read (s.length * wob), 0);
            if (std::abs (back) < 1.0e-20f) back = 0.0f;
            s.last = back;
            wet += back;
        }

        const float out = x + amount * 0.5f * outHigh.run (wet, 0);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }

    // Long silences: let the all-pass memories reach true zero (tiny leftovers are slow to process).
    for (auto& s : springs)
        for (auto& z : s.ap)
            if (std::abs (z) < 1.0e-20f) z = 0.0f;
}

} // namespace gigrig::fx
