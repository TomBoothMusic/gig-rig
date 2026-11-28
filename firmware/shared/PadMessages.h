// What the board's controller (the Teensy) and the app say to each other about the kick pad.
//
// This one file is built into BOTH programs: the controller's (firmware/bench-test) and the app
// (src/KickPad.cpp). So the two can never disagree about a number, and the app's self-test
// checks the very code the controller runs. Plain C++ only: nothing from Arduino or JUCE.
//
// Everything travels as ordinary MIDI over the controller's USB lead, on channel 10:
//
//   Controller to app, for each stomp (all three in one go):
//     CC 20 = the spike's height, upper part     CC 21 = lower part     (see padSplit)
//     Note 60 on, strength 1 to 127              (what the SPD::ONE Kick sends, so any MIDI
//                                                 program can play the pad; the app plays this)
//   Controller to app, a knock that did not count (at most four times a second):
//     CC 22 / CC 23 = how tall it was
//   Controller to app, when asked or after a change:
//     CC 24 / CC 25 = "Counts from"     CC 26 / CC 27 = "Hardest at"     CC 30 = program number
//   App to controller:
//     CC 24 / CC 25 and CC 26 / CC 27 = set the two levels (the controller remembers them)
//     CC 31 = "tell me what you hold"
//
// Heights are what the controller's pin reads: 0 to 4095. A pair of CCs carries one height: the
// upper part first, the lower part second, and the value is complete when the lower part lands.
//
// These numbers are saved nowhere on Tom's disk, but a controller already loaded with an older
// program still uses them: add new CCs, never give an old one a new meaning.

#pragma once

#include <math.h>
#include <stdint.h>

namespace gigrig
{
namespace padmsg
{
constexpr int channel = 10;
constexpr int note = 60;

constexpr int ccPeakHigh = 20, ccPeakLow = 21;
constexpr int ccIgnoredHigh = 22, ccIgnoredLow = 23;
constexpr int ccThresholdHigh = 24, ccThresholdLow = 25;
constexpr int ccFullHigh = 26, ccFullLow = 27;
constexpr int ccProgram = 30;
constexpr int ccAsk = 31;

constexpr int program = 2;              // 1 was the first bench program, which sent the note alone

constexpr int heightMax = 4095;
constexpr int thresholdDefault = 80;    // first guesses (28 Nov 2026), set from Tom's results
constexpr int fullDefault = 3000;
constexpr int thresholdMin = 20, thresholdMax = 2000;
constexpr int fullMin = 200;

constexpr int readEveryMicros = 100;    // how often the controller reads the pad's pin
constexpr int scanMicros = 2000;        // the controller watches a spike for its top this long, as the app does on a socket
constexpr int holdMillis = 40;          // then looks away this long, so one stomp is one hit

/** A height, 0 to 4095, as the two parts a pair of CCs carries. */
inline int padHigh (int height) noexcept    { return (height >> 5) & 127; }
inline int padLow (int height) noexcept     { return height & 31; }
inline int padJoin (int high, int low) noexcept     { return ((high & 127) << 5) | (low & 31); }

inline int padLimit (int v, int low, int high) noexcept     { return v < low ? low : v > high ? high : v; }

/** The two levels as both programs will actually use them, whatever was asked for. */
inline int padThreshold (int asked) noexcept        { return padLimit (asked, thresholdMin, thresholdMax); }
inline int padFull (int asked, int threshold) noexcept
{
    const int least = padThreshold (threshold) * 3 / 2;
    return padLimit (asked, least > fullMin ? least : fullMin, heightMax);
}

/** The strength (0 = not a hit, else 1 to 127) of a spike this tall. Between the two levels the
    strength follows the square root of the height, the same first guess the app makes for a
    piezo on a socket (KickPad::piezoStrength): a spike grows faster than a stomp feels harder. */
inline int padStrength (int height, int threshold, int full) noexcept
{
    threshold = padThreshold (threshold);
    full = padFull (full, threshold);

    if (height < threshold)
        return 0;

    float part = (float) (height - threshold) / (float) (full - threshold);
    part = part < 0.0f ? 0.0f : part > 1.0f ? 1.0f : part;
    return padLimit (1 + (int) lroundf (126.0f * sqrtf (part)), 1, 127);
}

//==============================================================================
// Everything below is the controller's own thinking, kept here (and not in the controller's
// main.cpp) so the app's self-test runs the very same lines on every build: there is no Teensy
// on a build machine, and this way the only untested part of the controller's pad code is the
// few lines that read the pin and hand messages to USB.

/** One MIDI message, on padmsg::channel: a CC (number, value) or the pad's note on (number, strength). */
struct PadMessage
{
    bool isNote = false;
    int number = 0, value = 0;
};

/** The two levels as the controller holds them, and the app's messages that change them. */
class PadLevels
{
public:
    int threshold = thresholdDefault, full = fullDefault;

