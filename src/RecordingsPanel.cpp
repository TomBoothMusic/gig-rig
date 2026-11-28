#include "RecordingsPanel.h"

namespace gigrig
{

namespace
{
    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, style::panel);
        b.setColour (juce::TextButton::buttonOnColourId, style::green.withAlpha (0.55f));
        b.setColour (juce::TextButton::textColourOffId, style::textMain);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void small (juce::Label& l, const juce::String& text, juce::Colour colour = style::textDim, float size = 12.5f)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, colour);
        l.setMinimumHorizontalScale (0.7f);
    }

    juce::String sizeText (juce::int64 bytes)
    {
        const double g = (double) bytes / 1.0e9;
        return g >= 0.1 ? juce::String (g, 1) + " GB" : juce::String (juce::roundToInt ((double) bytes / 1.0e6)) + " MB";
    }

    juce::String lengthText (double seconds)
    {
        const int s = (int) seconds;
        return s >= 3600 ? juce::String (s / 3600) + " h " + juce::String ((s / 60) % 60) + " min" : juce::String (s / 60) + " min " + juce::String (s % 60) + " s";
    }
}

RecordingsPanel::RecordingsPanel (AudioEngine& e) : engine (e)
{
    addAndMakeVisible (status);

    small (heading, "Recordings on the rig's drive, newest first", style::textMain, 14.0f);
    small (chosenLabel, {}, style::textMain, 14.0f);
    small (room, {});
    small (driveLabel, "USB drive:");
    small (note, {});
    small (checkHelp, "A soundcheck plays the recording's raw files into the sockets they came from, through today's effects, looper and mixer. Those sockets do not hear what is plugged in until Stop.");
    checkHelp.setJustificationType (juce::Justification::topLeft);
    checkHelp.setMinimumHorizontalScale (1.0f);
    note.setMinimumHorizontalScale (1.0f);
    note.setJustificationType (juce::Justification::topLeft);

    for (auto* l : { &heading, &chosenLabel, &room, &driveLabel, &note, &checkHelp })
        addAndMakeVisible (*l);

    box.setRowHeight (44);
    box.setColour (juce::ListBox::backgroundColourId, style::panel);
    box.setColour (juce::ListBox::outlineColourId, style::textDim.withAlpha (0.3f));
    box.setOutlineThickness (1);
    addAndMakeVisible (box);

    for (auto* b : { &playButton, &deleteButton, &copyOneButton, &copyAllButton, &cancelButton, &clearCopiedButton, &clearAllButton, &folderButton, &reportButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }

    for (auto* b : { &keepBox, &loopBox, &loopsBox })
    {
        b->setColour (juce::ToggleButton::textColourId, style::textMain);
        addAndMakeVisible (*b);
    }

    loopBox.setToggleState (true, juce::dontSendNotification);
    loopsBox.setToggleState (true, juce::dontSendNotification);
    loopsBox.setTooltip ("Ticked, every looper press, kick from the pad, effect switch press and expression pedal move of the recording is made again at the same moment, through the effects and the mixer as they are set now");
    loopBox.setTooltip ("Ticked, the recording starts again when it ends, so a mix can be worked on for as long as it takes");
    playButton.setTooltip ("Plays this recording's raw files through the rig as if Tom were playing. Press again to stop.");
    keepBox.setTooltip ("A kept recording is never removed by the two Clear buttons, and Delete refuses it");
    copyOneButton.setTooltip ("Copies this recording into the folder \"GigRig recordings\" on the drive, then reads every file back to check it");
    copyAllButton.setTooltip ("Copies every recording that has not been copied yet, and checks each one");
    clearCopiedButton.setTooltip ("Deletes every recording that has been copied to USB and checked, except the kept ones");
    clearAllButton.setTooltip ("Deletes every recording except the kept ones, copied or not");
    reportButton.setTooltip ("Copies this tab's part of the report, ready to paste to Claude");

    playButton.onClick = [this]
    {
        auto& sc = engine.soundcheck();

        if (sc.isPlaying())
        {
            sc.stop();
            say ("Stopped. The sockets hear what is plugged in again.");
        }
        else if (const auto* i = chosen())
        {
            const auto error = engine.startSoundcheck (i->name, loopBox.getToggleState(), loopsBox.getToggleState());
            say (error.isEmpty() ? "Playing " + i->name + " through the rig. " + (loopsBox.getToggleState() ? engine.loopReplay().statusLine() : juce::String ("Without its loops, kicks and switches.")) : error, error.isNotEmpty());
        }

        showChosen();
    };

    keepBox.onClick = [this]
    {
        if (loading)
            return;

        if (const auto* i = chosen())
        {
            const auto error = engine.recordings().setKept (i->name, keepBox.getToggleState());
            say (error, true);
            refresh();
        }
    };

    deleteButton.onClick = [this]
    {
        if (const auto* i = chosen())
            confirm ("Delete this recording?", i->name + " (" + sizeText (i->bytes) + ") will be gone for good." + (i->copied ? juce::String() : " It has NOT been copied to USB."), "Delete",
                     [this, name = i->name]
                     {
                         const auto error = engine.recordings().remove (name);
                         say (error.isEmpty() ? "Deleted " + name + "." : error, error.isNotEmpty());
                         refresh();
                     });
    };

    const auto startCopy = [this] (const juce::StringArray& names)
    {
        const auto drive = driveBox.getText();
        confirm ("Copy to the USB drive?", "A USB drive at work can make the sound click, so only copy when nobody is listening. The drive must stay in until the copy says it has finished.", "Copy",
                 [this, names, drive]
                 {
                     const auto error = engine.recordings().copy (names, drive);
                     say (error.isEmpty() ? juce::String ("Copying. Leave the drive in.") : error, error.isNotEmpty());
                 });
    };

    copyOneButton.onClick = [this, startCopy] { if (const auto* i = chosen()) startCopy ({ i->name }); };
    copyAllButton.onClick = [startCopy] { startCopy ({}); };
    cancelButton.onClick = [this] { engine.recordings().cancelCopy(); };

    clearCopiedButton.onClick = [this]
    {
        confirm ("Clear the copied recordings?", "Every recording that has been copied to USB and checked will be deleted from the rig. Kept ones stay.", "Clear",
                 [this] { say (engine.recordings().clear (true)); refresh(); });
    };

    clearAllButton.onClick = [this]
    {
        confirm ("Clear every recording?", "Every recording will be deleted from the rig, copied to USB or not. Only kept ones stay.", "Clear all",
                 [this] { say (engine.recordings().clear (false)); refresh(); });
    };

    folderButton.onClick = [] { Mixer::recordingFolder().createDirectory(); Mixer::recordingFolder().startAsProcess(); };
    reportButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (reportText()); say ("The recordings report is copied."); };

    addAndMakeVisible (driveBox);
    addChildComponent (progress);
    refresh();
    startTimerHz (4);
}

