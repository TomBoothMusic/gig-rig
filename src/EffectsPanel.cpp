#include "EffectsPanel.h"
#include "AmpModels.h"
#include "Responses.h"
#include "effects/Registry.h"

namespace gigrig
{

namespace
{
    constexpr int timerHz = 20;
    constexpr int refNoneId = 1000;
    constexpr double matchSeconds = 8.0;

    juce::String socketText (int index, bool isCQ)
    {
        return isCQ && index < 10 ? "Socket " + juce::String (index + 1) : inputName (index, isCQ).substring (0, 1).toUpperCase() + inputName (index, isCQ).substring (1);
    }

    int outputId (const EffectsRack::Output& o)    { return o.pair ? 100 + o.firstReturn : 200 + o.firstReturn; }
    EffectsRack::Output outputFromId (int id)      { return id >= 200 ? EffectsRack::Output { id - 200, false } : EffectsRack::Output { id - 100, true }; }

    void fillOutputBox (juce::ComboBox& box)
    {
        static const char* const pairs[4] = { "CQ channels 9 and 10", "ST1", "the USB stereo channel", "the Bluetooth channel" };
        box.clear (juce::dontSendNotification);

        for (int p = 0; p < 4; ++p)
            box.addItem ("Returns " + juce::String (p * 2 + 1) + "+" + juce::String (p * 2 + 2) + " (" + pairs[p] + ")", 100 + p * 2);

        box.addSeparator();

        for (int r = 0; r < numReturns; ++r)
            box.addItem (returnName (r) + " only", 200 + r);
    }

    void dim (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, style::textDim);
    }

    void styleSlider (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 22);
        s.setColour (juce::Slider::trackColourId, style::green.withAlpha (0.7f));
        s.setColour (juce::Slider::backgroundColourId, style::panel);
        s.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, style::panel);
        e.setColour (juce::TextEditor::textColourId, style::textMain);
        e.setColour (juce::TextEditor::outlineColourId, style::textDim.withAlpha (0.4f));
    }

    juce::String switchName (int sw)
    {
        return sw < 9 ? "Switch " + juce::String (sw + 1) : sw == 9 ? juce::String ("CTL3") : juce::String ("CTL4");
    }
}

//==============================================================================
juce::File EffectsPanel::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Effect presets");
}

juce::File EffectsPanel::captureFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Effect captures");
}

