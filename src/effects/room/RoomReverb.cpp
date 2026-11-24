#include "RoomReverb.h"

namespace gigrig::fx
{

namespace
{
    // Lengths in milliseconds for the middle size; no two share a factor, so echoes never line up.
    constexpr double baseMs[8] { 19.31, 23.87, 29.17, 31.73, 37.19, 41.93, 47.11, 53.27 };
    constexpr double sizeScale[3] { 0.42, 0.85, 1.55 };
    constexpr double shortest[3] { 0.15, 0.3, 0.8 }, longest[3] { 0.8, 2.2, 6.0 };     // seconds of ring at Decay 0 and 10
    constexpr double preMs[3] { 2.0, 8.0, 22.0 };
}

double RoomReverb::decaySeconds (int size, float knob) noexcept
{
    const auto s = (size_t) std::clamp (size, 0, 2);
    return shortest[s] * std::pow (longest[s] / shortest[s], (double) knob / 10.0);
}

RoomReverb::RoomReverb()
{
    pReverb = addParam ("reverb", "Reverb",  "",   0.0f, 10.0f, 2.5f);
    pSize   = addChoice ("size",  "Size", { "Small room", "Room", "Hall" }, 1);
    pDecay  = addParam ("decay",  "Decay",   "",   0.0f, 10.0f, 5.0f);
    pTone   = addParam ("tone",   "Tone",    "",   0.0f, 10.0f, 5.0f);
    pLowCut = addParam ("lowcut", "Low cut", "Hz", 20.0f, 500.0f, 120.0f);
}

void RoomReverb::onPrepare (double rate, int)
{
    for (int k = 0; k < lines; ++k)
        line[(size_t) k].setMaxSamples ((int) (baseMs[k] * sizeScale[2] * 0.001 * rate) + 64);

    const double diffuserMs[2] { 5.31, 7.93 };
    for (int k = 0; k < 2; ++k)
    {
        diffuserLength[(size_t) k] = std::max (2, (int) std::lround (diffuserMs[k] * 0.001 * rate));
        diffuser[(size_t) k].setMaxSamples (diffuserLength[(size_t) k] + 8);
    }

    pre.setMaxSamples ((int) (preMs[2] * 0.001 * rate) + 16);
    lastLowCut = -1.0f;
    sizeNow = -1;
    onReset();
}

void RoomReverb::setSize (int size) noexcept
{
    sizeNow = std::clamp (size, 0, 2);
    const double rate = sampleRate();
    for (int k = 0; k < lines; ++k)
        length[(size_t) k] = baseMs[k] * sizeScale[sizeNow] * 0.001 * rate;
    preLength = std::max (1, (int) (preMs[sizeNow] * 0.001 * rate));
}

void RoomReverb::onReset() noexcept
{
    for (auto& l : line) l.clear();
    for (auto& d : diffuser) d.clear();
    pre.clear();
    lowState = {};
    lowCut.clear();
    lfo.phase = 0.0;
    duck = 1.0f;
    setSize ((int) std::lround (value (pSize)));
}

void RoomReverb::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const int wanted = (int) std::lround (value (pSize));

    if (value (pLowCut) != lastLowCut)
    {
        lastLowCut = value (pLowCut);
        lowCut.highPass (rate, lastLowCut);
    }

    const double t60 = decaySeconds (sizeNow, value (pDecay));
    std::array<float, lines> gain {};
    for (int k = 0; k < lines; ++k)
        gain[(size_t) k] = (float) std::pow (10.0, -3.0 * (length[(size_t) k] / rate) / t60);

    // How much of the top each trip round the room loses: 2.5 to 14 kHz.
    const float a = (float) (1.0 - std::exp (-6.283185307179586 * 2500.0 * std::pow (5.6, value (pTone) / 10.0) / rate));
    const float amount = value (pReverb) * 0.1f * (float) std::pow (t60 / 1.0, -0.3);
    const float duckStep = (float) (1.0 / (0.012 * rate));
    const double excursion = 0.00025 * rate;

    for (int i = 0; i < n; ++i)
    {
        // A new size: fade the tail out, change, fade it back in.
        if (wanted != sizeNow)
        {
            duck -= duckStep;
            if (duck <= 0.0f)
            {
                duck = 0.0f;
                for (auto& l : line) l.clear();
                for (auto& d : diffuser) d.clear();
                lowState = {};
                setSize (wanted);
            }
        }
        else if (duck < 1.0f)
            duck = std::min (1.0f, duck + duckStep);

        const float x = ch[0][i];
        pre.write (x);
        float v = pre.readWhole (preLength);

        // Two short all-passes thicken the first reflections.
        for (int k = 0; k < 2; ++k)
        {
            auto& d = diffuser[(size_t) k];
            const float back = d.readWhole (diffuserLength[(size_t) k] - 1);
            const float w = v - 0.6f * back;
            d.write (w);
            v = back + 0.6f * w;
        }

        const double wob = excursion * lfo.next (0.7, rate);
        std::array<float, lines> out {};
        float sum = 0.0f, wet = 0.0f;

        for (int k = 0; k < lines; ++k)
        {
            const auto s = (size_t) k;
            // Two of the eight lines drift a little, which stops the tail ringing on one note.
            float back = k == 2 ? line[s].read (length[s] - 1.0 + wob)
                       : k == 5 ? line[s].read (length[s] - 1.0 - wob)
                                : line[s].readWhole ((int) length[s] - 1);
            lowState[s] += a * (back - lowState[s]);
            back = lowState[s] * gain[s];
            if (std::abs (back) < 1.0e-20f) { back = 0.0f; lowState[s] = 0.0f; }
            out[s] = back;
            sum += back;
            wet += (k & 1) ? -back : back;
        }

        // The mixing matrix (Householder): every line feeds every other, and nothing is lost or gained.
        const float spread = sum * (2.0f / (float) lines);
        for (int k = 0; k < lines; ++k)
            line[(size_t) k].write (out[(size_t) k] - spread + v * 0.35f);

        const float y = x + amount * duck * 1.6f * lowCut.run (wet, 0);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = y;
    }
}

} // namespace gigrig::fx
