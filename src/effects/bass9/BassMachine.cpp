#include "BassMachine.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace gigrig::fx::bass9
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double lowestBandHz = 70.0, highestBandHz = 4400.0;
    constexpr double innerRateWanted = 12000.0;
    constexpr double halfWidthOfPitch = 0.0578;         // a twelfth of an octave either side, before Tracking speed
    constexpr double widestHz = 13.0;                   // and never more than this: the low E's harmonics are 82 Hz apart
    constexpr double followTime = 0.5, quickTime = 0.33, steadyTime = 0.5, steadyLeastSeconds = 0.012;    // in filter ring times
    constexpr double wobbleAllowed = 0.7;
    constexpr double tiny = 1.0e-9;
    constexpr double quietest = 1.0e-7;                 // -140 dB: stands in for silence where a level is divided by
    constexpr double brightHz = 1000.0;
    constexpr double driveMost = 5.0;                   // how hard Drive at 1 pushes the peaks into the valve curve
    constexpr double pluckFromHz = 700.0, pluckToHz = 3500.0;
    constexpr double pluckOpen = 4.0, pluckShut = 1.5;  // the top of the guitar's sound jumping to 4 times its recent level opens the pluck fully, under 1.5 times shuts it
    constexpr double pluckMost = 10.0;                  // Pluck at 1 passes the guitar's attack ten times louder than it came in; at 0.5, two and a half times
    constexpr double keepAwake = 1.0e-18;   // far below hearing: stops the filters idling down into slow "denormal" numbers in dead silence

    using cd = std::complex<double>;

    double dbToGain (double db)     { return std::pow (10.0, db / 20.0); }

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
        { "pluck",    "Pluck (string noise)",   0.0f,   1.0f,   0.5f,   "" },
        { "brightDb", "String brightness",      0.0f,   18.0f,  9.0f,   "dB" },
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
    if (key == "pluck")     return pluck;
    if (key == "brightDb")  return brightDb;
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
    else if (key == "pluck")    pluck = value;
    else if (key == "brightDb") brightDb = value;
}

void BassSettings::setIndex (int index, float value) noexcept
{
    float* fields[] { &subMix, &tone, &tracking, &lowDb, &lowMidDb, &midDb, &compress, &drive, &gateDb, &levelDb, &pluck, &brightDb };

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

    // The bank only listens below 4.4 kHz, so it runs on every 2nd, 4th or 8th sample.
    step = std::clamp ((int) std::lround (rate / innerRateWanted), 1, (int) hist1.size());
    innerRate = rate / step;
    perStep = 1.0 / step;

    bands.assign ((size_t) maxBands, Band());
    signCoeff = 1.0 - std::exp (-1.0 / (0.002 * innerRate));

    {   // 2nd order Butterworth low-pass at 3 kHz on the bank's output: an octave below 4.4 kHz needs no more
        smoothHz = std::min (3000.0, 0.4 * innerRate);
        const double w = 2.0 * pi * smoothHz / innerRate;
        const double alpha = std::sin (w) / (2.0 * 0.7071);
        const double a0 = 1.0 + alpha, cw = std::cos (w);
        smooth.b0 = (1.0 - cw) / 2.0 / a0;
        smooth.b1 = (1.0 - cw) / a0;
        smooth.b2 = smooth.b0;
        smooth.a1 = -2.0 * cw / a0;
        smooth.a2 = (1.0 - alpha) / a0;
    }

    gateAttack  = 1.0 - std::exp (-1.0 / (0.001 * rate));
    gateRelease = 1.0 - std::exp (-1.0 / (0.060 * rate));
    gateHoldCoeff = 1.0 - std::exp (-1.0 / (0.010 * rate));
    compAttack  = 1.0 - std::exp (-1.0 / (0.004 * rate));
    compRelease = 1.0 - std::exp (-1.0 / (0.150 * rate));
    refAttack   = 1.0 - std::exp (-1.0 / (0.002 * rate));
    refRelease  = 1.0 - std::exp (-1.0 / (2.5 * rate));
    quickAttack = 1.0 - std::exp (-1.0 / (0.0001 * rate));
    quickRelease = 1.0 - std::exp (-1.0 / (0.008 * rate));
    slowAttack  = 1.0 - std::exp (-1.0 / (0.015 * rate));
    slowRelease = 1.0 - std::exp (-1.0 / (0.060 * rate));
    pluckSmooth = 1.0 - std::exp (-1.0 / (0.0005 * rate));

    buildBands();
    buildVoicing();
    reset();
}