RecordingsPanel::~RecordingsPanel() = default;

juce::String RecordingsPanel::reportText() const
{
    return engine.soundcheck().reportText() + engine.loopReplay().reportText() + engine.recordings().reportText();
}

void RecordingsPanel::confirm (const juce::String& title, const juce::String& words, const juce::String& yes, std::function<void()> then)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, title, words, yes, "Cancel", this),
                                  [safe = juce::Component::SafePointer<RecordingsPanel> (this), then] (int result)
                                  {
                                      if (result == 1 && safe != nullptr)
                                          then();
                                  });
}

void RecordingsPanel::say (const juce::String& text, bool bad)
{
    noteText = text;
    noteBad = bad && text.isNotEmpty();
    small (note, noteText, noteBad ? style::red : style::textDim);
    note.setMinimumHorizontalScale (1.0f);
}

const RecordingLibrary::Info* RecordingsPanel::chosen() const
{
    const int row = box.getSelectedRow();
    return row >= 0 && row < (int) list.size() ? &list[(size_t) row] : nullptr;
}

void RecordingsPanel::refresh()
{
    const auto was = chosen() != nullptr ? chosen()->name : juce::String();
    list = engine.recordings().list();
    driveList = engine.recordings().drives();
    box.updateContent();
    int row = list.empty() ? -1 : 0;

    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == was)
            row = (int) i;

    box.selectRow (row);

    const auto drive = driveBox.getText();
    driveBox.clear (juce::dontSendNotification);

    for (size_t i = 0; i < driveList.size(); ++i)
        driveBox.addItem (driveList[i].name, (int) i + 1);

    driveBox.setTextWhenNothingSelected ("no USB drive is plugged in");

    if (! driveList.empty())
    {
        driveBox.setText (drive, juce::dontSendNotification);

        if (driveBox.getSelectedId() == 0)
            driveBox.setSelectedId (1, juce::dontSendNotification);
    }

    juce::int64 bytes = 0;
    for (const auto& i : list)
        bytes += i.bytes;

    small (room, juce::String ((int) list.size()) + (list.size() == 1 ? " recording, " : " recordings, ") + sizeText (bytes) + ". "
                   + juce::String (juce::roundToInt (engine.recordings().freeGb())) + " GB free on the rig."
                   + (driveList.empty() ? juce::String() : " " + juce::String (juce::roundToInt (driveList[(size_t) juce::jmax (0, driveBox.getSelectedId() - 1)].freeGb)) + " GB free on the USB drive."));
    showChosen();
    repaint();
}

