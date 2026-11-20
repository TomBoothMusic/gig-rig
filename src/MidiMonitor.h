#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace gigrig
{

/** Listens to every MIDI input at once and turns what arrives into readable
    lines. MIDI arrives on a driver thread; lines are collected under a lock
    and handed to the screen when it asks with takeLines(). */
class MidiMonitor final : private juce::MidiInputCallback
{
public:
    MidiMonitor() = default;
    ~MidiMonitor() override;

    /** Closes whatever was open and opens every MIDI input now present.
        Returns the names that opened. */
    juce::StringArray openAllInputs();

    /** Lines that arrived since the last call, oldest first. */
    juce::StringArray takeLines();

    /** A readable description of one message, e.g. "CC 80 = 127 (channel 1)". */
    static juce::String describe (const juce::MidiMessage& message);

    /** Every message as it arrives, on the MIDI driver's thread. Set once, before opening. */
    std::function<void (const juce::String& deviceName, const juce::MidiMessage&)> onMessage;

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;

    juce::CriticalSection lock;
    juce::StringArray pending;
    juce::StringArray clockSeenFrom;   // MIDI clock is reported once per device, not 24 times a beat
    int dropped = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMonitor)
};

} // namespace gigrig
