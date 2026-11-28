#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

#include <map>

namespace gigrig
{

/** The "Recording" tab (built 28 Nov 2026, docs/recording.md): one big Record button, what
    each channel records (its tap point), which mixes are written, and the recording mix, a
    second balance of the same channels with its own levels, pans, EQ and limiter that is only
    ever written to a file.

    The recorder itself is MixRecorder, fed by the Mixer inside the engine; this is only its
    screen. Everything here is also on the remote screen's Record screen. */
class RecordingPanel final : public juce::Component,
                             private juce::Timer
{
public:
    explicit RecordingPanel (AudioEngine&);
    ~RecordingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** Brings every control in line with the mixer (an act or the remote screen may have changed it). */
    void refresh();
    juce::String reportText() const             { return mixer.recordingReportText(); }

private:
    struct ChannelRow
    {
        juce::Label name;
        juce::ComboBox point;
        juce::Slider level, pan;
    };

    struct FxRow
    {
        juce::Label name;
        juce::Slider level;
    };

    void timerCallback() override;
    void toggleRecording();
    void rebuildGrid();
    void layoutGrid();
    juce::String signature() const;
    const std::vector<fx::ParamInfo>& paramsOf (const std::string& type);

    AudioEngine& engine;
    Mixer& mixer;

    StatusRow status;
    juce::TextButton recordButton { "Record" }, folderButton { "Open the recordings folder" }, copyButton { "Copy recording report" };
    juce::Label clock, help, tracksHeading, pointHeading, levelHeading, panHeading, mixHeading, mixLevelLabel, blockHint, roomLabel;
    juce::ToggleButton mainBox { "Main mix (stereo, as the PA gets it)" }, mixBox { "Recording mix (stereo, set below)" }, fxBox { "Each mixer reverb and delay as a file of its own" },
                       checkBox { "A raw copy of every socket, for a virtual soundcheck" };
    std::array<ChannelRow, numMixChannels> rows;
    std::array<FxRow, numMixFx> fxRows;
    juce::Slider mixLevel;
    std::array<juce::TextButton, numRecBlocks> blockButtons;
    juce::ToggleButton blockOn { "This block on" };
    juce::OwnedArray<juce::Label> gridLabels;
    juce::OwnedArray<juce::Component> gridControls;
    juce::Rectangle<int> gridArea, leftArea, rightArea;
    juce::TextEditor reportView;

    std::map<std::string, std::unique_ptr<fx::Effect>> examples;
    juce::String shownSignature, shownReport, note;
    int block = recEq, ticks = 0, noteTicks = 0;
    bool loading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingPanel)
};

} // namespace gigrig
