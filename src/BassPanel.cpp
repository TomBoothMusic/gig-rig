#include "BassPanel.h"

#include <map>

namespace gigrig
{

namespace
{
    constexpr int timerHz = 20;
    constexpr double matchNeedsSeconds = 8.0;

    juce::File appDataFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GigRig").getChildFile ("bass-current.txt");
    }

    /** Knob-style settings (0 to 1 inside) show as 0 to 10, like a pedal. */
    double sliderScale (const BassSettings::Range& r)       { return juce::String (r.unit).isEmpty() ? 10.0 : 1.0; }

    juce::String tipFor (const std::string& key)
    {
        static const std::map<std::string, juce::String> tips {
            { "subMix",   "The Bass9's CTRL 1 on PRECISION: 0 = one octave down, 10 = two octaves down, in between mixes the two." },
            { "tone",     "The Bass9's CTRL 2: a model of a P-bass's own tone knob (pickup, pot and capacitor). 10 = full up, 0 = fully rolled off (the dark, honky sound)." },
            { "tracking", "How quickly the low notes speak. Higher = quicker, but notes of a chord blur into each other more. Lower = cleanest chords, low notes a little slower." },
            { "lowDb",    "Body: boost or cut around 90 Hz." },
            { "lowMidDb", "Thump: boost or cut around 250 Hz." },
            { "midDb",    "Growl: boost or cut around 800 Hz, where a P-bass's finger sound is." },
            { "compress", "Evens out how loud each note is. 0 = none." },
            { "drive",    "A little valve-amp warmth. 0 = clean." },
            { "gateDb",   "Below this guitar level the bass is silent, so hum and string noise don't come out as low rumble. -90 = off." },
            { "levelDb",  "Output level of the app's bass. The Match button sets this for you." },
        };
        const auto it = tips.find (key);
        return it != tips.end() ? it->second : juce::String();
    }

    juce::String settingsSummary (const BassSettings& s)
    {
        juce::String out;
        for (const auto& r : BassSettings::ranges())
            out << "  " << r.label << ": " << juce::String (s.get (r.key) * sliderScale (r), 1) << (juce::String (r.unit).isEmpty() ? "" : " " + juce::String (r.unit)) << "\n";
        return out;
    }
}

juce::File BassPanel::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Bass presets");
}

juce::File BassPanel::captureFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Bass9 captures");
}

