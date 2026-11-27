#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "PedalPanel.h"
#include "SongBook.h"

namespace gigrig
{

/** One loop track drawn on screen, in the RC-600's colours with the state always written in words too. */
class TrackTile final : public juce::Component
{
public:
    void set (int index, const TrackView& view, const TrackSetup& setup, bool isCurrent, bool blinkOn);
    void paint (juce::Graphics&) override;

private:
    int index = 0;
    TrackView view;
    TrackSetup setup;
    bool current = false, blink = false;
};

/** The "Looper" tab (stage 3): six tracks, the switches as the chosen song sets them,
    and a running account of what the looper did and when. */
class LooperPanel final : public juce::Component,
                          private juce::Timer
{
public:
    LooperPanel (AudioEngine&, PedalInput&, SongBook&);
    ~LooperPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** After the song book loaded a song or read its folder again. */
    void songChanged();

    /** The stage 3 part of the report. Message thread. */
    juce::String reportText();

private:
    void timerCallback() override;
    void chooseSong (int index);
    void pushOptions();
    void labelSwitches (int bank);
    void slowUpdate();

    AudioEngine& engine;
    PedalInput& pedal;
    SongBook& book;

    juce::Label songLabel, outLabel, trimLabel, descriptionHeading, logHeading;
    juce::ComboBox songBox, firstLoopBox, outBox;
    juce::TextButton clearButton { "Clear all" }, copyButton { "Copy looper results" };
    juce::OwnedArray<TrackTile> trackTiles;
    juce::OwnedArray<SwitchTile> switchTiles;
    juce::OwnedArray<juce::Label> socketLabels;
    juce::OwnedArray<juce::ComboBox> socketBoxes;
    int followedKickId = 0;     // the stomp socket box's id the last time it followed the kick pad's socket
    juce::Slider trimSlider;
    juce::TextEditor descriptionView, logView;

    SongSetup song;
    LooperSnapshot snapshot;
    AudioEngine::LoopTiming timing;
    juce::StringArray history;
    juce::String stripMain, stripNote;
    juce::Rectangle<int> stripArea;
    bool beatLit = false, stripWarning = false;
    int shownBank = -1, logLines = 0, ticks = 0, copiedTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};

} // namespace gigrig
