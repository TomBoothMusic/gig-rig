#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

namespace gigrig
{

/** The "Effects" tab (stage 6, first batch 23 Nov 2026): up to four chains, each an input
    socket through a row of effects out to a return. Every setting is a live slider, a
    chain can be saved as a named preset, and A/B, the level match and Capture make
    comparing against the dry sound or a real pedal fair. */
class EffectsPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit EffectsPanel (AudioEngine&);
    ~EffectsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Brings every control in line with the rack (after loading, or a pedal press). */
    void refresh();

    static juce::File presetFolder();
    static juce::File captureFolder();

private:
    void timerCallback() override;
    void chooseChain (int c);
    void chooseSlot (int s);
    void rebuildSlotTiles();
    void rebuildEditor();
    void pushRouting();
    void fillInputBox (juce::ComboBox&, bool allowNone);
    void refreshPresets();
    void savePreset();
    void loadPreset (const juce::File&);
    void addPreset (const juce::File&);
    void levelMatch();
    void fillPedalBoxes();
    void pushPedal (int i);

    AudioEngine& engine;
    EffectsRack& rack;
    int chainIndex = 0, slotIndex = 0;

    juce::ToggleButton rackOn { "Effects on" }, chainOn { "This chain on" };
    juce::OwnedArray<juce::TextButton> chainButtons;
    juce::TextEditor nameEditor;
    juce::Label inLabel, outLabel, looperLabel, listenLabel, levelLabel, slotsLabel, presetLabel, pedalLabel, statusLabel, descriptionLabel;
    juce::ComboBox inBox, outBox, looperBox, refBox;
    juce::TextButton listenEffects { "Effects" }, listenDry { "Dry" }, listenRef { "Reference" };
    juce::TextButton matchButton { "Level match" }, captureButton { "Capture" }, copyButton { "Copy effects results" }, tapButton { "Tap tempo" };
    juce::Label tempoLabel;
    juce::Slider levelSlider;

    // The row of slots.
    juce::OwnedArray<juce::TextButton> slotButtons;
    juce::OwnedArray<juce::ToggleButton> slotToggles;
    juce::ComboBox addBox;
    juce::TextButton addButton { "Add" };

    // The selected slot.
    juce::ComboBox typeBox;
    juce::TextButton leftButton { "Move left" }, rightButton { "Move right" }, removeButton { "Remove" };
    juce::OwnedArray<juce::Label> paramLabels;
    juce::OwnedArray<juce::Slider> paramSliders;
    juce::OwnedArray<juce::ComboBox> paramChoices;
    juce::OwnedArray<juce::ToggleButton> paramToggles;
    juce::OwnedArray<juce::TextButton> paramResets;

    // For an effect that loads a file (the amp model): which one, and its folder.
    bool showModel = false;
    juce::Label modelLabel;
    juce::ComboBox modelBox;
    juce::TextButton modelFolderButton { "Open folder" };
    juce::StringArray modelNames;

    // Presets.
    juce::ComboBox presetBox;
    juce::TextEditor presetName;
    juce::TextButton loadButton { "Load" }, addPresetButton { "Add to chain" }, saveButton { "Save as" }, folderButton { "Open folder" };
    juce::Array<juce::File> presetFiles;

    // Pedal switches.
    std::array<juce::ComboBox, EffectsRack::numPedalAssigns> pedalSwitchBoxes, pedalActionBoxes;

    juce::Rectangle<int> meterArea;
    float inDb = -100.0f, outDb = -100.0f, refDb = -100.0f;
    juce::String levelNote, captureNote;
    int ticks = 0, copiedTicks = 0;
    double loadShown = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

/** The "Amp capture" tab: plays a test sound out through an amp pedal (Tom's UA Dream '65)
    and records what comes back, one take per amp setting, so Claude can train a Neural Amp
    Modeler (NAM) copy of each setting. */
class AmpCapturePanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit AmpCapturePanel (AudioEngine&);
    ~AmpCapturePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::File captureFolder();
    juce::String reportText() const;

private:
    void timerCallback() override;
    void start();

    AudioEngine& engine;
    juce::Label stepsLabel, nameLabel, outLabel, inLabel, levelLabel, guitarLabel, statusLabel;
    juce::TextEditor nameEditor;
    juce::ComboBox outBox, inBox, guitarBox;
    juce::Slider levelSlider;
    juce::TextButton fromPlayingButton { "Set from my playing (play for 10 s)" }, startButton { "Start capture" },
                     fileButton { "Use a test sound file..." }, folderButton { "Open captures folder" };
    juce::File soundFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> meterArea;
    float returnDb = -100.0f, guitarPeak = 0.0f;
    int listeningTicks = 0;
    double progress = 0.0;
    juce::String note;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpCapturePanel)
};

} // namespace gigrig