EffectsPanel::EffectsPanel (AudioEngine& e)
    : engine (e), rack (e.effects())
{
    for (auto* t : { &rackOn, &chainOn })
    {
        t->setColour (juce::ToggleButton::textColourId, style::textMain);
        t->setColour (juce::ToggleButton::tickColourId, style::green);
        addAndMakeVisible (t);
    }

    rackOn.setTooltip ("Off: the effects do nothing and the looper hears the inputs as before");
    rackOn.onClick = [this] { rack.setEnabled (rackOn.getToggleState()); rack.markChanged(); };
    chainOn.onClick = [this] { pushRouting(); };

    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        auto* b = chainButtons.add (new juce::TextButton());
        b->setClickingTogglesState (false);
        b->onClick = [this, c] { chooseChain (c); };
        addAndMakeVisible (b);
    }

    styleEditor (nameEditor);
    nameEditor.setTooltip ("A name for this chain, shown on its button");
    nameEditor.onTextChange = [this] { pushRouting(); chainButtons[chainIndex]->setButtonText (juce::String (chainIndex + 1) + "  " + nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    style::body (inLabel, "Input");
    style::body (outLabel, "Out to");
    style::body (looperLabel, "Looper hears it as");
    style::body (listenLabel, "Listen to");
    style::body (modelLabel, "Model");
    style::combo (modelBox);
    modelFolderButton.onClick = [this]
    {
        const auto folder = fileKey == "file" ? responses::folder() : ampmodels::folder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &modelLabel, &modelBox, &modelFolderButton })
        addChildComponent (c);
    style::body (levelLabel, "Chain level");
    style::heading (slotsLabel, "EFFECTS IN THIS CHAIN, IN ORDER (CLICK ONE TO SET IT UP)");
    style::heading (presetLabel, "PRESETS");
    style::heading (pedalLabel, "RC-600 SWITCHES FOR THE EFFECTS");
    style::body (statusLabel, {});
    style::body (descriptionLabel, {});
    dim (descriptionLabel);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    descriptionLabel.setJustificationType (juce::Justification::centredLeft);

    for (auto* l : { &inLabel, &outLabel, &looperLabel, &listenLabel, &levelLabel, &slotsLabel, &presetLabel, &pedalLabel, &statusLabel, &descriptionLabel })
        addAndMakeVisible (l);

    for (auto* b : { &inBox, &outBox, &looperBox, &refBox, &addBox, &typeBox, &presetBox })
    {
        style::combo (*b);
        addAndMakeVisible (b);
    }

    fillOutputBox (outBox);
    inBox.onChange = [this] { pushRouting(); };
    outBox.onChange = [this] { pushRouting(); };
    looperBox.onChange = [this] { pushRouting(); };
    refBox.onChange = [this] { pushRouting(); };
    refBox.setTooltip ("A real pedal on another socket, to compare with (Reference)");
    looperBox.setTooltip ("The looper records the sound you hear from this chain in place of that socket");

    for (auto* b : { &listenEffects, &listenDry, &listenRef })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.3f));
        addAndMakeVisible (b);
    }

    listenEffects.onClick = [this] { rack.setListen (chainIndex, EffectsRack::Listen::effects); refresh(); };
    listenDry.onClick     = [this] { rack.setListen (chainIndex, EffectsRack::Listen::dry); refresh(); };
    listenRef.onClick     = [this] { rack.setListen (chainIndex, EffectsRack::Listen::reference); refresh(); };

    styleSlider (levelSlider);
    levelSlider.setRange (-36.0, 24.0, 0.1);
    levelSlider.setTextValueSuffix (" dB");
    levelSlider.setDoubleClickReturnValue (true, 0.0);
    levelSlider.onValueChange = [this] { pushRouting(); };
    addAndMakeVisible (levelSlider);

    matchButton.setTooltip ("Play for 8 seconds: sets the chain level so the effects are as loud as the reference (or the dry sound)");
    matchButton.onClick = [this] { levelMatch(); };
    captureButton.setTooltip ("Records the dry input, the reference and the effects into one WAV file for Claude");
    tapButton.setTooltip ("Tap the beat (two taps or more) to set the tempo for delays set to follow it, when no loop is playing");
    tapButton.onClick = [this] { rack.tap (juce::Time::getMillisecondCounterHiRes()); };
    addAndMakeVisible (tapButton);
    tempoLabel.setFont (juce::FontOptions (13.0f));
    tempoLabel.setColour (juce::Label::textColourId, style::textDim);
    addAndMakeVisible (tempoLabel);

    // The test take: play once, then listen hands-free while turning knobs.
    style::body (takeHeading, "Test take");
    takeLabel.setFont (juce::FontOptions (13.0f));
    takeLabel.setColour (juce::Label::textColourId, style::textDim);
    addAndMakeVisible (takeHeading);
    addAndMakeVisible (takeLabel);
    style::combo (takeBox);
    takeBox.setTooltip ("Takes saved earlier (in Documents\\GigRig\\Test takes). Choosing one loads it.");
    addAndMakeVisible (takeBox);
    takeRecordButton.setTooltip ("Records what goes into every chain that is switched on (before the effects), for up to a minute. Press again when you have finished playing.");
    takePlayButton.setTooltip ("Plays the take round and round into the chains in place of their sockets, so you can turn knobs and listen with your hands free");
    takeRecordButton.onClick = [this]
    {
        if (rack.takeState() == EffectsRack::TakeState::recording)
        {
            rack.takeNote = rack.finishTake();
            refreshTakes();
        }
        else
        {
            const auto error = rack.startTake();
            rack.takeNote = error.isEmpty() ? juce::String ("Recording a take: play (and sing), then press Stop recording.") : error;
        }
    };
    takePlayButton.onClick = [this]
    {
        if (rack.takeState() == EffectsRack::TakeState::playing)
        {
            rack.stopTake();
            rack.takeNote = "Take stopped. The chains hear their sockets again.";
        }
        else
        {
            const bool wasRecording = rack.takeState() == EffectsRack::TakeState::recording;
            if (wasRecording)
            {
                rack.takeNote = rack.finishTake();
                refreshTakes();
            }
            const auto error = rack.playTake();
            if (error.isNotEmpty())
                rack.takeNote = error;
            else if (! wasRecording)
                rack.takeNote = {};
        }
    };
    takeBox.onChange = [this]
    {
        const auto i = takeBox.getSelectedId() - 1;
        if (i < 0 || i >= takeFiles.size())
            return;
        const auto error = rack.loadTake (takeFiles[i]);
        rack.takeNote = error.isEmpty() ? "Loaded the take \"" + takeFiles[i].getFileNameWithoutExtension() + "\". Press Play the take." : error;
    };
    addAndMakeVisible (takeRecordButton);
    addAndMakeVisible (takePlayButton);

    captureButton.onClick = [this]
    {
        if (rack.isCapturing())
        {
            rack.stopCapture();
            captureNote = "Saved " + rack.lastCaptureFile().getFileName() + " in Documents\\GigRig\\Effect captures.";
        }
        else
        {
            const auto error = rack.startCapture (chainIndex, captureFolder());
            captureNote = error.isEmpty() ? "Recording..." : error;
        }
    };
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard ("GigRig build " + juce::String (GIGRIG_BUILD_LABEL) + "\n" + rack.reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = 2 * timerHz;
    };

    for (auto* b : { &matchButton, &captureButton, &copyButton, &addButton, &leftButton, &rightButton, &removeButton, &loadButton, &addPresetButton, &saveButton, &folderButton })
        addAndMakeVisible (b);

    for (const auto& t : fx::effectTypes())
    {
        addBox.addItem (t.name, (int) (&t - fx::effectTypes().data()) + 1);
        typeBox.addItem (t.name, (int) (&t - fx::effectTypes().data()) + 1);
    }

    addBox.setSelectedId (1, juce::dontSendNotification);
    addButton.onClick = [this]
    {
        auto& ch = rack.chain (chainIndex);
        auto m = ch.model();
        const auto i = addBox.getSelectedId() - 1;

        if (i < 0 || i >= (int) fx::effectTypes().size() || (int) m.slots.size() >= fx::Chain::maxSlots)
            return;

        m.slots.push_back ({ fx::effectTypes()[(size_t) i].id, true, {}, {} });
        ch.setModel (m);
        rack.markChanged();
        slotIndex = (int) m.slots.size() - 1;
        rebuildSlotTiles();
        rebuildEditor();
    };

    typeBox.onChange = [this]
    {
        auto& ch = rack.chain (chainIndex);
        auto m = ch.model();
        const auto i = typeBox.getSelectedId() - 1;

        if (slotIndex >= (int) m.slots.size() || i < 0 || i >= (int) fx::effectTypes().size() || m.slots[(size_t) slotIndex].type == fx::effectTypes()[(size_t) i].id)
            return;

        // A new type starts from its own defaults.
        m.slots[(size_t) slotIndex] = { fx::effectTypes()[(size_t) i].id, m.slots[(size_t) slotIndex].on, {}, {} };
        ch.setModel (m);
        rack.markChanged();
        rebuildSlotTiles();
        rebuildEditor();
    };

    const auto move = [this] (int by)
    {
        auto& ch = rack.chain (chainIndex);
        auto m = ch.model();
        const int to = slotIndex + by;

        if (slotIndex >= (int) m.slots.size() || to < 0 || to >= (int) m.slots.size())
            return;

        // Settings travel with the effect (the model always holds the live values).
        std::swap (m.slots[(size_t) slotIndex], m.slots[(size_t) to]);
        ch.setModel (m);
        rack.markChanged();
        slotIndex = to;
        rebuildSlotTiles();
        rebuildEditor();
    };

    leftButton.onClick = [move] { move (-1); };
    rightButton.onClick = [move] { move (1); };
    removeButton.onClick = [this]
    {
        auto& ch = rack.chain (chainIndex);
        auto m = ch.model();

        if (slotIndex >= (int) m.slots.size())
            return;

        m.slots.erase (m.slots.begin() + slotIndex);
        ch.setModel (m);
        rack.markChanged();
        slotIndex = juce::jmax (0, juce::jmin (slotIndex, (int) m.slots.size() - 1));
        rebuildSlotTiles();
        rebuildEditor();
    };

    styleEditor (presetName);
    presetName.setTextToShowWhenEmpty ("Name for a new preset, e.g. Blues", style::textDim);
    addAndMakeVisible (presetName);
    loadButton.onClick = [this]
    {
        const auto i = presetBox.getSelectedId() - 1;
        if (i >= 0 && i < presetFiles.size())
            loadPreset (presetFiles[i]);
    };
    addPresetButton.setTooltip ("Adds the preset's effects to this chain, keeping the ones already in it (in front of the amp model, if there is one)");
    addPresetButton.onClick = [this]
    {
        const auto i = presetBox.getSelectedId() - 1;
        if (i >= 0 && i < presetFiles.size())
            addPreset (presetFiles[i]);
    };
    saveButton.onClick = [this] { savePreset(); };
    folderButton.onClick = [] { presetFolder().createDirectory(); presetFolder().startAsProcess(); };

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
    {
        for (auto* b : { &pedalSwitchBoxes[(size_t) i], &pedalActionBoxes[(size_t) i] })
        {
            style::combo (*b);
            b->onChange = [this, i] { pushPedal (i); };
            addAndMakeVisible (b);
        }

        pedalSwitchBoxes[(size_t) i].setTooltip ("The chosen switch stops working for the looper while the effects are on");
    }

    rack.onPedalAction = [this] { refresh(); refreshTakes(); };

    // What the app remembered from last time.
    rack.loadStateText (EffectsRack::stateFile().loadFileAsString());
    refresh();
    refreshPresets();
    refreshTakes();
    startTimerHz (timerHz);
}

EffectsPanel::~EffectsPanel()
{
    stopTimer();
    rack.onPedalAction = nullptr;
    rack.stopCapture();
    if (rack.takeState() == EffectsRack::TakeState::recording)
        rack.finishTake();
    else
        rack.stopTake();
    rack.saveState();
}

//==============================================================================
void EffectsPanel::fillInputBox (juce::ComboBox& box, bool allowNone)
{
    const int id = box.getSelectedId();
    box.clear (juce::dontSendNotification);
    const bool cq = engine.isCQ();
    const int count = engine.isOpen() ? juce::jmin (16, engine.numInputs()) : 16;

    if (allowNone)
        box.addItem ("No reference", refNoneId);

    // On the CQ only the sockets: channels 11 to 16 are the mixer's own outputs and would howl.
    for (int i = 0; i < (cq ? juce::jmin (10, count) : count); ++i)
        box.addItem (socketText (i, cq), i + 1);

    box.setSelectedId (id, juce::dontSendNotification);
}

