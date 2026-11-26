#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Style.h"

#include <map>

namespace gigrig
{

/** The "Mixer" tab (built 26 Nov 2026, see docs/mixer.md): the workstation view of the mixer
    inside the engine. One row of strips (eight channels, four effects, three outputs), each
    with a fader, a meter and a mute; press a strip's name and everything about it is below:
    where it comes from, its sends, and its EQ, compressor and the rest as live sliders.

    The four kinds of preset (act, venue, monitors, one output) load and save along the top,
    and the three "Venue knob" buttons stand in for the board's ENC 5 until the board exists. */
class MixerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit MixerPanel (AudioEngine&);

    /** "Save to this song" and "Save as default" on a mixer reverb or delay: writes it into the song's file (or the default sounds) and returns a line for the screen. */
    std::function<juce::String (int fx, bool asDefault)> onKeepEffect;
    ~MixerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Brings every control in line with the mixer (after a preset load). */
    void refresh();

    /** The mixer's part of the report, with what this tab knows about the sockets added. */
    juce::String reportText() const;

private:
    enum class Kind { channel, fx, output };
    static constexpr int numStrips = numMixChannels + numMixFx + numMixOutputs;

    struct Strip
    {
        Kind kind = Kind::channel;
        int index = 0;
        juce::TextButton select, mute;
        juce::Slider fader;
        juce::Rectangle<int> area, meter;
        std::array<float, 2> shownDb { -100.0f, -100.0f };
        float turnDownDb = 0.0f;
    };

    /** One labelled control in the area under the strips. */
    struct Field
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        int width = 120, row = 0;
    };

    void timerCallback() override;
    void choose (int strip);
    void refreshStrips();
    void refreshPresetBoxes();
    void rebuildDetail();
    void rebuildGrid();
    void layoutDetail();
    juce::String socketNote (int socket) const;
    juce::String socketName (int socket) const;
    const std::vector<fx::ParamInfo>& paramsOf (const std::string& type);

    // The selected strip's processing as one list of blocks, whatever kind of strip it is.
    fx::ChainModel currentChain() const;
    void setCurrentChain (const fx::ChainModel&);

    // Builders for the area under the strips.
    juce::Slider* addSlider (int row, int width, const juce::String& label, double min, double max, double step, const juce::String& suffix,
                             double value, double def, std::function<void (float)> onChange);
    juce::Slider* addSend (int row, int width, const juce::String& label, float valueDb, std::function<void (float)> onChange);
    juce::ComboBox* addCombo (int row, int width, const juce::String& label, const std::vector<std::pair<int, juce::String>>& items, int selectedId, std::function<void (int)> onChange);
    void addToggle (int row, int width, const juce::String& label, const juce::String& text, bool state, std::function<void (bool)> onChange);
    void addText (int row, int width, const juce::String& label, const juce::String& text, std::function<void (juce::String)> onChange);
    void addButton (int row, int width, const juce::String& text, std::function<void()> onClick);

    AudioEngine& engine;
    Mixer& mixer;
    int chosen = 0, block = stripEq;

    juce::ToggleButton onButton { "Mixer on (the app mixes, not the CQ)" };
    juce::TextButton recordButton { "Record" }, recordFolderButton { "Open recordings" }, resetButton { "Reset" }, copyButton { "Copy mixer results" }, folderButton { "Open presets folder" };
    juce::Label noteLabel, warningLabel;

    struct PresetRow
    {
        Mixer::Preset kind;
        juce::Label label;
        juce::ComboBox box;
        juce::TextButton load { "Load" }, save { "Save" };
    };
    std::array<PresetRow, 3> presets;

    juce::Label venueKnobLabel, venueScreen;
    juce::TextButton turnLeft { "Turn left" }, turnRight { "Turn right" }, push { "Push" };
    juce::String lastKnobLine;
    double lastKnobMs = -1.0e12;

    std::array<Strip, numStrips> strips;
    juce::Rectangle<int> stripsArea;

    juce::Label detailTitle, blockHint;
    std::vector<Field> fields;
    juce::OwnedArray<juce::TextButton> blockButtons;
    juce::ToggleButton blockOn { "This block on" };
    juce::OwnedArray<juce::Label> gridLabels;
    juce::OwnedArray<juce::Component> gridControls;
    int gridColumns = 3;
    juce::Rectangle<int> detailArea;
    juce::ComboBox* outputPresetBox = nullptr;

    std::map<std::string, std::unique_ptr<fx::Effect>> examples;     // one of each block, to ask what its settings are
    juce::String note;
    int ticks = 0, noteTicks = 0, copiedTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};

} // namespace gigrig
