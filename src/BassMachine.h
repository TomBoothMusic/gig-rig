#pragma once

// Plain C++ on purpose (no JUCE): the sound can be built and measured on its own,
// and the same file runs inside the app.

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace gigrig
{

/** Every setting of the bass sound. The first two are the Bass9's own knobs on its
    PRECISION patch; the rest are the parts of the sound the Bass9 hides inside. */
struct BassSettings
{
    float subMix   = 0.0f;      // CTRL 1: 0 = one octave down, 1 = two octaves down
    float tone     = 0.7f;      // CTRL 2: the P-bass tone knob, 0 = fully down, 1 = fully up
    float tracking = 0.6f;      // 0 = cleanest chords (slowest low notes), 1 = quickest low notes
    float lowDb    = 2.0f;      // body, around 90 Hz
    float lowMidDb = 0.0f;      // thump, around 250 Hz
    float midDb    = 1.0f;      // growl, around 800 Hz
    float compress = 0.35f;     // 0 = none, 1 = heavy and even
    float drive    = 0.1f;      // 0 = clean, 1 = a valve bass amp pushed hard
    float gateDb   = -66.0f;    // quieter than this at the input and the bass goes silent; -90 = off
    float levelDb  = 0.0f;      // output level

    bool operator== (const BassSettings&) const = default;

    /** "subMix=0.5\ntone=0.7\n...": one setting a line, so a preset is a small text file
        anyone can read or send. Unknown or missing lines keep their defaults. */
    std::string toText() const;
    static BassSettings fromText (const std::string& text);

    /** Every setting's name, range and default, for sliders and checks. */
    struct Range { const char* key; const char* label; float min, max, def; const char* unit; };
    static const std::vector<Range>& ranges();
    float get (const std::string& key) const;
    void set (const std::string& key, float value);
};

/** Guitar in, bass out: a polyphonic octave (and two-octave) down with a P-bass voicing.

    How it works, in order:
      1. A bank of narrow filters, six to an octave from 55 Hz to 2.4 kHz, splits the guitar
         into strands, so each string's notes and harmonics end up in their own filters.
         Each filter gives a "complex" output: an arrow that turns once per cycle.
      2. Each arrow is turned at half speed (the square root, kept continuous), which halves
         that strand's frequency without touching how loud it is or how it rises and dies
         away. Halving every harmonic of a note gives exactly the harmonics of the note an
         octave down, so a single note comes out as a clean, natural bass note with the
         guitar's own attack and dynamics. Two octaves down is the same again.
      3. Halving leaves each strand free to come out upside down. Neighbouring filters that
         hear the same harmonic are lined up with each other so they add rather than
         cancel (without this a note comes out at a random level between strong and almost
         gone, the "unnatural" sound of many octave pedals).
      4. The bass then goes through a model of a P-bass pickup and its passive tone knob,
         an EQ, a compressor, a soft valve-style drive and a level.

    Nothing looks ahead: the only delay is what the filters themselves take to ring up,
    a few ms on high notes and more on the lowest notes (see measuredDelayMs()). */
class BassMachine
{
public:
    void prepare (double sampleRate);
    void reset();

    /** Takes effect on the next process() call. Not thread-safe by itself: the engine
        hands settings across with its own lock-free swap. */
    void setSettings (const BassSettings& s);
    const BassSettings& settings() const noexcept   { return current; }

    /** In and out may be the same buffer. */
    void process (const float* in, float* out, int numSamples) noexcept;

    int numBands() const noexcept                   { return (int) bands.size(); }
    double sampleRate() const noexcept              { return rate; }

private:
    struct Band
    {
        std::complex<double> pole, s1, s2;      // two complex one-pole filters in a row
        double gain = 0.0;
        std::complex<double> w { 1.0, 0.0 };    // the half-speed arrow (one octave down)
        std::complex<double> v { 1.0, 0.0 };    // half again (two octaves down)
        std::complex<double> lastRatio { 1.0, 0.0 };
        double coherence = 1.0;                 // smoothed turning speed against the band below
        double coherenceLimit = 0.0;            // slower than this counts as the same harmonic
        double mag = 0.0;
    };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept   { z1 = z2 = 0.0; }
    };

    /** Third-order filter: the pickup, cable and tone knob. */
    struct Cubic
    {
        double b[4] {}, a[4] {}, z[3] {};
        double run (double x) noexcept
        {
            const double y = b[0] * x + z[0];
            z[0] = b[1] * x - a[1] * y + z[1];
            z[1] = b[2] * x - a[2] * y + z[2];
            z[2] = b[3] * x - a[3] * y;
            return y;
        }
        void clear() noexcept   { z[0] = z[1] = z[2] = 0.0; }
    };

    void buildBands();
    void buildVoicing();

    double rate = 48000.0;
    BassSettings current, pending;
    bool pendingChanged = false;

    std::vector<Band> bands;
    double coherenceCoeff = 0.0, bankScale = 1.0;

    Cubic pickup;
    Biquad highPass, low, lowMid, mid;
    double gateEnv = 0.0, gateGain = 0.0, gateAttack = 0.0, gateRelease = 0.0, gateHoldCoeff = 0.0;
    double compEnv = 0.0, compAttack = 0.0, compRelease = 0.0;
    double level = 1.0;
};

} // namespace gigrig