void EffectsPanel::refresh()
{
    const auto r = rack.routing (chainIndex);
    rackOn.setToggleState (rack.isEnabled(), juce::dontSendNotification);
    chainOn.setToggleState (r.on, juce::dontSendNotification);

    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        const auto rc = rack.routing (c);
        chainButtons[c]->setButtonText (juce::String (c + 1) + "  " + rc.name + (rc.on ? "" : " (off)"));
        chainButtons[c]->setColour (juce::TextButton::buttonColourId, c == chainIndex ? style::green.darker (0.5f) : style::panel);
    }

    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (r.name, juce::dontSendNotification);

    fillInputBox (inBox, false);
    fillInputBox (refBox, true);
    inBox.setSelectedId (r.source + 1, juce::dontSendNotification);
    refBox.setSelectedId (r.reference >= 0 ? r.reference + 1 : refNoneId, juce::dontSendNotification);
    outBox.setSelectedId (outputId (r.out), juce::dontSendNotification);

    looperBox.clear (juce::dontSendNotification);
    looperBox.addItem ("Its own socket (" + juce::String (r.source + 1) + ")", 1);
    looperBox.addItem ("Not recorded", 2);
    for (int s = 0; s < 10; ++s)
        if (s != r.source)
            looperBox.addItem ("Socket " + juce::String (s + 1) + " (in place of what is plugged in there)", 10 + s);
    looperBox.setSelectedId (r.looperHears == -2 ? 1 : r.looperHears == -1 ? 2 : 10 + r.looperHears, juce::dontSendNotification);

    listenEffects.setToggleState (r.listen == EffectsRack::Listen::effects, juce::dontSendNotification);
    listenDry.setToggleState (r.listen == EffectsRack::Listen::dry, juce::dontSendNotification);
    listenRef.setToggleState (r.listen == EffectsRack::Listen::reference, juce::dontSendNotification);
    listenRef.setEnabled (r.reference >= 0);
    levelSlider.setValue (r.trimDb, juce::dontSendNotification);

    rebuildSlotTiles();
    rebuildEditor();
    fillPedalBoxes();
}

void EffectsPanel::pushRouting()
{
    auto r = rack.routing (chainIndex);
    r.on = chainOn.getToggleState();
    r.name = nameEditor.getText().trim().isEmpty() ? "Chain " + juce::String (chainIndex + 1) : nameEditor.getText().trim();
    if (inBox.getSelectedId() > 0)  r.source = inBox.getSelectedId() - 1;
    if (outBox.getSelectedId() > 0) r.out = outputFromId (outBox.getSelectedId());
    r.reference = refBox.getSelectedId() == refNoneId || refBox.getSelectedId() == 0 ? -1 : refBox.getSelectedId() - 1;
    const auto lid = looperBox.getSelectedId();
    r.looperHears = lid == 1 ? -2 : lid == 2 ? -1 : lid >= 10 ? lid - 10 : r.looperHears;
    r.trimDb = (float) levelSlider.getValue();

    if (r.reference < 0 && r.listen == EffectsRack::Listen::reference)
        r.listen = EffectsRack::Listen::effects;

    const bool sourceChanged = r.source != rack.routing (chainIndex).source;
    rack.setRouting (chainIndex, r);
    rack.clearGuard (chainIndex);

    if (sourceChanged)
        refresh();
    else
    {
        listenRef.setEnabled (r.reference >= 0);
        chainButtons[chainIndex]->setButtonText (juce::String (chainIndex + 1) + "  " + r.name + (r.on ? "" : " (off)"));
    }
}

void EffectsPanel::chooseChain (int c)
{
    chainIndex = c;
    slotIndex = 0;
    refresh();
}

void EffectsPanel::chooseSlot (int s)
{
    slotIndex = s;
    rebuildSlotTiles();
    rebuildEditor();
}

void EffectsPanel::rebuildSlotTiles()
{
    const auto& m = rack.chain (chainIndex).model();
    slotButtons.clear();
    slotToggles.clear();

    for (int s = 0; s < (int) m.slots.size(); ++s)
    {
        const auto* t = fx::findEffectType (m.slots[(size_t) s].type);
        auto* b = slotButtons.add (new juce::TextButton (juce::String (s + 1) + "  " + (t != nullptr ? juce::String (t->name) : juce::String (m.slots[(size_t) s].type))));
        b->setColour (juce::TextButton::buttonColourId, s == slotIndex ? style::green.darker (0.5f) : style::panel);
        // Later, not now: choosing rebuilds these buttons, and this one is still mid-click.
        b->onClick = [safe = juce::Component::SafePointer<EffectsPanel> (this), s]
        {
            juce::MessageManager::callAsync ([safe, s] { if (safe != nullptr) safe->chooseSlot (s); });
        };
        addAndMakeVisible (b);

        auto* on = slotToggles.add (new juce::ToggleButton ("On"));
        on->setToggleState (m.slots[(size_t) s].on, juce::dontSendNotification);
        on->setColour (juce::ToggleButton::textColourId, style::textMain);
        on->setColour (juce::ToggleButton::tickColourId, style::green);
        on->onClick = [this, s, on] { rack.chain (chainIndex).setSlotOn (s, on->getToggleState()); rack.markChanged(); fillPedalBoxes(); };
        addAndMakeVisible (on);
    }

    resized();
}

