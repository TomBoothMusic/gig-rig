#include "TapeMachine.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

TapeMachine::TapeMachine()
{
    pDrive = addParam ("drive", "Drive", "", 0.0f, 10.0f, 4.0f);
    pSpeed = addChoice ("speed", "Tape speed", { "7.5 (fat and soft)", "15 (the classic)", "30 (clean and open)" }, 1);
    pLevel = addParam ("level", "Level", "dB", -12.0f, 12.0f, 0.0f);
}

void TapeMachine::onPrepare (double, int)   { update(); }

void TapeMachine::onReset() noexcept
{
    pre.clear(); de.clear(); bump.clear(); top.clear();
}

void TapeMachine::update()
{
    static constexpr double bumpHz[3] { 45.0, 70.0, 115.0 }, bumpDb[3] { 2.0, 1.5, 1.0 }, topHz[3] { 11000.0, 17000.0, 22000.0 };
    const int speed = std::clamp ((int) std::lround (value (pSpeed)), 0, 2);
    const double rate = sampleRate();

    in = dbToGain (-8.0f + 2.0f * value (pDrive));     // -8 dB (nearly clean) to +12 dB into the tape
    out = dbToGain (value (pLevel));
    pre.highShelf (rate, 4000.0, 4.0, 0.7);
    de.highShelf (rate, 4000.0, -4.0, 0.7);
    bump.peak (rate, bumpHz[speed], 0.9, bumpDb[speed]);
    top.lowPass (rate, std::min (topHz[speed], rate * 0.45), 0.6);
}

void TapeMachine::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
        update();

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            const double x = pre.run (ch[c][i], c) * in;
            const double y = std::tanh (x) / in;
            ch[c][i] = (float) (out * top.run (bump.run (de.run ((float) y, c), c), c));
        }
}

//==============================================================================
void tapeChecks (const Check& check)
{
    using namespace test;
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.5 * rate);

    auto fx = makeEffect ("tape");
    fx->prepare (rate, 64);

    const auto changeDb = [&] (double hz, double level)
    {
        fx->reset();
        const auto x = sine (hz, 1.0, level, rate);
        return 20.0 * std::log10 (std::max (strength (run (*fx, x), from, hz, rate), 1.0e-12) / level);
    };

    const double mid = changeDb (1000.0, 0.01), low = changeDb (70.0, 0.01);
    check (std::abs (mid) < 0.3 && low > 1.0 && low < 2.2, "tape: a quiet mix passes at its own level (" + num (mid, 2) + " dB at 1 kHz) with the head's bass bump (" + num (low, 1) + " dB at 70 Hz)");

    const double loudMid = changeDb (1000.0, 0.9), loudTop = changeDb (8000.0, 0.9), quietTop = changeDb (8000.0, 0.01);
    check (loudMid < -0.5 && loudMid > -6.0 && (loudTop - quietTop) < loudMid - 0.5,
           "tape: a hot mix is squashed softly (" + num (loudMid, 1) + " dB at 1 kHz) and the top is squashed first (" + num (loudTop - quietTop, 1) + " dB at 8 kHz)");

    fx->setParam ("speed", 0.0f);
    const double slowTop = changeDb (14000.0, 0.01);
    fx->setParam ("speed", 2.0f);
    const double fastTop = changeDb (14000.0, 0.01);
    check (slowTop < fastTop - 2.0, "tape: the slow speed has the softer top (" + num (slowTop, 1) + " dB at 14 kHz against " + num (fastTop, 1) + " dB at the fast speed)");
}

} // namespace gigrig::fx
