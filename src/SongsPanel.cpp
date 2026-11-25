#include "SongsPanel.h"
#include "EffectsPanel.h"

namespace gigrig
{

namespace
{
    void field (juce::TextEditor& e, const juce::String& hint)
    {
        e.setTextToShowWhenEmpty (hint, style::textDim);
        e.setColour (juce::TextEditor::backgroundColourId, style::panel);
        e.setColour (juce::TextEditor::textColourId, style::textMain);
        e.setColour (juce::TextEditor::outlineColourId, style::textDim.withAlpha (0.4f));
    }
}

SongsPanel::SongsPanel (AudioEngine& e, SongBook& b)
    : engine (e), book (b), rack (e.effects())
{
    style::body (songLabel, "Song");
    style::body (statusLabel, {});
    style::body (programLabel, "Try a program number");
    style::body (newLabel, "New entry (this looper layout, these sounds)");
    style::heading (switchHeading, "SWITCHES THE EFFECTS USE IN THIS SONG (THE LOOPER DOES NOT HEAR THEM)");
    style::heading (fileHeading, "THIS SONG'S FILE");
    style::heading (notesHeading, "WHAT HAPPENED");
    style::body (helpLabel, "Set the chains up on the Effects tab with Load and Add to chain, then come back and press Keep. "
                            "A song names its presets, so changing a preset changes every song that uses it.");
    helpLabel.setColour (juce::Label::textColourId, style::textDim);
    helpLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
    statusLabel.setColour (juce::Label::textColourId, style::green);

    style::combo (songBox);
    songBox.setTooltip ("The same list as the Looper tab's Song box. Loading a song clears every loop and sets its sounds.");
    songBox.onChange = [this] { book.load (songBox.getSelectedId() - 1, "chosen on the Songs tab"); };

    field (programEditor, "e.g. 11");
    field (newName, "Name, e.g. Under The Bridge");
    field (newProgram, "Program");
    programEditor.setInputRestrictions (3, "0123456789");
    newProgram.setInputRestrictions (3, "0123456789");
    programEditor.onReturnKey = [this] { programButton.triggerClick(); };
    programButton.setTooltip ("Does exactly what a program change from Songbook Pro does. Programs count from 1, like the RC-600's memories.");

    programButton.onClick = [this]
    {
        if (const int n = programEditor.getText().getIntValue(); n >= 1 && n <= 128)
            book.programChange (n, "the Songs tab");
        else
            say ("Type a program number from 1 to 128 first.");
    };

    keepButton.setTooltip ("Writes this song's file: for each chain that is on, the presets it was built from and anything changed since, plus the switches below.");
    defaultButton.setTooltip ("Every song with no lines of its own for a chain uses the default sounds.");
    forgetButton.setTooltip ("Takes this song's own sounds away, so it follows the default sounds again.");
    againButton.setTooltip ("Puts the chains and switches back the way this song's file (and the default sounds) say.");
    keepButton.onClick    = [this] { say (book.keepForCurrent()); };
    defaultButton.onClick = [this] { say (book.keepAsDefault()); };
    forgetButton.onClick  = [this] { say (book.forgetCurrent()); };
    againButton.onClick   = [this] { book.applySounds ("asked for on the Songs tab"); };
    folderButton.onClick  = [this] { book.folder().createDirectory(); book.folder().startAsProcess(); };

    addButton.onClick = [this]
    {
        const auto error = book.addEntry (newName.getText(), newProgram.getText().getIntValue());

        if (error.isNotEmpty())
        {
            say (error);
            return;
        }

        newName.clear();
        newProgram.clear();
    };

    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard ("Gig Rig build " + juce::String (GIGRIG_BUILD_LABEL) + "\n" + book.reportText() + "\n" + rack.reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = 8;
    };

    tailsBox.setColour (juce::ToggleButton::textColourId, style::textMain);
    tailsBox.setTooltip ("On: when a song or preset changes a chain's effects, the old delay repeats and reverb fade away by themselves (up to 10 seconds). Off: they stop at once.");
    tailsBox.onClick = [this] { rack.setTailsRingOut (tailsBox.getToggleState()); };

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        for (auto* box : { &switchBoxes[(size_t) i], &actionBoxes[(size_t) i] })
        {
            style::combo (*box);
            box->onChange = [this, i] { EffectsPanel::pushPedalRow (rack, i, switchBoxes[(size_t) i], actionBoxes[(size_t) i]); };
            addAndMakeVisible (box);
        }

    style::readOnlyEditor (fileView);
    style::readOnlyEditor (notesView);
    fileView.setMultiLine (true, true);         // wrap: these are sentences, not tables
    notesView.setMultiLine (true, true);

    for (auto* c : std::initializer_list<juce::Component*> { &songLabel, &statusLabel, &programLabel, &newLabel, &switchHeading, &fileHeading, &notesHeading, &helpLabel,
                                                             &songBox, &programEditor, &newName, &newProgram, &fileView, &notesView, &programButton, &copyButton,
                                                             &keepButton, &defaultButton, &forgetButton, &folderButton, &addButton, &againButton, &tailsBox })
        addAndMakeVisible (c);

    refresh();
    startTimerHz (4);
}

SongsPanel::~SongsPanel()
{
    stopTimer();
}

void SongsPanel::say (const juce::String& text)
{
    said = text;
    refresh();
}

void SongsPanel::refresh()
{
    songBox.clear (juce::dontSendNotification);

    for (int i = 0; i < book.numEntries(); ++i)
    {
        const auto& e = book.entry (i);
        songBox.addItem (e.name + (e.program > 0 ? "   (program " + juce::String (e.program) + ")" : juce::String())
                                + (e.hasFile && e.sounds.saysAnything() ? "   *" : ""), i + 1);
    }

    songBox.setSelectedId (book.currentIndex() + 1, juce::dontSendNotification);
    statusLabel.setText (book.statusLine(), juce::dontSendNotification);
    tailsBox.setToggleState (rack.tailsRingOut(), juce::dontSendNotification);
    forgetButton.setEnabled (book.currentEntry().hasFile);

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        EffectsPanel::fillPedalRow (rack, i, switchBoxes[(size_t) i], actionBoxes[(size_t) i]);

    refreshNotes();
}

void SongsPanel::refreshNotes()
{
    statusLabel.setText (book.statusLine(), juce::dontSendNotification);

    const auto& e = book.currentEntry();
    const auto file = e.hasFile ? e.sounds.toText (e.name)
                                : "\"" + e.name + "\" has no file of its own yet.\n\n"
                                  + (book.hasDefault() ? "It uses the default sounds." : "There are no default sounds either, so loading it leaves the effects as they are.")
                                  + "\n\nPress \"Keep these sounds for this song\" to give it one.";

    if (file != shownFile)
    {
        shownFile = file;
        fileView.setText (file, false);
    }

    juce::String notes;

    if (said.isNotEmpty())
        notes << said << "\n\n";

    notes << book.lastNote() << "\n";

    for (const auto& p : book.lastProblems())
        notes << "PROBLEM: " << p << "\n";

    if (rack.ampNote.isNotEmpty())
        notes << "\n" << rack.ampNote << "\n";

    if (const auto heard = book.programLines(); ! heard.isEmpty())
        notes << "\nProgram changes heard:\n" << heard.joinIntoString ("\n") << "\n";

    if (rack.tailsRinging() > 0)
        notes << "\nRinging out now: " << rack.tailsRinging() << " chain(s).\n";

    if (notes != shownNotes)
    {
        shownNotes = notes;
        notesView.setText (notes, false);
    }
}

void SongsPanel::timerCallback()
{
    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy song results");

    // Once a second: a song file written by hand (or sent by Claude) is picked up by itself.
    // The words only: filling the boxes again would close one Tom has open.
    if (++ticks % 4 == 0 && ! book.rescanIfChanged() && isShowing())
        refreshNotes();
}

void SongsPanel::visibilityChanged()
{
    if (isShowing())
        refresh();      // the effect lists in the switch boxes follow what the Effects tab did meanwhile
}

void SongsPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void SongsPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    auto top = area.removeFromTop (28);
    songLabel.setBounds (top.removeFromLeft (44));
    songBox.setBounds (top.removeFromLeft (330));
    copyButton.setBounds (top.removeFromRight (160));
    top.removeFromRight (8);
    programButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    programEditor.setBounds (top.removeFromRight (64));
    programLabel.setBounds (top.removeFromRight (150));

