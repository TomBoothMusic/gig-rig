#include "TapeDelay.h"

namespace gigrig::fx
{

TapeDelay::TapeDelay()
{
    pFollow   = addToggle ("follow",   "Follow tempo", false);
    pDivision = addChoice ("division", "Note value", sync::names(), 0);
    pTime     = addParam ("time",      "Time",     "ms", 60.0f, 1000.0f, 300.0f, 0.0f);    // glides on its own
    pFeedback = addParam ("feedback",  "Repeats",  "%",  0.0f, 95.0f, 35.0f);
    pMix      = addParam ("mix",       "Mix",      "%",  0.0f, 100.0f, 30.0f);
    pWow      = addParam ("wow",       "Wow",      "",   0.0f, 10.0f, 3.0f);
    pFlutter  = addParam ("flutter",   "Flutter",  "",   0.0f, 10.0f, 3.0f);
    pAge      = addParam ("age",       "Age",      "",   0.0f, 10.0f, 4.0f);
}

double TapeDelay::targetSamples() const noexcept
{
    const double s = sync::seconds (value (pFollow) > 0.5f, tempo(), (int) std::lround (value (pDivision)), value (pTime));
    return std::clamp (s, 0.06, 1.0) * sampleRate();
}

void TapeDelay::onPrepare (double rate, int)
{
    line.setMaxSamples ((int) (1.02 * rate) + 1024);
    lastAge = -1.0f;
    onReset();
}

void TapeDelay::onReset() noexcept
{
    line.clear();
    headLow.clear(); headHigh.clear();
    wow.phase = 0.0; flutter.phase = 0.25; drift.phase = 0.6;
    delayNow = targetSamples();
}

void TapeDelay::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const float age = value (pAge);

    if (age != lastAge)
    {
        lastAge = age;
        headLow.lowPass (rate, 7000.0 * std::pow (0.3, age / 10.0));    // 7 kHz new, about 2 kHz worn
        headHigh.highPass (rate, 80.0 + 12.0 * age);                    // the head's thin low end
    }

    const double target = targetSamples();
    const double fb = value (pFeedback) * 0.01;
    const float wet = value (pMix) * 0.01f;
    const double wowSamples = value (pWow) * 0.1 * 0.0025 * rate;         // up to 2.5 ms, slow
    const double flutterSamples = value (pFlutter) * 0.1 * 0.00012 * rate; // up to 0.12 ms, fast
    const float drive = 1.0f + 0.25f * age;

    for (int i = 0; i < n; ++i)
    {
        delayNow += std::clamp (target - delayNow, -0.25, 0.25);

        // Wow: a slow wobble plus a slower drift, so it never sounds like a clean LFO.
        const double w = wowSamples * (1.0 + 0.7 * wow.next (0.55, rate) + 0.3 * drift.next (0.13, rate));
        const double f = flutterSamples * (1.0 + flutter.next (7.5, rate));
        const float back = line.read (delayNow + w + f);

        const float x = ch[0][i];
        const float repeat = headHigh.run (headLow.run (back, 0), 0);
        float in = x + (float) (fb * repeat);
        in = std::tanh (in * drive) / drive;              // tape saturation, more as it ages
        if (std::abs (in) < 1.0e-20f) in = 0.0f;
        line.write (in);

        const float out = x + wet * repeat;
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }
}

} // namespace gigrig::fx
