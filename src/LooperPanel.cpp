#include "LooperPanel.h"

namespace gigrig
{

namespace
{
    constexpr int timerHz = 30;
    constexpr int maxLogLines = 400;
    constexpr int historyLines = 150;
    constexpr int noSocketId = 100;

    juce::String seconds (double value)
    {
        return juce::String (value, 2) + " s";
    }

    juce::String barsText (double bars)
    {
        const auto whole = juce::roundToInt (bars);
        return std::abs (bars - whole) < 0.001 ? juce::String (whole) + (whole == 1 ? " bar" : " bars") : juce::String (bars, 2) + " bars";
    }
}

//==============================================================================
void TrackTile::set (int newIndex, const TrackView& newView, const TrackSetup& newSetup, bool isCurrent, bool blinkOn)
{
    index = newIndex;
    view = newView;
    setup = newSetup;
    current = isCurrent;
    blink = blinkOn;
    repaint();
}

void TrackTile::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    // The RC-600's own colours: red recording, yellow overdubbing, green playing.
    juce::Colour colour = style::textDim;
    juce::String word = trackStateName (view.state);

    switch (view.state)
    {
        case TrackState::empty:        colour = style::textDim.withAlpha (0.5f); break;
        case TrackState::armed:        colour = blink ? style::red : style::red.withAlpha (0.4f); break;
        case TrackState::recording:    colour = style::red; break;
        case TrackState::closing:      colour = style::red; break;
        case TrackState::playing:      colour = style::green; break;
        case TrackState::overdubbing:  colour = style::amber; break;
        case TrackState::stopping:     colour = blink ? style::green : style::green.withAlpha (0.4f); break;
        case TrackState::stopped:      colour = style::textMain; break;
    }

    if (view.undone && view.state != TrackState::empty)
        word << " (undone)";

    const bool lit = view.state != TrackState::empty && view.state != TrackState::stopped;

    g.setColour (lit ? colour.withAlpha (0.16f).overlaidWith (style::panel.withAlpha (0.84f)) : style::panel);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (area, 6.0f, lit ? 2.0f : 1.0f);

    area = area.reduced (8.0f, 6.0f);

    auto top = area.removeFromTop (22.0f);
    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("T" + juce::String (index + 1), top.removeFromLeft (30.0f), juce::Justification::centredLeft);

    const bool somethingUndone = view.state != TrackState::empty && (view.undone || (view.hasOverdub && view.overdubUndone));

