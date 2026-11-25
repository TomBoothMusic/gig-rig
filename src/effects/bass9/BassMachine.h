#pragma once

// Plain C++ on purpose (no JUCE): the sound can be built and measured on its own,
// and the same file runs inside the app.

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace gigrig::fx::bass9
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
    /** The same by position in ranges(): no text, so safe on the audio thread. */
    void setIndex (int index, float value) noexcept;
};

/** Guitar in, bass out: a polyphonic octave (and two-octave) down with a P-bass voicing.

    How it works, in order:
      1. A bank of narrow filters from 70 Hz to 2.4 kHz splits the guitar into strands. Low
         down each filter is a fixed fraction of its pitch wide; higher up the width stops
         growing, so the harmonics of a low note (82 Hz apart on the low E) still land in
         filters of their own. Each filter gives a "complex" output: an arrow that turns once
         per cycle. The bank runs at about 12 kHz (every 8th sample at 96 kHz), which is
         plenty for what it listens to and keeps it cheap.
      2. Each band works out where its tone really sits (from how fast its arrow turns) and
         how steady it is. A tone is then spoken for only by the one or two bands nearest to
         it, with the filter's own loss and lag at that pitch taken back out, so every pitch
         comes out at the same level. A band that hears two tones fighting, or only hiss,
         has an unsteady arrow and is turned down: that band would come out as static.
      3. Each arrow is turned at half speed (stepped on by half of its own step, so it stays
         continuous), which halves that strand's frequency without touching how loud it is
         or how it rises and dies away. Halving every harmonic of a note gives exactly the
         harmonics of the note an octave down. Two octaves down is the same again.
      4. Halving leaves each strand free to come out upside down. Two neighbouring bands
         that speak for the same tone are kept the same way up, and when one has to turn
         over it is the quieter of the two and it fades through zero in a few ms. (The first
         version flipped bands in one sample, at full level, many times a second on a real
         guitar: Tom heard it on 25 Nov 2026 as ticking and static.)
      5. The bass then goes through a model of a P-bass pickup and its passive tone knob,
         an EQ, a compressor, a soft valve-style drive and a level.

    Nothing looks ahead: the only delay is what the filters themselves take to ring up,
    a few ms on high notes and more on the lowest notes, plus a third of a millisecond for
    the change of rate. */
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

    int numBands() const noexcept                   { return count; }
    double sampleRate() const noexcept              { return rate; }

private:
    static constexpr int maxBands = 160;

    struct Band
    {
        std::complex<double> pole, s1, s2;      // two complex one-pole filters in a row
        std::complex<double> centre;            // one sample's turn at the band's own pitch
        double gain = 0.0, trim = 2.0;
        double perHalfWidth = 0.0;              // 1 / (half the band's width, as a turn per sample)
        double followCoeff = 0.0, steadyCoeff = 0.0;
        std::complex<double> last;              // the band's output one sample ago
        std::complex<double> turn;              // smoothed turn per sample: how fast its arrow really goes round
        double offset = 0.0;                    // the tone's pitch against the band's centre, in half-widths
        double wobble = 0.0;                    // how unsteady that pitch is (one steady tone: 0)
        std::complex<double> w { 1.0, 0.0 };    // the half-speed arrow (one octave down)
        std::complex<double> v { 1.0, 0.0 };    // half again (two octaves down)
        std::complex<double> lined { 1.0, 0.0 };    // the half-speed arrow with the filter's own lag taken out
        double size = 0.0;                      // how long the band's arrow was one sample ago
        double perGapDown = 0.5, perGapUp = 0.5;    // half-width over the distance to the next band's centre
        double sign = 1.0, signNow = 1.0;       // which way up it plays, and the smooth move there
        std::complex<double> turned { 1.0, 0.0 }, turnedNow { 1.0, 0.0 };   // the same for two octaves down: a quarter turn at a time
        double share = 0.0;                     // how much of its tone this band speaks for
        double hz = 0.0, halfHz = 0.0, amp = 0.0;
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
    double runBank (double x, double sub) noexcept;
    void buildVoicing();

    double rate = 48000.0;
    BassSettings current, pending;
    bool pendingChanged = false;

    std::vector<Band> bands;
    int count = 0;                              // bands in use (depends on Tracking speed)
    int step = 1, phase = 0;                    // the bank runs on every step-th sample
    double innerRate = 12000.0, smoothHz = 3000.0;
    double perStep = 1.0;
    double box1 = 0.0, box2 = 0.0, box3 = 0.0, box4 = 0.0;      // running averages before and after the bank
    std::array<double, 16> hist1 {}, hist2 {}, hist3 {}, hist4 {};
    int histAt = 0;
    double bankNow = 0.0, bankBefore = 0.0;     // the bank's last two outputs, joined by a straight line
    double signCoeff = 0.0;
    Biquad smooth;                              // tidies the bank's output before it goes back to the full rate

    Cubic pickup;
    Biquad highPass, low, lowMid, mid;
    double gateEnv = 0.0, gateGain = 0.0, gateAttack = 0.0, gateRelease = 0.0, gateHoldCoeff = 0.0;
    double compEnv = 0.0, compAttack = 0.0, compRelease = 0.0;
    double level = 1.0;
};

} // namespace gigrig
