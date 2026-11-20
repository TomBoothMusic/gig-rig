#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Pedal.h"
#include "Style.h"

namespace gigrig
{

/** One footswitch drawn on screen. Clicking it with the mouse is the same as a
    foot on the real one, so everything can be tried with no pedal plugged in. */
class SwitchTile final : public juce::Component
{
public:
    std::function<void (bool isDown)> onSwitch;

    void setText (const juce::String& title, const juce::String& detail);
    void setDown (bool isDown);

    /** The stage 2 checklist under the name: which of press, hold and double-press have been seen. */
    void setChecks (bool press, bool hold, bool doublePress);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String title, detail;
    bool down = false, showChecks = false, seenPress = false, seenHold = false, seenDouble = false;
};

/** Lays eleven tiles out the way the RC-600 has them: three rows of three, with CTL3 and CTL4 to the right. */
void layOutSwitches (juce::Rectangle<int> area, juce::OwnedArray<SwitchTile>& tiles);

/** The "Pedal" tab (stage 2): every RC-600 switch by name, and whether a press,
    a 2-second hold and a double-press have each been seen from it. */
class PedalPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit PedalPanel (PedalInput&);
    ~PedalPanel() override;

    void resized() override;

private:
    void timerCallback() override;

    PedalInput& pedal;

    StatusRow status;
    juce::Label help, windowLabel, logHeading;
    juce::OwnedArray<SwitchTile> tiles;
    juce::ComboBox windowBox;
    juce::TextButton resetButton { "Start again" }, copyButton { "Copy pedal results" };
    juce::TextEditor logView;

    int logLines = 0, copiedTicks = 0;
    juce::String shownStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalPanel)
};

} // namespace gigrig