    if (somethingUndone)
    {
        // Like the RC-600's red light: something on this track is undone and can be brought back.
        const auto badge = top.removeFromRight (104.0f).reduced (0.0f, 2.0f);
        g.setColour (style::red);
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText ("UNDONE: redo?", badge, juce::Justification::centred);
    }
    else if (current)
    {
        g.setColour (style::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("current", top, juce::Justification::centredRight);
    }

    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawFittedText (word, area.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

    auto bar = area.removeFromBottom (8.0f);
    g.setColour (style::background);
    g.fillRoundedRectangle (bar, 3.0f);

    const bool recording = view.state == TrackState::recording || view.state == TrackState::closing;

    if (view.hasAudio && ! recording && view.state != TrackState::empty)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, view.position))), 3.0f);
    }

    area.removeFromBottom (4.0f);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.setColour (style::textMain);

    juce::String length;

    if (recording)
        length = seconds (view.seconds) + (view.bars > 0.0 ? "  (" + juce::String (view.bars, 1) + " bars)" : juce::String());
    else if (view.hasAudio)
        length = barsText (view.bars) + "  " + seconds (view.seconds) + (view.hasOverdub ? (view.overdubUndone ? "  overdub undone" : "  + overdub") : "");
    else
        length = setup.bars > 0 ? "preset: " + barsText (setup.bars) : juce::String ("free length");

    g.drawFittedText (length, area.removeFromBottom (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

    g.setColour (style::textDim);
    g.drawFittedText ("hears: " + describeRcInputs (setup.rcInputs) + (view.lanes > 1 ? " (kept apart)" : ""), area.toNearestInt(), juce::Justification::centredLeft, 2, 0.75f);
}

//==============================================================================
LooperPanel::LooperPanel (AudioEngine& engineToUse, PedalInput& pedalToUse)
    : engine (engineToUse), pedal (pedalToUse)
{
    style::body (songLabel, "Song");
    style::combo (songBox);
    style::combo (firstLoopBox);
    style::combo (outBox);

    for (int i = 0; i < numBuiltInSongs(); ++i)
    {
        const auto& s = builtInSong (i);
        songBox.addItem (s.title() + (s.rcMemory > 0 ? "   (RC-600 memory " + juce::String (s.rcMemory) + ")" : juce::String()), i + 1);
    }

    songBox.setTooltip ("Each song sets the switches, the tracks' inputs and any preset lengths the way the RC-600 memory does. Changing song clears every loop.");
    songBox.onChange = [this] { chooseSong (songBox.getSelectedId() - 1); };

    firstLoopBox.addItem ("First loop sets the tempo", 1);
    firstLoopBox.addItem ("Use the song's saved tempo", 2);
    firstLoopBox.setSelectedId (1, juce::dontSendNotification);
    firstLoopBox.setTooltip ("First loop sets the tempo: the first loop's length, divided by its bars, becomes one bar. "
                             "Saved tempo: the bar length comes from the tempo saved in the RC-600 memory. Takes effect when every track is clear.");
    firstLoopBox.onChange = [this] { pushOptions(); };

    clearButton.setTooltip ("Clears all six tracks");
    clearButton.onClick = [this] { engine.looper().clearAll(); };

    copyButton.setTooltip ("Copies the looper's set-up and everything it did, ready to paste to Claude");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = timerHz * 2;
    };

    for (int t = 0; t < numLoopTracks; ++t)
        addAndMakeVisible (trackTiles.add (new TrackTile()));

    for (int sw = 0; sw < numPedalSwitches; ++sw)
    {
        auto* tile = switchTiles.add (new SwitchTile());
        tile->onSwitch = [this, sw] (bool isDown) { pedal.screenSwitch (sw, isDown); };
        addAndMakeVisible (tile);
    }

    const auto options = engine.looper().options();

    for (int s = 0; s < numLoopSources; ++s)
    {
        auto* label = socketLabels.add (new juce::Label());
        style::body (*label, loopSourceName (s));
        addAndMakeVisible (label);

        auto* box = socketBoxes.add (new juce::ComboBox());
        style::combo (*box);
        box->addItem ("none", noSocketId);

        for (int socket = 1; socket <= 16; ++socket)
            box->addItem ("socket " + juce::String (socket), socket);

        const auto input = options.inputFor[(size_t) s];
        box->setSelectedId (input >= 0 ? input + 1 : noSocketId, juce::dontSendNotification);
        box->setTooltip ("Which of the mixer's input sockets this is plugged into");
        box->onChange = [this] { pushOptions(); };
        addAndMakeVisible (box);
    }

    style::body (outLabel, "Loops play on");
    outBox.addItem ("returns 5+6 (the CQ's USB channel)", 1);
    outBox.addItem ("vocal loops on 3+4 (ST1), the rest on 5+6", 2);
    outBox.addItem ("two-source tracks split: first on 3+4, second on 5+6", 3);
    outBox.setSelectedId (1, juce::dontSendNotification);
    outBox.onChange = [this] { pushOptions(); };

    style::body (trimLabel, "Timing trim");
    trimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    trimSlider.setRange (-10.0, 10.0, 0.1);
    trimSlider.setValue (engine.getLoopTrimMs(), juce::dontSendNotification);
    trimSlider.setTextValueSuffix (" ms");
    trimSlider.setDoubleClickReturnValue (true, 0.0);
    trimSlider.setColour (juce::Slider::textBoxTextColourId, style::textMain);
    trimSlider.setColour (juce::Slider::textBoxOutlineColourId, style::textDim.withAlpha (0.4f));
    trimSlider.setTooltip ("Leave at 0 unless loops sound early or late against your live playing. Plus moves the loop earlier. Double-click to go back to 0.");
    trimSlider.onValueChange = [this] { engine.setLoopTrimMs (trimSlider.getValue()); slowUpdate(); repaint (stripArea); };

    style::heading (descriptionHeading, "How this song is set up");
    style::heading (logHeading, "What the looper did");
    style::readOnlyEditor (descriptionView);
    style::readOnlyEditor (logView);
    descriptionView.setMultiLine (true, true);
    logView.setMultiLine (true, true);
    descriptionView.setFont (style::monoFont (13.0f));
    logView.setFont (style::monoFont (13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &songLabel, &songBox, &firstLoopBox, &clearButton, &copyButton, &outLabel, &outBox,
                                                             &trimLabel, &trimSlider, &descriptionHeading, &logHeading, &descriptionView, &logView })
        addAndMakeVisible (c);

    songBox.setSelectedId (1, juce::dontSendNotification);
    chooseSong (0);
    slowUpdate();
    startTimerHz (timerHz);
}