//==============================================================================
BassPanel::BassPanel (AudioEngine& engineToUse)
    : engine (engineToUse), rig (engineToUse.bass())
{
    style::heading (routeHeading, "Plugging in");
    style::body (dryLabel, "Bass9 DRY OUT into");
    style::body (realLabel, "Bass9 EFFECT OUT into");
    style::body (outLabel, "Bass comes back on");

    for (auto* box : { &dryBox, &realBox })
    {
        style::combo (*box);
        for (int socket = 1; socket <= 16; ++socket)
            box->addItem ("socket " + juce::String (socket), socket);
        box->onChange = [this] { rig.setDryInput (dryBox.getSelectedId() - 1); rig.setRealInput (realBox.getSelectedId() - 1); dirty = true; };
    }

    dryBox.setTooltip ("The input socket the Bass9's DRY OUTPUT jack is plugged into. The app makes its bass from this.");
    realBox.setTooltip ("The input socket the Bass9's EFFECT OUTPUT jack is plugged into (your usual bass channel).");

    style::combo (outBox);
    outBox.addItem ("returns 3+4 (ST1)", 3);
    outBox.addItem ("returns 5+6 (the CQ's USB channel)", 5);
    outBox.addItem ("returns 1+2 (channels 9 and 10)", 1);
    outBox.setTooltip ("Both basses come back here, so they go through the same channel and EQ. "
                       "On the CQ, set that channel's Input Source to USB and mute the Bass9's own channel while you compare.");
    outBox.onChange = [this] { rig.setOutputPair (outBox.getSelectedId() - 1); dirty = true; };

    style::heading (listenHeading, "Listen");
    for (auto* b : { &offButton, &realButton, &appButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.3f));
        addAndMakeVisible (b);
    }

    offButton.onClick = [this] { rig.setListen (BassRig::Listen::off); updateListenButtons(); };
    realButton.onClick = [this] { rig.setListen (blindOn.load() && blindSwap ? BassRig::Listen::app : BassRig::Listen::real); updateListenButtons(); };
    appButton.onClick = [this] { rig.setListen (blindOn.load() && blindSwap ? BassRig::Listen::real : BassRig::Listen::app); updateListenButtons(); };

    style::body (switchLabel, "A/B from the pedal");
    style::combo (switchBox);
    switchBox.addItem ("no switch", 1);
    for (int sw = 0; sw < numPedalSwitches; ++sw)
        switchBox.addItem (pedalSwitchName (sw), sw + 2);
    switchBox.setTooltip ("Each press of this RC-600 switch flips between A and B. While it is chosen here, the looper does not see that switch.");
    switchBox.onChange = [this] { abSwitch.store (switchBox.getSelectedId() - 2); dirty = true; };

    blindButton.setTooltip ("The app hides which is which: A and B become 1 and 2 in a random order. Listen, then say which one you think is the real Bass9.");
    blindButton.onClick = [this]
    {
        if (blindOn.load())
        {
            blindOn.store (false);
            blindButton.setButtonText ("Blind test");
            realButton.setButtonText ("A: real Bass9");
            appButton.setButtonText ("B: app bass");
        }
        else
        {
            startBlind();
        }
        updateListenButtons();
    };
    guess1Button.onClick = [this] { guess (1); };
    guess2Button.onClick = [this] { guess (2); };
    style::body (blindLabel, "");
    blindLabel.setJustificationType (juce::Justification::topLeft);

    style::body (meterLabel, "Dry guitar, real Bass9, app bass");
    matchButton.setTooltip ("Play normally for about 8 seconds. The app then sets its Level so it is as loud as your real Bass9.");
    matchButton.onClick = [this]
    {
        if (matching)
        {
            matching = false;
            matchResult = "Level match stopped.";
            return;
        }

        if (rig.listen() == BassRig::Listen::off)
        {
            rig.setListen (BassRig::Listen::real);
            updateListenButtons();
        }

        rig.takeLevels();
        matching = true;
        matchApp = matchReal = matchSeconds = 0.0;
        matchResult = "Play for about 8 seconds...";
    };
    style::body (matchLabel, "");

    captureButton.setTooltip ("Records the dry guitar, the real Bass9 and the app's bass together into one file in Documents\\GigRig\\Bass9 captures. "
                              "Play 20 to 30 seconds of the kind of thing you play, then press again. Send the .wav (and the .txt beside it) to Claude.");
    captureButton.onClick = [this]
    {
        if (rig.isCapturing())
        {
            rig.stopCapture();
            captureLog.add (rig.lastCaptureFile().getFileName() + " (" + juce::String (rig.captureSeconds(), 1) + " s)");
            return;
        }

        if (! engine.isOpen())
        {
            captureLog.add ("Start the CQ on the first tab before capturing.");
            return;
        }

        const auto error = rig.startCapture (captureFolder());
        if (error.isNotEmpty())
            captureLog.add (error);
    };
    openCapturesButton.onClick = [] { captureFolder().createDirectory(); captureFolder().startAsProcess(); };
    style::body (captureLabel, "");
    captureLabel.setJustificationType (juce::Justification::topLeft);

    style::heading (soundHeading, "Sound (changes are heard at once)");

    for (const auto& r : BassSettings::ranges())
    {
        auto* row = sliders.add (new SliderRow());
        row->key = r.key;
        style::body (row->label, r.label);
        row->label.setTooltip (tipFor (r.key));

        auto& s = row->slider;
        const auto scale = sliderScale (r);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
        s.setRange (r.min * scale, r.max * scale, juce::String (r.unit) == "dB" ? 0.5 : 0.1);
        s.setDoubleClickReturnValue (true, r.def * scale);
        s.setTextValueSuffix (juce::String (r.unit).isEmpty() ? "" : " " + juce::String (r.unit));
        s.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.setColour (juce::Slider::textBoxOutlineColourId, style::textDim.withAlpha (0.4f));
        s.setTooltip (tipFor (r.key) + " Double-click for the default.");
        s.onValueChange = [this] { settingsChanged(); };

        addAndMakeVisible (row->label);
        addAndMakeVisible (s);
    }

    style::body (presetLabel, "Preset");
    style::combo (presetBox);
    presetBox.setTooltip ("Saved presets, from Documents\\GigRig\\Bass presets");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && presetBox.getSelectedId() > 0)
            loadPresetFile (presetFolder().getChildFile (name + ".txt"));
    };
    saveButton.onClick = [this] { savePresetAs(); };
    loadButton.setTooltip ("Load a preset file, for example one Claude sent you");
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a bass preset", presetFolder(), "*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      // Keep a copy with the others so it shows in the list next time.
                                      const auto copy = presetFolder().getChildFile (file.getFileName());
                                      if (presetFolder().createDirectory() && file != copy)
                                          file.copyFileTo (copy);
                                      loadPresetFile (file);
                                      refreshPresets();
                                  }
                              });
    };
    defaultsButton.onClick = [this] { presetName = "(defaults)"; showSettings (BassSettings()); settingsChanged(); };

    for (auto* c : std::initializer_list<juce::Component*> {
             &routeHeading, &dryLabel, &realLabel, &outLabel, &dryBox, &realBox, &outBox, &listenHeading,
             &switchLabel, &switchBox, &blindButton, &guess1Button, &guess2Button, &blindLabel, &meters, &meterLabel,
             &matchButton, &matchLabel, &captureButton, &openCapturesButton, &captureLabel, &soundHeading,
             &presetLabel, &presetBox, &saveButton, &loadButton, &defaultsButton, &status })
        addAndMakeVisible (c);

    loadCurrent();
    refreshPresets();
    updateListenButtons();
    startTimerHz (timerHz);
}

