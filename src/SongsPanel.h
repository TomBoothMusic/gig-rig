#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongBook.h"
#include "Style.h"

namespace gigrig
{

/** The "Songs" tab (the song-linking batch, 25 Nov 2026): which song is loaded, where each
    chain's sound comes from (the song's own lines, the default sounds, or left as set by hand),
    the buttons that keep the sounds as they stand for the song or as the default, new entries,
    all six effect switches, and a box to try a Songbook Pro program number without Songbook Pro. */
class SongsPanel final : public juce::Component,
                         private juce::Timer
{
public:
    SongsPanel (AudioEngine&, SongBook&);
    ~SongsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override           { visibilityChanged(); }

    /** Brings everything in line with the song book and the rack. */
    void refresh();

    /** The button that lifts this panel out into a window of its own (so the Effects tab can stay
        open beside it on a second screen) and puts it back. The main window does the moving. */
    std::function<void()> onWindowButton;
    void setInOwnWindow (bool);

private:
    void timerCallback() override;
    void say (const juce::String& text);
    void refreshNotes();

    AudioEngine& engine;
    SongBook& book;
    EffectsRack& rack;

    juce::Label songLabel, statusLabel, programLabel, newLabel, switchHeading, fileHeading, notesHeading, helpLabel;
    juce::ComboBox songBox;
    juce::TextEditor programEditor, newName, newProgram, fileView, notesView;
    juce::TextButton programButton { "Load that program" }, copyButton { "Copy song results" },
                     keepButton { "Keep these sounds for this song" }, defaultButton { "Keep these sounds as the default" },
                     forgetButton { "Forget this song's own sounds" }, folderButton { "Open songs folder" },
                     addButton { "Add as a new entry" }, againButton { "Set this song's sounds again" },
                     windowButton { "Open in its own window" },
                     moveButton { "Move reverbs and delays to the mixer" }, backButton { "Put the songs back" };
    juce::ToggleButton tailsBox { "Delays and reverbs ring out when the sound changes" };
    std::array<juce::ComboBox, EffectsRack::numPedalAssigns> switchBoxes, actionBoxes;
    juce::String said, shownNotes, shownFile;
    int copiedTicks = 0, ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongsPanel)
};

} // namespace gigrig