LooperPanel::~LooperPanel()
{
    stopTimer();
}

void LooperPanel::chooseSong (int index)
{
    if (index < 0 || index >= numBuiltInSongs())
        return;

    song = builtInSong (index);
    engine.looper().setSong (song);
    descriptionView.setText (describeSong (song), false);
    descriptionView.setCaretPosition (0);
    shownBank = -1;
}

void LooperPanel::pushOptions()
{
    auto options = engine.looper().options();

    options.fixedTempo = firstLoopBox.getSelectedId() == 2;
    options.outputMode = outBox.getSelectedId() == 2 ? 1 : outBox.getSelectedId() == 3 ? 3 : 0;
    options.doubleWindowMs = pedal.doubleWindowMs();

    for (int s = 0; s < numLoopSources; ++s)
    {
        const auto id = socketBoxes[s]->getSelectedId();
        options.inputFor[(size_t) s] = id == noSocketId || id <= 0 ? -1 : id - 1;
    }

    engine.looper().setOptions (options);
}

void LooperPanel::labelSwitches (int bank)
{
    shownBank = bank;

    for (int sw = 0; sw < numPedalSwitches; ++sw)
    {
        juce::String detail;

        if (sw < 9)
        {
            detail = describeFunction (decodeRcFunction (song.bank[juce::jlimit (0, numLoopBanks - 1, bank)][sw]));
        }
        else
        {
            juce::StringArray linked;

            for (const auto& a : song.assign)
                if (a.source == LoopAssign::Source::pedal && a.sw == sw)
                    linked.add (describeAssign (a).fromFirstOccurrenceOf ("also does: ", false, false));

            detail = linked.isEmpty() ? juce::String ("-") : linked.joinIntoString (", ");
        }

        switchTiles[sw]->setText (pedalSwitchName (sw), detail);
    }
}

void LooperPanel::slowUpdate()
{
    timing = engine.updateLoopTiming();

    if (pedal.followsBeat())
    {
        // Tom double-presses in time with the music (say beats 3 and 4), so the window is one beat and a bit.
        const auto bpm = snapshot.tempoKnown && snapshot.bpm > 0.0 ? snapshot.bpm : song.savedBpm;
        const auto beatMs = bpm > 0.0 ? 60000.0 / bpm : 500.0;
        pedal.setDoubleWindowMs (juce::jlimit (300, 1000, juce::roundToInt (beatMs * 1.2 / 10.0) * 10));
    }

    if (engine.looper().options().doubleWindowMs != pedal.doubleWindowMs())
        pushOptions();

    const auto rate = engine.sampleRate();
    stripWarning = true;

    if (! engine.isOpen())
        stripNote = "NO AUDIO: start the CQ on the Latency and audio test tab first. Nothing can be recorded until then.";
    else if (engine.isMeasuring())
        stripNote = "PAUSED while the latency test runs.";
    else if (engine.glitchCheckOn())
        stripNote = "PAUSED while the glitch check runs. Switch the glitch check off to use the looper.";
    else if (! timing.measured)
        stripNote = "TIMING NOT MEASURED: click Measure latency on the Latency and audio test tab before recording (no cable needed). "
                    "Until then loops will play back out of time.";
    else if (const auto grown = engine.driverDelayWarning(); grown.isNotEmpty())
        stripNote = grown;
    else
    {
        stripWarning = false;
        stripNote = engine.currentChoice().deviceName + ", " + juce::String (rate / 1000.0, 1) + " kHz, buffer " + juce::String (engine.bufferSize())
                      + ".  Timing correction " + juce::String (rate > 0.0 ? timing.samples * 1000.0 / rate : 0.0, 2) + " ms, from "
                      + timing.source + ".";
    }
}

