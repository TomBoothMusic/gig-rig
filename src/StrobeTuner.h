#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

namespace gigrig
{

/** The tuner filling the rig's own screen (Tom, 28 Nov 2026: "when I hit the tuner button, I'd
    like the 8-inch touchscreen to show up with the tuner", a strobe like his pedal tuner, purple
    lights on a dark background). It lies over the whole window while the tuner is on and goes
    away when it is switched off, from here (Done), from the header, or from the phone.

    The picture is a strobe: four rows of purple bands, each row twice as fine as the one under
    it. They stand still when the string is in tune, slide right when it is sharp and left when
    it is flat, slower the closer it gets, so the last fraction of a cent is easy to see. The
    slide comes straight from Tuner::Reading::strobe (the sound compared wave against wave with
    the note), not from the cents figure. docs/tuner.md is the guide. */
class StrobeTuner final : public juce::Component,
                          private juce::Timer
{
public:
    explicit StrobeTuner (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For pictures taken where there is no guitar (GIGRIG_TUNER=demo): a pretend A string, a little sharp. */
    void pretend (bool on);

    /** Opens the tuner's choices next to the given button (the same menu as the header's Options). */
    std::function<void (juce::Component&)> onOptions;

    static const juce::Colour dark, purple, lilac, dim;

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override {}        // nothing underneath is pressed by mistake

    AudioEngine& engine;
    juce::TextButton doneButton { "Done" }, optionsButton { "Options" };
    juce::String words;
    bool pretending = false;
    double pretendTurn = 0.0;
    int sinceWords = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrobeTuner)
};

} // namespace gigrig
