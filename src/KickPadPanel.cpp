#include "KickPadPanel.h"

namespace gigrig
{

namespace
{
    constexpr int builtInId = 1, firstFileId = 2;
    constexpr int softStrength = 25, mediumStrength = 75, hardStrength = 127;
    constexpr double longestSoundSeconds = 4.0;     // a kick, not a song: anything longer is cut here

    void setUpSlider (juce::Slider& s, double low, double high, double value, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        s.setRange (low, high, 0.01);
        s.setValue (value, juce::dontSendNotification);
        s.setTooltip (tip);
        s.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
}

//==============================================================================
void HitGraph::setHits (std::vector<KickPad::Hit> newHits)
{
    hits = std::move (newHits);
    repaint();
}

void HitGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f, 8.0f);

    // The newest strength, large, on the right.
    auto numberArea = area.removeFromRight (150.0f);
    g.setColour (style::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("Last hit (1 to 127)", numberArea.removeFromTop (18.0f), juce::Justification::centred);
    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (64.0f, juce::Font::bold)));
    g.drawText (hits.empty() ? juce::String ("-") : juce::String (hits.back().strength), numberArea, juce::Justification::centred);

    area.removeFromRight (10.0f);

    // Guide lines at a quarter, a half and three quarters of full strength.
    g.setColour (style::textDim.withAlpha (0.25f));

    for (int i = 1; i < 4; ++i)
        g.fillRect (area.getX(), area.getBottom() - area.getHeight() * (float) i / 4.0f, area.getWidth(), 1.0f);

    if (hits.empty())
    {
        g.setColour (style::textDim);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("Each hit shows here as a bar: the harder the hit, the taller the bar.", area, juce::Justification::centred);
        return;
    }

    const auto slot = area.getWidth() / (float) KickPad::maxRecent;

    for (size_t i = 0; i < hits.size(); ++i)
    {
        const auto& h = hits[i];
        const auto height = juce::jmax (2.0f, area.getHeight() * (float) h.strength / 127.0f);
        const juce::Rectangle<float> bar (area.getX() + slot * (float) i + 2.0f, area.getBottom() - height, slot - 4.0f, height);

        g.setColour (h.fromScreen ? style::textDim : i + 1 == hits.size() ? style::green : style::green.withAlpha (0.55f));
        g.fillRoundedRectangle (bar, 2.0f);

        if (slot >= 22.0f)
        {
            g.setColour (style::textMain);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (juce::String (h.strength), bar.withY (bar.getY() - 14.0f).withHeight (13.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }
    }
}

//==============================================================================
juce::File KickPadPanel::soundFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Kick sounds");
}

juce::File KickPadPanel::settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("kick-pad.txt");
}

