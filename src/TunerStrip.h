#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

namespace gigrig
{

/** The tuner on the rig's own screen: a button in the window's header (beside Copy report) and,
    while the tuner is on, the note and a needle next to it. Options has the input it listens
    to, what A is tuned to, and whether the main mix goes silent (docs/tuner.md). */
class TunerStrip final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerStrip (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    /** The button follows a tuner switched on or off from the remote screen. */
    void refresh();

private:
    void timerCallback() override;
    void showOptions();

    AudioEngine& engine;
    juce::TextButton onButton { "Tuner" }, optionsButton { "Options" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerStrip)
};

} // namespace gigrig
