#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DeviceReport.h"
#include "MidiMonitor.h"
#include "Style.h"

namespace gigrig
{

/** The "Device check" tab (stage 0): what was found, the full device list, and live MIDI. */
class DeviceCheckPanel final : public juce::Component,
                               private juce::Timer
{
public:
    DeviceCheckPanel();
    ~DeviceCheckPanel() override;

    void resized() override;

    /** Rescans audio and MIDI devices a moment from now. */
    void requestScan();

    /** The device report as text, ready to paste into a message. */
    juce::String reportText() const     { return text; }

    /** Called just before a scan: anything holding an audio device open must let go of it,
        because ASIO drivers only allow one user at a time. */
    std::function<void()> onBeforeScan;

    /** Called when a scan has finished. */
    std::function<void (const DeviceReport&)> onScanned;

private:
    void timerCallback() override;
    void runScan();
    void appendMidiLines (const juce::StringArray& lines);

    juce::Label reportHeading, midiHeading;
    StatusRow mixerRow, looperRow;
    juce::TextButton scanButton { "Scan again" };
    juce::TextEditor reportView, midiView;

    MidiMonitor midiMonitor;
    juce::String text;
    int scanCountdown = 0;      // timer ticks until a requested scan runs
    int midiLineCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceCheckPanel)
};

} // namespace gigrig
