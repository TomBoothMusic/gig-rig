#include "BassMachine.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace gigrig::fx::bass9
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int bandsPerOctave = 6;
    constexpr double lowestBandHz = 55.0, highestBandHz = 2400.0;
    constexpr double tiny = 1.0e-9;

    using cd = std::complex<double>;

    double dbToGain (double db)     { return std::pow (10.0, db / 20.0); }

    /** The square root of a unit arrow, on the right-hand side (no trig needed). */
    cd halfTurn (cd unit) noexcept
    {
        const double re = std::sqrt (std::max (0.0, 0.5 * (1.0 + unit.real())));
        const double im = std::sqrt (std::max (0.0, 0.5 * (1.0 - unit.real())));
        return { re, unit.imag() < 0.0 ? -im : im };
    }

    double dot (cd a, cd b) noexcept    { return a.real() * b.real() + a.imag() * b.imag(); }

    /** RBJ cookbook peaking EQ. */
    void setPeak (double rate, double hz, double q, double db, double& b0, double& b1, double& b2, double& a1, double& a2)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2.0 * pi * hz / rate;
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = -2.0 * std::cos (w) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = b1;
        a2 = (1.0 - alpha / A) / a0;
    }

    /** Multiplies two polynomials in z^-1. */
    std::vector<double> times (const std::vector<double>& a, const std::vector<double>& b)
    {
        std::vector<double> r (a.size() + b.size() - 1, 0.0);
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = 0; j < b.size(); ++j)
                r[i + j] += a[i] * b[j];
        return r;
    }

    /** Bilinear transform of a third-order (or lower) polynomial in s into z^-1. */
    std::array<double, 4> bilinear3 (const double (&c)[4], double rate)
    {
        const double k = 2.0 * rate;
        std::array<double, 4> out {};
        double kp = 1.0;

        for (int i = 0; i < 4; ++i)
        {
            std::vector<double> poly { c[i] * kp };
            for (int m = 0; m < i; ++m)      poly = times (poly, { 1.0, -1.0 });
            for (int m = i; m < 3; ++m)      poly = times (poly, { 1.0, 1.0 });
            for (int j = 0; j < 4; ++j)      out[(size_t) j] += poly[(size_t) j];
            kp *= k;
        }

        return out;
    }
}

//==============================================================================
const std::vector<BassSettings::Range>& BassSettings::ranges()
{
    static const std::vector<Range> list {
        { "subMix",   "Sub mix (CTRL 1)",       0.0f,   1.0f,   0.0f,   "" },
        { "tone",     "Tone (CTRL 2)",          0.0f,   1.0f,   0.7f,   "" },
        { "tracking", "Tracking speed",         0.0f,   1.0f,   0.6f,   "" },
        { "lowDb",    "Body (90 Hz)",           -12.0f, 12.0f,  2.0f,   "dB" },
        { "lowMidDb", "Thump (250 Hz)",         -12.0f, 12.0f,  0.0f,   "dB" },
        { "midDb",    "Growl (800 Hz)",         -12.0f, 12.0f,  1.0f,   "dB" },
        { "compress", "Compression",            0.0f,   1.0f,   0.35f,  "" },
        { "drive",    "Drive",                  0.0f,   1.0f,   0.1f,   "" },
        { "gateDb",   "Noise gate",             -90.0f, -30.0f, -66.0f, "dB" },
        { "levelDb",  "Level",                  -24.0f, 24.0f,  0.0f,   "dB" },
    };
    return list;
}

float BassSettings::get (const std::string& key) const
{
    if (key == "subMix")    return subMix;
    if (key == "tone")      return tone;
    if (key == "tracking")  return tracking;
    if (key == "lowDb")     return lowDb;
    if (key == "lowMidDb")  return lowMidDb;
    if (key == "midDb")     return midDb;
    if (key == "compress")  return compress;
    if (key == "drive")     return drive;
    if (key == "gateDb")    return gateDb;
    if (key == "levelDb")   return levelDb;
    return 0.0f;
}

