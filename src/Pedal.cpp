#include "Pedal.h"
#include "DeviceReport.h"

namespace gigrig
{

juce::String pedalSwitchName (int sw)
{
    if (sw >= 0 && sw < 9)
        return "Switch " + juce::String (sw + 1);

    return sw == 9 ? "CTL3" : sw == 10 ? "CTL4" : juce::String();
}

juce::String pedalSwitchRole (int sw)
{
    if (sw >= 0 && sw < 6)  return "track";
    if (sw == 6)            return "bank";
    if (sw == 7)            return "undo / redo";
    if (sw == 8)            return "all start / stop";
    if (sw == 9 || sw == 10) return "external footswitch";
    return {};
}

int pedalSwitchForCC (int channel, int controller) noexcept
{
    const auto sw = controller - firstPedalCC;
    return channel == pedalChannel && sw >= 0 && sw < numPedalSwitches ? sw : -1;
}

//==============================================================================
bool GestureDecoder::press (int sw, double now, PedalEvent& event) noexcept
{
    if (sw < 0 || sw >= numPedalSwitches)
        return false;

    auto& s = state[(size_t) sw];

    if (s.down)
        return false;

    // A third quick press starts again as a first press, as a double-click does.
    const bool isDouble = ! s.lastWasDouble && now - s.downAt <= doubleWindow;

    s.down = true;
    s.holdSent = false;
    s.lastWasDouble = isDouble;
    s.downAt = now;

    event.kind = PedalEvent::Kind::down;
    event.sw = sw;
    event.isDouble = isDouble;
    event.seconds = now;
    return true;
}

bool GestureDecoder::release (int sw, double now, PedalEvent& event) noexcept
{
    if (sw < 0 || sw >= numPedalSwitches)
        return false;

    auto& s = state[(size_t) sw];

    if (! s.down)
        return false;

    s.down = false;

    event.kind = PedalEvent::Kind::up;
    event.sw = sw;
    event.isDouble = false;
    event.seconds = now;
    return true;
}

int GestureDecoder::pollHolds (double now, PedalEvent* events, int maxEvents) noexcept
{
    int count = 0;

    for (int sw = 0; sw < numPedalSwitches && count < maxEvents; ++sw)
    {
        auto& s = state[(size_t) sw];

        if (s.down && ! s.holdSent && now - s.downAt >= holdSeconds)
        {
            s.holdSent = true;
            events[count].kind = PedalEvent::Kind::hold;
            events[count].sw = sw;
            events[count].isDouble = false;
            events[count].seconds = s.downAt + holdSeconds;
            ++count;
        }
    }

    return count;
}

//==============================================================================
PedalInput::PedalInput()
{
    countsSince = juce::Time::getCurrentTime();
    startTimer (5);
}

PedalInput::~PedalInput()
{
    stopTimer();
}

double PedalInput::now() const
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void PedalInput::setDoubleWindowMs (int ms)
{
    const juce::ScopedLock sl (lock);
    decoder.setDoubleWindow (juce::jlimit (100, 1000, ms) * 0.001);
}

int PedalInput::doubleWindowMs() const
{
    const juce::ScopedLock sl (lock);
    return juce::roundToInt (decoder.getDoubleWindow() * 1000.0);
}

void PedalInput::setOpenDevices (const juce::StringArray& names)
{
    bool found = false;

    for (const auto& name : names)
        found = found || looksLikeRC600 (name);

    const juce::ScopedLock sl (lock);
    connected = found;
}

bool PedalInput::rc600Connected() const
{
    const juce::ScopedLock sl (lock);
    return connected;
}

std::array<PedalSeen, numPedalSwitches> PedalInput::seen() const
{
    const juce::ScopedLock sl (lock);
    return seenSwitches;
}

void PedalInput::resetSeen()
{
    const juce::ScopedLock sl (lock);

    for (auto& s : seenSwitches)
    {
        const auto down = s.down;
        s = {};
        s.down = down;
    }
}

void PedalInput::resetCounts()
{
    const juce::ScopedLock sl (lock);
    switchMessages = otherChannelMessages = 0;
    clockTicks = 0;
    countsSince = juce::Time::getCurrentTime();
}

juce::StringArray PedalInput::takeLines()
{
    const juce::ScopedLock sl (lock);
    juce::StringArray lines;
    lines.swapWith (pending);
    return lines;
}

juce::String PedalInput::describe (const PedalEvent& event, double heldSeconds)
{
    const auto who = pedalSwitchName (event.sw) + " (" + pedalSwitchRole (event.sw) + "): ";

    switch (event.kind)
    {
        case PedalEvent::Kind::down:  return who + (event.isDouble ? "DOUBLE-PRESS" : "press");
        case PedalEvent::Kind::hold:  return who + "HOLD (held for 2 seconds)";
        case PedalEvent::Kind::up:    return who + "release after " + juce::String (heldSeconds, 2) + " s";
    }

    return who;
}

void PedalInput::dispatch (const PedalEvent& event)
{
    // Called with the lock held, so gestures reach the looper in the order they happened.
    auto& s = seenSwitches[(size_t) event.sw];
    double held = 0.0;

    switch (event.kind)
    {
        case PedalEvent::Kind::down:
            s.down = true;
            ++s.presses;
            downAt[(size_t) event.sw] = event.seconds;

            if (event.isDouble)
                ++s.doubles;
            break;

        case PedalEvent::Kind::up:
            s.down = false;
            ++s.releases;
            held = event.seconds - downAt[(size_t) event.sw];
            break;

        case PedalEvent::Kind::hold:
            ++s.holds;
            break;
    }

    if (pending.size() < 2000)
        pending.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + describe (event, held));

