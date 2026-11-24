#include "AnalogDelay.h"

namespace gigrig::fx
{

AnalogDelay::AnalogDelay()
{
    pTime     = addParam ("time",     "Time",     "ms", 40.0f, 600.0f, 120.0f, 0.0f);   // glides on its own
    pFeedback = addParam ("feedback", "Repeats",  "%",  0.0f, 95.0f, 25.0f);
    pMix      = addParam ("mix",      "Mix",      "%",  0.0f, 100.0f, 35.0f);
    pTone     = addParam ("tone",     "Tone",     "",   0.0f, 10.0f, 5.0f);
    pMod      = addParam ("mod",      "Mod",      "",   0.0f, 10.0f, 2.0f);
}

void AnalogDelay::onPrepare (double rate, int)
{
    line.setMaxSamples ((int) (0.62 * rate) + 64);
    fbHigh.highPass (rate, 90.0);
    lastTone = -1.0f;
    onReset();
}

void AnalogDelay::onReset() noexcept
{
    line.clear();
    inLow.clear(); fbLow.clear(); fbHigh.clear();
    lfo.phase = 0.0;
    delayNow = value (pTime) * 0.001 * sampleRate();
}

void AnalogDelay::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();

    if (value (pTone) != lastTone)
    {
        lastTone = value (pTone);
        const double hz = 1500.0 * std::pow (4.0, lastTone / 10.0);    // 1.5 to 6 kHz: a BBD's darkness
        inLow.lowPass (rate, hz * 1.4);
        fbLow.lowPass (rate, hz);
    }

    const double target = value (pTime) * 0.001 * rate;
    const double fb = value (pFeedback) * 0.01;
    const float wet = value (pMix) * 0.01f;
    const double modSamples = value (pMod) * 0.1 * 0.0006 * rate;     // up to 0.6 ms of wobble

    for (int i = 0; i < n; ++i)
    {
        // Time glides: at most a quarter faster or slower than real time, so the pitch bend stays musical.
        delayNow += std::clamp (target - delayNow, -0.25, 0.25);

        const double d = delayNow + modSamples * (1.0 + lfo.next (0.45, rate));
        const float back = line.read (d);

        float x = ch[0][i];
        float repeat = fbLow.run (back, 0);
        repeat = fbHigh.run (repeat, 0);
        float in = inLow.run (x, 0) + (float) fb * repeat;
        in = std::tanh (in * 0.8f) / 0.8f;            // the BBD and its compander running out of room
        if (std::abs (in) < 1.0e-20f) in = 0.0f;
        line.write (in);

        const float out = x + wet * repeat;
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }
}

} // namespace gigrig::fx
