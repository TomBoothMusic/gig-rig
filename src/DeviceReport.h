#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace gigrig
{

/** One audio device as one driver type (ASIO, Windows Audio, ALSA...) sees it. */
struct AudioDeviceEntry
{
    juce::String typeName;
    juce::String name;
    juce::StringArray inputChannels;
    juce::StringArray outputChannels;
    juce::Array<double> sampleRates;
    juce::Array<int> bufferSizes;
    juce::String problem;   // empty when the device answered normally
    juce::String note;      // set when the device was listed but deliberately not opened
};

/** The answer to one of the stage 0 questions, in plain words. */
struct Verdict
{
    enum class State { found, partial, missing };

    State state = State::missing;
    juce::String headline;
    juce::String detail;
};

/** Everything the device check found on this computer. */
struct DeviceReport
{
    juce::StringArray audioTypeNames;
    juce::Array<AudioDeviceEntry> audioDevices;
    juce::Array<juce::MidiDeviceInfo> midiInputs;
    juce::Array<juce::MidiDeviceInfo> midiOutputs;

    Verdict mixer;      // Allen & Heath CQ over USB audio
    Verdict looper;     // Boss RC-600 over USB MIDI

    /** The whole report as text, ready to paste into a message. */
    juce::String toText() const;
};

/** Scans every audio driver type and MIDI port. Call on the message thread:
    ASIO drivers on Windows expect to be loaded from the main thread. */
DeviceReport scanDevices();

/** True if a device name looks like an Allen & Heath CQ mixer. */
bool looksLikeCQ (const juce::String& deviceName);

/** True if a device name looks like the Boss RC-600. */
bool looksLikeRC600 (const juce::String& deviceName);

/** Works out the two verdicts from what was scanned. Split out from
    scanDevices() so it can be checked without the real hardware. */
void judge (DeviceReport& report);

} // namespace gigrig