void BassSettings::set (const std::string& key, float value)
{
    for (const auto& r : ranges())
        if (key == r.key)
            value = std::clamp (value, r.min, r.max);

    if (key == "subMix")        subMix = value;
    else if (key == "tone")     tone = value;
    else if (key == "tracking") tracking = value;
    else if (key == "lowDb")    lowDb = value;
    else if (key == "lowMidDb") lowMidDb = value;
    else if (key == "midDb")    midDb = value;
    else if (key == "compress") compress = value;
    else if (key == "drive")    drive = value;
    else if (key == "gateDb")   gateDb = value;
    else if (key == "levelDb")  levelDb = value;
}

void BassSettings::setIndex (int index, float value) noexcept
{
    float* fields[] { &subMix, &tone, &tracking, &lowDb, &lowMidDb, &midDb, &compress, &drive, &gateDb, &levelDb };

    if (index >= 0 && index < (int) (sizeof (fields) / sizeof (fields[0])))
        *fields[index] = value;
}

std::string BassSettings::toText() const
{
    std::ostringstream s;
    for (const auto& r : ranges())
        s << r.key << '=' << get (r.key) << '\n';
    return s.str();
}

BassSettings BassSettings::fromText (const std::string& text)
{
    BassSettings result;
    std::istringstream lines (text);
    std::string line;

    while (std::getline (lines, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos || line[0] == '#')
            continue;

        auto key = line.substr (0, eq);
        key.erase (std::remove_if (key.begin(), key.end(), [] (unsigned char c) { return std::isspace (c); }), key.end());

        try                 { result.set (key, std::stof (line.substr (eq + 1))); }
        catch (...)         {}
    }

    return result;
}

//==============================================================================
void BassMachine::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    const int count = (int) std::floor (std::log2 (highestBandHz / lowestBandHz) * bandsPerOctave) + 1;
    bands.assign ((size_t) count, Band());

    // How quickly two neighbouring bands are judged to hear the same harmonic.
    coherenceCoeff = 1.0 - std::exp (-1.0 / (0.004 * rate));

    gateAttack  = 1.0 - std::exp (-1.0 / (0.001 * rate));
    gateRelease = 1.0 - std::exp (-1.0 / (0.060 * rate));
    gateHoldCoeff = 1.0 - std::exp (-1.0 / (0.010 * rate));
    compAttack  = 1.0 - std::exp (-1.0 / (0.004 * rate));
    compRelease = 1.0 - std::exp (-1.0 / (0.150 * rate));

    buildBands();
    buildVoicing();
    reset();
}

void BassMachine::reset()
{
    for (auto& b : bands)
    {
        b.s1 = b.s2 = {};
        b.w = b.v = b.lastRatio = { 1.0, 0.0 };
        b.coherence = pi;
        b.mag = 0.0;
    }

    pickup.clear();
    for (auto* f : { &highPass, &low, &lowMid, &mid })
        f->clear();

    gateEnv = gateGain = compEnv = 0.0;
}

void BassMachine::setSettings (const BassSettings& s)
{
    pending = s;
    pendingChanged = true;
}

void BassMachine::buildBands()
{
    // Tracking speed widens the filters: wider rings up sooner (quicker low notes)
    // but lets neighbouring notes of a chord bleed into each other more.
    const double widen = 0.8 + 1.6 * std::clamp ((double) current.tracking, 0.0, 1.0);
    const double spread = std::pow (2.0, 0.5 / bandsPerOctave) - std::pow (2.0, -0.5 / bandsPerOctave);

    for (size_t k = 0; k < bands.size(); ++k)
    {
        const double hz = lowestBandHz * std::pow (2.0, (double) k / bandsPerOctave);
        const double width = hz * spread * widen;
        const double r = std::exp (-pi * width / rate);
        bands[k].pole = std::polar (r, 2.0 * pi * hz / rate);
        bands[k].gain = 1.0 - r;

        // Two neighbouring bands drifting against each other by less than 3% of the
        // band's pitch (half a semitone) are taken to be hearing the same harmonic.
        bands[k].coherenceLimit = 2.0 * pi * 0.03 * hz / rate;
    }

    // Level: the bands overlap, so a steady note comes through several of them at once.
    // Average how much gets through across the guitar's range, so the bass comes out
    // about as loud as the guitar went in.
    double total = 0.0;
    int n = 0;

    for (double hz = 82.0; hz < 1200.0; hz *= std::pow (2.0, 1.0 / 24.0), ++n)
    {
        const cd e = std::polar (1.0, -2.0 * pi * hz / rate);
        double sum = 0.0;

        for (const auto& b : bands)
            sum += std::norm (b.gain / (1.0 - b.pole * e));     // two stages: |H|^2 of one

        total += sum;
    }

    // The 1.5 is measured: lined-up bands still meet at slight angles, so they add to a little less than the sum.
    bankScale = 1.5 * 2.0 / std::max (tiny, total / std::max (1, n));
}

