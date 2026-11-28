#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

namespace gigrig
{

/** The tuner on the rig's own screen: a button in the window's header (beside Copy report) and,
    while the tuner is on, the note and a needle next to it (hidden behind the full-screen strobe
    tuner, src/StrobeTuner.*, unless "Fill the screen while tuning" is unticked). Options has what
    goes silent, the input it listens to and what A is tuned to (docs/tuner.md). */
class TunerStrip final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerStrip (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    /** The button follows a tuner switched on or off from the remote screen. */
    void refresh();

    /** The tuner's choices as a menu beside `target`: what goes silent, which channels are
        guitars, the input, the A, and whether it fills the screen. Also used by the full-screen tuner. */
    static void showOptions (AudioEngine&, juce::Component& target);

private:
    void timerCallback() override;

    AudioEngine& engine;
    juce::TextButton onButton { "Tuner" }, optionsButton { "Options" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerStrip)
};

} // namespace gigrig
