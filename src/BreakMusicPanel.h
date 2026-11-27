#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

namespace gigrig
{

/** The "Break music" tab (docs/break-music.md): picks one of Tom's mixes (or the phone), plays
    it round and round, and sets how it fades down when he starts playing. The music itself is
    BreakMusic, inside the engine; this is only its screen. */
class BreakMusicPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit BreakMusicPanel (AudioEngine&);
    ~BreakMusicPanel() override;

    void resized() override;
    /** Brings every control in line with the music (the remote screen may have changed it). */
    void refresh();
    juce::String reportText() const             { return engine.breakMusic().reportText(); }

private:
    void timerCallback() override;
    void refreshList();
    void pushSettings();
    juce::String chosenName() const;

    AudioEngine& engine;
    BreakMusic& music;

    StatusRow status;
    juce::Label help, playLabel, levelLabel, downLabel, upLabel, whenLabel, soundLabel, pairLabel, knobLabel, knobScreen, reportHeading;
    juce::ComboBox mixBox, pairBox;
    juce::TextButton playButton { "Play" }, stopButton { "Stop" }, restartButton { "Play from the start" }, folderButton { "Open the break music folder" },
                     upButton { "Bring the music up" }, downButton { "Fade it down" },
                     turnLeft { "Turn left" }, turnRight { "Turn right" }, push { "Push" }, hold { "Hold" };
    juce::Slider levelSlider, downSlider, upSlider, soundSlider;
    juce::ToggleButton switchBox { "a footswitch is pressed" }, songBox { "a song loads" }, soundBox { "an input is louder than" };
    juce::TextEditor reportView;

    juce::StringArray shownMixes;
    juce::String shownStatus, shownReport, note, lastKnobLine;
    double lastKnobMs = -1.0e12;
    int listTicks = 0, noteTicks = 0;
    bool loading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakMusicPanel)
};

} // namespace gigrig