void BassMachine::buildVoicing()
{
    // ---- P-bass pickup, cable and tone knob (passive circuit) ----
    // Pickup: about 10.5 kOhm and 3.5 H. Volume pot 250k (full up), tone pot 250k (audio taper)
    // in series with a 0.047 uF cap, about 470 pF of pickup plus cable, 1 MOhm amp input.
    const double rc = 10.5e3, l = 3.5, c = 470e-12, ct = 47e-9, rvol = 250e3, ramp = 1.0e6;
    const double toneKnob = std::clamp ((double) current.tone, 0.0, 1.0);
    const double rt = 250e3 * (std::pow (10.0, 2.0 * toneKnob) - 1.0) / 99.0 + 100.0;
    const double g = 1.0 / rvol + 1.0 / ramp;
    const double t = ct * rt;

    const double num[4] { 1.0, t, 0.0, 0.0 };
    double den[4];
    den[0] = 1.0 + rc * g;
    den[1] = t + rc * (g * t + c + ct) + l * g;
    den[2] = rc * c * t + l * (g * t + c + ct);
    den[3] = l * c * t;

    auto bz = bilinear3 (num, rate);
    auto az = bilinear3 (den, rate);
    const double norm = az[0];
    const double dcFix = den[0];   // unity gain at low frequencies

    for (int i = 0; i < 4; ++i)
    {
        pickup.b[i] = bz[(size_t) i] * dcFix / norm;
        pickup.a[i] = az[(size_t) i] / norm;
    }

    // ---- clean-up high-pass (2nd order Butterworth, 25 Hz) ----
    {
        const double w = 2.0 * pi * 25.0 / rate;
        const double alpha = std::sin (w) / (2.0 * 0.7071);
        const double a0 = 1.0 + alpha, cw = std::cos (w);
        highPass.b0 = (1.0 + cw) / 2.0 / a0;
        highPass.b1 = -(1.0 + cw) / a0;
        highPass.b2 = highPass.b0;
        highPass.a1 = -2.0 * cw / a0;
        highPass.a2 = (1.0 - alpha) / a0;
    }

    setPeak (rate, 90.0,  0.9, current.lowDb,    low.b0, low.b1, low.b2, low.a1, low.a2);
    setPeak (rate, 250.0, 1.0, current.lowMidDb, lowMid.b0, lowMid.b1, lowMid.b2, lowMid.a1, lowMid.a2);
    setPeak (rate, 800.0, 1.2, current.midDb,    mid.b0, mid.b1, mid.b2, mid.a1, mid.a2);

    level = dbToGain (current.levelDb);
}

