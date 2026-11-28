#include "BreakMusicPanel.h"

namespace gigrig
{

namespace
{
    void setUpSlider (juce::Slider& s, double low, double high, double step, double value, const juce::String& suffix, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        s.setRange (low, high, step);
        s.setValue (value, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.setTooltip (tip);
        s.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
}

BreakMusicPanel::BreakMusicPanel (AudioEngine& engineToUse)
    : engine (engineToUse), music (engineToUse.breakMusic())
{
    if (BreakMusic::memoryOnDisk.load() && BreakMusic::stateFile().existsAsFile())
        music.loadStateText (BreakMusic::stateFile().loadFileAsString());

    style::body (help, {});
    help.setColour (juce::Label::textColourId, style::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setMinimumHorizontalScale (1.0f);

    style::body (playLabel, "Play");
    style::combo (mixBox);
    mixBox.setTooltip ("Every music file in the break music folder. On the rig computer the phone is in the list too.");
    mixBox.setTextWhenNothingSelected ("No mixes");

    const auto say = [this] (const juce::String& line)
    {
        note = line;
        noteTicks = 0;
        shownStatus.clear();
    };

    playButton.setTooltip ("Starts the mix where it was left last time, fading in");
    playButton.onClick = [this, say]       { say (music.play (chosenName())); };
    restartButton.setTooltip ("Starts the mix from its beginning");
    restartButton.onClick = [this, say]    { say (music.play (chosenName(), true)); };
    stopButton.setTooltip ("Fades out and stops. The place in the mix is remembered.");
    stopButton.onClick = [this, say]       { music.stop(); say ({}); };
    folderButton.setTooltip ("Put your mixes in this folder and they appear in the Play box");
    folderButton.onClick = [this]          { music.folder().createDirectory(); music.folder().startAsProcess(); };

    style::body (levelLabel, "Level");
    setUpSlider (levelSlider, BreakMusic::minLevelDb, BreakMusic::maxLevelDb, 0.5, music.levelDb(), " dB", "How loud the break music is when it is up. With the mixer on it also follows the main output's level.");
    levelSlider.onValueChange = [this] { if (! loading) music.setLevelDb ((float) levelSlider.getValue()); };
    upButton.setTooltip ("Fades the music back up to the level set. This is the only way it ever comes back.");
    upButton.onClick = [this]   { music.bringUp(); shownStatus.clear(); };
    downButton.setTooltip ("Fades the music down. It keeps playing silently.");
    downButton.onClick = [this] { music.fadeDown(); shownStatus.clear(); };

    const auto s = music.settings();
    style::body (downLabel, "Fade down");
    style::body (upLabel, "Fade up");
    setUpSlider (downSlider, 0.5, 15.0, 0.5, s.fadeDownSeconds, " s", "How long the music takes to fade away when you start playing");
    setUpSlider (upSlider, 0.5, 15.0, 0.5, s.fadeUpSeconds, " s", "How long the music takes to come back when you bring it up");
    downSlider.onValueChange = [this] { pushSettings(); };
    upSlider.onValueChange = [this] { pushSettings(); };

    style::body (whenLabel, "Auto fade down on");

    for (auto* b : { &switchBox, &songBox, &soundBox })
    {
        b->setColour (juce::ToggleButton::textColourId, style::textMain);
        b->onClick = [this] { pushSettings(); };
    }

    switchBox.setTooltip ("Any press of a footswitch on the pedal counts as starting to play");
    songBox.setTooltip ("A song loading (from Songbook Pro or the Songs tab) counts as starting to play. Leave it off if you choose the next song while the music is still wanted.");
    soundBox.setTooltip ("Sound on any input socket counts as starting to play. A microphone that hears the break music from the speakers can set it off, so try it before trusting it.");
    setUpSlider (soundSlider, -60.0, 0.0, 1.0, s.soundDb, " dB", "How loud an input has to be, for about a sixth of a second, to count");
    soundSlider.onValueChange = [this] { pushSettings(); };

    style::body (pairLabel, "Output (mixer off)");
    style::combo (pairBox);

    for (int pair = 0; pair < 4; ++pair)
        pairBox.addItem (returnName (pair * 2) + " and " + returnName (pair * 2 + 1).fromFirstOccurrenceOf ("Return ", false, false), pair + 1);

    pairBox.setTooltip ("With the mixer on, the break music always goes to the main output. With the mixer off it goes straight to this pair of returns.");
    pairBox.onChange = [this] { pushSettings(); };

    style::body (knobLabel, "ENC 5");
    knobLabel.setTooltip ("These buttons stand in for the fifth knob on the board. It rests on PA/venue. Hold hands it to the break music: Turn is then the level and Push fades it up or down.");
    knobScreen.setFont (style::monoFont (13.0f));
    knobScreen.setColour (juce::Label::textColourId, style::textMain);
    knobScreen.setColour (juce::Label::backgroundColourId, style::panel);
    knobScreen.setMinimumHorizontalScale (0.6f);

    const auto knob = [this] (juce::String line)
    {
        lastKnobLine = std::move (line);
        lastKnobMs = juce::Time::getMillisecondCounterHiRes();
        refresh();
    };
    turnLeft.onClick = [this, knob]    { knob (engine.venueKnob().turn (-1, juce::Time::getMillisecondCounterHiRes())); };
    turnRight.onClick = [this, knob]   { knob (engine.venueKnob().turn (1, juce::Time::getMillisecondCounterHiRes())); };
    push.onClick = [this, knob]        { knob (engine.venueKnob().push (juce::Time::getMillisecondCounterHiRes())); };
    hold.onClick = [this, knob]        { knob (engine.venueKnob().hold (juce::Time::getMillisecondCounterHiRes())); };

    style::heading (reportHeading, "Report");
    style::readOnlyEditor (reportView);

    for (auto* c : std::initializer_list<juce::Component*> { &status, &help, &playLabel, &mixBox, &playButton, &restartButton, &stopButton, &folderButton,
                                                             &levelLabel, &levelSlider, &upButton, &downButton, &downLabel, &downSlider, &upLabel, &upSlider,
                                                             &whenLabel, &switchBox, &songBox, &soundBox, &soundSlider, &pairLabel, &pairBox,
                                                             &knobLabel, &turnLeft, &turnRight, &push, &hold, &knobScreen, &reportHeading, &reportView })
        addAndMakeVisible (c);

    refreshList();
    refresh();
    startTimerHz (10);
}

BreakMusicPanel::~BreakMusicPanel()
{
    stopTimer();
    music.saveState();
}

juce::String BreakMusicPanel::chosenName() const
{
    const int i = mixBox.getSelectedId() - 1;
    return i >= 0 && i < shownMixes.size() ? shownMixes[i] : juce::String();
}

void BreakMusicPanel::refreshList()
{
    auto list = music.mixes();

    if (BreakMusic::phoneSupported())
        list.add (BreakMusic::phoneName());

    if (list == shownMixes && mixBox.getSelectedId() > 0)
        return;

    const auto keep = chosenName().isNotEmpty() ? chosenName() : music.mixName();
    shownMixes = list;
    mixBox.clear (juce::dontSendNotification);

    for (int i = 0; i < list.size(); ++i)
        mixBox.addItem (list[i], i + 1);

    const int at = list.indexOf (keep);
    mixBox.setSelectedId (at >= 0 ? at + 1 : (list.isEmpty() ? 0 : 1), juce::dontSendNotification);
}

void BreakMusicPanel::pushSettings()
{
    if (loading)
        return;

    auto s = music.settings();
    s.fadeDownSeconds = downSlider.getValue();
    s.fadeUpSeconds = upSlider.getValue();
    s.onSwitch = switchBox.getToggleState();
    s.onSong = songBox.getToggleState();
    s.onSound = soundBox.getToggleState();
    s.soundDb = (float) soundSlider.getValue();
    s.returnPair = juce::jmax (0, pairBox.getSelectedId() - 1);
    music.setSettings (s);
}

void BreakMusicPanel::refresh()
{
    loading = true;
    const auto s = music.settings();
    levelSlider.setValue (music.levelDb(), juce::dontSendNotification);
    downSlider.setValue (s.fadeDownSeconds, juce::dontSendNotification);
    upSlider.setValue (s.fadeUpSeconds, juce::dontSendNotification);
    switchBox.setToggleState (s.onSwitch, juce::dontSendNotification);
    songBox.setToggleState (s.onSong, juce::dontSendNotification);
    soundBox.setToggleState (s.onSound, juce::dontSendNotification);
    soundSlider.setValue (s.soundDb, juce::dontSendNotification);
    pairBox.setSelectedId (s.returnPair + 1, juce::dontSendNotification);

    if (music.isPlaying())
        if (const int at = shownMixes.indexOf (music.mixName()); at >= 0)
            mixBox.setSelectedId (at + 1, juce::dontSendNotification);

    loading = false;
    shownStatus.clear();
}

void BreakMusicPanel::timerCallback()
{
    music.tick();

    if (++listTicks >= 20)
    {
        listTicks = 0;
        refreshList();
    }

    if (++noteTicks > 80)
        note.clear();

    const auto line = music.statusLine();
    const bool playing = music.isPlaying();
    const auto key = line + "/" + note + "/" + juce::String ((int) engine.isOpen()) + "/" + juce::String ((int) engine.mixer().isOn());

    if (key != shownStatus)
    {
        shownStatus = key;
        const auto where = ! engine.isOpen() ? juce::String ("No sound yet: start the audio on the first tab.")
                                             : engine.mixer().isOn() ? juce::String ("It goes to the mixer's main output.")
                                                                     : juce::String ("The mixer is off, so it goes straight to the returns chosen below.");

        if (note.isNotEmpty())
            status.set (StatusRow::Tone::bad, "PROBLEM", note, where);
        else if (! playing)
            status.set (StatusRow::Tone::neutral, "STOPPED", line, shownMixes.isEmpty() ? juce::String ("Press 'Open the break music folder', put a mix in it, then choose it here.") : "Choose a mix and press Play. " + where);
        else if (music.isUp())
            status.set (StatusRow::Tone::good, "UP", line, where + " It fades down by itself when you start playing.");
        else
            status.set (StatusRow::Tone::caution, "DOWN", line, "It only comes back when you press 'Bring the music up' (or turn the knob up).");

        levelSlider.setValue (music.levelDb(), juce::dontSendNotification);
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto knobLine = nowMs - lastKnobMs < Mixer::browseTimeoutMs && lastKnobLine.isNotEmpty() ? lastKnobLine : engine.venueKnob().screen (nowMs);

    if (knobScreen.getText() != knobLine)
        knobScreen.setText (knobLine, juce::dontSendNotification);

    if (const auto report = music.reportText(); report != shownReport)
    {
        shownReport = report;
        reportView.setText (report, false);
    }
}

void BreakMusicPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    status.setBounds (area.removeFromTop (68));
    area.removeFromTop (6);
    // The help paragraph went on 28 Nov 2026 (Tom: short labels, no tips); docs/break-music.md says it all.

    auto row = area.removeFromTop (28);
    playLabel.setBounds (row.removeFromLeft (44));
    mixBox.setBounds (row.removeFromLeft (300));
    row.removeFromLeft (8);
    playButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (6);
    restartButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (6);
    stopButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (16);
    folderButton.setBounds (row.removeFromLeft (juce::jmin (230, row.getWidth())));

    area.removeFromTop (10);
    row = area.removeFromTop (30);
    levelLabel.setBounds (row.removeFromLeft (44));
    levelSlider.setBounds (row.removeFromLeft (400));
    row.removeFromLeft (16);
    upButton.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (6);
    downButton.setBounds (row.removeFromLeft (120));

    area.removeFromTop (10);
    row = area.removeFromTop (28);
    downLabel.setBounds (row.removeFromLeft (120));
    downSlider.setBounds (row.removeFromLeft (280));
    row.removeFromLeft (24);
    upLabel.setBounds (row.removeFromLeft (100));
    upSlider.setBounds (row.removeFromLeft (280));

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    whenLabel.setBounds (row.removeFromLeft (180));
    switchBox.setBounds (row.removeFromLeft (190));
    songBox.setBounds (row.removeFromLeft (120));
    soundBox.setBounds (row.removeFromLeft (180));
    soundSlider.setBounds (row.removeFromLeft (juce::jmin (240, row.getWidth())));

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    pairLabel.setBounds (row.removeFromLeft (220));
    pairBox.setBounds (row.removeFromLeft (330));

    area.removeFromTop (10);
    row = area.removeFromTop (26);
    knobLabel.setBounds (row.removeFromLeft (100));
    turnLeft.setBounds (row.removeFromLeft (78));
    row.removeFromLeft (4);
    turnRight.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    push.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (4);
    hold.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (8);
    knobScreen.setBounds (row);

    area.removeFromTop (10);
    reportHeading.setBounds (area.removeFromTop (22));
    reportView.setBounds (area);
}

} // namespace gigrig
