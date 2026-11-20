#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "AudioPanel.h"
#include "BassPanel.h"
#include "DeviceCheckPanel.h"
#include "LooperPanel.h"
#include "PedalPanel.h"

namespace gigrig
{

/** The window's contents: a title, the Copy report button and one tab per stage. */
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

    AudioEngine engine;     // declared first: the panels below use it, so it must outlive them
    PedalInput pedal;       // the RC-600's switches; feeds the looper inside the engine

    juce::Label title, subtitle;
    juce::TextButton copyButton { "Copy report" };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AudioPanel audioPanel { engine };
    DeviceCheckPanel devicePanel;
    PedalPanel pedalPanel { pedal };
    LooperPanel looperPanel { engine, pedal };
    BassPanel bassPanel { engine };
    juce::TooltipWindow tooltips { this };

    DeviceChoice reopenChoice;      // what was running before a rescan, to start it again after
    int reopenBufferSize = 0;
    bool firstScan = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace gigrig