KickPadPanel::KickPadPanel (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    style::body (help, "A test (26 Nov 2026): plug the SPD::ONE Kick into the computer with a micro-USB cable, with its own power adaptor or batteries in "
                       "and its power switch on, then press Scan again on the Device check tab. Stomp from very soft to very hard. Each hit shows below "
                       "with its strength, and the app plays its own kick to match. The three hit buttons try the sound with no pad.");
    help.setColour (juce::Label::textColourId, style::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setMinimumHorizontalScale (1.0f);

    auto& kick = engine.kick();

    onBox.setColour (juce::ToggleButton::textColourId, style::textMain);
    onBox.setTooltip ("Off: hits are still counted and shown, but no kick is played");
    onBox.onClick = [this] { engine.kick().setOn (onBox.getToggleState()); saveSettings(); };

    style::body (levelLabel, "Level");
    style::body (touchLabel, "Touch");
    style::body (clickLabel, "Click");
    setUpSlider (levelSlider, 0.0, 1.0, kick.getLevel(), "How loud the hardest hit is");
    setUpSlider (touchSlider, 0.5, 3.0, kick.getTouch(), "How much quieter a soft hit is than a hard one. 1 is even steps; higher makes soft hits softer still.");
    setUpSlider (clickSlider, 0.0, 2.0, kick.getClick(), "How much beater click the kick has. A soft hit always has less click than a hard one.");
    levelSlider.onValueChange = [this] { engine.kick().setLevel ((float) levelSlider.getValue()); saveSettings(); };
    touchSlider.onValueChange = [this] { engine.kick().setTouch ((float) touchSlider.getValue()); saveSettings(); };
    clickSlider.onValueChange = [this] { engine.kick().setClick ((float) clickSlider.getValue()); saveSettings(); };

    style::body (pitchLabel, "Pitch");
    style::body (lengthLabel, "Length");
    setUpSlider (pitchSlider, 40.0, 90.0, kick.getPitch(), "The built-in kick only: the note the drum settles on, in Hz. 55 sits under the bass guitar.");
    setUpSlider (lengthSlider, 0.03, 0.2, kick.getLength(), "The built-in kick only: how long the drum rings, in seconds. Shorter is tighter.");
    pitchSlider.setRange (40.0, 90.0, 1.0);
    pitchSlider.onValueChange = [this] { engine.kick().setPitch ((float) pitchSlider.getValue()); saveSettings(); };
    lengthSlider.setRange (0.03, 0.2, 0.005);
    lengthSlider.onValueChange = [this] { engine.kick().setLength ((float) lengthSlider.getValue()); saveSettings(); };

    style::body (soundLabel, "Sound");
    style::combo (soundBox);
    soundBox.setTooltip ("The built-in kick, or a WAV recording of your own from the kick sounds folder");
    soundBox.onChange = [this]
    {
        if (loading)
            return;

        const auto id = soundBox.getSelectedId();
        chooseSound (id >= firstFileId ? soundFiles[id - firstFileId] : juce::String());
        saveSettings();
    };

    folderButton.setTooltip ("Put a WAV of one kick in this folder and it appears in the Sound box");
    folderButton.onClick = [] { soundFolder().createDirectory(); soundFolder().startAsProcess(); };

    style::body (returnLabel, "Plays on");
    style::combo (returnBox);

    for (int pair = 0; pair < 4; ++pair)
        returnBox.addItem (returnName (pair * 2) + " and " + returnName (pair * 2 + 1).fromFirstOccurrenceOf ("Return ", false, false), pair + 1);

    returnBox.setTooltip ("Where the kick goes. A socket: it joins that socket like a stomp box plugged in there, so the looper can record it and the mixer channel on that socket plays it (Stomp is on socket 10). A pair of returns: straight out, never looped.");
    for (int s = 0; s < 16; ++s)
        returnBox.addItem ("Socket " + juce::String (s + 1) + ": through its mixer channel, and into loops", 101 + s);

    returnBox.onChange = [this]
    {
        const int id = returnBox.getSelectedId();

        if (id > 100)
            engine.kick().setSocket (id - 101);
        else
        {
            engine.kick().setSocket (-1);
            engine.kick().setReturnPair (id - 1);
        }

        saveSettings();
    };

    softButton.onClick = [this] { engine.kick().screenHit (softStrength); };
    mediumButton.onClick = [this] { engine.kick().screenHit (mediumStrength); };
    hardButton.onClick = [this] { engine.kick().screenHit (hardStrength); };
    softButton.setTooltip ("Plays the kick as a soft hit (strength " + juce::String (softStrength) + ")");
    mediumButton.setTooltip ("Plays the kick as a medium hit (strength " + juce::String (mediumStrength) + ")");
    hardButton.setTooltip ("Plays the kick as the hardest hit (strength " + juce::String (hardStrength) + ")");

    learnButton.setTooltip ("For a pad the app does not know by name: press this, then hit the pad once");
    learnButton.onClick = [this] { engine.kick().learnNextNote(); };
    forgetButton.setTooltip ("Forgets the pad, so the app looks for an SPD::ONE by name again");
    forgetButton.onClick = [this] { engine.kick().setPad ({}, -1); saveSettings(); };

    resetButton.setTooltip ("Clears the hits and the counts so the test can be done again");
    resetButton.onClick = [this] { engine.kick().resetCounts(); };

    copyButton.setTooltip ("Copies what the pad sent, ready to paste to Claude (Copy report at the top includes it too)");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (engine.kick().reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = 20;
    };

    style::heading (graphHeading, "The last hits");
    style::heading (logHeading, "What the pad sent");
    style::readOnlyEditor (logView);

    for (auto* c : std::initializer_list<juce::Component*> { &status, &help, &onBox, &levelLabel, &levelSlider, &touchLabel, &touchSlider, &clickLabel, &clickSlider, &pitchLabel, &pitchSlider, &lengthLabel, &lengthSlider,
                                                             &soundLabel, &soundBox, &folderButton, &returnLabel, &returnBox, &softButton, &mediumButton, &hardButton,
                                                             &learnButton, &forgetButton, &resetButton, &copyButton, &graphHeading, &graph, &logHeading, &logView })
        addAndMakeVisible (c);

    loadSettings();
    startTimerHz (10);
}

KickPadPanel::~KickPadPanel()
{
    stopTimer();
}

void KickPadPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    status.setBounds (area.removeFromTop (68));
    area.removeFromTop (6);
    help.setBounds (area.removeFromTop (58));
    area.removeFromTop (4);

    auto row = area.removeFromTop (28);
    onBox.setBounds (row.removeFromLeft (120));
    const auto third = (row.getWidth() - 16) / 3;

    using Pair = std::pair<juce::Label*, juce::Slider*>;

    for (auto pair : { Pair { &levelLabel, &levelSlider }, Pair { &touchLabel, &touchSlider }, Pair { &clickLabel, &clickSlider } })
    {
        auto cell = row.removeFromLeft (third);
        pair.first->setBounds (cell.removeFromLeft (46));
        pair.second->setBounds (cell);
        row.removeFromLeft (8);
    }

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    row.removeFromLeft (120);

    for (auto pair : { Pair { &pitchLabel, &pitchSlider }, Pair { &lengthLabel, &lengthSlider } })
    {
        auto cell = row.removeFromLeft (third);
        pair.first->setBounds (cell.removeFromLeft (46));
        pair.second->setBounds (cell);
        row.removeFromLeft (8);
    }

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    soundLabel.setBounds (row.removeFromLeft (50));
    soundBox.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (8);
    folderButton.setBounds (row.removeFromLeft (180));
    row.removeFromLeft (20);
    returnLabel.setBounds (row.removeFromLeft (62));
    returnBox.setBounds (row.removeFromLeft (juce::jmin (330, row.getWidth())));

    area.removeFromTop (8);
    row = area.removeFromTop (30);

    for (auto* b : { &softButton, &mediumButton, &hardButton })
    {
        b->setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
    }

    copyButton.setBounds (row.removeFromRight (170));
    row.removeFromRight (8);
    resetButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    forgetButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    learnButton.setBounds (row.removeFromRight (100));

    area.removeFromTop (10);
    graphHeading.setBounds (area.removeFromTop (22));
    graph.setBounds (area.removeFromTop (140));
    area.removeFromTop (8);
    logHeading.setBounds (area.removeFromTop (22));
    logView.setBounds (area);
}

