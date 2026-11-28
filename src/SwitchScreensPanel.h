#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Style.h"
#include "SwitchScreens.h"

namespace gigrig
{

/** The "Switch screens" tab: what each footswitch's small screen shows right now, drawn by the
    same code the board's controller draws with, and the choices Tom is comparing (docs/switch-screens.md).
    Sending to the controller is off until the box is ticked; what was chosen is kept in switch-screens.txt. */
class SwitchScreensPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit SwitchScreensPanel (SwitchScreens&);
    ~SwitchScreensPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void apply();
    void showSettings();
    int samplePageNow() const;

    struct Shown
    {
        tile::Tile tile;
        bool flashOn = false, drawn = false;
        juce::Image image;
        juce::String caption, under;
    };

    bool refresh (Shown&, const tile::Tile&, const juce::String& caption, const juce::String& under);

    SwitchScreens& screens;
    juce::Label heading, explain, lookLabel, showLabel, pageLabel, benchLabel;
    juce::ToggleButton onBox { "Send to the board's screens (the controller plugged in by USB)" };
    juce::ComboBox lookBox, showBox;
    juce::TextButton previousButton { "Previous page" }, nextButton { "Next page" };
    std::array<juce::ComboBox, 3> benchBox;
    StatusRow status;
    std::array<Shown, numPedalSwitches> shown;
    int numShown = 0;
    juce::Rectangle<int> tileArea;
    juce::uint32 startedAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchScreensPanel)
};

} // namespace gigrig
