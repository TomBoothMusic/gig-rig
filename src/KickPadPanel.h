#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

namespace gigrig
{

/** The last hits as bars, tallest for the hardest, with the newest strength as a big number:
    a stomp that was meant soft and came out tall is seen at a glance. */
class HitGraph final : public juce::Component
{
public:
    void setHits (std::vector<KickPad::Hit> newHits);
    void paint (juce::Graphics&) override;

private:
    std::vector<KickPad::Hit> hits;
};

/** The "Kick pad" tab (docs/kick-pad.md): shows what the SPD::ONE Kick sends over USB, hit by hit,
    and plays a kick made inside the app to match. A test, to decide whether a pad built into the
    board can take over from the SPD::ONE's own sounds. */
class KickPadPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit KickPadPanel (AudioEngine&);
    ~KickPadPanel() override;

    void resized() override;

    /** Where Tom puts a recording of his own kick to try it in place of the built-in one. */
    static juce::File soundFolder();
    juce::String reportText() const             { engine.kick().collect(); return engine.kick().reportText(); }

    /** The MIDI devices were opened again (Scan again, or the app starting): looks for the board's
        controller among the outputs, so the two levels of the board's pad can be sent to it. */
    void midiOpened();

private:
    void timerCallback() override;
    void refreshSounds();
    void chooseSound (const juce::String& fileName);
    void loadSettings();
    void saveSettings();
    static juce::File settingsFile();

    AudioEngine& engine;

    StatusRow status;
    juce::Label help, levelLabel, touchLabel, clickLabel, pitchLabel, lengthLabel, soundLabel, returnLabel, piezoLabel, piezoFromLabel, piezoFullLabel, boardFromLabel, boardFullLabel, boardNote, graphHeading, logHeading;
    juce::ToggleButton onBox { "Kick pad on" }, boardBox { "Board's pad" };
    juce::Slider levelSlider, touchSlider, clickSlider, pitchSlider, lengthSlider, piezoFromSlider, piezoFullSlider, boardFromSlider, boardFullSlider;
    std::unique_ptr<juce::MidiOutput> controllerOutput;     // to the board's controller, only ever open when one is plugged in
    juce::ComboBox soundBox, returnBox, piezoBox;
    juce::TextButton softButton { "Soft hit" }, mediumButton { "Medium hit" }, hardButton { "Hard hit" },
                     learnButton { "Learn pad" }, forgetButton { "Forget pad" }, folderButton { "Open kick sounds folder" },
                     resetButton { "Start again" }, copyButton { "Copy kick pad results" };
    HitGraph graph;
    juce::TextEditor logView;

    juce::String shownStatus, shownReport, soundNote;
    juce::StringArray soundFiles;
    int copiedTicks = 0, soundScanTicks = 0;
    bool loading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickPadPanel)
};

} // namespace gigrig
