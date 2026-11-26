#include "OptoComp.h"
#include "../EffectChecks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gigrig::fx
{

namespace
{
    double coeffFor (double seconds, double rate) noexcept
    {
        return 1.0 - std::exp (-1.0 / std::max (1.0, seconds * rate));
    }

    constexpr double kneeDb = 12.0;
}

OptoComp::OptoComp()
{
    pReduction = addParam ("reduction", "Peak reduction", "", 0.0f, 100.0f, 30.0f);
    pGain      = addParam ("gain", "Gain", "dB", -12.0f, 24.0f, 0.0f);
    pMode      = addChoice ("mode", "Mode", { "Compress", "Limit" }, 0);
}

void OptoComp::onPrepare (double, int)  { onReset(); }

void OptoComp::onReset() noexcept
{
    fast = slow = 0.0;
    setMeter (0.0f);
}

void OptoComp::processChunk (float* const* ch, int numCh, int n) noexcept
{
    const double rate = sampleRate();
    const double thresholdDb = -0.5 * value (pReduction);          // 0 = never, 100 = from -50 dB
    const double slope = value (pMode) >= 0.5f ? 1.0 - 1.0 / 12.0 : 1.0 - 1.0 / 3.0;
    const double attackC = coeffFor (0.010, rate), releaseFastC = coeffFor (0.060, rate);
    const double chargeC = coeffFor (0.5, rate), releaseSlowC = coeffFor (3.0, rate);
    const double outGain = dbToGain (value (pGain));
    const bool off = value (pReduction) < 0.05f;
    double gr = 0.0;

    for (int i = 0; i < n; ++i)
    {
        double level = 0.0;
        for (int c = 0; c < numCh; ++c)
            level = std::max (level, std::abs ((double) ch[c][i]));

        const double over = (level > 1.0e-9 ? 20.0 * std::log10 (level) : -180.0) - thresholdDb;
        double target = 0.0;
        if (off)                         target = 0.0;
        else if (over >= kneeDb * 0.5)   target = over * slope;
        else if (over > -kneeDb * 0.5)   target = slope * (over + kneeDb * 0.5) * (over + kneeDb * 0.5) / (2.0 * kneeDb);

        // The cell: quick to squeeze and to let half of it go, with a slow memory underneath
        // that builds up while it squeezes and holds the rest for seconds.
        fast += (target > fast ? attackC : releaseFastC) * (target - fast);
        slow += (fast > slow ? chargeC : releaseSlowC) * (fast - slow);
        gr = std::max (fast, 0.5 * slow);
        const double g = std::pow (10.0, -gr * 0.05) * outGain;

        for (int c = 0; c < numCh; ++c)
        {
            // The output valve: clean at normal levels, rounding off gently near full level.
            const double y = (double) ch[c][i] * g;
            ch[c][i] = (float) (y / std::sqrt (1.0 + 0.06 * y * y));
        }
    }

    setMeter ((float) -gr);
}

//==============================================================================
void optoCompChecks (const Check& check)
{
    using namespace test;
    constexpr double rate = 96000.0;
    const size_t from = (size_t) (0.6 * rate);

    const auto changeDb = [&] (Effect& fx, double inDb)
    {
        fx.reset();
        const auto x = sine (220.0, 1.0, std::pow (10.0, inDb / 20.0), rate);
        return 20.0 * std::log10 (std::max (rms (run (fx, x), from), 1.0e-12) / rms (x, from));
    };

    auto fx = makeEffect ("la2a");
    fx->prepare (rate, 64);

    const double quiet = changeDb (*fx, -50.0);
    check (std::abs (quiet) < 0.3 && fx->meterValue() > -0.3f, "la2a: a quiet voice passes untouched (" + num (quiet, 2) + " dB)");

    const double loud = changeDb (*fx, -6.0);
    const float meter = fx->meterValue();
    check (loud < -3.0 && loud > -9.0 && meter < -3.0f, "la2a: a loud voice is turned down gently, about 3 to 1 (" + num (loud, 1) + " dB, meter " + num (meter, 1) + " dB)");

    fx->setParam ("mode", 1.0f);
    const double limited = changeDb (*fx, -6.0);
    check (limited < loud - 1.5, "la2a: Limit holds it harder than Compress (" + num (limited, 1) + " dB)");
    fx->setParam ("mode", 0.0f);

    fx->setParam ("reduction", 0.0f);
    const double none = changeDb (*fx, -6.0);
    check (std::abs (none) < 0.3, "la2a: with Peak reduction at 0 nothing is turned down (" + num (none, 2) + " dB)");
    fx->setParam ("reduction", 60.0f);

    // The two-stage release: after a long loud note, half the squeeze goes quickly and the rest hangs on.
    fx->reset();
    auto x = sine (220.0, 3.0, 0.5, rate);
    const auto tail = sine (220.0, 1.5, 0.003, rate);
    x.insert (x.end(), tail.begin(), tail.end());
    std::vector<float> meterAt;
    for (size_t at = 0; at < x.size(); at += 64)
    {
        float* p = x.data() + at;
        fx->process (&p, 1, (int) std::min<size_t> (64, x.size() - at));
        meterAt.push_back (-fx->meterValue());
    }
    const auto at = [&] (double seconds) { return (double) meterAt[std::min (meterAt.size() - 1, (size_t) (seconds * rate / 64.0))]; };
    const double held = at (2.95), soon = at (3.25), later = at (4.4);
    check (held > 6.0 && soon < held * 0.75 && soon > held * 0.2 && later < soon && later > 0.3,
           "la2a: after a long loud note it lets go in two stages: " + num (held, 1) + " dB of squeeze, " + num (soon, 1) + " dB a quarter of a second later, still " + num (later, 1) + " dB after 1.4 seconds");
}

} // namespace gigrig::fx