    area.removeFromTop (6);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto buttons = area.removeFromTop (30);
    keepButton.setBounds (buttons.removeFromLeft (250));
    buttons.removeFromLeft (6);
    defaultButton.setBounds (buttons.removeFromLeft (240));
    buttons.removeFromLeft (6);
    forgetButton.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (6);
    againButton.setBounds (buttons.removeFromLeft (juce::jmin (220, buttons.getWidth())));

    area.removeFromTop (4);
    helpLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    auto fresh = area.removeFromTop (28);
    newLabel.setBounds (fresh.removeFromLeft (290));
    newName.setBounds (fresh.removeFromLeft (250));
    fresh.removeFromLeft (6);
    newProgram.setBounds (fresh.removeFromLeft (80));
    fresh.removeFromLeft (6);
    addButton.setBounds (fresh.removeFromLeft (150));
    folderButton.setBounds (fresh.removeFromRight (150));

    area.removeFromTop (12);
    switchHeading.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    const int half = (area.getWidth() - 14) / 2;

    for (int row = 0; row < 3; ++row)
    {
        auto line = area.removeFromTop (28);

        for (int column = 0; column < 2; ++column)
        {
            const int i = row + column * 3;

            if (i >= EffectsRack::numPedalAssigns)
                continue;

            auto cell = column == 0 ? line.removeFromLeft (half) : line.removeFromRight (half);
            switchBoxes[(size_t) i].setBounds (cell.removeFromLeft (110).reduced (0, 2));
            actionBoxes[(size_t) i].setBounds (cell.reduced (4, 2));
        }
    }

    area.removeFromTop (6);
    tailsBox.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto left = area.removeFromLeft (half);
    area.removeFromLeft (14);
    fileHeading.setBounds (left.removeFromTop (20));
    fileView.setBounds (left);
    notesHeading.setBounds (area.removeFromTop (20));
    notesView.setBounds (area);
}

} // namespace gigrig
