#include "PlateReverb.h"

namespace gigrig::fx
{

PlateReverb::PlateReverb()
{
    pReverb = addParam ("reverb",   "Reverb",    "",   0.0f, 10.0f, 3.0f);
    pDecay  = addParam ("decay",    "Decay",     "",   0.0f, 10.0f, 5.0f);
    pPre    = addParam ("predelay", "Pre-delay", "ms", 0.0f, 150.0f, 30.0f, 0.0f);     // glides on its own
    pTone   = addParam ("tone",     "Tone",      "",   0.0f, 10.0f, 6.0f);
    pLowCut = addParam ("lowcut",   "HPF",       "Hz", 20.0f, 500.0f, 180.0f);
}

void PlateReverb::onPrepare (double rate, int)
{
    scale = rate / 29761.0;
    const auto len = [this] (double samples) { return std::max (2, (int) std::lround (samples * scale)); };

    pre.setMaxSamples ((int) (0.16 * rate) + 64);
    bandwidth.lowPass (rate, 11000.0);

    const double inLengths[4] { 142.0, 107.0, 379.0, 277.0 };
    for (int k = 0; k < 4; ++k)
    {
        inAp[(size_t) k].length = len (inLengths[k]);
        inAp[(size_t) k].line.setMaxSamples (inAp[(size_t) k].length + 8);
    }

    const double modLengths[2] { 672.0, 908.0 }, d1Lengths[2] { 4453.0, 4217.0 }, ap2Lengths[2] { 1800.0, 2656.0 }, d2Lengths[2] { 3720.0, 3163.0 };
    for (int k = 0; k < 2; ++k)
    {
        const auto i = (size_t) k;
        modLen[i] = modLengths[k] * scale;
        modAp[i].setMaxSamples ((int) (modLen[i] + 24.0 * scale) + 16);
        d1Len[i] = len (d1Lengths[k]);
        d1[i].setMaxSamples (d1Len[i] + 8);
        ap2[i].length = len (ap2Lengths[k]);
        ap2[i].line.setMaxSamples (ap2[i].length + 8);
        d2Len[i] = len (d2Lengths[k]);
        d2[i].setMaxSamples (d2Len[i] + 8);
    }

    // Where the output is picked off the loops (Dattorro's table: seven points for each side).
    const double taps[14] { 266.0, 2974.0, 1913.0, 1996.0, 1990.0, 187.0, 1066.0,      // "left"
                            353.0, 3627.0, 1228.0, 2673.0, 2111.0, 335.0, 121.0 };     // "right"
    for (int k = 0; k < 14; ++k)
        tap[(size_t) k] = len (taps[k]);

    lastTone = lastLowCut = -1.0f;
    onReset();
}

void PlateReverb::onReset() noexcept
{
    pre.clear();
    bandwidth.clear(); lowCut.clear();
    for (auto& d : damp) d.clear();
    for (auto& a : inAp) a.line.clear();
    for (auto& a : ap2) a.line.clear();
    for (auto& m : modAp) m.clear();
    for (auto& d : d1) d.clear();
    for (auto& d : d2) d.clear();
    loopOut = {};
    lfo.phase = 0.0;
    preNow = value (pPre) * 0.001 * sampleRate();
}

void PlateReverb::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();

    if (value (pTone) != lastTone)
    {
        lastTone = value (pTone);
        const double hz = 1800.0 * std::pow (7.0, lastTone / 10.0);       // 1.8 to 12.6 kHz
        for (auto& d : damp) d.lowPass (rate, hz);
    }

    if (value (pLowCut) != lastLowCut)
    {
        lastLowCut = value (pLowCut);
        lowCut.highPass (rate, lastLowCut);
    }

    // Once round the figure of eight is about 0.73 s and passes the decay gain four times.
    const double t60 = decaySeconds (value (pDecay));
    const float decay = (float) std::pow (10.0, -3.0 * 0.7254 / (4.0 * t60));
    // The tail is louder the longer it rings: take part of that back, so Reverb means about the same at any Decay.
    const float amount = value (pReverb) * 0.1f * 1.6f * (float) std::pow (t60 / 2.0, -0.2);
    const double target = value (pPre) * 0.001 * rate;
    const double excursion = 8.0 * scale;

    for (int i = 0; i < n; ++i)
    {
        const float x = ch[0][i];

        // Pre-delay (it glides when the knob moves, at most a quarter off real time).
        preNow += std::clamp (target - preNow, -0.25, 0.25);
        pre.write (x);
        float v = bandwidth.run (pre.read (preNow), 0);

        // Smear.
        v = inAp[0].run (v, 0.75f);
        v = inAp[1].run (v, 0.75f);
        v = inAp[2].run (v, 0.625f);
        v = inAp[3].run (v, 0.625f);

        const double wob = lfo.next (0.8, rate);
        const double wob2 = std::sin ((lfo.phase + 0.25) * 6.283185307179586);
        std::array<float, 2> fresh {};

        for (int k = 0; k < 2; ++k)
        {
            const auto s = (size_t) k;
            float in = v + decay * loopOut[(size_t) (1 - k)];

            // The wobbled all-pass (coefficient -0.7).
            const float d = modAp[s].read (modLen[s] - 1.0 + excursion * (k == 0 ? wob : wob2));
            const float w = in + 0.7f * d;
            modAp[s].write (w);
            in = d - 0.7f * w;

            const float a = d1[s].readWhole (d1Len[s] - 1);
            d1[s].write (in);
            float b = damp[s].run (a, 0) * decay;
            if (std::abs (b) < 1.0e-20f) b = 0.0f;
            b = ap2[s].run (b, 0.5f);
            fresh[s] = d2[s].readWhole (d2Len[s] - 1);
            d2[s].write (b);
        }

        loopOut = fresh;

        // Both of Dattorro's outputs, added: one mono tail with twice the detail.
        const float left  = d1[1].readWhole (tap[0]) + d1[1].readWhole (tap[1]) - ap2[1].line.readWhole (tap[2]) + d2[1].readWhole (tap[3])
                          - d1[0].readWhole (tap[4]) - ap2[0].line.readWhole (tap[5]) - d2[0].readWhole (tap[6]);
        const float right = d1[0].readWhole (tap[7]) + d1[0].readWhole (tap[8]) - ap2[0].line.readWhole (tap[9]) + d2[0].readWhole (tap[10])
                          - d1[1].readWhole (tap[11]) - ap2[1].line.readWhole (tap[12]) - d2[1].readWhole (tap[13]);

        const float out = x + amount * 0.3f * lowCut.run (left + right, 0);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = out;
    }
}

} // namespace gigrig::fx