BassPanel::~BassPanel()
{
    stopTimer();
    if (dirty)
        saveCurrent();
}

//==============================================================================
void BassPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void BassPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    status.setBounds (area.removeFromTop (64));
    area.removeFromTop (10);

    auto left = area.removeFromLeft (juce::jmin (440, area.getWidth() / 2));
    area.removeFromLeft (20);
    auto right = area;

    const auto row = [] (juce::Rectangle<int>& a, int h = 28) { auto r = a.removeFromTop (h); a.removeFromTop (4); return r; };

    routeHeading.setBounds (row (left, 22));
    for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &dryLabel, &dryBox }, { &realLabel, &realBox }, { &outLabel, &outBox } })
    {
        auto r = row (left);
        label->setBounds (r.removeFromLeft (170));
        box->setBounds (r);
    }

    left.removeFromTop (6);
    listenHeading.setBounds (row (left, 22));
    {
        auto r = row (left, 40);
        const auto w = r.getWidth() / 3;
        offButton.setBounds (r.removeFromLeft (w - 60).reduced (2, 0));
        realButton.setBounds (r.removeFromLeft (w + 30).reduced (2, 0));
        appButton.setBounds (r.reduced (2, 0));
    }
    {
        auto r = row (left);
        switchLabel.setBounds (r.removeFromLeft (170));
        switchBox.setBounds (r);
    }
    {
        auto r = row (left);
        blindButton.setBounds (r.removeFromLeft (110).reduced (0, 1));
        r.removeFromLeft (6);
        guess1Button.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 1));
        guess2Button.setBounds (r.reduced (2, 1));
    }
    blindLabel.setBounds (row (left, 40));

    meterLabel.setBounds (row (left, 20));
    meters.setBounds (row (left, 70).removeFromLeft (150));
    matchButton.setBounds (row (left).removeFromLeft (260));
    matchLabel.setBounds (row (left, 22));
    {
        auto r = row (left);
        captureButton.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (8);
        openCapturesButton.setBounds (r.removeFromLeft (180));
    }
    captureLabel.setBounds (left);

    soundHeading.setBounds (row (right, 22));
    {
        auto r = row (right);
        presetLabel.setBounds (r.removeFromLeft (60));
        presetBox.setBounds (r.removeFromLeft (r.getWidth() - 3 * 84));
        r.removeFromLeft (4);
        saveButton.setBounds (r.removeFromLeft (84).reduced (2, 0));
        loadButton.setBounds (r.removeFromLeft (84).reduced (2, 0));
        defaultsButton.setBounds (r.reduced (2, 0));
    }
    right.removeFromTop (4);

    for (auto* s : sliders)
    {
        auto r = row (right, 30);
        s->label.setBounds (r.removeFromLeft (150));
        s->slider.setBounds (r);
    }
}