void RecordingsPanel::showChosen()
{
    const auto* i = chosen();
    const auto& sc = engine.soundcheck();
    const bool playing = sc.isPlaying();
    const bool copying = engine.recordings().progress().running;
    const bool haveDrive = ! driveList.empty();

    small (chosenLabel, i == nullptr ? juce::String ("No recording is chosen") : i->name + ": " + juce::String (i->files) + " files, " + lengthText (i->seconds) + ", " + sizeText (i->bytes),
           style::textMain, 14.0f);

    loading = true;
    keepBox.setToggleState (i != nullptr && i->kept, juce::dontSendNotification);
    loading = false;

    playButton.setButtonText (playing ? "Stop the soundcheck" : "Play as a soundcheck");
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setEnabled (playing || (i != nullptr && i->soundcheckSockets > 0));
    keepBox.setEnabled (i != nullptr);
    deleteButton.setEnabled (i != nullptr && ! i->kept && ! copying);
    copyOneButton.setEnabled (i != nullptr && haveDrive && ! copying);
    copyAllButton.setEnabled (haveDrive && ! copying);
    clearCopiedButton.setEnabled (! copying);
    clearAllButton.setEnabled (! copying);
    cancelButton.setVisible (copying);
    progress.setVisible (copying);
}

void RecordingsPanel::timerCallback()
{
    if (! isShowing())
        return;

    auto& sc = engine.soundcheck();
    const auto p = engine.recordings().progress();
    const bool recording = engine.mixer().recorder().isRecording();
    const bool playing = sc.isPlaying();
    progressValue = p.done;

    if (playing)
        status.set (sc.missedBlocks() > 0 ? StatusRow::Tone::caution : StatusRow::Tone::good, "SOUNDCHECK",
                    "Playing " + sc.name() + ": " + lengthText (sc.positionSeconds()) + " of " + lengthText (sc.lengthSeconds()),
                    sc.missedBlocks() > 0 ? "The drive did not keep up " + juce::String (sc.missedBlocks()) + " times." : "Its sockets hear the recording, not what is plugged in. Everything after them is live.");
    else if (p.running)
        status.set (StatusRow::Tone::caution, "COPYING", p.now + " (" + juce::String (juce::roundToInt (p.done * 100.0)) + "%)", "Leave the USB drive in until this says it has finished.");
    else if (p.result.isNotEmpty())
        status.set (p.failed ? StatusRow::Tone::bad : StatusRow::Tone::good, p.failed ? "COPY FAILED" : "COPIED", p.result, {});
    else
        status.set (StatusRow::Tone::neutral, "RECORDINGS", juce::String ((int) list.size()) + (list.size() == 1 ? " recording on the drive" : " recordings on the drive"),
                    "Choose one to play it back as a soundcheck, keep it, copy it or delete it.");

    // Something finished: the drive has changed.
    if ((wasCopying && ! p.running) || (wasRecording && ! recording) || wasPlaying != playing || ++ticks >= 20)
    {
        ticks = 0;
        const bool changed = (wasCopying && ! p.running) || (wasRecording && ! recording);
        wasCopying = p.running;
        wasRecording = recording;
        wasPlaying = playing;

        if (changed || driveList.size() != engine.recordings().drives().size())
            refresh();
        else
            showChosen();
    }

    wasCopying = p.running;
    wasRecording = recording;
}

void RecordingsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) list.size())
        return;

    const auto& i = list[(size_t) row];
    const bool playing = engine.soundcheck().isPlaying() && engine.soundcheck().name() == i.name;

    if (selected)
        g.fillAll (style::green.withAlpha (0.22f));

    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (i.name + (playing ? "   (playing)" : ""), 10, 3, width - 20, 20, juce::Justification::centredLeft);

    juce::StringArray facts { lengthText (i.seconds), juce::String (i.files) + " files", sizeText (i.bytes) };
    if (i.kept)                         facts.add ("KEPT");
    facts.add (i.copied ? "copied to USB" : "not copied");
    facts.add (i.soundcheckSockets > 0 ? "soundcheck: " + juce::String (i.soundcheckSockets) + (i.soundcheckSockets == 1 ? " socket" : " sockets") : "no raw files");

    g.setColour (style::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (facts.joinIntoString ("   |   "), 10, 22, width - 20, 18, juce::Justification::centredLeft);
    g.setColour (style::textDim.withAlpha (0.2f));
    g.fillRect (0, height - 1, width, 1);
}

void RecordingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void RecordingsPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    status.setBounds (area.removeFromTop (64));
    area.removeFromTop (10);

    auto right = area.removeFromRight (juce::jmin (380, area.getWidth() / 2));
    area.removeFromRight (16);

    heading.setBounds (area.removeFromTop (22));
    room.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);
    auto foot = area.removeFromBottom (30);
    folderButton.setBounds (foot.removeFromLeft (220));
    foot.removeFromLeft (8);
    reportButton.setBounds (foot.removeFromLeft (200));
    area.removeFromBottom (8);
    box.setBounds (area);

    chosenLabel.setBounds (right.removeFromTop (22));
    right.removeFromTop (6);
    playButton.setBounds (right.removeFromTop (48));
    right.removeFromTop (4);
    loopBox.setBounds (right.removeFromTop (24));
    loopsBox.setBounds (right.removeFromTop (24));
    checkHelp.setBounds (right.removeFromTop (52));
    right.removeFromTop (6);
    keepBox.setBounds (right.removeFromTop (24));
    right.removeFromTop (6);
    deleteButton.setBounds (right.removeFromTop (30));
    right.removeFromTop (16);

    auto driveRow = right.removeFromTop (26);
    driveLabel.setBounds (driveRow.removeFromLeft (80));
    driveBox.setBounds (driveRow);
    right.removeFromTop (6);
    copyOneButton.setBounds (right.removeFromTop (30));
    right.removeFromTop (4);
    copyAllButton.setBounds (right.removeFromTop (30));
    right.removeFromTop (4);
    auto progressRow = right.removeFromTop (24);
    cancelButton.setBounds (progressRow.removeFromRight (110));
    progressRow.removeFromRight (6);
    progress.setBounds (progressRow);
    right.removeFromTop (16);
    clearCopiedButton.setBounds (right.removeFromTop (30));
    right.removeFromTop (4);
    clearAllButton.setBounds (right.removeFromTop (30));
    right.removeFromTop (10);
    note.setBounds (right.removeFromTop (60));
}

} // namespace gigrig