void BassMachine::reset()
{
    for (auto& b : bands)
    {
        b.s1 = b.s2 = b.last = b.turn = b.turnQuick = {};
        b.w = b.v = b.lined = { 1.0, 0.0 };
        b.sign = b.signNow = 1.0;
        b.turned = b.turnedNow = { 1.0, 0.0 };
        b.offset = b.wobble = b.share = b.amp = b.size = 0.0;
    }

    pickup.clear();
    pluckPickup.clear();
    pluckQuick = pluckSlow = pluckGate = 0.0;
    compRef = driveRef = 0.0;
    for (auto* f : { &highPass, &low, &lowMid, &mid, &smooth, &bright, &pluckHigh, &pluckLow })
        f->clear();

    for (auto* h : { &hist1, &hist2, &hist3, &hist4, &hist5 })
        h->fill (0.0);
    box1 = box2 = box3 = box4 = box5 = bankNow = bankBefore = 0.0;
    histAt = phase = 0;
    gateEnv = gateGain = compEnv = 0.0;
}

void BassMachine::setSettings (const BassSettings& s)
{
    pending = s;
    pendingChanged = true;
}

void BassMachine::buildBands()
{
    // Tracking speed sets how wide the filters are. Wider rings up sooner (quicker notes) but
    // tells neighbouring harmonics and the notes of a chord apart less well. Low down each
    // filter is a fixed fraction of its pitch wide; higher up the width stops growing, so the
    // harmonics of a low note (82 Hz apart on the low E) each still get a filter to themselves.
    const double tracking = std::clamp ((double) current.tracking, 0.0, 1.0);
    const double fraction = halfWidthOfPitch * (0.8 + 1.6 * tracking);
    const double widest = widestHz;

    const int before = count;
    count = 0;

    for (double hz = lowestBandHz; hz <= highestBandHz && count < maxBands;)
    {
        const double half = std::min (fraction * hz, widest);
        auto& b = bands[(size_t) count++];
        const double turn = 2.0 * pi * hz / innerRate, halfTurnWide = 2.0 * pi * half / innerRate;
        const double r = std::exp (-halfTurnWide);
        b.hz = hz;
        b.halfHz = half;
        b.pole = std::polar (r, turn);
        b.centre = std::polar (1.0, turn);
        b.gain = 1.0 - r;
        b.perHalfWidth = r / (1.0 - r);

        // The running averages either side of the bank take a little off the top: put it back.
        const auto average = [this] (double f)
        {
            const double a = pi * f / rate;
            return step > 1 && a > 1.0e-9 ? std::abs (std::sin (a * step) / (step * std::sin (a))) : 1.0;
        };
        const double in4 = std::pow (average (hz), 4.0), out3 = std::pow (average (0.5 * hz), 3.0);
        const double tidy = 1.0 / std::sqrt (1.0 + std::pow (0.5 * hz / smoothHz, 4.0));
        b.trim = 2.0 / (in4 * out3 * tidy);     // 2: a filter that turns one way hears half of a real tone

        b.quickCoeff = 1.0 - std::exp (-1.0 / (quickTime / (2.0 * pi * half) * innerRate));
        b.followCoeff = 1.0 - std::exp (-1.0 / (followTime / (2.0 * pi * half) * innerRate));
        b.steadyCoeff = 1.0 - std::exp (-1.0 / (std::max (steadyLeastSeconds, steadyTime / (2.0 * pi * half)) * innerRate));

        if (fraction * hz >= widest)
            hz += 2.0 * widest;
        else
        {
            const double next = hz * (1.0 + fraction) / (1.0 - fraction);
            hz = fraction * next > widest ? hz + half + widest : next;
        }
    }

    // How far each band's say reaches: to the centre of the next band either side.
    for (int k = 0; k < count; ++k)
    {
        auto& b = bands[(size_t) k];
        b.perGapDown = b.halfHz / (k > 0 ? b.hz - bands[(size_t) (k - 1)].hz : 2.0 * b.halfHz);
        b.perGapUp   = b.halfHz / (k + 1 < count ? bands[(size_t) (k + 1)].hz - b.hz : 2.0 * b.halfHz);
    }

    // Filters no longer in use forget what they held.
    for (int k = count; k < std::max (before, count) && k < maxBands; ++k)
    {
        auto& b = bands[(size_t) k];
        b.s1 = b.s2 = b.last = b.turn = b.turnQuick = {};
        b.share = b.amp = b.size = 0.0;
    }
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
        pickup.b[i] = pluckPickup.b[i] = bz[(size_t) i] * dcFix / norm;
        pickup.a[i] = pluckPickup.a[i] = az[(size_t) i] / norm;
    }

    // ---- the pluck: the guitar's own attack above 700 Hz, with the fizz above 3.5 kHz taken off ----
    {
        const auto butter = [this] (Biquad& f, double hz, bool high)
        {
            const double w = 2.0 * pi * std::min (hz, 0.45 * rate) / rate;
            const double alpha = std::sin (w) / (2.0 * 0.7071);
            const double a0 = 1.0 + alpha, cw = std::cos (w);
            f.b0 = (high ? 1.0 + cw : 1.0 - cw) / 2.0 / a0;
            f.b1 = (high ? -(1.0 + cw) : 1.0 - cw) / a0;
            f.b2 = f.b0;
            f.a1 = -2.0 * cw / a0;
            f.a2 = (1.0 - alpha) / a0;
        };
        butter (pluckHigh, pluckFromHz, true);
        butter (pluckLow, pluckToHz, false);
        pluckLevel = pluckMost * current.pluck * current.pluck;
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

    // ---- string brightness ----
    // Halving the guitar also halves where its pickups roll the top off, so the octave comes
    // out an octave duller than a bass string through a bass pickup. A lift above 1 kHz puts
    // that back, and gives the tone knob below something to work on.
    {
        const double A = std::pow (10.0, current.brightDb / 40.0);
        const double w = 2.0 * pi * brightHz / rate, cw = std::cos (w);
        const double alpha = std::sin (w) / 2.0 * std::sqrt (2.0);      // shelf slope 1
        const double root = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * cw + root;
        bright.b0 = A * ((A + 1.0) + (A - 1.0) * cw + root) / a0;
        bright.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
        bright.b2 = A * ((A + 1.0) + (A - 1.0) * cw - root) / a0;
        bright.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
        bright.a2 = ((A + 1.0) - (A - 1.0) * cw - root) / a0;
    }

    setPeak (rate, 90.0,  0.9, current.lowDb,    low.b0, low.b1, low.b2, low.a1, low.a2);
    setPeak (rate, 250.0, 1.0, current.lowMidDb, lowMid.b0, lowMid.b1, lowMid.b2, lowMid.a1, lowMid.a2);
    setPeak (rate, 800.0, 1.2, current.midDb,    mid.b0, mid.b1, mid.b2, mid.a1, mid.a2);

    level = dbToGain (current.levelDb);
}