void EffectsPanel::rebuildEditor()
{
    paramLabels.clear();
    paramSliders.clear();
    paramChoices.clear();
    paramToggles.clear();
    showModel = false;
    for (auto* c : std::initializer_list<juce::Component*> { &modelLabel, &modelBox, &modelFolderButton })
        c->setVisible (false);

    auto& ch = rack.chain (chainIndex);
    const auto& m = ch.model();
    const bool has = slotIndex < (int) m.slots.size();

    for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &leftButton, &rightButton, &removeButton })
        c->setVisible (has);

    if (! has)
    {
        descriptionLabel.setText (m.slots.empty() ? "No effects in this chain yet: pick one on the right and press Add." : "", juce::dontSendNotification);
        resized();
        return;
    }

    const auto& slot = m.slots[(size_t) slotIndex];
    const auto* type = fx::findEffectType (slot.type);
    typeBox.setSelectedId (type != nullptr ? (int) (type - fx::effectTypes().data()) + 1 : 0, juce::dontSendNotification);
    descriptionLabel.setText (type != nullptr ? juce::String (type->description) + "  (double-click a slider for its default)" : "", juce::dontSendNotification);

    auto* fx = ch.effect (slotIndex);

    if (fx == nullptr)
        return;

    for (int p = 0; p < (int) fx->params().size(); ++p)
    {
        const auto& info = fx->params()[(size_t) p];
        auto* label = paramLabels.add (new juce::Label());
        style::body (*label, info.label);
        addAndMakeVisible (label);

        const int s = slotIndex;

        if (info.kind == fx::ParamInfo::Kind::continuous)
        {
            auto* slider = paramSliders.add (new juce::Slider());
            styleSlider (*slider);
            const auto range = info.max - info.min;
            slider->setRange (info.min, info.max, range > 20.0f ? 0.1 : range > 2.0f ? 0.05 : 0.005);
            slider->setTextValueSuffix (info.unit.empty() ? "" : " " + juce::String (info.unit));
            slider->setDoubleClickReturnValue (true, info.def);
            slider->setValue (fx->getParam (p), juce::dontSendNotification);
            slider->onValueChange = [this, s, p, slider] { rack.chain (chainIndex).setParam (s, p, (float) slider->getValue()); rack.markChanged(); };
            slider->getProperties().set ("param", p);
            addAndMakeVisible (slider);
        }
        else if (info.kind == fx::ParamInfo::Kind::choice)
        {
            auto* box = paramChoices.add (new juce::ComboBox());
            style::combo (*box);
            for (size_t k = 0; k < info.choices.size(); ++k)
                box->addItem (info.choices[k], (int) k + 1);
            box->setSelectedId (juce::roundToInt (fx->getParam (p)) + 1, juce::dontSendNotification);
            box->onChange = [this, s, p, box] { rack.chain (chainIndex).setParam (s, p, (float) (box->getSelectedId() - 1)); rack.markChanged(); };
            box->getProperties().set ("param", p);
            addAndMakeVisible (box);
        }
        else
        {
            auto* t = paramToggles.add (new juce::ToggleButton ("On"));
            t->setColour (juce::ToggleButton::textColourId, style::textMain);
            t->setToggleState (fx->getParam (p) >= 0.5f, juce::dontSendNotification);
            t->onClick = [this, s, p, t] { rack.chain (chainIndex).setParam (s, p, t->getToggleState() ? 1.0f : 0.0f); rack.markChanged(); };
            t->getProperties().set ("param", p);
            addAndMakeVisible (t);
        }
    }

    // A file to load (the amp's model, a response file): a list of what is in its folder.
    const auto keys = fx->textKeys();
    fileKey = std::find (keys.begin(), keys.end(), "model") != keys.end() ? "model"
            : std::find (keys.begin(), keys.end(), "file") != keys.end() ? "file" : "";

    if (! fileKey.empty())
    {
        showModel = true;
        const bool response = fileKey == "file";
        modelLabel.setText (response ? "Response file" : "Model", juce::dontSendNotification);
        modelNames.clear();

        if (response)
        {
            // Listed with ".wav", which is how the effect looks them up; the first line is "none".
            modelNames.add ("(none: a straight wire)");
            auto files = responses::folder().findChildFiles (juce::File::findFiles, false, "*.wav");
            files.sort();
            for (const auto& f : files)
                modelNames.add (f.getFileName());
        }
        else
        {
            modelNames = ampmodels::names (ampmodels::folder());
        }

        const auto now = juce::String (fx->getText (fileKey));
        if (now.isNotEmpty() && ! modelNames.contains (now))
            modelNames.add (now);

        modelBox.clear (juce::dontSendNotification);
        for (int i = 0; i < modelNames.size(); ++i)
            modelBox.addItem (modelNames[i], i + 1);
        modelBox.setSelectedId (now.isEmpty() && response ? 1 : modelNames.indexOf (now) + 1, juce::dontSendNotification);

        const int s = slotIndex;
        modelBox.onChange = [this, s, response]
        {
            const int id = modelBox.getSelectedId();
            const auto name = response && id == 1 ? juce::String() : modelNames[id - 1];
            if (id <= 0 || (name.isEmpty() && ! response))
                return;
            rack.chain (chainIndex).setText (s, fileKey, name.toStdString());
            rack.markChanged();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &modelLabel, &modelBox, &modelFolderButton })
            c->setVisible (true);
    }

    resized();
}

//==============================================================================
void EffectsPanel::refreshTakes()
{
    takeFiles = EffectsRack::takeFolder().findChildFiles (juce::File::findFiles, false, "*.wav");
    takeFiles.sort();
    takeBox.clear (juce::dontSendNotification);

    for (int i = 0; i < takeFiles.size(); ++i)
    {
        takeBox.addItem (takeFiles[i].getFileNameWithoutExtension(), i + 1);
        if (takeFiles[i].getFileNameWithoutExtension() == rack.takeName())
            takeBox.setSelectedId (i + 1, juce::dontSendNotification);
    }

    takeBox.setTextWhenNothingSelected (takeFiles.isEmpty() ? "No saved takes yet" : "Saved takes");
}

void EffectsPanel::refreshPresets()
{
    presetFiles = presetFolder().findChildFiles (juce::File::findFiles, false, "*.txt");
    presetFiles.sort();
    presetBox.clear (juce::dontSendNotification);

    for (int i = 0; i < presetFiles.size(); ++i)
        presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), i + 1);

    presetBox.setTextWhenNothingSelected (presetFiles.isEmpty() ? "No presets saved yet" : "Choose a preset");
}

void EffectsPanel::savePreset()
{
    const auto name = juce::File::createLegalFileName (presetName.getText().trim());

    if (name.isEmpty())
    {
        captureNote = "Type a name for the preset first.";
        return;
    }

    presetFolder().createDirectory();
    const auto file = presetFolder().getChildFile (name + ".txt");
    const bool ok = file.replaceWithText ("# GigRig effect preset \"" + name + "\" (build " + juce::String (GIGRIG_BUILD_LABEL) + ")\n"
                                          + juce::String (rack.chain (chainIndex).model().toText()));
    captureNote = ok ? "Saved the preset \"" + name + "\"." : "Could not save " + file.getFullPathName();
    refreshPresets();

    for (int i = 0; i < presetFiles.size(); ++i)
        if (presetFiles[i] == file)
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
}

void EffectsPanel::loadPreset (const juce::File& file)
{
    std::vector<std::string> skipped;
    const auto m = fx::ChainModel::fromText (file.loadFileAsString().toStdString(), &skipped);
    rack.chain (chainIndex).setModel (m);
    rack.markChanged();
    slotIndex = 0;
    captureNote = "Loaded \"" + file.getFileNameWithoutExtension() + "\"" + (skipped.empty() ? juce::String (".") : ", leaving out effects this build does not have.");
    refresh();
}

void EffectsPanel::addPreset (const juce::File& file)
{
    std::vector<std::string> skipped;
    const auto extra = fx::ChainModel::fromText (file.loadFileAsString().toStdString(), &skipped);
    auto m = rack.chain (chainIndex).model();
    const int added = m.addFrom (extra, fx::Chain::maxSlots);
    rack.chain (chainIndex).setModel (m);
    rack.markChanged();
    const int left = (int) extra.slots.size() - added;
    captureNote = "Added \"" + file.getFileNameWithoutExtension() + "\" to the chain"
                + (left > 0 ? ", but the chain is full: " + juce::String (left) + " effect(s) left out." : juce::String ("."))
                + (skipped.empty() ? juce::String() : juce::String (" Effects this build does not have were left out."));
    refresh();
}

void EffectsPanel::levelMatch()
{
    rack.takeLevels (chainIndex);     // start counting from now
    levelNote = "Level match: play normally for " + juce::String ((int) matchSeconds) + " seconds...";
    matchButton.getProperties().set ("matching", true);
    matchButton.getProperties().set ("effE", 0.0);
    matchButton.getProperties().set ("cmpE", 0.0);
    matchButton.getProperties().set ("secs", 0.0);
}