void LooperPanel::timerCallback()
{
    ++ticks;
    snapshot = engine.looper().snapshot();

    LoopLogEntry entries[64];
    const auto count = engine.looper().takeLog (entries, 64);

    if (count > 0)
    {
        juce::StringArray lines;

        for (int i = 0; i < count; ++i)
            lines.add (describeLogEntry (entries[i], snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 48000.0));

        history.addArray (lines);

        while (history.size() > historyLines)
            history.remove (0);

        logLines += lines.size();

        if (logLines > maxLogLines)
        {
            logView.clear();
            logLines = lines.size();
        }

        logView.moveCaretToEnd();
        logView.insertTextAtCaret (lines.joinIntoString ("\n") + "\n");
    }

    const bool blink = (ticks / (timerHz / 4)) % 2 == 0;

    for (int t = 0; t < numLoopTracks; ++t)
        trackTiles[t]->set (t, snapshot.track[(size_t) t], song.track[(size_t) t], snapshot.currentTrack == t, blink);

    if (snapshot.bank != shownBank)
        labelSwitches (snapshot.bank);

    const auto seen = pedal.seen();

    for (int sw = 0; sw < numPedalSwitches; ++sw)
        switchTiles[sw]->setDown (seen[(size_t) sw].down);

    if (ticks % (timerHz / 2) == 0)
        slowUpdate();

    juce::String main;

    if (snapshot.tempoKnown)
    {
        main << juce::String (snapshot.bpm, 1) << " BPM";

        if (snapshot.running)
            main << "    bar " << juce::String (snapshot.bar + 1) << "  beat " << juce::String (snapshot.beatInBar + 1);
        else
            main << "    stopped";
    }
    else
    {
        main << (snapshot.running ? "Recording the first loop: it sets the tempo" : "No tempo yet: the first loop sets it");
    }

    main << "      Bank " << juce::String (snapshot.bank + 1);

    const bool lit = snapshot.running && snapshot.tempoKnown && snapshot.msSinceBeat < 110.0;

    if (main != stripMain || lit != beatLit || ticks % (timerHz / 2) == 0)
    {
        stripMain = main;
        beatLit = lit;
        repaint (stripArea);
    }

    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy looper results");
}

