#include "DigitalDelay.h"

namespace gigrig::fx
{

DigitalDelay::DigitalDelay()
{
    pFollow   = addToggle ("follow",   "Sync", true);
    pDivision = addChoice ("division", "Div", sync::names(), 0);
    pTime     = addParam ("time",      "Time",     "ms", 20.0f, 1500.0f, 375.0f, 0.0f);   // crossfades on its own
    pFeedback = addParam ("feedback",  "Fdbk",     "%",  0.0f, 95.0f, 30.0f);
    pMix      = addParam ("mix",       "Mix",      "%",  0.0f, 100.0f, 30.0f);
    pTone     = addParam ("tone",      "Tone",     "",   0.0f, 10.0f, 8.0f);
    pMod      = addParam ("mod",       "Mod depth", "",  0.0f, 10.0f, 2.0f);
    pModRate  = addParam ("modrate",   "Mod rate", "Hz", 0.1f, 5.0f, 0.6f);
    // New settings go at the end. The order of these three is saved in presets: never change it.
    pThrow    = addChoice ("throw",    "Throw", { "Off: the delay is always on", "Ready: clean until the switch is held", "Catching: the switch is held" }, 0);
}

double DigitalDelay::targetSamples() const noexcept
{
    const double s = sync::seconds (value (pFollow) > 0.5f, tempo(), (int) std::lround (value (pDivision)), value (pTime));
    return std::clamp (s, 0.02, 1.5) * sampleRate();
}

void DigitalDelay::onPrepare (double rate, int)
{
    line.setMaxSamples ((int) (1.52 * rate) + 256);
    fbHigh.highPass (rate, 60.0);
    fadeLength = std::max (1, (int) (0.040 * rate));
    lastTone = -1.0f;
    onReset();
}

void DigitalDelay::onReset() noexcept
{
    line.clear();
    fbLow.clear(); fbHigh.clear();
    lfo.phase = 0.0;
    tapA = tapB = targetSamples();
    fadeLeft = 0;
    send = (int) std::lround (value (pThrow)) == 1 ? 0.0f : 1.0f;
}

void DigitalDelay::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();

    if (value (pTone) != lastTone)
    {
        lastTone = value (pTone);
        fbLow.lowPass (rate, 2000.0 * std::pow (7.0, lastTone / 10.0));   // 2 to 14 kHz
    }

    // A new time starts a crossfade (one at a time; a later change waits for this one to end).
    const double target = targetSamples();
    if (fadeLeft == 0 && std::abs (target - tapA) > 1.0)
    {
        tapB = target;
        fadeLeft = fadeLength;
    }

    const double fb = value (pFeedback) * 0.01;
    const float wet = value (pMix) * 0.01f;
    const double modSamples = value (pMod) * 0.1 * 0.0012 * rate;     // up to 1.2 ms on the repeats
    const double modHz = value (pModRate);

    // The throw: the way into the delay is shut while it waits for the switch. It opens in
    // 5 ms and shuts in 15, so the first word is caught whole and the last one is not chopped.
    const float sendTarget = (int) std::lround (value (pThrow)) == 1 ? 0.0f : 1.0f;
    const float sendUp = (float) (1.0 / (0.005 * rate)), sendDown = (float) (1.0 / (0.015 * rate));

    for (int i = 0; i < n; ++i)
    {
        const double m = modSamples * (1.0 + lfo.next (modHz, rate));
        float back = line.read (tapA + m);

        if (fadeLeft > 0)
        {
            const float t = 1.0f - (float) fadeLeft / (float) fadeLength;
            const float s = 0.5f - 0.5f * std::cos (t * 3.14159265f);
            back += (line.read (tapB + m) - back) * s;
            if (--fadeLeft == 0)
                tapA = tapB;
        }

        const float x = ch[0][i];
        const float repeat = fbHigh.run (fbLow.run (back, 0), 0);
        send = sendTarget > send ? std::min (sendTarget, send + sendUp) : std::max (sendTarget, send - sendDown);
        float in = send * x + (float) (fb * repeat);
        if (std::abs (in) > 1.5f) in = 1.5f * std::tanh (in / 1.5f);   // only a safety, never heard at sane levels
        if (std::abs (in) < 1.0e-20f) in = 0.0f;
        line.write (in);

        const float out = x + wet * repeat;
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }
}

} // namespace gigrig::fx