void EffectsPanel::fillPedalBoxes()
{
    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
    {
        auto& sb = pedalSwitchBoxes[(size_t) i];
        auto& ab = pedalActionBoxes[(size_t) i];
        sb.clear (juce::dontSendNotification);
        ab.clear (juce::dontSendNotification);
        sb.addItem ("No switch", 1);

        for (int sw = 0; sw < 11; ++sw)
            sb.addItem (switchName (sw), 10 + sw);

        ab.addItem ("Tap tempo (for synced delays)", 3);
        ab.addItem ("Test take: record, then play it round, then stop", 4);

        for (int c = 0; c < EffectsRack::numChains; ++c)
        {
            const auto r = rack.routing (c);
            ab.addItem ("Chain " + juce::String (c + 1) + " (" + r.name + "): A/B", c * 100 + 1);
            ab.addItem ("Chain " + juce::String (c + 1) + " (" + r.name + "): on/off", c * 100 + 2);

            const auto& m = rack.chain (c).model();
            for (int k = 0; k < (int) m.slots.size(); ++k)
                ab.addItem ("Chain " + juce::String (c + 1) + ": slot " + juce::String (k + 1) + " ("
                            + juce::String (fx::findEffectType (m.slots[(size_t) k].type)->name) + ") on/off", c * 100 + 11 + k);
        }

        const auto a = rack.pedalAssign (i);
        sb.setSelectedId (a.sw < 0 ? 1 : 10 + a.sw, juce::dontSendNotification);
        ab.setSelectedId (a.action == 2 ? 3 : a.action == 3 ? 4 : a.chain * 100 + (a.action == 0 ? 1 : a.action == 1 ? 2 : 11 + a.action - 10), juce::dontSendNotification);
    }
}

void EffectsPanel::pushPedal (int i)
{
    const auto sid = pedalSwitchBoxes[(size_t) i].getSelectedId();
    const auto aid = juce::jmax (1, pedalActionBoxes[(size_t) i].getSelectedId());
    EffectsRack::PedalAssign a;
    a.sw = sid >= 10 ? sid - 10 : -1;
    a.chain = aid / 100;
    const auto code = aid % 100;
    a.action = code == 1 ? 0 : code == 2 ? 1 : code == 3 ? 2 : code == 4 ? 3 : 10 + code - 11;
    rack.setPedalAssign (i, a);
}

//==============================================================================
void EffectsPanel::timerCallback()
{
    rack.tick();

    const auto toDb = [] (float peak) { return juce::Decibels::gainToDecibels (peak, -100.0f); };
    const auto fall = [] (float shown, float now) { return now > shown ? now : juce::jmax (now, shown - 1.5f); };
    inDb = fall (inDb, toDb (rack.takeInputPeak (chainIndex)));
    outDb = fall (outDb, toDb (rack.takeOutputPeak (chainIndex)));
    refDb = fall (refDb, toDb (rack.takeReferencePeak (chainIndex)));
    repaint (meterArea);

    loadShown = juce::jmax (loadShown * 0.97, rack.takeLoad());

    {
        const double bpm = rack.effectsTempo();
        tempoLabel.setText (bpm > 0.0 ? "Tempo " + rack.tempoText() : "Tempo: none yet", juce::dontSendNotification);
    }

    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy effects results");

    // The level match, counting while the guitar is played.
    if ((bool) matchButton.getProperties()["matching"])
    {
        const auto l = rack.takeLevels (chainIndex);
        auto& p = matchButton.getProperties();
        const double secs = (double) p["secs"] + l.playedSeconds;
        const double effE = (double) p["effE"] + std::pow (10.0, l.effectsDb / 10.0) * l.playedSeconds;
        const double cmpE = (double) p["cmpE"] + std::pow (10.0, l.compareDb / 10.0) * l.playedSeconds;
        p.set ("secs", secs); p.set ("effE", effE); p.set ("cmpE", cmpE);
        levelNote = "Level match: keep playing... " + juce::String (secs, 1) + " of " + juce::String ((int) matchSeconds) + " s";

        if (secs >= matchSeconds)
        {
            p.set ("matching", false);

            if (effE <= 1.0e-12 || cmpE <= 1.0e-12)
                levelNote = "Level match: nothing heard from the " + juce::String (effE <= 1.0e-12 ? "effects" : "reference") + ". Is it switched on and plugged in?";
            else
            {
                const auto change = (float) (10.0 * std::log10 (cmpE / effE));
                auto r = rack.routing (chainIndex);
                r.trimDb = juce::jlimit (-36.0f, 24.0f, r.trimDb + change);
                rack.setRouting (chainIndex, r);
                levelSlider.setValue (r.trimDb, juce::dontSendNotification);
                levelNote = "Level match: chain level moved by " + juce::String (change, 1) + " dB to " + juce::String (r.trimDb, 1) + " dB, so the effects are as loud as the "
                            + (r.reference >= 0 ? "reference." : "dry sound.");
            }
        }
    }

    {
        const auto state = rack.takeState();
        takeRecordButton.setButtonText (state == EffectsRack::TakeState::recording ? "Stop recording (" + juce::String (rack.takeSeconds(), 0) + " s)" : juce::String ("Record a take"));
        takePlayButton.setButtonText (state == EffectsRack::TakeState::playing ? "Stop the take" : "Play the take");
        takePlayButton.setEnabled (state != EffectsRack::TakeState::none);
        takeRecordButton.setColour (juce::TextButton::buttonColourId, state == EffectsRack::TakeState::recording ? style::red.darker (0.3f) : style::panel);
        takePlayButton.setColour (juce::TextButton::buttonColourId, state == EffectsRack::TakeState::playing ? style::green.darker (0.3f) : style::panel);
        takeLabel.setText (state == EffectsRack::TakeState::none ? juce::String ("None yet. It records every chain that is on, then plays it back into them.")
                                                                 : rack.takeText().fromFirstOccurrenceOf (": ", false, false),
                           juce::dontSendNotification);

        // A take that filled its minute has ended by itself: keep it like any other.
        if ((bool) takeRecordButton.getProperties()["recording"] && state != EffectsRack::TakeState::recording && rack.takeNote.startsWith ("Recording"))
        {
            rack.takeNote = rack.finishTake() + " (It stopped at the one minute a take can hold.)";
            refreshTakes();
        }
        takeRecordButton.getProperties().set ("recording", state == EffectsRack::TakeState::recording);
    }

    if (rack.isCapturing())
    {
        captureButton.setButtonText ("Stop capture (" + juce::String (rack.captureSeconds(), 0) + " s)");

        if (rack.captureSeconds() >= EffectsRack::maxCaptureSeconds)
        {
            rack.stopCapture();
            captureNote = "Saved " + rack.lastCaptureFile().getFileName() + " (stopped at 3 minutes).";
        }
    }
    else
        captureButton.setButtonText ("Capture");

    if (++ticks % 5 == 0)
    {
        const auto r = rack.routing (chainIndex);
        juce::String s;

        if (! rack.isEnabled())
            s << "The effects are switched off (tick 'Effects on'). The looper hears the inputs as before.\n";
        else if (! r.on)
            s << "This chain is off (tick 'This chain on').\n";

        if (rack.guardTripped (chainIndex))
            s << "FEEDBACK GUARD: this chain's output was stuck at full level, so it was switched off. Check the mixer is not sending the return back into the input socket, then tick 'This chain on'.\n";

        const auto load = rack.chainLoad (chainIndex);
        s << "Delay through this chain: " << juce::String (rack.chain (chainIndex).latencySamples() * 1000.0 / rack.sampleRate(), 2)
          << " ms.   Processor: this chain " << juce::String (load.average * 100.0, 1) << "% on average, " << juce::String (load.busiest * 100.0, 1)
          << "% at its busiest; all chains together " << juce::String (loadShown * 100.0, 1) << "% at their busiest.\n";

        if (levelNote.isNotEmpty()) s << levelNote << "\n";
        if (captureNote.isNotEmpty()) s << captureNote << "\n";

        if (rack.takeNote.isNotEmpty()) s << rack.takeNote << "\n";

        // The chosen effect's own line (the amp: which model; the harmony: the note it hears and the one it sings).
        if (auto* f = rack.chain (chainIndex).effect (slotIndex); f != nullptr && ! f->status().empty())
            s << "This effect: " << juce::String (f->status()) << "\n";

        // The chosen effect's own meter (the gate: how far it is turning down now).
        if (auto* f = rack.chain (chainIndex).effect (slotIndex); f != nullptr && f->hasMeter())
        {
            const float db = f->meterValue();
            s << "Turning down by: " << (db > -0.5f ? juce::String ("nothing (open)") : juce::String (-db, 0) + " dB") << "\n";
        }

        statusLabel.setText (s.trimEnd(), juce::dontSendNotification);
    }
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);

    // Three meters: what goes in, what comes out, and the reference.
    auto area = meterArea;
    const std::array<std::pair<const char*, float>, 3> meters { { { "In", inDb }, { "Out", outDb }, { "Ref", refDb } } };

    for (const auto& [name, db] : meters)
    {
        auto row = area.removeFromTop (area.getHeight() / 3).reduced (0, 2);
        g.setColour (style::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (name, row.removeFromLeft (30), juce::Justification::centredLeft);
        auto num = row.removeFromRight (52);
        g.drawText (db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1), num, juce::Justification::centredRight);
        g.setColour (style::panel);
        g.fillRect (row);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -1.0f ? style::red : db > -12.0f ? style::amber : style::green);
        g.fillRect (row.withWidth (juce::roundToInt ((float) row.getWidth() * fill)));
    }
}

void EffectsPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    // Row 1: on, the chains, the name.
    {
        auto row = area.removeFromTop (30);
        rackOn.setBounds (row.removeFromLeft (110));
        for (auto* b : chainButtons)
            b->setBounds (row.removeFromLeft (140).reduced (2, 0));
        row.removeFromLeft (8);
        chainOn.setBounds (row.removeFromLeft (120));
        nameEditor.setBounds (row.reduced (0, 2));
    }

    area.removeFromTop (6);

    // Rows 2 and 3: routing and listening, with the meters on the right.
    {
        auto block = area.removeFromTop (64);
        meterArea = block.removeFromRight (200).reduced (4, 2);
        auto row = block.removeFromTop (30);
        inLabel.setBounds (row.removeFromLeft (44));
        inBox.setBounds (row.removeFromLeft (110).reduced (0, 2));
        row.removeFromLeft (8);
        outLabel.setBounds (row.removeFromLeft (52));
        outBox.setBounds (row.removeFromLeft (230).reduced (0, 2));
        row.removeFromLeft (8);
        looperLabel.setBounds (row.removeFromLeft (120));
        looperBox.setBounds (row.reduced (0, 2));

        block.removeFromTop (4);
        row = block.removeFromTop (30);
        listenLabel.setBounds (row.removeFromLeft (64));
        listenEffects.setBounds (row.removeFromLeft (70).reduced (1, 2));
        listenDry.setBounds (row.removeFromLeft (50).reduced (1, 2));
        listenRef.setBounds (row.removeFromLeft (84).reduced (1, 2));
        refBox.setBounds (row.removeFromLeft (140).reduced (2, 2));

        area.removeFromTop (4);
        row = area.removeFromTop (30);
        levelLabel.setBounds (row.removeFromLeft (80));
        levelSlider.setBounds (row.removeFromLeft (300).reduced (0, 2));
        matchButton.setBounds (row.removeFromLeft (110).reduced (4, 2));
        captureButton.setBounds (row.removeFromLeft (170).reduced (4, 2));
        row.removeFromLeft (8);
        tapButton.setBounds (row.removeFromLeft (90).reduced (2, 2));
        tempoLabel.setBounds (row);

        area.removeFromTop (2);
        row = area.removeFromTop (30);
        takeHeading.setBounds (row.removeFromLeft (80));
        takeRecordButton.setBounds (row.removeFromLeft (170).reduced (0, 2));
        takePlayButton.setBounds (row.removeFromLeft (130).reduced (4, 2));
        takeBox.setBounds (row.removeFromLeft (200).reduced (0, 2));
        takeLabel.setBounds (row.reduced (6, 0));
    }

    area.removeFromTop (6);
    slotsLabel.setBounds (area.removeFromTop (20));

    // Row 4: the slots (six to a row, a second row once there are more), then "add".
    {
        constexpr int perRow = 6;
        const int rows = slotButtons.size() > perRow ? 2 : 1;
        auto block = area.removeFromTop (54 * rows);
        auto add = block.removeFromRight (230);
        addBox.setBounds (add.removeFromTop (26).reduced (0, 1));
        addButton.setBounds (add.removeFromTop (26).removeFromLeft (80).reduced (0, 1));

        const int w = juce::jmin (170, block.getWidth() / juce::jlimit (4, perRow, slotButtons.size()));
        for (int r = 0; r < rows; ++r)
        {
            auto row = block.removeFromTop (54);
            for (int s = r * perRow; s < juce::jmin (slotButtons.size(), (r + 1) * perRow); ++s)
            {
                auto tile = row.removeFromLeft (w).reduced (2, 0);
                slotButtons[s]->setBounds (tile.removeFromTop (28));
                slotToggles[s]->setBounds (tile);
            }
        }
    }

    area.removeFromTop (6);

    // Row 5: the chosen slot.
    {
        auto row = area.removeFromTop (28);
        typeBox.setBounds (row.removeFromLeft (220).reduced (0, 2));
        leftButton.setBounds (row.removeFromLeft (90).reduced (2, 2));
        rightButton.setBounds (row.removeFromLeft (90).reduced (2, 2));
        removeButton.setBounds (row.removeFromLeft (80).reduced (2, 2));
        descriptionLabel.setBounds (row.reduced (6, 0));
    }

    area.removeFromTop (4);

    // Its settings, in two columns.
    {
        auto grid = area.removeFromTop (5 * 28);
        const int colW = grid.getWidth() / 2;
        int sliderI = 0, choiceI = 0, toggleI = 0;

        for (int p = 0; p < paramLabels.size(); ++p)
        {
            const int col = p % 2, line = p / 2;
            juce::Rectangle<int> cell (grid.getX() + col * colW, grid.getY() + line * 28, colW - 12, 28);
            paramLabels[p]->setBounds (cell.removeFromLeft (130));

            if (sliderI < paramSliders.size() && (int) paramSliders[sliderI]->getProperties()["param"] == p)
                paramSliders[sliderI++]->setBounds (cell.reduced (0, 2));
            else if (choiceI < paramChoices.size() && (int) paramChoices[choiceI]->getProperties()["param"] == p)
                paramChoices[choiceI++]->setBounds (cell.removeFromLeft (200).reduced (0, 2));
            else if (toggleI < paramToggles.size())
                paramToggles[toggleI++]->setBounds (cell.removeFromLeft (80));
        }

        if (showModel)
        {
            const int p = paramLabels.size(), col = p % 2, line = p / 2;
            juce::Rectangle<int> cell (grid.getX() + col * colW, grid.getY() + line * 28, colW - 12, 28);
            modelLabel.setBounds (cell.removeFromLeft (130));
            modelBox.setBounds (cell.removeFromLeft (240).reduced (0, 2));
            modelFolderButton.setBounds (cell.removeFromLeft (100).reduced (4, 2));
        }
    }

    area.removeFromTop (6);

    // Presets and pedal switches side by side.
    {
        auto block = area.removeFromTop (84);
        auto left = block.removeFromLeft (block.getWidth() / 2).reduced (0, 0);
        presetLabel.setBounds (left.removeFromTop (20));
        auto row = left.removeFromTop (28);
        presetBox.setBounds (row.removeFromLeft (170).reduced (0, 2));
        loadButton.setBounds (row.removeFromLeft (60).reduced (2, 2));
        addPresetButton.setBounds (row.removeFromLeft (100).reduced (2, 2));
        folderButton.setBounds (row.removeFromLeft (100).reduced (2, 2));
        row = left.removeFromTop (28);
        presetName.setBounds (row.removeFromLeft (200).reduced (0, 2));
        saveButton.setBounds (row.removeFromLeft (80).reduced (2, 2));

        block.removeFromLeft (12);
        pedalLabel.setBounds (block.removeFromTop (20));
        for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        {
            auto r = block.removeFromTop (28);
            pedalSwitchBoxes[(size_t) i].setBounds (r.removeFromLeft (110).reduced (0, 2));
            pedalActionBoxes[(size_t) i].setBounds (r.reduced (4, 2));
        }
    }

    area.removeFromTop (4);
    auto bottom = area.removeFromBottom (30);
    copyButton.setBounds (bottom.removeFromRight (170).reduced (0, 2));
    statusLabel.setBounds (area);
}