double BassMachine::runBank (double x, double sub) noexcept
{
    const bool wantTwo = sub > 0.0;
    double one = 0.0, two = 0.0;

    for (int k = 0; k < count; ++k)
    {
        auto& b = bands[(size_t) k];
        b.s1 = b.gain * x + b.pole * b.s1;
        b.s2 = b.gain * b.s1 + b.pole * b.s2;

        const cd z = b.s2;
        const double m = std::sqrt (std::norm (z));

        // How fast the arrow really turns, against the band's own pitch: where the tone sits.
        const cd d = z * std::conj (b.last);
        const double nowSize = m * b.size;
        b.last = z;
        b.size = m;
        b.turn += b.followCoeff * (d - b.turn);
        b.turnQuick += b.quickCoeff * (d - b.turnQuick);
        const cd rot = b.turn * std::conj (b.centre), rotQuick = b.turnQuick * std::conj (b.centre);

        b.signNow += signCoeff * (b.sign - b.signNow);

        if (nowSize < tiny * tiny || rot.real() < tiny * tiny || rotQuick.real() < tiny * tiny)
        {
            b.share = 0.0;
            b.amp = 0.0;
            continue;
        }

        const double offset = std::clamp (rot.imag() / rot.real() * b.perHalfWidth, -4.0, 4.0);
        const double offsetNow = std::clamp (rotQuick.imag() / rotQuick.real() * b.perHalfWidth, -8.0, 8.0);
        const double dev = offsetNow - offset;
        b.wobble += b.steadyCoeff * (dev * dev - b.wobble);
        b.offset = offset;

        // The half-speed arrow. Stepping it on by half of the arrow's own step keeps it continuous.
        cd q = m * b.w + z * std::conj (b.w);
        const double qq = std::norm (q);
        q = qq > tiny * tiny * m * m ? q / std::sqrt (qq) : b.w * cd (0.0, 1.0);
        b.w = q;

        // The filter lags a tone above its centre and leads one below, by a known angle.
        // Taking half of that out leaves every band that hears a tone pointing the same way.
        const double stretch = 1.0 + offset * offset;
        b.lined = q * cd (1.0, offset);     // (not unit length: stretch^0.5 long)

        // Each tone is spoken for by the one or two bands nearest to it, and only while it is steady.
        const double near = std::max (0.0, 1.0 - std::abs (offset) * (offset < 0.0 ? b.perGapDown : b.perGapUp));
        const double steady = 1.0 / (1.0 + (b.wobble * b.wobble) / (wobbleAllowed * wobbleAllowed));
        b.share = near * steady;
        const double amp = b.trim * m * std::sqrt (stretch) * b.share;      // times lined: trim * m * stretch * share

        bool sameTone = false;

        if (k > 0)
        {
            auto& below = bands[(size_t) (k - 1)];
            sameTone = b.share > 0.02 && below.share > 0.02
                       && std::abs ((b.hz + offset * b.halfHz) - (below.hz + below.offset * below.halfHz)) < 0.6 * b.halfHz;

            if (sameTone && b.sign * below.sign * dot (b.lined, below.lined) < 0.0)
            {
                if (amp <= below.amp)   b.sign = -b.sign;
                else                    below.sign = -below.sign;
            }
        }

        b.amp = amp;
        one += amp * b.signNow * b.lined.real();

        if (wantTwo)
        {
            // Half again, from the same lined-up arrow. A quarter-speed arrow can sit at any of
            // four quarter turns against its neighbour's, so here a band is turned (smoothly)
            // rather than flipped.
            const double perLength = 1.0 / std::sqrt (stretch);
            cd u = b.v + b.lined * perLength * std::conj (b.v);
            const double uu = std::norm (u);
            u = uu > tiny ? u / std::sqrt (uu) : b.v * cd (0.0, 1.0);
            b.v = u;

            if (k > 0)
            {
                auto& below = bands[(size_t) (k - 1)];

                if (sameTone)
                {
                    const cd apart = (b.turned * u) * std::conj (below.turned * below.v);

                    if (apart.real() < 0.7)
                    {
                        const cd quarter = std::abs (apart.real()) > std::abs (apart.imag()) ? cd (apart.real() > 0.0 ? 1.0 : -1.0, 0.0)
                                                                                            : cd (0.0, apart.imag() > 0.0 ? 1.0 : -1.0);
                        if (amp <= below.amp)   b.turned *= std::conj (quarter);
                        else                    below.turned *= quarter;
                    }
                }
            }

            b.turnedNow += signCoeff * (b.turned - b.turnedNow);
            two += amp * std::sqrt (stretch) * (b.turnedNow * u).real();
        }
    }

    return (1.0 - sub) * one + sub * two;
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
    const double gateOpen = dbToGain (current.gateDb);
    const double gateClose = gateOpen * 0.5;
    const bool gateOff = current.gateDb <= -89.0f;

    // Compression and drive work against how loud the playing has lately been (its peaks over
    // the last couple of seconds), not against a fixed level, so they do the same thing to a
    // quiet guitar as to a hot one. (They used to need a guitar near full scale: Tom's arrived
    // 28 dB down and neither did anything he could hear, 25 Nov 2026.)
    const double ratio = 1.0 + 5.0 * current.compress;
    const double depthDb = 6.0 + 18.0 * current.compress;       // the squeeze starts this far below the recent peaks
    const double makeupDb = depthDb * (1.0 - 1.0 / ratio) * 0.85;     // about as loud squeezed as not
    const double driveK = driveMost * std::pow ((double) current.drive, 1.5);
    const bool driving = driveK > 1.0e-3;
    const double driveTrim = driving ? 0.5 * driveK / std::tanh (0.5 * driveK) : 1.0;    // keeps a middling note as loud as it was

    for (int i = 0; i < numSamples; ++i)
    {
        const double x = in[i];

        // ---- down to the bank's own rate: four running averages, each one bank step long ----
        box1 += x - hist1[(size_t) histAt];
        hist1[(size_t) histAt] = x;
        const double a1 = box1 * perStep;
        box2 += a1 - hist2[(size_t) histAt];
        hist2[(size_t) histAt] = a1;
        const double a2 = box2 * perStep;
        box3 += a2 - hist3[(size_t) histAt];
        hist3[(size_t) histAt] = a2;
        const double a3 = box3 * perStep;
        box5 += a3 - hist5[(size_t) histAt];
        hist5[(size_t) histAt] = a3;

        if (++phase >= step)
        {
            phase = 0;
            bankBefore = bankNow;
            bankNow = smooth.run (runBank (box5 * perStep + keepAwake, sub));
        }

        // ---- and back up: a straight line between the bank's outputs, then one more running average ----
        const double line = bankBefore + (bankNow - bankBefore) * ((double) (phase + 1) * perStep);
        box4 += line - hist4[(size_t) histAt];
        hist4[(size_t) histAt] = line;
        histAt = histAt + 1 < step ? histAt + 1 : 0;
        double y = box4 * perStep + keepAwake;

        // ---- string brightness, pickup and tone knob, EQ ----
        y = pickup.run (bright.run (y));
        y = highPass.run (y);
        y = low.run (y);
        y = lowMid.run (y);
        y = mid.run (y);

        // ---- compressor ----
        const double level0 = std::abs (y);
        compEnv += (level0 > compEnv ? compAttack : compRelease) * (level0 - compEnv);
        compRef += (level0 > compRef ? refAttack : refRelease) * (level0 - compRef);
        if (current.compress > 0.0f)
        {
            const double over = 20.0 * std::log10 ((compEnv + quietest) / (compRef + quietest)) + depthDb;
            const double cutDb = over > 0.0 ? -over * (1.0 - 1.0 / ratio) : 0.0;
            y *= dbToGain (cutDb + makeupDb);
        }

        // ---- drive: a soft valve-style squash of the peaks ----
        const double level1 = std::abs (y);
        driveRef += (level1 > driveRef ? refAttack : refRelease) * (level1 - driveRef);
        if (driving)
        {
            const double ref = driveRef + quietest;
            y = ref * driveTrim * std::tanh (driveK * y / ref) / driveK;
        }

        // ---- the pluck: the front edge of each note, straight from the guitar ----
        // The octave takes 20 to 50 ms to ring up, so on its own every note swells in and the
        // bass sounds like an octave pedal. A bass string gives a short click first. The
        // guitar's own attack is let through for the first few ms of each note and no longer:
        // it opens when the top of the guitar's sound jumps up and shuts as soon as that
        // settles, so nothing of the guitar comes through while a note rings.
        {
            const double top = pluckHigh.run (x) + keepAwake;
            const double size = std::abs (top);
            pluckQuick += (size > pluckQuick ? quickAttack : quickRelease) * (size - pluckQuick);
            pluckSlow += (pluckQuick > pluckSlow ? slowAttack : slowRelease) * (pluckQuick - pluckSlow);
            const double jump = pluckQuick / (pluckSlow + quietest);
            const double open = std::clamp ((jump - pluckShut) / (pluckOpen - pluckShut), 0.0, 1.0);
            pluckGate += pluckSmooth * (open - pluckGate);
            if (pluckGate < 1.0e-6)
                pluckGate = 0.0;
            const double click = pluckPickup.run (pluckLow.run (top) * pluckGate + keepAwake);

            if (pluckLevel > 0.0)
                y += pluckLevel * click;
        }

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
