#pragma once

#include <juce_core/juce_core.h>

namespace gigrig
{

/** Notices that the audio device has gone away (the mixer was switched off, or its USB lead came
    out) and says when to try opening it again, so nobody has to restart the app (asked for after
    24 Nov 2026, when a power-cycled CQ passed no audio until GigRig was restarted).

    Pure logic so the automatic builds can check it: the Latency and audio test tab feeds it about
    20 times a second and does what it says. A device counts as gone when the driver reports an
    error, when the device is no longer open, or when no block of audio has run for a second and
    a half. Then one try to open it again every three seconds until it is back. */
class ReconnectWatch
{
public:
    enum class Action { none, lost, tryOpen };

    static constexpr int ticksPerSecond = 20;
    static constexpr int stallTicks = 30;       // 1.5 s without a block of audio
    static constexpr int retryTicks = 60;       // 3 s between tries

    /** True once a device has been opened on purpose; false when it was closed on purpose
        (the device check, choosing "none"), so that is not taken for a loss. */
    void arm (bool wanted) noexcept
    {
        armed = wanted;
        lost = false;
        quiet = sinceTry = 0;
        seenBlocks = -1;
    }

    bool isArmed() const noexcept       { return armed; }
    bool isLost() const noexcept        { return lost; }
    int timesLost() const noexcept      { return lostCount; }
    int timesBack() const noexcept      { return backCount; }

    /** busy: a latency measurement is reopening the device itself, so nothing is judged. */
    Action tick (bool deviceOpen, juce::int64 blocksRun, bool errorReported, bool busy) noexcept
    {
        if (! armed)
            return Action::none;

        if (lost)
        {
            ++outageTicks;

            if (++sinceTry >= retryTicks)
            {
                sinceTry = 0;
                ++tries;
                return Action::tryOpen;
            }

            return Action::none;
        }

        if (busy)
        {
            quiet = 0;
            seenBlocks = -1;
            return Action::none;
        }

        quiet = blocksRun != seenBlocks ? 0 : quiet + 1;
        seenBlocks = blocksRun;

        if (! deviceOpen || errorReported || quiet >= stallTicks)
        {
            why = ! deviceOpen ? "the device closed" : errorReported ? "the driver reported an error" : "no audio arrived for 1.5 seconds";
            lost = true;
            ++lostCount;
            lostAt = juce::Time::getCurrentTime();
            sinceTry = retryTicks - ticksPerSecond;     // first try a second after the loss
            outageTicks = tries = 0;
            return Action::lost;
        }

        return Action::none;
    }

    /** How a try to open went. */
    void result (bool openNow) noexcept
    {
        if (! openNow)
            return;

        lost = false;
        ++backCount;
        lastOutageSeconds = outageTicks / (double) ticksPerSecond;
        quiet = 0;
        seenBlocks = -1;
    }

    /** For the screen and the report. */
    juce::String text() const
    {
        if (lostCount == 0)
            return "Audio device watch: " + juce::String (armed ? "on (if the mixer goes away, the app opens it again by itself)" : "waiting for a device to be started") + ". Not lost since the app started.";

        juce::String t = "Audio device watch: lost " + juce::String (lostCount) + (lostCount == 1 ? " time" : " times") + " since the app started (last at "
                       + lostAt.toString (false, true, true, true) + ", because " + why + "). ";

        if (lost)
            t << "STILL GONE: trying to open it again every 3 seconds (" << tries << (tries == 1 ? " try" : " tries") << " so far).";
        else
            t << "Back after " << juce::String (lastOutageSeconds, 1) << " seconds, opened again by the app. Loops recorded before it went were cleared.";

        return t;
    }

private:
    bool armed = false, lost = false;
    int quiet = 0, sinceTry = 0, outageTicks = 0, tries = 0, lostCount = 0, backCount = 0;
    juce::int64 seenBlocks = -1;
    double lastOutageSeconds = 0.0;
    juce::Time lostAt;
    juce::String why;
};

} // namespace gigrig