    /** Returns true when a level changed (whatever was asked for is brought into range). */
    bool set (int newThreshold, int newFull) noexcept
    {
        newThreshold = padThreshold (newThreshold);
        newFull = padFull (newFull, newThreshold);
        const bool changed = newThreshold != threshold || newFull != full;
        threshold = newThreshold;
        full = newFull;
        return changed;
    }

    enum Took { nothing, changed, asked };

    /** One CC from the app (already known to be on padmsg::channel). */
    Took take (int cc, int value) noexcept
    {
        if (cc == ccThresholdHigh)      { partThreshold = value; return nothing; }
        if (cc == ccFullHigh)           { partFull = value; return nothing; }

        if (cc == ccThresholdLow && partThreshold >= 0)
        {
            const bool c = set (padJoin (partThreshold, value), full);
            partThreshold = -1;
            return c ? changed : nothing;
        }

        if (cc == ccFullLow && partFull >= 0)
        {
            const bool c = set (threshold, padJoin (partFull, value));
            partFull = -1;
            return c ? changed : nothing;
        }

        return cc == ccAsk ? asked : nothing;
    }

    /** What the controller answers when asked: its program number and the two levels. Fills five messages. */
    int answer (PadMessage out[5]) const noexcept
    {
        out[0] = { false, ccProgram, program };
        out[1] = { false, ccThresholdHigh, padHigh (threshold) };
        out[2] = { false, ccThresholdLow, padLow (threshold) };
        out[3] = { false, ccFullHigh, padHigh (full) };
        out[4] = { false, ccFullLow, padLow (full) };
        return 5;
    }

private:
    int partThreshold = -1, partFull = -1;      // the upper part of a level, waiting for its lower part
};

/** Watches the pad's pin. The controller hands it a reading (0 to 4095) with the time in
    millionths of a second, at a steady rate (ten thousand times a second, from a timer, so
    nothing else the controller is doing can make it miss the top of a spike); it says when a
    stomp has been measured and when a knock too small to count is worth telling the app about.

    A reading at or over "Counts from" starts a watch of scanMicros for the top of the spike;
    then the hit is given, and the pad is ignored for holdMillis while the plate rings. */
class PadWatcher
{
public:
    enum Kind { none, hit, knock };

    struct Event
    {
        Kind kind = none;
        int height = 0;         // a hit: the top of its spike. A knock: the tallest reading that did not count.
        int strength = 0;       // a hit: 1 to 127

        /** The MIDI messages to send for this event, in order. Fills at most three; returns how many. */
        int messages (PadMessage out[3]) const noexcept
        {
            if (kind == hit)
            {
                out[0] = { false, ccPeakHigh, padHigh (height) };       // the height first, so the app has it when the note lands
                out[1] = { false, ccPeakLow, padLow (height) };
                out[2] = { true, note, strength };
                return 3;
            }

            if (kind == knock)
            {
                out[0] = { false, ccIgnoredHigh, padHigh (height) };
                out[1] = { false, ccIgnoredLow, padLow (height) };
                return 2;
            }

            return 0;
        }
    };

    Event read (int reading, uint32_t nowMicros, int threshold, int full) noexcept
    {
        // Times are compared by subtraction, which stays right when the controller's clock
        // wraps round to nought (every 71 minutes).
        if (holding)
        {
            if (nowMicros - holdStart < (uint32_t) holdMillis * 1000u)
                return {};

            holding = false;
        }

        if (scanning)
        {
            if (reading > peak)
                peak = reading;

            if (nowMicros - scanStart < (uint32_t) scanMicros)
                return {};

            scanning = false;
            holding = true;
            holdStart = nowMicros;
            tallestKnock = 0;
            knockSeen = false;
            return { hit, peak, padStrength (peak, threshold, full) };
        }

        if (reading >= padThreshold (threshold))
        {
            scanning = true;
            scanStart = nowMicros;
            peak = reading;
            return {};
        }

        // A knock that came anywhere near counting (a quarter of the way or more) goes in the
        // app's report: a tap beside the pad, a footswitch. It is watched for a moment first, so
        // its top is what is told. Hum and stillness are not sent, and never more than four a second.
        const int worthTelling = padThreshold (threshold) / 4 > knockFloor ? padThreshold (threshold) / 4 : knockFloor;

        if (reading > tallestKnock)
            tallestKnock = reading;

        if (! knockSeen && tallestKnock >= worthTelling)
        {
            knockSeen = true;
            knockStart = nowMicros;
        }

        if (knockSeen && nowMicros - knockStart >= knockWatchMicros && nowMicros - knockSent >= knockEveryMicros)
        {
            const Event e { knock, tallestKnock, 0 };
            tallestKnock = 0;
            knockSeen = false;
            knockSent = nowMicros;
            return e;
        }

        return {};
    }

    static constexpr int knockFloor = 12;
    static constexpr uint32_t knockEveryMicros = 250000u, knockWatchMicros = 5000u;

private:
    bool scanning = false, holding = false, knockSeen = false;
    uint32_t scanStart = 0, holdStart = 0, knockStart = 0, knockSent = 0;
    int peak = 0, tallestKnock = 0;
};
}
}