//==============================================================================
juce::File AmpCapturePanel::captureFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Amp captures");
}

AmpCapturePanel::AmpCapturePanel (AudioEngine& e)
    : engine (e)
{
    style::body (stepsLabel,
        "Records your amp pedal (the Dream '65) so Claude can make a copy of it. One capture per amp setting you use. "
        "A tone-only pedal (the BodyRez) is captured the same way, with the tick box below: the app makes its copy by itself.\n"
        "1. Patch a free CQ aux output into the Dream's input, and the Dream's output straight into a CQ input socket (socket 8 in place of the pedalboard's lead is fine).\n"
        "2. On the CQ, send the return chosen below to that output only (not to the main speakers), output level well down.\n"
        "3. Set the Dream as you play it, but with its reverb and vibrato/tremolo depth OFF (the model cannot learn effects that move; the app adds them back later). Leave its speaker emulation on. Type a name for the setting, for example \"Dream A edge of breakup\".\n"
        "4. Press 'Set from my playing' and play into socket 10 with the lead that normally feeds the Dream, so the test sound is as loud as you are.\n"
        "5. Press Start capture. It plays a test sound for about two and a half minutes and records what comes back. Keep the room quiet; "
        "you will hear the test sound through the Dream if it reaches a speaker.\n"
        "6. When it finishes, send Claude the WAV file from Documents\\GigRig\\Amp captures (Open captures folder).");
    stepsLabel.setJustificationType (juce::Justification::topLeft);
    style::body (nameLabel, "Name of this setting");
    style::body (outLabel, "Test sound out to");
    style::body (inLabel, "Amp comes back on");
    style::body (levelLabel, "Test sound peak");
    style::body (guitarLabel, "My guitar is on");
    style::body (statusLabel, {});
    statusLabel.setJustificationType (juce::Justification::topLeft);

    for (auto* l : { &stepsLabel, &nameLabel, &outLabel, &inLabel, &levelLabel, &guitarLabel, &statusLabel })
        addAndMakeVisible (l);

    styleEditor (nameEditor);
    nameEditor.setTextToShowWhenEmpty ("Dream A edge of breakup", style::textDim);
    addAndMakeVisible (nameEditor);

    for (auto* b : { &outBox, &inBox, &guitarBox })
    {
        style::combo (*b);
        addAndMakeVisible (b);
    }

    fillOutputBox (outBox);
    outBox.setSelectedId (outputId ({ 2, true }), juce::dontSendNotification);

    for (int i = 0; i < 10; ++i)
    {
        inBox.addItem ("Socket " + juce::String (i + 1), i + 1);
        guitarBox.addItem ("Socket " + juce::String (i + 1), i + 1);
    }

    inBox.setSelectedId (8, juce::dontSendNotification);       // socket 8: the electric today
    guitarBox.setSelectedId (10, juce::dontSendNotification);  // socket 10: the dry guitar for the drive test

    styleSlider (levelSlider);
    levelSlider.setRange (-40.0, -1.0, 0.1);
    levelSlider.setValue (-12.0, juce::dontSendNotification);
    levelSlider.setTextValueSuffix (" dBFS");
    addAndMakeVisible (levelSlider);

    fromPlayingButton.onClick = [this]
    {
        engine.effects().takeSocketMax (guitarBox.getSelectedId() - 1);
        guitarPeak = 0.0f;
        listeningTicks = 10 * 20;
    };

    startButton.onClick = [this]
    {
        if (engine.effects().isAmpCapturing())
        {
            engine.effects().stopAmpCapture();
            note = "Stopped early. The part recorded is saved in " + engine.effects().lastAmpCaptureFile().getFileName() + ", but a full run is better.";
        }
        else
            start();
    };

    fileButton.setTooltip ("Use another test sound (for example the standard NAM input file) instead of the app's own");
    fileButton.onClick = [this]
    {
        if (soundFile != juce::File())
        {
            soundFile = juce::File();
            fileButton.setButtonText ("Use a test sound file...");
            return;
        }

        chooser = std::make_unique<juce::FileChooser> ("Choose a test sound (WAV)", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
            {
                soundFile = fc.getResult();
                fileButton.setButtonText ("Using " + soundFile.getFileName() + " (click for the app's own)");
            }
        });
    };

    folderButton.onClick = [] { captureFolder().createDirectory(); captureFolder().startAsProcess(); };

    // A tone-only pedal needs no training: its capture becomes a response file here and now.
    toneOnlyBox.setColour (juce::ToggleButton::textColourId, style::textMain);
    toneOnlyBox.setColour (juce::ToggleButton::tickColourId, style::green);
    toneOnlyBox.setTooltip ("For a pedal that only changes the tone, such as the TC BodyRez or an EQ. Not for an amp or a drive: those need a model, which Claude trains from the capture.");
    addAndMakeVisible (toneOnlyBox);
    responseButton.setTooltip ("Pick a capture made earlier (a WAV in Documents\\GigRig\\Amp captures) and make a response file from it");
    responseButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a capture of a tone-only pedal (WAV)", captureFolder(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                makeResponse (fc.getResult());
        });
    };

    for (auto* b : { &fromPlayingButton, &startButton, &fileButton, &folderButton, &responseButton })
        addAndMakeVisible (b);

    startTimerHz (20);
}

AmpCapturePanel::~AmpCapturePanel()
{
    stopTimer();
    engine.effects().stopAmpCapture();
}

void AmpCapturePanel::start()
{
    if (! engine.isOpen())
    {
        note = "Start the audio first (Latency and audio test tab).";
        return;
    }

    EffectsRack::AmpCaptureSetup setup;
    setup.out = outputFromId (outBox.getSelectedId());
    setup.input = inBox.getSelectedId() - 1;
    setup.levelDb = (float) levelSlider.getValue();
    setup.name = nameEditor.getText().trim().isEmpty() ? juce::String ("Amp capture") : nameEditor.getText().trim();
    setup.soundFile = soundFile;

    const auto timing = engine.updateLoopTiming();
    const auto error = engine.effects().startAmpCapture (setup, captureFolder(), timing.samples * 1000.0 / juce::jmax (1.0, engine.sampleRate()), timing.source);
    note = error.isEmpty() ? "Capturing \"" + setup.name + "\"..." : error;
}