void LooperPanel::paint (juce::Graphics& g)
{
    auto strip = stripArea.toFloat();

    g.setColour (style::panel);
    g.fillRoundedRectangle (strip, 6.0f);

    auto light = strip.removeFromLeft (40.0f).withSizeKeepingCentre (18.0f, 18.0f);
    const bool downbeat = snapshot.beatInBar == 0;

    g.setColour (beatLit ? (downbeat ? style::red : style::green) : style::background);
    g.fillEllipse (light);
    g.setColour (style::textDim.withAlpha (0.6f));
    g.drawEllipse (light, 1.0f);

    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (stripMain, strip.removeFromLeft (370.0f), juce::Justification::centredLeft);

    g.setColour (stripWarning ? style::amber : style::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText (stripNote, strip.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
}

void LooperPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    auto top = area.removeFromTop (28);
    songLabel.setBounds (top.removeFromLeft (44));
    songBox.setBounds (top.removeFromLeft (290));
    top.removeFromLeft (8);
    firstLoopBox.setBounds (top.removeFromLeft (220));
    copyButton.setBounds (top.removeFromRight (160));
    top.removeFromRight (8);
    clearButton.setBounds (top.removeFromRight (90));

    area.removeFromTop (8);
    stripArea = area.removeFromTop (40);
    area.removeFromTop (8);

    auto tracks = area.removeFromTop (108);
    const auto trackWidth = (tracks.getWidth() - 5 * 6) / numLoopTracks;

    for (int t = 0; t < numLoopTracks; ++t)
        trackTiles[t]->setBounds (tracks.getX() + t * (trackWidth + 6), tracks.getY(), trackWidth, tracks.getHeight());

    area.removeFromTop (8);
    auto middle = area.removeFromTop (3 * 52 + 12);
    auto settings = middle.removeFromRight (330);
    middle.removeFromRight (14);
    layOutSwitches (middle, switchTiles);

    const int rowHeight = 26, rowGap = 3;

    for (int row = 0; row < 3; ++row)
    {
        auto line = settings.removeFromTop (rowHeight);
        settings.removeFromTop (rowGap);

        for (int column = 0; column < 2; ++column)
        {
            const auto s = row * 2 + column;

            if (s >= numLoopSources)
                break;

            auto cell = column == 0 ? line.removeFromLeft (line.getWidth() / 2 - 4) : line.withTrimmedLeft (8);
            socketLabels[s]->setBounds (cell.removeFromLeft (66));
            socketBoxes[s]->setBounds (cell);
        }
    }

    auto outRow = settings.removeFromTop (rowHeight);
    settings.removeFromTop (rowGap);
    outLabel.setBounds (outRow.removeFromLeft (92));
    outBox.setBounds (outRow);

    auto trimRow = settings.removeFromTop (rowHeight);
    trimLabel.setBounds (trimRow.removeFromLeft (92));
    trimSlider.setBounds (trimRow);

    area.removeFromTop (8);
    auto headings = area.removeFromTop (22);
    auto left = area.removeFromLeft (area.getWidth() * 44 / 100);
    area.removeFromLeft (10);

    descriptionHeading.setBounds (headings.removeFromLeft (left.getWidth()));
    headings.removeFromLeft (10);
    logHeading.setBounds (headings);
    descriptionView.setBounds (left);
    logView.setBounds (area);
}

juce::String LooperPanel::reportText()
{
    slowUpdate();
    const auto snap = engine.looper().snapshot();
    const auto options = engine.looper().options();
    const auto rate = engine.sampleRate();
    juce::StringArray lines;

    lines.add ("STAGE 3: LOOPER");
    lines.add ("---------------");
    lines.add ("Song: " + song.title() + (song.rcMemory > 0 ? " (RC-600 memory " + juce::String (song.rcMemory) + ")" : juce::String()));
    lines.add (juce::String ("First loop: ") + (options.fixedTempo ? "uses the song's saved tempo (" + juce::String (song.savedBpm, 1) + " BPM)"
                                                                   : juce::String ("sets the tempo")));
    lines.add ("Timing correction: " + juce::String (timing.samples) + " samples ("
                 + juce::String (rate > 0.0 ? timing.samples * 1000.0 / rate : 0.0, 2) + " ms), from " + timing.source
                 + ". Trim " + juce::String (engine.getLoopTrimMs(), 1) + " ms.");
    lines.add (juce::String ("Loops play on: ") + (options.outputMode == 1 ? "vocal loops on returns 3+4, the rest on 5+6"
                                                                  : options.outputMode == 3 ? "two-source tracks split, first source on returns 3+4 and second on 5+6; other tracks on 5+6"
                                                                                            : "returns 5+6"));

    juce::StringArray sockets;

    for (int s = 0; s < numLoopSources; ++s)
        sockets.add (loopSourceName (s) + " " + (options.inputFor[(size_t) s] >= 0 ? juce::String (options.inputFor[(size_t) s] + 1) : juce::String ("none")));

    lines.add ("Input sockets: " + sockets.joinIntoString (", "));
    lines.add ("Double-press window: " + juce::String (options.doubleWindowMs) + " ms");
    lines.add ("Late press allowance: a new track pressed up to " + juce::String (options.lateStartBeats, 2) + " of a beat after a bar line starts from that bar line");
    lines.add ("First loop lead-in: the first loop starts " + juce::String (options.firstLoopLeadMs) + " ms before the press, to keep the attack of a note struck with the switch");

    if (! engine.isOpen())
        lines.add ("NOTE: no audio device was running when this was copied.");

    lines.add ({});
    lines.add (snap.tempoKnown ? "Now: " + juce::String (snap.bpm, 2) + " BPM, one bar = " + juce::String (snap.barSeconds, 4) + " s, "
                                   + (snap.running ? "running" : "stopped") + ", bank " + juce::String (snap.bank + 1)
                               : juce::String ("Now: no tempo set"));

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& v = snap.track[(size_t) t];
        juce::String line = "  T" + juce::String (t + 1) + ": " + trackStateName (v.state);

        if (v.hasAudio)
            line << ", " << barsText (v.bars) << ", " << juce::String (v.seconds, 4) << " s"
                 << (v.hasOverdub ? (v.overdubUndone ? ", overdub undone" : ", with overdub") : "")
                 << (v.undone ? ", undone" : "");

        lines.add (line);
    }

    lines.add ({});
    lines.add ("What the looper did (latest " + juce::String (history.size()) + " lines):");

    for (const auto& h : history)
        lines.add ("  " + h);

    lines.add ({});
    lines.add (describeSong (song).trimEnd());

    return lines.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