    if (onEvent != nullptr)
        onEvent (event);
}

void PedalInput::handleMidi (const juce::String& deviceName, const juce::MidiMessage& message)
{
    if (! looksLikeRC600 (deviceName))
        return;

    const juce::ScopedLock sl (lock);

    if (message.isMidiClock())
    {
        ++clockTicks;
        lastClockAt = now();
        return;
    }

    if (! message.isController())
        return;

    const auto sw = pedalSwitchForCC (message.getChannel(), message.getControllerNumber());

    if (sw < 0)
    {
        if (message.getControllerNumber() >= firstPedalCC && message.getControllerNumber() < firstPedalCC + numPedalSwitches)
            ++otherChannelMessages;

        return;
    }

    ++switchMessages;
    PedalEvent event;

    // 64 and over is "on", as for any MIDI switch.
    if (message.getControllerValue() >= 64 ? decoder.press (sw, now(), event) : decoder.release (sw, now(), event))
        dispatch (event);
}

void PedalInput::screenSwitch (int sw, bool isDown)
{
    const juce::ScopedLock sl (lock);
    PedalEvent event;

    if (isDown ? decoder.press (sw, now(), event) : decoder.release (sw, now(), event))
        dispatch (event);
}

void PedalInput::hiResTimerCallback()
{
    const juce::ScopedLock sl (lock);
    PedalEvent events[numPedalSwitches];
    const auto count = decoder.pollHolds (now(), events, numPedalSwitches);

    for (int i = 0; i < count; ++i)
        dispatch (events[i]);
}

juce::String PedalInput::reportText() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray lines;

    lines.add ("STAGE 2: RC-600 AS THE PEDAL");
    lines.add ("----------------------------");
    lines.add (juce::String ("RC-600 MIDI over USB: ") + (connected ? "connected" : "NOT connected"));

    const auto seconds = (int) (juce::Time::getCurrentTime() - countsSince).inSeconds();
    const bool clockRunning = now() - lastClockAt < 1.0;

    lines.add ("In the last " + juce::String (seconds / 60) + " min " + juce::String (seconds % 60) + " s: "
                 + juce::String (switchMessages) + " switch messages, MIDI clock "
                 + (clockRunning ? "running" : clockTicks > 0 ? "seen but stopped" : "not seen")
                 + " (" + juce::String (clockTicks) + " ticks)");

    if (otherChannelMessages > 0)
        lines.add ("NOTE: " + juce::String (otherChannelMessages) + " messages for CC 20 to 30 arrived on another MIDI channel "
                   "and were ignored. The MIDI CTRL memory sends on channel 1.");

    lines.add ("Double-press window: " + juce::String (juce::roundToInt (decoder.getDoubleWindow() * 1000.0))
                 + " ms. Hold: 2 seconds.");
    lines.add ("Seen from each switch (presses / releases / holds / double-presses):");

    int complete = 0;

    for (int sw = 0; sw < numPedalSwitches; ++sw)
    {
        const auto& s = seenSwitches[(size_t) sw];
        const bool all = s.presses > 0 && s.releases > 0 && s.holds > 0 && s.doubles > 0;
        complete += all ? 1 : 0;

        lines.add ("  " + (pedalSwitchName (sw) + " (CC " + juce::String (firstPedalCC + sw) + ")").paddedRight (' ', 18)
                     + juce::String (s.presses).paddedLeft (' ', 4) + " /" + juce::String (s.releases).paddedLeft (' ', 4)
                     + " /" + juce::String (s.holds).paddedLeft (' ', 3) + " /" + juce::String (s.doubles).paddedLeft (' ', 3)
                     + (all ? "   all three seen" : s.presses == 0 ? "   NOT SEEN YET" : "   still to see:"
                            + juce::String (s.holds == 0 ? " hold" : "") + (s.doubles == 0 ? " double-press" : "")
                            + (s.releases == 0 ? " release" : "")));
    }

    lines.add (juce::String (complete) + " of " + juce::String (numPedalSwitches) + " switches have shown a press, a hold and a double-press.");
    return lines.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
