#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

namespace gigrig
{

/** The "Recordings" tab (built 28 Nov 2026, docs/recording.md): the recordings on the drive,
    playing one back through the rig as a virtual soundcheck, keeping one for good, copying
    them to a USB drive and clearing old ones.

    The work is done by Soundcheck (in the engine) and RecordingLibrary; this is only their
    screen. Everything here is also on the remote screen's Record screen, under Recordings. */
class RecordingsPanel final : public juce::Component,
                              private juce::ListBoxModel,
                              private juce::Timer
{
public:
    explicit RecordingsPanel (AudioEngine&);
    ~RecordingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** Reads the drive again (a recording has just stopped, or the remote screen changed something). */
    void refresh();
    juce::String reportText() const;

private:
    int getNumRows() override                           { return (int) list.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override             { showChosen(); }
    void timerCallback() override;
    void visibilityChanged() override                   { if (isVisible()) refresh(); }

    const RecordingLibrary::Info* chosen() const;
    void showChosen();
    void say (const juce::String& text, bool bad = false);
    void confirm (const juce::String& title, const juce::String& words, const juce::String& yes, std::function<void()> then);

    AudioEngine& engine;
    std::vector<RecordingLibrary::Info> list;
    std::vector<RecordingLibrary::Drive> driveList;

    StatusRow status;
    juce::ListBox box { {}, this };
    juce::Label heading, chosenLabel, room, driveLabel, note, checkHelp;
    juce::TextButton playButton { "Play as a soundcheck" }, deleteButton { "Delete this recording" },
                     copyOneButton { "Copy this one to the drive" }, copyAllButton { "Copy all that are not copied yet" }, cancelButton { "Stop the copy" },
                     clearCopiedButton { "Clear the copied ones" }, clearAllButton { "Clear all but the kept ones" },
                     folderButton { "Open the recordings folder" }, reportButton { "Copy recordings report" };
    juce::ToggleButton keepBox { "Keep (clearing never removes it)" }, loopBox { "Round and round" };
    juce::ComboBox driveBox;
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    juce::String noteText;
    bool noteBad = false, wasCopying = false, wasRecording = false, wasPlaying = false, loading = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingsPanel)
};

} // namespace gigrig