//==============================================================================
bool BassPanel::handlePedal (const PedalEvent& event)
{
    if (event.sw != abSwitch.load())
        return false;

    if (event.kind == PedalEvent::Kind::down)
    {
        if (rig.listen() == BassRig::Listen::off)
            rig.setListen (BassRig::Listen::real);
        else
            rig.toggle();
    }

    return true;
}

void BassPanel::updateListenButtons()
{
    const auto mode = rig.listen();
    const bool blind = blindOn.load();

    if (blind)
    {
        realButton.setButtonText ("1");
        appButton.setButtonText ("2");
    }

    const bool oneLit = mode != BassRig::Listen::off && ((mode == BassRig::Listen::real) != (blind && blindSwap));
    offButton.setToggleState (mode == BassRig::Listen::off, juce::dontSendNotification);
    realButton.setToggleState (mode != BassRig::Listen::off && oneLit, juce::dontSendNotification);
    appButton.setToggleState (mode != BassRig::Listen::off && ! oneLit, juce::dontSendNotification);

    guess1Button.setEnabled (blind);
    guess2Button.setEnabled (blind);
}

void BassPanel::startBlind()
{
    blindSwap = juce::Random::getSystemRandom().nextBool();
    blindOn.store (true);
    blindButton.setButtonText ("End blind test");
    if (rig.listen() == BassRig::Listen::off)
        rig.setListen (BassRig::Listen::real);
}

void BassPanel::guess (int which)
{
    if (! blindOn.load())
        return;

    const int realIs = blindSwap ? 2 : 1;
    const bool right = which == realIs;
    ++blindTotal;
    blindRight += right ? 1 : 0;
    blindLog = juce::String (right ? "Right" : "Wrong") + ": " + juce::String (realIs) + " was the real Bass9.  Score "
             + juce::String (blindRight) + " of " + juce::String (blindTotal) + " (guessing scores about half). New round: listen again.";
    blindSwap = juce::Random::getSystemRandom().nextBool();
    updateListenButtons();
}

//==============================================================================
void BassPanel::showSettings (const BassSettings& s)
{
    loading = true;
    for (auto* row : sliders)
        for (const auto& r : BassSettings::ranges())
            if (row->key == r.key)
                row->slider.setValue (s.get (r.key) * sliderScale (r), juce::dontSendNotification);
    loading = false;
}

void BassPanel::settingsChanged()
{
    if (loading)
        return;

    BassSettings s;
    for (auto* row : sliders)
        for (const auto& r : BassSettings::ranges())
            if (row->key == r.key)
                s.set (r.key, (float) (row->slider.getValue() / sliderScale (r)));

    rig.setSettings (s);
    dirty = true;
    saveCountdown = timerHz;    // write the file a second after the last change
}

