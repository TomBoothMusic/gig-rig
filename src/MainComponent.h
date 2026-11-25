#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "AudioPanel.h"
#include "DeviceCheckPanel.h"
#include "EffectsPanel.h"
#include "LooperPanel.h"
#include "PedalPanel.h"
#include "SongBook.h"
#include "SongsPanel.h"

namespace gigrig
{

/** The window's contents: a title, the Copy report button and one tab per stage. */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void moveSongsPanel (bool toOwnWindow);

    AudioEngine engine;     // declared first: the panels below use it, so it must outlive them
    PedalInput pedal;       // the RC-600's switches; feeds the looper inside the engine

    SongBook songBook { engine, SongBook::defaultSongFolder(), EffectsPanel::presetFolder() };     // the song list: looper layouts and sounds

    juce::Label title, subtitle;
    juce::TextButton copyButton { "Copy report" };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AudioPanel audioPanel { engine };
    DeviceCheckPanel devicePanel;
    PedalPanel pedalPanel { pedal };
    LooperPanel looperPanel { engine, pedal, songBook };
    SongsPanel songsPanel { engine, songBook };
    EffectsPanel effectsPanel { engine };
    AmpCapturePanel ampPanel { engine };
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<juce::DocumentWindow> songsWindow;      // the Songs tab while it is lifted out; declared after the panel so it goes first

    DeviceChoice reopenChoice;      // what was running before a rescan, to start it again after
    int reopenBufferSize = 0;
    bool firstScan = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace gigrig
