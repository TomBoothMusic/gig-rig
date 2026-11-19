#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

namespace gigrig
{

/** The "Latency and audio test" tab (stage 1): choose the device and buffer
    size, measure the round trip, play through, and check each return. */
class AudioPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit AudioPanel (AudioEngine&);
    ~AudioPanel() override;

    void resized() override;

    /** Refills the device list. If nothing is running and openCQ is true,
        starts the CQ when it is there. */
    void refreshDevices (bool openCQ);

    /** Starts a device and brings every control in line with it. */
    void openDevice (const DeviceChoice& choice, int bufferSize);

private:
    void timerCallback() override;
    void syncToEngine();
    void updateVerdict();
    void updateDropoutLine();
    void updateGlitchLine();
    void setControlsEnabled();

    AudioEngine& engine;
    juce::Array<DeviceChoice> choices;

    juce::Label deviceLabel, bufferLabel, pingLabel, playLabel, toneLabel, dropoutLabel, glitchLabel, inputsLabel, returnsLabel;
    juce::ComboBox deviceBox, bufferBox, pingBox, playBox;
    juce::TextButton panelButton { "Driver settings" }, measureButton { "Measure latency" },
                     sweepButton { "Measure every size, 8 to 256" }, resetButton { "Reset count" }, glitchButton { "Glitch check" };
    juce::OwnedArray<juce::TextButton> toneButtons;
    StatusRow verdict;
    MeterStrip inputMeters { numMeteredInputs, 1 }, returnMeters { numReturns, 1 };
    juce::TextEditor resultsView;

    juce::String shownResults;
    bool wasMeasuring = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPanel)
};

} // namespace gigrig
