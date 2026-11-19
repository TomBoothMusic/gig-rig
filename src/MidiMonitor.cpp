#include "MidiMonitor.h"

namespace gigrig
{

namespace
{
    // If the screen stops asking (window frozen, say), don't grow without limit.
    constexpr int maxPendingLines = 2000;
}

MidiMonitor::~MidiMonitor()
{
    for (auto& input : inputs)
        input->stop();
}

juce::StringArray MidiMonitor::openAllInputs()
{
    for (auto& input : inputs)
        input->stop();

    inputs.clear();

    {
        const juce::ScopedLock sl (lock);
        clockSeenFrom.clear();
    }

    juce::StringArray opened;

    for (const auto& info : juce::MidiInput::getAvailableDevices())
    {
        if (auto input = juce::MidiInput::openDevice (info.identifier, this))
        {
            input->start();
            opened.add (info.name);
            inputs.push_back (std::move (input));
        }
    }

    return opened;
}

juce::StringArray MidiMonitor::takeLines()
{
    const juce::ScopedLock sl (lock);

    juce::StringArray lines;
    lines.swapWith (pending);

    if (dropped > 0)
    {
        lines.add ("(" + juce::String (dropped) + " more messages arrived too fast to show)");
        dropped = 0;
    }

    return lines;
}

juce::String MidiMonitor::describe (const juce::MidiMessage& m)
{
    const auto channel = " (channel " + juce::String (m.getChannel()) + ")";

    if (m.isController())
        return "CC " + juce::String (m.getControllerNumber()) + " = " + juce::String (m.getControllerValue()) + channel;

    if (m.isProgramChange())
        return "Program change " + juce::String (m.getProgramChangeNumber() + 1) + channel;

    if (m.isNoteOn())
        return "Note on  " + juce::MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 4)
                 + " velocity " + juce::String (m.getVelocity()) + channel;

    if (m.isNoteOff())
        return "Note off " + juce::MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 4) + channel;

    if (m.isMidiStart())    return "Start";
    if (m.isMidiStop())     return "Stop";
    if (m.isMidiContinue()) return "Continue";

    return m.getDescription();
}

void MidiMonitor::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    if (message.isActiveSense())
        return;

    const auto name = source != nullptr ? source->getName() : juce::String ("unknown");
    juce::String text;

    {
        const juce::ScopedLock sl (lock);

        if (message.isMidiClock())
        {
            if (clockSeenFrom.contains (name))
                return;

            clockSeenFrom.add (name);
            text = "MIDI clock is running (shown once; the ticks themselves are hidden)";
        }
        else
        {
            text = describe (message);
        }

        if (pending.size() >= maxPendingLines)
        {
            ++dropped;
            return;
        }

        pending.add (juce::Time::getCurrentTime().toString (false, true, true, true)
                       + "  " + name + "  " + text);
    }
}

} // namespace gigrig