void BassPanel::saveCurrent()
{
    juce::String text;
    text << "# GigRig bass settings, saved automatically\n"
         << "preset=" << presetName << "\n"
         << "dryInput=" << rig.dryInput() << "\n"
         << "realInput=" << rig.realInput() << "\n"
         << "outputPair=" << rig.outputPair() << "\n"
         << "abSwitch=" << abSwitch.load() << "\n"
         << juce::String (rig.getSettings().toText());

    appDataFile().getParentDirectory().createDirectory();
    appDataFile().replaceWithText (text);
    dirty = false;
}

void BassPanel::loadCurrent()
{
    const auto text = appDataFile().loadFileAsString();
    std::map<juce::String, juce::String> values;

    for (const auto& line : juce::StringArray::fromLines (text))
        if (line.containsChar ('=') && ! line.startsWithChar ('#'))
            values[line.upToFirstOccurrenceOf ("=", false, false).trim()] = line.fromFirstOccurrenceOf ("=", false, false).trim();

    const auto number = [&values] (const char* key, int fallback)
    {
        const auto it = values.find (key);
        return it != values.end() ? it->second.getIntValue() : fallback;
    };

    rig.setDryInput (juce::jlimit (0, 15, number ("dryInput", 9)));
    rig.setRealInput (juce::jlimit (0, 15, number ("realInput", 8)));
    const auto pair = number ("outputPair", 2);
    rig.setOutputPair (pair == 0 || pair == 4 ? pair : 2);
    abSwitch.store (juce::jlimit (-1, numPedalSwitches - 1, number ("abSwitch", 10)));
    if (values.count ("preset") > 0)
        presetName = values["preset"];

    const auto settings = BassSettings::fromText (text.toStdString());
    rig.setSettings (settings);
    showSettings (settings);

    dryBox.setSelectedId (rig.dryInput() + 1, juce::dontSendNotification);
    realBox.setSelectedId (rig.realInput() + 1, juce::dontSendNotification);
    outBox.setSelectedId (rig.outputPair() + 1, juce::dontSendNotification);
    switchBox.setSelectedId (abSwitch.load() + 2, juce::dontSendNotification);
    dirty = false;
}

void BassPanel::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();

    int id = 1;
    for (const auto& f : files)
        presetBox.addItem (f.getFileNameWithoutExtension(), id++);

    presetBox.setText (presetName, juce::dontSendNotification);
}

void BassPanel::savePresetAs()
{
    auto* window = new juce::AlertWindow ("Save bass preset", "Name for this sound:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presetName.startsWithChar ('(') ? juce::String ("My Precision") : presetName);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());

        if (result == 1 && name.isNotEmpty())
        {
            presetFolder().createDirectory();
            const auto file = presetFolder().getChildFile (name + ".txt");
            file.replaceWithText ("# GigRig bass preset: " + name + "\n" + juce::String (rig.getSettings().toText()));
            presetName = name;
            refreshPresets();
            dirty = true;
        }
    }), true);
}

void BassPanel::loadPresetFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    const auto settings = BassSettings::fromText (file.loadFileAsString().toStdString());
    presetName = file.getFileNameWithoutExtension();
    showSettings (settings);
    rig.setSettings (settings);
    presetBox.setText (presetName, juce::dontSendNotification);
    dirty = true;
    saveCountdown = timerHz;
}

