#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DeviceReport.h"
#include "MidiMonitor.h"

namespace gigrig
{

/** One line of the answer at the top of the window: a coloured tag,
    a headline and a sentence on what to do next. */
class VerdictRow final : public juce::Component
{
public:
    void setVerdict (const Verdict& newVerdict);
    void setScanning (const juce::String& what);
    void paint (juce::Graphics&) override;

private:
    Verdict verdict;
    bool scanning = true;
};

/** The stage 0 screen: what was found, the full report, and live MIDI. */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void requestScan();
    void runScan();
    void appendMidiLines (const juce::StringArray& lines);

    juce::Label title, subtitle, reportHeading, midiHeading;
    VerdictRow mixerRow, looperRow;
    juce::TextButton scanButton { "Scan again" }, copyButton { "Copy report" };
    juce::TextEditor reportView, midiView;

    MidiMonitor midiMonitor;
    juce::String reportText;
    int scanCountdown = 0;      // timer ticks until a requested scan runs
    int copiedCountdown = 0;    // timer ticks until the Copy button's label resets
    int midiLineCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace gigrig