void BassMachine::process (const float* in, float* out, int numSamples) noexcept
{
    if (pendingChanged)
    {
        const bool rebuildBands = std::abs (pending.tracking - current.tracking) > 1.0e-6f;
        current = pending;
        pendingChanged = false;

        if (rebuildBands)
            buildBands();

        buildVoicing();
    }

    const double sub = std::clamp ((double) current.subMix, 0.0, 1.0);
    const bool wantTwo = sub > 0.0;
    const double gateOpen = dbToGain (current.gateDb);
    const double gateClose = gateOpen * 0.5;
    const bool gateOff = current.gateDb <= -89.0f;

    const double ratio = 1.0 + 5.0 * current.compress;
    const double thresholdDb = -12.0 - 18.0 * current.compress;
    const double makeupDb = -thresholdDb * (1.0 - 1.0 / ratio) * 0.5;
    const double driveK = 1.0 + 8.0 * current.drive * current.drive;

    const size_t count = bands.size();

    for (int i = 0; i < numSamples; ++i)
    {
        const double x = in[i];

        // ---- 1 to 3: split, halve, line up ----
        double one = 0.0, two = 0.0;
        cd belowUnit { 1.0, 0.0 };
        double belowMag = 0.0;

        for (size_t k = 0; k < count; ++k)
        {
            auto& b = bands[k];
            b.s1 = b.gain * x + b.pole * b.s1;
            b.s2 = b.gain * b.s1 + b.pole * b.s2;

            const double m = std::abs (b.s2);
            b.mag = m;

            if (m < tiny)
            {
                b.coherence = pi;
                belowMag = 0.0;
                continue;
            }

            const cd unit = b.s2 / m;

            cd q = halfTurn (unit);
            if (dot (q, b.w) < 0.0)
                q = -q;

            // Line up with the band below when both hear the same harmonic.
            bool lined = false;

            if (belowMag > tiny && m > 0.1 * belowMag && belowMag > 0.1 * m)
            {
                const cd rel = unit * std::conj (belowUnit);
                const cd turn = rel * std::conj (b.lastRatio);
                const double speed = turn.real() > 0.0 ? std::abs (turn.imag()) : pi;
                b.coherence += coherenceCoeff * (speed - b.coherence);
                b.lastRatio = rel;

                if (b.coherence < b.coherenceLimit)
                {
                    const cd target = bands[k - 1].w * halfTurn (rel);
                    if (dot (q, target) < 0.0)
                    {
                        q = -q;
                        b.v *= cd (0.0, 1.0);    // keep the two-octave arrow next to its new parent
                    }
                    lined = true;
                }
            }
            else
            {
                b.coherence = pi;
                if (k > 0)
                    b.lastRatio = unit * std::conj (belowUnit);
            }

            b.w = q;
            one += m * q.real();

            if (wantTwo)
            {
                cd u = halfTurn (q);
                if (dot (u, b.v) < 0.0)
                    u = -u;

                if (lined)
                {
                    const cd target = bands[k - 1].v * halfTurn (q * std::conj (bands[k - 1].w));
                    if (dot (u, target) < 0.0)
                        u = -u;
                }

                b.v = u;
                two += m * u.real();
            }

            belowUnit = unit;
            belowMag = m;
        }

        double y = bankScale * ((1.0 - sub) * one + sub * two);

        // ---- 4: pickup and tone knob, EQ ----
        y = pickup.run (y);
        y = highPass.run (y);
        y = low.run (y);
        y = lowMid.run (y);
        y = mid.run (y);

        // ---- compressor ----
        const double level0 = std::abs (y);
        compEnv += (level0 > compEnv ? compAttack : compRelease) * (level0 - compEnv);
        if (current.compress > 0.0f)
        {
            const double envDb = 20.0 * std::log10 (compEnv + 1.0e-12);
            const double over = envDb - thresholdDb;
            const double cutDb = over > 0.0 ? -over * (1.0 - 1.0 / ratio) : 0.0;
            y *= dbToGain (cutDb + makeupDb);
        }

        // ---- drive ----
        if (current.drive > 0.0f)
            y = std::tanh (driveK * y) / driveK;

        // ---- gate, keyed by the guitar itself ----
        const double ax = std::abs (x);
        gateEnv += (ax > gateEnv ? gateAttack : gateHoldCoeff) * (ax - gateEnv);

        double gateTarget = 1.0;
        if (! gateOff)
            gateTarget = gateEnv > gateOpen ? 1.0 : (gateEnv < gateClose ? 0.0 : (gateGain > 0.5 ? 1.0 : 0.0));

        gateGain += (gateTarget > gateGain ? gateAttack : gateRelease) * (gateTarget - gateGain);

        out[i] = (float) (y * gateGain * level);
    }
}

} // namespace gigrig