//==============================================================================
void KickPadPanel::refreshSounds()
{
    juce::StringArray found;

    for (const auto& f : soundFolder().findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV;*.aif;*.aiff;*.flac"))
        found.addIfNotAlreadyThere (f.getFileName());

    found.sortNatural();

    if (found == soundFiles && soundBox.getNumItems() > 0)
        return;

    soundFiles = found;
    const auto current = engine.kick().sampleName();

    loading = true;
    soundBox.clear (juce::dontSendNotification);
    soundBox.addItem ("Built-in kick (deep, with a click)", builtInId);

    for (int i = 0; i < soundFiles.size(); ++i)
        soundBox.addItem (soundFiles[i], firstFileId + i);

    const auto index = soundFiles.indexOf (current);
    soundBox.setSelectedId (index >= 0 ? firstFileId + index : builtInId, juce::dontSendNotification);
    loading = false;
}

void KickPadPanel::chooseSound (const juce::String& fileName)
{
    soundNote.clear();

    if (fileName.isEmpty())
    {
        engine.kick().setSample ({}, 48000.0, {});
        return;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (soundFolder().getChildFile (fileName)));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        soundNote = "\"" + fileName + "\" could not be read as a sound file, so the built-in kick is playing.";
        engine.kick().setSample ({}, 48000.0, {});
        return;
    }

    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (longestSoundSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    reader->read (&buffer, 0, length, 0, true, true);

    // Both sides of a stereo file are added into one: a kick has no left and right here.
    std::vector<float> mono ((size_t) length, 0.0f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += buffer.getSample (ch, i) / (float) buffer.getNumChannels();

    // The last 5 ms fade out, so a recording that was cut short does not end on a tick.
    const auto fade = juce::jmin (length, (int) (0.005 * reader->sampleRate));

    for (int i = 0; i < fade; ++i)
        mono[(size_t) (length - 1 - i)] *= (float) i / (float) fade;

    engine.kick().setSample (std::move (mono), reader->sampleRate, fileName);
}

void KickPadPanel::loadSettings()
{
    auto& kick = engine.kick();
    juce::StringPairArray values;

    for (const auto& line : juce::StringArray::fromLines (settingsFile().loadFileAsString()))
        if (line.containsChar ('='))
            values.set (line.upToFirstOccurrenceOf ("=", false, false).trim(), line.fromFirstOccurrenceOf ("=", false, false).trim());

    if (values.containsKey ("on"))      kick.setOn (values["on"].getIntValue() != 0);
    if (values.containsKey ("level"))   kick.setLevel (values["level"].getFloatValue());
    if (values.containsKey ("touch"))   kick.setTouch (values["touch"].getFloatValue());
    if (values.containsKey ("click"))   kick.setClick (values["click"].getFloatValue());
    if (values.containsKey ("pitch"))   kick.setPitch (values["pitch"].getFloatValue());
    if (values.containsKey ("length"))  kick.setLength (values["length"].getFloatValue());
    if (values.containsKey ("returns")) kick.setReturnPair (values["returns"].getIntValue());
    if (values.containsKey ("socket"))  kick.setSocket (values["socket"].getIntValue());

    if (values["pad"].isNotEmpty())
        kick.setPad (values["pad"], values.containsKey ("note") ? values["note"].getIntValue() : -1);

    loading = true;
    onBox.setToggleState (kick.isOn(), juce::dontSendNotification);
    levelSlider.setValue (kick.getLevel(), juce::dontSendNotification);
    touchSlider.setValue (kick.getTouch(), juce::dontSendNotification);
    clickSlider.setValue (kick.getClick(), juce::dontSendNotification);
    pitchSlider.setValue (kick.getPitch(), juce::dontSendNotification);
    lengthSlider.setValue (kick.getLength(), juce::dontSendNotification);
    returnBox.setSelectedId (kick.getSocket() >= 0 ? 101 + kick.getSocket() : kick.getReturnPair() + 1, juce::dontSendNotification);
    loading = false;

    if (values["sound"].isNotEmpty() && soundFolder().getChildFile (values["sound"]).existsAsFile())
        chooseSound (values["sound"]);

    refreshSounds();
}

void KickPadPanel::saveSettings()
{
    if (loading)
        return;

    const auto& kick = engine.kick();
    juce::StringArray lines;
    lines.add ("on=" + juce::String (kick.isOn() ? 1 : 0));
    lines.add ("level=" + juce::String (kick.getLevel(), 2));
    lines.add ("touch=" + juce::String (kick.getTouch(), 2));
    lines.add ("click=" + juce::String (kick.getClick(), 2));
    lines.add ("pitch=" + juce::String (kick.getPitch(), 0));
    lines.add ("length=" + juce::String (kick.getLength(), 3));
    lines.add ("returns=" + juce::String (kick.getReturnPair()));
    lines.add ("socket=" + juce::String (kick.getSocket()));
    lines.add ("sound=" + kick.sampleName());

    // A pad found by its name is found again next time; only a learned one is written down.
    if (const auto note = kick.padNote(); note >= 0)
    {
        lines.add ("pad=" + kick.padDevice());
        lines.add ("note=" + juce::String (note));
    }

    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
}

void KickPadPanel::timerCallback()
{
    auto& kick = engine.kick();

    if (engine.isOpen() && engine.sampleRate() > 0.0)
        kick.setBlockInfo (engine.bufferSize() * 1000.0 / engine.sampleRate(), engine.driverLatencySamples() * 1000.0 / engine.sampleRate());

    const auto pad = kick.padDevice();
    const auto hits = kick.hitCount();
    const bool learning = kick.isLearning();
    const auto key = pad + "/" + juce::String (hits) + "/" + juce::String ((int) learning) + "/" + juce::String ((int) engine.isOpen()) + "/" + juce::String ((int) kick.isOn()) + soundNote;

    if (key != shownStatus)
    {
        // A pad learned a moment ago is written down straight away.
        if (shownStatus.isNotEmpty() && ! shownStatus.startsWith (pad + "/"))
            saveSettings();

        shownStatus = key;
        const auto audioNote = ! engine.isOpen() ? juce::String (" No sound yet: start the audio on the first tab.")
                                                 : ! kick.isOn() ? juce::String (" The kick pad is switched off, so hits are counted but not played.") : juce::String();

        if (learning)
            status.set (StatusRow::Tone::caution, "LEARNING", "Hit the pad once", "The next note from any MIDI device becomes the pad.");
        else if (pad.isEmpty())
            status.set (StatusRow::Tone::caution, "NO PAD", "No SPD::ONE is showing as a MIDI device",
                        "Plug in its USB cable (one that carries data, not a charge-only cable), switch it on, then press Scan again on the Device check tab. "
                        "The hit buttons still play the kick." + audioNote);
        else if (hits == 0)
            status.set (StatusRow::Tone::good, "PAD FOUND", "\"" + pad + "\" is connected. Now stomp on it.",
                        "Try twenty or so hits from the lightest touch to a full stomp." + audioNote);
        else
            status.set (StatusRow::Tone::good, "HITS SEEN", juce::String (hits) + (hits == 1 ? " hit" : " hits") + " from \"" + pad + "\"",
                        "When you have tried soft, medium and hard, press Copy kick pad results and paste them to Claude." + audioNote
                          + (soundNote.isEmpty() ? juce::String() : " " + soundNote));
    }

    graph.setHits (kick.recentHits());

    if (const auto text = kick.reportText(); text != shownReport)
    {
        shownReport = text;
        logView.setText (text, false);
    }

    if (++soundScanTicks >= 20)     // a file dropped into the folder shows up within two seconds
    {
        soundScanTicks = 0;
        refreshSounds();
    }

    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy kick pad results");
}

} // namespace gigrig
