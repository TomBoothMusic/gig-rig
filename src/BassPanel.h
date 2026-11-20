#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Pedal.h"
#include "Style.h"

namespace gigrig
{

/** The "Bass (Bass9 A/B)" tab: the app's copy of the Bass9 PRECISION sound beside the
    real Bass9, with live sliders, presets, a level match, a blind test and a recorder. */
class BassPanel final : public juce::Component,
                        private juce::Timer
{
public:
    explicit BassPanel (AudioEngine&);
    ~BassPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** A pedal gesture. Returns true if it belongs to the A/B switch (and so should not
        reach the looper). Called on the pedal's thread. */
    bool handlePedal (const PedalEvent& event);

    /** The bass part of the report. Message thread. */
    juce::String reportText() const;

    static juce::File presetFolder();
    static juce::File captureFolder();

private:
    void timerCallback() override;
    void settingsChanged();
    void showSettings (const BassSettings& s);
    void refreshPresets();
    void saveCurrent();
    void loadCurrent();
    void savePresetAs();
    void loadPresetFile (const juce::File& file);
    void updateListenButtons();
    void startBlind();
    void guess (int which);

    AudioEngine& engine;
    BassRig& rig;

    juce::Label routeHeading, dryLabel, realLabel, outLabel, listenHeading, switchLabel, meterLabel,
                matchLabel, captureLabel, soundHeading, presetLabel, blindLabel;
    juce::ComboBox dryBox, realBox, outBox, switchBox, presetBox;
    juce::TextButton offButton { "Off" }, realButton { "A: real Bass9" }, appButton { "B: app bass" },
                     blindButton { "Blind test" }, guess1Button { "1 is the real Bass9" }, guess2Button { "2 is the real Bass9" },
                     matchButton { "Match app level to the Bass9" },
                     captureButton { "Capture" }, openCapturesButton { "Open captures folder" },
                     saveButton { "Save as..." }, loadButton { "Load file..." }, defaultsButton { "Defaults" };
    MeterStrip meters { 3, 1 };
    StatusRow status;

    struct SliderRow
    {
        std::string key;
        juce::Label label;
        juce::Slider slider;
    };
    juce::OwnedArray<SliderRow> sliders;

    std::atomic<int> abSwitch { 10 };     // CTL4 by default; -1 = none
    std::atomic<bool> blindOn { false };
    bool blindSwap = false;
    int blindRight = 0, blindTotal = 0;
    juce::String blindLog;

    bool matching = false;
    double matchApp = 0.0, matchReal = 0.0, matchSeconds = 0.0;
    juce::String matchResult;

    juce::String presetName { "(unsaved)" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray captureLog;
    bool dirty = false, loading = false;
    int saveCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassPanel)
};

} // namespace gigrig