void AmpCapturePanel::makeResponse (const juce::File& capture)
{
    if (makingResponse)
        return;

    makingResponse = true;
    responseNote = "Working out a response file from " + capture.getFileName() + " (a few seconds)...";
    // The name of the capture without its date and time, so "BodyRez 10 oclock 2026-11-25 14-03-10.wav" makes "BodyRez 10 oclock.wav".
    auto name = capture.getFileNameWithoutExtension();
    if (name.length() > 20 && name.substring (name.length() - 19).containsOnly ("0123456789- "))
        name = name.dropLastCharacters (20).trim();
    const auto target = responses::folder().getChildFile (juce::File::createLegalFileName (name.isEmpty() ? capture.getFileNameWithoutExtension() : name) + ".wav");

    juce::Thread::launch ([capture, target, safe = juce::Component::SafePointer<AmpCapturePanel> (this)]
    {
        const auto made = responses::makeFromCapture (capture, target);
        juce::MessageManager::callAsync ([safe, made]
        {
            if (safe != nullptr)
            {
                safe->responseNote = made.text;
                safe->makingResponse = false;
            }
        });
    });
}

juce::String AmpCapturePanel::reportText() const
{
    const auto& rack = engine.effects();
    juce::String t = "--- Amp capture ---\n";
    t << (rack.isAmpCapturing() ? "Capturing now, " + juce::String (rack.ampCaptureProgress() * 100.0, 0) + "% done\n" : juce::String());
    t << (rack.lastAmpCaptureFile() != juce::File() ? "Last capture: " + rack.lastAmpCaptureFile().getFullPathName() + "\n" : "No capture made yet\n");
    if (responseNote.isNotEmpty())
        t << "Response file: " << responseNote << "\n";
    return t;
}

void AmpCapturePanel::timerCallback()
{
    auto& rack = engine.effects();
    const auto back = juce::Decibels::gainToDecibels (rack.takeAmpReturnPeak(), -100.0f);
    returnDb = back > returnDb ? back : juce::jmax (back, returnDb - 1.5f);
    progress = rack.ampCaptureProgress();
    repaint (meterArea);

    if (listeningTicks > 0)
    {
        guitarPeak = juce::jmax (guitarPeak, rack.takeSocketMax (guitarBox.getSelectedId() - 1));

        if (--listeningTicks == 0)
        {
            const auto db = juce::Decibels::gainToDecibels (guitarPeak, -100.0f);

            if (db < -50.0f)
                note = "Heard nothing on " + guitarBox.getText() + ". Is the guitar plugged in there and the audio running?";
            else
            {
                levelSlider.setValue (juce::jlimit (-40.0f, -1.0f, db), juce::dontSendNotification);
                note = "Your loudest playing peaked at " + juce::String (db, 1) + " dBFS, so the test sound will too.";
            }
        }
        else
            note = "Play your guitar as you would at a gig... " + juce::String (listeningTicks / 20 + 1) + " s";
    }

    if (rack.ampCaptureFinished() || ! rack.isAmpCapturing())
    {
        if (startButton.getButtonText() != "Start capture")
        {
            if (rack.ampCaptureFinished())
                rack.stopAmpCapture();      // closes the file once the sound has finished

            if (! rack.isAmpCapturing())
            {
                startButton.setButtonText ("Start capture");
                if (rack.lastAmpCaptureFile().existsAsFile() && note.startsWith ("Capturing"))
                {
                    if (toneOnlyBox.getToggleState())
                    {
                        note = "Done: saved " + rack.lastAmpCaptureFile().getFileName() + ".";
                        makeResponse (rack.lastAmpCaptureFile());
                    }
                    else
                        note = "Done: saved " + rack.lastAmpCaptureFile().getFileName() + ". Send that WAV file (and the .txt beside it) to Claude.";
                }
            }
        }
    }
    else
        startButton.setButtonText ("Stop (" + juce::String (progress * 100.0, 0) + "%, " + juce::String (rack.ampCaptureSeconds(), 0) + " s)");

    juce::String s = note;

    if (responseNote.isNotEmpty())
        s << "\n" << responseNote;

    if (rack.isAmpCapturing() && returnDb < -60.0f && rack.ampCaptureSeconds() > 4.0)
        s << "\nNothing is coming back on " << inBox.getText() << ": check the cables and the CQ send to the Dream.";
    else if (rack.isAmpCapturing() && returnDb > -1.0f)
        s << "\nThe amp's return is clipping: turn the CQ input gain for " << inBox.getText() << " down a little and start again.";

    statusLabel.setText (s, juce::dontSendNotification);
}

void AmpCapturePanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
    auto area = meterArea;

    if (area.isEmpty())
        return;

    auto top = area.removeFromTop (area.getHeight() / 2).reduced (0, 3);
    g.setColour (style::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Amp return", top.removeFromLeft (80), juce::Justification::centredLeft);
    g.drawText (returnDb <= -99.0f ? juce::String ("-inf") : juce::String (returnDb, 1) + " dB", top.removeFromRight (70), juce::Justification::centredRight);
    g.setColour (style::panel);
    g.fillRect (top);
    g.setColour (returnDb > -1.0f ? style::red : returnDb > -12.0f ? style::amber : style::green);
    g.fillRect (top.withWidth (juce::roundToInt ((float) top.getWidth() * juce::jlimit (0.0f, 1.0f, (returnDb + 60.0f) / 60.0f))));

    auto bottom = area.reduced (0, 3);
    g.setColour (style::textDim);
    g.drawText ("Progress", bottom.removeFromLeft (80), juce::Justification::centredLeft);
    bottom.removeFromRight (70);
    g.setColour (style::panel);
    g.fillRect (bottom);
    g.setColour (style::green);
    g.fillRect (bottom.withWidth (juce::roundToInt ((float) bottom.getWidth() * (float) progress)));
}

void AmpCapturePanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    stepsLabel.setBounds (area.removeFromTop (168));
    area.removeFromTop (6);

    const auto row = [&area] (juce::Label& label, juce::Component& c, int w)
    {
        auto r = area.removeFromTop (30);
        label.setBounds (r.removeFromLeft (160));
        c.setBounds (r.removeFromLeft (w).reduced (0, 2));
        return r;
    };

    row (nameLabel, nameEditor, 320);
    row (outLabel, outBox, 320);
    row (inLabel, inBox, 160);
    row (guitarLabel, guitarBox, 160);
    auto r = row (levelLabel, levelSlider, 320);
    fromPlayingButton.setBounds (r.removeFromLeft (260).reduced (6, 2));

    area.removeFromTop (8);
    auto buttons = area.removeFromTop (34);
    startButton.setBounds (buttons.removeFromLeft (200).reduced (0, 2));
    fileButton.setBounds (buttons.removeFromLeft (300).reduced (6, 2));
    folderButton.setBounds (buttons.removeFromLeft (180).reduced (6, 2));

    area.removeFromTop (4);
    auto tone = area.removeFromTop (30);
    responseButton.setBounds (tone.removeFromRight (290).reduced (6, 2));
    toneOnlyBox.setBounds (tone);

    area.removeFromTop (8);
    meterArea = area.removeFromTop (48).withWidth (juce::jmin (600, area.getWidth()));
    area.removeFromTop (6);
    statusLabel.setBounds (area);
}

} // namespace gigrig