//==============================================================================
void BassPanel::timerCallback()
{
    updateListenButtons();

    for (int i = 0; i < 3; ++i)
        meters.setLevel (i, rig.takePeak (i));

    // ---- what the tab says at the top ----
    const auto mode = rig.listen();
    const auto truth = mode == BassRig::Listen::real ? "real Bass9" : "app bass";

    if (! engine.isOpen())
        status.set (StatusRow::Tone::caution, "NO AUDIO", "Start the CQ on the first tab", "The bass test needs the audio running.");
    else if (mode == BassRig::Listen::off)
        status.set (StatusRow::Tone::neutral, "OFF", "Bass is off",
                    "Press A or B, or your A/B switch. Both come back on " + outBox.getText() + ".");
    else if (blindOn.load())
        status.set (StatusRow::Tone::good, "BLIND", "Playing " + juce::String ((mode == BassRig::Listen::real) != blindSwap ? "1" : "2"),
                    "Which is the real Bass9? Flip between 1 and 2, then press your guess.");
    else
        status.set (StatusRow::Tone::good, mode == BassRig::Listen::real ? "A" : "B", "Playing the " + juce::String (truth),
                    "Preset: " + presetName + ".  Flip with the A/B switch (" + switchBox.getText() + ").");

    blindLabel.setText (blindOn.load() ? blindLog : juce::String(), juce::dontSendNotification);

    // ---- level match ----
    if (matching)
    {
        const auto l = rig.takeLevels();
        if (l.playedSeconds > 0.0)
        {
            matchApp += std::pow (10.0, l.appDb / 10.0) * l.playedSeconds;
            matchReal += std::pow (10.0, l.realDb / 10.0) * l.playedSeconds;
            matchSeconds += l.playedSeconds;
        }

        matchResult = "Keep playing... " + juce::String (juce::jmin (matchSeconds, matchNeedsSeconds), 1) + " of " + juce::String (matchNeedsSeconds, 0) + " s";

        if (matchSeconds >= matchNeedsSeconds)
        {
            matching = false;

            if (matchReal < 1.0e-10)
                matchResult = "No sound from the real Bass9 on " + realBox.getText() + ". Check the EFFECT OUT lead.";
            else if (matchApp < 1.0e-12)
                matchResult = "No sound from the app's bass. Check the DRY OUT lead on " + dryBox.getText() + ".";
            else
            {
                const auto difference = 10.0 * std::log10 (matchReal / matchApp);
                for (auto* row : sliders)
                    if (row->key == "levelDb")
                        row->slider.setValue (juce::jlimit (-24.0, 24.0, row->slider.getValue() + difference));

                matchResult = "Done: app level moved by " + juce::String (difference, 1) + " dB.";
            }
        }
    }
    matchLabel.setText (matchResult, juce::dontSendNotification);

    // ---- capture ----
    if (rig.isCapturing() && rig.captureSeconds() >= BassRig::maxCaptureSeconds - 0.1)
    {
        rig.stopCapture();
        captureLog.add (rig.lastCaptureFile().getFileName() + " (stopped at the 3 minute limit)");
    }

    captureButton.setButtonText (rig.isCapturing() ? "Stop capture" : "Capture");
    captureButton.setColour (juce::TextButton::buttonColourId, rig.isCapturing() ? style::red.darker (0.4f) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

    juce::String captureText;
    if (rig.isCapturing())
        captureText << "Recording " << juce::String (rig.captureSeconds(), 0) << " s: dry guitar, real Bass9 and app bass.\n";
    for (int i = juce::jmax (0, captureLog.size() - 3); i < captureLog.size(); ++i)
        captureText << captureLog[i] << "\n";
    captureLabel.setText (captureText, juce::dontSendNotification);

    // ---- keep the settings for next time ----
    if (saveCountdown > 0 && --saveCountdown == 0 && dirty)
        saveCurrent();
}

//==============================================================================
juce::String BassPanel::reportText() const
{
    juce::String r;
    r << "=== Bass (Bass9 A/B) ===\n"
      << "Dry from " << dryBox.getText() << ", real Bass9 from " << realBox.getText() << ", out on " << outBox.getText()
      << ", A/B switch " << switchBox.getText() << "\n"
      << "Preset: " << presetName << "\n"
      << settingsSummary (rig.getSettings());

    if (blindTotal > 0)
        r << "Blind test: " << blindRight << " of " << blindTotal << " right\n";
    if (matchResult.isNotEmpty())
        r << "Level match: " << matchResult << "\n";
    for (const auto& c : captureLog)
        r << "Capture: " << c << "\n";

    return r;
}

} // namespace gigrig
