#include "MixerPanel.h"

#include "effects/Registry.h"

namespace gigrig
{

namespace
{
    constexpr double faderLowDb = -60.0;        // the bottom of a fader or send: off

    float fromFader (double v)      { return v <= faderLowDb + 0.05 ? mixOffDb : (float) v; }
    double toFader (float db)       { return juce::jmax (faderLowDb, (double) db); }

    juce::String levelWords (double v)
    {
        if (v <= faderLowDb + 0.05)
            return "off";

        return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB";
    }

    void styleBar (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setColour (juce::Slider::trackColourId, style::green.withAlpha (0.35f));
        s.setColour (juce::Slider::backgroundColourId, style::panel);
        s.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.setColour (juce::Slider::textBoxOutlineColourId, style::textDim.withAlpha (0.3f));
    }

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, style::panel);
        b.setColour (juce::TextButton::buttonOnColourId, style::green.withAlpha (0.55f));
        b.setColour (juce::TextButton::textColourOffId, style::textMain);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void small (juce::Label& l, const juce::String& text, juce::Colour colour = style::textDim)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.5f)));
        l.setColour (juce::Label::textColourId, colour);
        l.setMinimumHorizontalScale (0.7f);
    }

    juce::String blockName (const std::string& type)
    {
        if (type == "gate")         return "Gate";
        if (type == "comp")         return "1176 style";
        if (type == "la2a")         return "LA-2A style";
        if (type == "warmth")       return "Warmth";
        if (type == "enhancer")     return "Enhancer";
        if (type == "buscomp")      return "Bus comp";
        if (type == "tape")         return "Tape";
        if (type == "mixeq")        return "EQ";
        if (type == "mixcomp")      return "Compressor";
        if (type == "notches")      return "Notches";
        if (type == "limiter")      return "Limiter";
        if (type == "linedelay")    return "Speaker delay";

        if (const auto* t = fx::findEffectType (type))
            return t->name;

        return type;
    }

    juce::String blockWords (const std::string& type)
    {
        if (type == "gate")         return "Shuts the channel when nothing is played, so a quiet stage stays quiet. Threshold is how loud a sound must be to open it.";
        if (type == "comp")         return "An analog-style insert after the EQ and compressor: the 1176-style compressor from the effects list, for a voice or a bass that should sound pressed and forward.";
        if (type == "la2a")         return "A slow, smooth valve-studio leveller with two knobs, kind to a voice or a bass. Turn Peak reduction up until the amber bar moves, then make the level up with Gain.";
        if (type == "enhancer")     return "The gentle gate for a voice: untouched while you sing, turned down by Depth between phrases, never shut. Set Threshold just under your quietest singing; Time is how slowly it closes.";
        if (type == "warmth")       return "A touch of valve-desk colour before the EQ: Drive adds the harmonics, Colour leans it dark or bright, and the loudness stays about where it was.";
        if (type == "buscomp")      return "The desk-style compressor that glues a whole mix together. A few dB of movement on the amber bar is plenty; Mix blends the untouched sound back in.";
        if (type == "tape")         return "A tape machine across the whole mix: gentle squeeze on the peaks, a low bump and a softer top. Drive pushes the tape harder.";
        if (type == "mixeq")        return "Low cut, low shelf, four bells you can sweep (where, how much, how wide), high shelf, high cut. Double-click a slider for its starting point.";
        if (type == "mixcomp")      return "Evens out the loud and the quiet. Lower the Threshold until the strip's amber bar moves on the loud notes; Ratio is how firmly it holds them.";
        if (type == "notches")      return "Six narrow cuts for feedback notes and room booms. A notch at 0 dB does nothing.";
        if (type == "limiter")      return "Nothing leaves louder than the Ceiling. Set it just under where the speaker's own limiter starts, so this one catches the peaks first.";
        if (type == "linedelay")    return "Holds this output back to line a far speaker up with the near ones: about 3 ms for every metre.";

        if (const auto* t = fx::findEffectType (type))
            return juce::String (t->description) + "  Only its wet sound is used here; the channel faders carry the dry.";

        return {};
    }

    constexpr int noneId = 100;
}

//==============================================================================
MixerPanel::MixerPanel (AudioEngine& e) : engine (e), mixer (e.mixer())
{
    // The mixer comes back as it was left, and the starter presets are written once.
    if (Mixer::stateFile().existsAsFile())
        mixer.loadStateText (Mixer::stateFile().loadFileAsString());

    mixer.writeStarterPresets();

    onButton.setColour (juce::ToggleButton::textColourId, style::textMain);
    onButton.setTooltip ("Off: effect chains and loops go straight to their returns and the CQ mixes, exactly as before. On: everything goes through this mixer first.");
    onButton.onClick = [this]
    {
        mixer.setOn (onButton.getToggleState());
        note = onButton.getToggleState() ? "The mixer is on. On the CQ, turn down every channel that used to carry the app's sound except the one the main mix arrives on."
                                         : "The mixer is off: chains and loops go straight to their returns again.";
        noteTicks = 0;
    };
    addAndMakeVisible (onButton);

    for (auto* b : { &recordButton, &recordFolderButton, &resetButton, &copyButton, &folderButton, &turnLeft, &turnRight, &push })
    {
        styleButton (*b);
        addAndMakeVisible (b);
    }

    recordButton.setTooltip ("Records every channel at its own record point, and the main mix, as WAV files in a new folder");
    recordButton.onClick = [this]
    {
        if (mixer.recorder().isRecording())
        {
            mixer.stopRecording();
            note = "Recording stopped: " + juce::String (mixer.recorder().fileNames().size()) + " files in " + mixer.recorder().folder().getFullPathName();
        }
        else if (! mixer.isOn())
            note = "Switch the mixer on first: the recorder takes its sound from the mixer's channels.";
        else if (const auto error = mixer.startRecording (Mixer::recordingFolder()); error.isNotEmpty())
            note = error;
        else
            note = "Recording " + juce::String (mixer.recorder().fileNames().size()) + " files into " + mixer.recorder().folder().getFullPathName();

        noteTicks = 0;
    };
    recordFolderButton.onClick = [] { Mixer::recordingFolder().createDirectory(); Mixer::recordingFolder().startAsProcess(); };
    folderButton.onClick = [] { Mixer::presetRoot().createDirectory(); Mixer::presetRoot().startAsProcess(); };
    resetButton.onClick = [this] { mixer.clearGuard(); };
    resetButton.setColour (juce::TextButton::buttonColourId, style::red.withAlpha (0.6f));
    copyButton.setTooltip ("Copies the mixer's part of the report, ready to paste to Claude");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = 30;
    };

    small (noteLabel, {}, style::textMain);
    small (warningLabel, {}, style::amber);
    warningLabel.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &noteLabel, &warningLabel, &venueKnobLabel, &venueScreen, &detailTitle, &blockHint })
        addAndMakeVisible (l);

    // ---- presets ----
    static const char* const presetWords[3] { "Act", "Venue / PA", "Monitors" };
    static const Mixer::Preset presetKinds[3] { Mixer::Preset::act, Mixer::Preset::venue, Mixer::Preset::monitors };
    static const char* const presetTips[3] {
        "An act is every channel: what is plugged in where, levels, EQ, compressor and sends. Loading one leaves the venue and the monitors alone.",
        "A venue is the main output's whole chain (EQ, notches, compressor, limiter, delay, level) and the mixer's reverbs and delays. It fades in over a second and never jumps louder.",
        "A monitor set-up is both monitor outputs: where they go, mono or stereo, their EQ, notches and limiter." };

    for (int i = 0; i < 3; ++i)
    {
        auto& p = presets[(size_t) i];
        p.kind = presetKinds[i];
        style::body (p.label, presetWords[i]);
        p.label.setTooltip (presetTips[i]);
        style::combo (p.box);
        p.box.setEditableText (true);
        p.box.setTooltip (juce::String (presetTips[i]) + "  Type a new name here and press Save to keep tonight's settings under it.");
        styleButton (p.load);
        styleButton (p.save);

        p.load.onClick = [this, i]
        {
            auto& row = presets[(size_t) i];
            const auto name = row.box.getText().trim();

            if (name.isEmpty())
                note = "Choose a preset in the list first.";
            else if (row.kind == Mixer::Preset::venue)
                note = mixer.loadVenue (name);
            else
            {
                const auto error = row.kind == Mixer::Preset::act ? mixer.loadAct (name) : mixer.loadMonitors (name);
                note = error.isNotEmpty() ? error : juce::String (row.kind == Mixer::Preset::act ? "Act" : "Monitor set-up") + " '" + name + "' loaded.";
            }

            noteTicks = 0;
            refresh();
        };

        p.save.onClick = [this, i]
        {
            auto& row = presets[(size_t) i];
            const auto name = row.box.getText().trim();

            if (name.isEmpty())
                note = "Type a name in the box first.";
            else
            {
                const auto error = row.kind == Mixer::Preset::act ? mixer.saveAct (name) : row.kind == Mixer::Preset::venue ? mixer.saveVenue (name) : mixer.saveMonitors (name);
                note = error.isNotEmpty() ? error : "Saved as '" + name + "'.";
            }

            noteTicks = 0;
            refreshPresetBoxes();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &p.label, &p.box, &p.load, &p.save })
            addAndMakeVisible (c);
    }

    // ---- the venue knob ----
    style::body (venueKnobLabel, "Venue knob (ENC 5)");
    venueKnobLabel.setTooltip ("These three buttons stand in for the fifth knob on the board. Turning only shows names; nothing changes until Push.");
    venueScreen.setFont (style::monoFont (13.0f));
    venueScreen.setColour (juce::Label::textColourId, style::textMain);
    venueScreen.setColour (juce::Label::backgroundColourId, style::panel);
    const auto knob = [this] (juce::String line)
    {
        lastKnobLine = std::move (line);
        lastKnobMs = juce::Time::getMillisecondCounterHiRes();
        refresh();
    };
    turnLeft.onClick = [this, knob]   { knob (mixer.turnVenue (-1, juce::Time::getMillisecondCounterHiRes())); };
    turnRight.onClick = [this, knob]  { knob (mixer.turnVenue (1, juce::Time::getMillisecondCounterHiRes())); };
    push.onClick = [this, knob]       { knob (mixer.pushVenue (juce::Time::getMillisecondCounterHiRes())); };

    // ---- the strips ----
    for (int i = 0; i < numStrips; ++i)
    {
        auto& s = strips[(size_t) i];
        s.kind = i < numMixChannels ? Kind::channel : i < numMixChannels + numMixFx ? Kind::fx : Kind::output;
        s.index = s.kind == Kind::channel ? i : s.kind == Kind::fx ? i - numMixChannels : i - numMixChannels - numMixFx;

        styleButton (s.select);
        s.select.setClickingTogglesState (false);
        s.select.onClick = [this, i] { choose (i); };

        s.fader.setSliderStyle (juce::Slider::LinearVertical);
        s.fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        s.fader.setRange (faderLowDb, 10.0, 0.1);
        s.fader.setSkewFactorFromMidPoint (-12.0);
        s.fader.setDoubleClickReturnValue (true, 0.0);
        s.fader.textFromValueFunction = [] (double v) { return levelWords (v); };
        s.fader.setColour (juce::Slider::trackColourId, style::green.withAlpha (0.6f));
        s.fader.setColour (juce::Slider::backgroundColourId, style::background);
        s.fader.setColour (juce::Slider::textBoxTextColourId, style::textMain);
        s.fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.fader.onValueChange = [this, i]
        {
            auto& strip = strips[(size_t) i];
            const float db = fromFader (strip.fader.getValue());

            if (strip.kind == Kind::channel)        { auto c = mixer.setup().channel[(size_t) strip.index]; c.faderDb = db; mixer.setChannel (strip.index, c); }
            else if (strip.kind == Kind::fx)        { auto f = mixer.setup().fx[(size_t) strip.index]; f.returnDb = db; mixer.setFx (strip.index, f); }
            else                                    { auto o = mixer.setup().output[(size_t) strip.index]; o.levelDb = db; mixer.setOutput (strip.index, o); }
        };

        styleButton (s.mute);
        s.mute.onClick = [this, i]
        {
            auto& strip = strips[(size_t) i];

            if (strip.kind == Kind::channel)        { auto c = mixer.setup().channel[(size_t) strip.index]; c.mute = ! c.mute; mixer.setChannel (strip.index, c); }
            else if (strip.kind == Kind::fx)        { auto f = mixer.setup().fx[(size_t) strip.index]; f.on = ! f.on; mixer.setFx (strip.index, f); }
            else                                    { auto o = mixer.setup().output[(size_t) strip.index]; o.mute = ! o.mute; mixer.setOutput (strip.index, o); }

            refreshStrips();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &s.select, &s.fader, &s.mute })
            addAndMakeVisible (c);
    }

    detailTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    detailTitle.setColour (juce::Label::textColourId, style::textMain);
    small (blockHint, {});

    blockOn.setColour (juce::ToggleButton::textColourId, style::textMain);
    blockOn.onClick = [this]
    {
        auto m = currentChain();

        if (block < (int) m.slots.size())
        {
            m.slots[(size_t) block].on = blockOn.getToggleState();
            setCurrentChain (m);

            for (int b = 0; b < blockButtons.size(); ++b)
                blockButtons[b]->setButtonText (blockName (m.slots[(size_t) b].type) + (m.slots[(size_t) b].on ? ": on" : ": off"));
        }
    };
    addAndMakeVisible (blockOn);

    // For screenshots in the cloud (no mouse there): GIGRIG_MIXER_STRIP=12 opens with the main output chosen, =12.3 with its fourth block shown.
    if (const auto strip = juce::SystemStats::getEnvironmentVariable ("GIGRIG_MIXER_STRIP", {}); strip.isNotEmpty())
    {
        chosen = juce::jlimit (0, numStrips - 1, strip.getIntValue());
        block = strip.fromFirstOccurrenceOf (".", false, false).getIntValue();
    }

    refresh();
    startTimerHz (15);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
    mixer.stopRecording();
    mixer.saveState();
}

//==============================================================================
juce::String MixerPanel::socketName (int socket) const
{
    const bool cq = engine.isCQ();
    auto name = cq && socket < 10 ? "Socket " + juce::String (socket + 1)
                                  : inputName (socket, cq).substring (0, 1).toUpperCase() + inputName (socket, cq).substring (1);
    auto& rack = engine.effects();

    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        const auto r = rack.routing (c);
        const int heard = r.looperHears == -2 ? r.source : r.looperHears;

        if (r.on && heard == socket)
            name << ", after the " << (r.name.isNotEmpty() ? "'" + r.name + "'" : "chain " + juce::String (c + 1)) << " effects";
    }

    return name;
}

juce::String MixerPanel::socketNote (int socket) const
{
    if (engine.isOpen() && socket >= engine.numInputs())
        return "socket " + juce::String (socket + 1) + " does not exist on this interface, so the channel is silent.";

    auto& rack = engine.effects();

    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        const auto r = rack.routing (c);

        if (r.on && r.source == socket && r.looperHears == -1)
            return "the " + (r.name.isNotEmpty() ? "'" + r.name + "'" : "chain " + juce::String (c + 1)) + " effects chain on socket " + juce::String (socket + 1)
                   + " is set to 'Not recorded' on the Effects tab, so the mixer gets the plain socket without its effects. Set 'Looper hears it as' to its own socket there.";
    }

    return {};
}

juce::String MixerPanel::reportText() const
{
    auto t = mixer.reportText();

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = mixer.setup().channel[(size_t) c];

        if (ch.on && ch.socket >= 0)
        {
            t << "Channel " << (c + 1) << " hears: " << socketName (ch.socket) << "\n";

            if (const auto n = socketNote (ch.socket); n.isNotEmpty())
                t << "MIXER WARNING: Channel " << (c + 1) << " (" << ch.name << "): " << n << "\n";
        }
    }

    return t;
}

const std::vector<fx::ParamInfo>& MixerPanel::paramsOf (const std::string& type)
{
    auto& example = examples[type];

    if (example == nullptr)
        example = fx::makeEffect (type);

    static const std::vector<fx::ParamInfo> none;
    return example != nullptr ? example->params() : none;
}

fx::ChainModel MixerPanel::currentChain() const
{
    const auto& s = strips[(size_t) chosen];
    const auto& now = mixer.setup();
    return s.kind == Kind::channel ? now.channel[(size_t) s.index].strip : s.kind == Kind::fx ? mixer.effectNow (s.index) : now.output[(size_t) s.index].chain;
}

void MixerPanel::setCurrentChain (const fx::ChainModel& m)
{
    const auto& s = strips[(size_t) chosen];

    if (s.kind == Kind::channel)        { auto c = mixer.setup().channel[(size_t) s.index]; c.strip = m; mixer.setChannel (s.index, c); }
    else if (s.kind == Kind::fx)
    {
        // A song's own reverb or delay is edited in the song's copy; the venue's is left alone.
        if (mixer.hasSongFx (s.index))     mixer.setSongFx (s.index, m);
        else                               { auto f = mixer.setup().fx[(size_t) s.index]; f.effect = m; mixer.setFx (s.index, f); }
    }
    else                                { auto o = mixer.setup().output[(size_t) s.index]; o.chain = m; mixer.setOutput (s.index, o); }
}

//==============================================================================
void MixerPanel::choose (int strip)
{
    chosen = juce::jlimit (0, numStrips - 1, strip);
    block = strips[(size_t) chosen].kind == Kind::channel ? stripEq : 0;       // a channel opens on its EQ
    refreshStrips();
    rebuildDetail();
}

void MixerPanel::refresh()
{
    onButton.setToggleState (mixer.isOn(), juce::dontSendNotification);
    refreshPresetBoxes();
    refreshStrips();
    rebuildDetail();
}

void MixerPanel::refreshPresetBoxes()
{
    for (auto& p : presets)
    {
        const auto names = mixer.presetNames (p.kind);
        const auto current = p.kind == Mixer::Preset::act ? mixer.actName() : p.kind == Mixer::Preset::venue ? mixer.venueName() : mixer.monitorsName();
        p.box.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            p.box.addItem (names[i], i + 1);

        p.box.setText (current, juce::dontSendNotification);
    }
}

void MixerPanel::refreshStrips()
{
    const auto& now = mixer.setup();

    for (int i = 0; i < numStrips; ++i)
    {
        auto& s = strips[(size_t) i];
        s.select.setToggleState (i == chosen, juce::dontSendNotification);

        if (s.kind == Kind::channel)
        {
            const auto& c = now.channel[(size_t) s.index];
            s.select.setButtonText (c.name.isNotEmpty() ? c.name : "Ch " + juce::String (s.index + 1));
            s.select.setColour (juce::TextButton::textColourOffId, c.on ? style::textMain : style::textDim.withAlpha (0.6f));
            s.fader.setValue (toFader (c.faderDb), juce::dontSendNotification);
            s.fader.setEnabled (c.on);
            s.mute.setEnabled (c.on);
            s.mute.setButtonText (! c.on ? "Off" : c.mute ? "MUTED" : "Mute");
            s.mute.setColour (juce::TextButton::buttonColourId, c.on && c.mute ? style::red.withAlpha (0.7f) : style::panel);
        }
        else if (s.kind == Kind::fx)
        {
            const auto& f = now.fx[(size_t) s.index];
            s.select.setButtonText (f.name.isNotEmpty() ? f.name : "Effect " + juce::String (s.index + 1));
            s.fader.setValue (toFader (f.returnDb), juce::dontSendNotification);
            s.mute.setButtonText (f.on ? "On" : "Off");
            s.mute.setColour (juce::TextButton::buttonColourId, f.on ? style::green.withAlpha (0.45f) : style::panel);
        }
        else
        {
            const auto& o = now.output[(size_t) s.index];
            s.select.setButtonText (s.index == 0 ? juce::String ("Main") : "Mon " + juce::String (s.index) + (o.name.isNotEmpty() ? " " + o.name : juce::String()));
            s.fader.setValue (toFader (o.levelDb), juce::dontSendNotification);
            s.mute.setButtonText (o.mute ? "MUTED" : "Mute");
            s.mute.setColour (juce::TextButton::buttonColourId, o.mute ? style::red.withAlpha (0.7f) : style::panel);
        }
    }

    static const char* const words[3] { "Act", "Venue / PA", "Monitors" };
    const bool changed[3] { mixer.actChanged(), mixer.venueChanged(), mixer.monitorsChanged() };

    for (int i = 0; i < 3; ++i)
    {
        presets[(size_t) i].label.setText (juce::String (words[i]) + (changed[i] ? " *" : ""), juce::dontSendNotification);
        presets[(size_t) i].label.setColour (juce::Label::textColourId, changed[i] ? style::amber : style::textMain);
    }
}

//==============================================================================
juce::Slider* MixerPanel::addSlider (int row, int width, const juce::String& label, double min, double max, double step, const juce::String& suffix,
                                     double value, double def, std::function<void (float)> onChange)
{
    auto s = std::make_unique<juce::Slider>();
    styleBar (*s);
    s->setRange (min, max, step);
    s->setTextValueSuffix (suffix);
    s->setDoubleClickReturnValue (true, def);
    s->setValue (value, juce::dontSendNotification);
    auto* raw = s.get();
    s->onValueChange = [raw, change = std::move (onChange)] { change ((float) raw->getValue()); };

    Field f;
    f.label = std::make_unique<juce::Label>();
    small (*f.label, label);
    f.control = std::move (s);
    f.width = width;
    f.row = row;
    addAndMakeVisible (*f.label);
    addAndMakeVisible (*f.control);
    fields.push_back (std::move (f));
    return raw;
}

juce::Slider* MixerPanel::addSend (int row, int width, const juce::String& label, float valueDb, std::function<void (float)> onChange)
{
    auto* s = addSlider (row, width, label, faderLowDb, 10.0, 0.1, {}, toFader (valueDb), faderLowDb, [change = std::move (onChange)] (float v) { change (fromFader (v)); });
    s->setSkewFactorFromMidPoint (-12.0);
    s->textFromValueFunction = [] (double v) { return levelWords (v); };
    s->updateText();
    return s;
}

juce::ComboBox* MixerPanel::addCombo (int row, int width, const juce::String& label, const std::vector<std::pair<int, juce::String>>& items, int selectedId, std::function<void (int)> onChange)
{
    auto b = std::make_unique<juce::ComboBox>();
    style::combo (*b);

    for (const auto& item : items)
        b->addItem (item.second, item.first);

    b->setSelectedId (selectedId, juce::dontSendNotification);
    auto* raw = b.get();
    if (onChange != nullptr)
        b->onChange = [raw, change = std::move (onChange)] { change (raw->getSelectedId()); };

    Field f;
    f.label = std::make_unique<juce::Label>();
    small (*f.label, label);
    f.control = std::move (b);
    f.width = width;
    f.row = row;
    addAndMakeVisible (*f.label);
    addAndMakeVisible (*f.control);
    fields.push_back (std::move (f));
    return raw;
}

void MixerPanel::addToggle (int row, int width, const juce::String& label, const juce::String& text, bool state, std::function<void (bool)> onChange)
{
    auto t = std::make_unique<juce::ToggleButton> (text);
    t->setColour (juce::ToggleButton::textColourId, style::textMain);
    t->setToggleState (state, juce::dontSendNotification);
    auto* raw = t.get();
    t->onClick = [raw, change = std::move (onChange)] { change (raw->getToggleState()); };

    Field f;
    f.label = std::make_unique<juce::Label>();
    small (*f.label, label);
    f.control = std::move (t);
    f.width = width;
    f.row = row;
    addAndMakeVisible (*f.label);
    addAndMakeVisible (*f.control);
    fields.push_back (std::move (f));
}

void MixerPanel::addText (int row, int width, const juce::String& label, const juce::String& text, std::function<void (juce::String)> onChange)
{
    auto e = std::make_unique<juce::TextEditor>();
    e->setColour (juce::TextEditor::backgroundColourId, style::panel);
    e->setColour (juce::TextEditor::textColourId, style::textMain);
    e->setColour (juce::TextEditor::outlineColourId, style::textDim.withAlpha (0.4f));
    e->setText (text, false);
    e->setInputRestrictions (24);
    auto* raw = e.get();
    const auto done = [raw, change = std::move (onChange)] { change (raw->getText().trim()); };
    e->onReturnKey = done;
    e->onFocusLost = done;

    Field f;
    f.label = std::make_unique<juce::Label>();
    small (*f.label, label);
    f.control = std::move (e);
    f.width = width;
    f.row = row;
    addAndMakeVisible (*f.label);
    addAndMakeVisible (*f.control);
    fields.push_back (std::move (f));
}

void MixerPanel::addButton (int row, int width, const juce::String& text, std::function<void()> onClick)
{
    auto b = std::make_unique<juce::TextButton> (text);
    styleButton (*b);
    b->onClick = std::move (onClick);

    Field f;
    f.label = std::make_unique<juce::Label>();
    f.control = std::move (b);
    f.width = width;
    f.row = row;
    addAndMakeVisible (*f.label);
    addAndMakeVisible (*f.control);
    fields.push_back (std::move (f));
}

//==============================================================================
void MixerPanel::rebuildDetail()
{
    fields.clear();
    outputPresetBox = nullptr;

    const auto& now = mixer.setup();
    const auto& s = strips[(size_t) chosen];
    const int index = s.index;

    // A control that changes what is on screen rebuilds it a moment later, never from inside its own click.
    const auto later = [safe = juce::Component::SafePointer<MixerPanel> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->refresh(); });
    };

    std::vector<std::pair<int, juce::String>> returns { { noneId, "None" } };
    for (int r = 0; r < numReturns; ++r)
        returns.push_back ({ r + 1, returnName (r) });

    if (s.kind == Kind::channel)
    {
        const auto& c = now.channel[(size_t) index];
        const bool rightSide = index > 0 && now.channel[(size_t) index - 1].link;
        const int lead = rightSide ? index - 1 : index;
        detailTitle.setText ("Channel " + juce::String (index + 1) + ": " + c.name + (c.on ? "" : "  (off)")
                             + (rightSide ? "   (the right side of a stereo pair with channel " + juce::String (index) + ": they share every setting but the name and the socket)"
                                : c.link ? "   (the left side of a stereo pair with channel " + juce::String (index + 2) + ")" : juce::String()), juce::dontSendNotification);

        const auto change = [this, index] (std::function<void (MixChannelSetup&)> edit)
        {
            auto next = mixer.setup().channel[(size_t) index];
            edit (next);
            mixer.setChannel (index, next);
            refreshStrips();
        };

        addText (0, 120, "Name", c.name, [change] (juce::String t) { change ([&t] (MixChannelSetup& n) { n.name = t; }); });
        addToggle (0, 64, "In use", "On", c.on, [change, later] (bool on) { change ([on] (MixChannelSetup& n) { n.on = on; }); later(); });

        std::vector<std::pair<int, juce::String>> sockets { { noneId, "No socket" } };
        const int count = engine.isCQ() ? 10 : engine.isOpen() ? juce::jmin (numMixSockets, engine.numInputs()) : numMixSockets;
        for (int i = 0; i < juce::jmax (count, c.socket + 1); ++i)
            sockets.push_back ({ i + 1, socketName (i) });

        addCombo (0, 250, "Comes from (after its effects chain)", sockets, c.socket >= 0 ? c.socket + 1 : noneId,
                  [change] (int id) { change ([id] (MixChannelSetup& n) { n.socket = id == noneId ? -1 : id - 1; }); });
        addSlider (0, 100, "Trim", -24.0, 24.0, 0.1, " dB", c.trimDb, 0.0, [change] (float v) { change ([v] (MixChannelSetup& n) { n.trimDb = v; }); });
        auto* pan = addSlider (0, 100, "Pan", -1.0, 1.0, 0.01, {}, c.pan, 0.0, [change] (float v) { change ([v] (MixChannelSetup& n) { n.pan = v; }); });
        pan->textFromValueFunction = [] (double v) { return std::abs (v) < 0.005 ? juce::String ("centre") : juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0.0 ? "% left" : "% right"); };
        pan->updateText();
        addCombo (0, 150, "When recording, record", { { 1, "Nothing" }, { 2, "The plain socket" }, { 3, "After its effects" }, { 4, "After the looper" } }, (int) c.record + 1,
                  [change] (int id) { change ([id] (MixChannelSetup& n) { n.record = (RecordPoint) juce::jlimit (0, 3, id - 1); }); });
        addCombo (0, 150, "Direct out for a sound crew", returns, c.directReturn >= 0 ? c.directReturn + 1 : noneId,
                  [change] (int id) { change ([id] (MixChannelSetup& n) { n.directReturn = id == noneId ? -1 : id - 1; }); });

        for (int m = 0; m < numMixMonitors; ++m)
        {
            const auto& out = now.output[(size_t) m + 1];
            addSend (1, 150, "To monitor " + juce::String (m + 1) + (out.name.isNotEmpty() ? " (" + out.name + ")" : juce::String()), c.monDb[(size_t) m],
                     [change, m] (float v) { change ([v, m] (MixChannelSetup& n) { n.monDb[(size_t) m] = v; }); });
        }

        bool songs = false;
        for (int f = 0; f < numMixFx; ++f)
            songs = songs || mixer.hasSongSend (lead, f);

        for (int f = 0; f < numMixFx; ++f)
            addSend (1, 150, "To " + now.fx[(size_t) f].name + (mixer.hasSongSend (lead, f) ? " (song)" : ""), mixer.sendDb (index, f),
                     [this, change, f, lead] (float v)
                     {
                         if (mixer.hasSongSend (lead, f))   mixer.setSongSend (lead, f, v);
                         else                               change ([v, f] (MixChannelSetup& n) { n.fxDb[(size_t) f] = v; });
                     });

        if (! rightSide && index + 1 < numMixChannels)
            addToggle (2, 230, "Stereo (keys, a backing track)", "Stereo with channel " + juce::String (index + 2), c.link,
                       [change, later] (bool on) { change ([on] (MixChannelSetup& n) { n.link = on; }); later(); });

        addToggle (2, 330, "Reverb and delay sends (the four on the right above)", "Belong to this song, not the act", songs, [this, lead, later] (bool on)
        {
            for (int f = 0; f < numMixFx; ++f)
            {
                if (on)     mixer.setSongSend (lead, f, mixer.sendDb (lead, f));
                else        mixer.clearSongSend (lead, f);
            }

            note = on ? "These four sends now belong to the song that is loaded. Set them, then press 'Keep these sounds for this song' on the Songs tab. Another song puts them back to the act's."
                      : "These four sends are back to the act's own. Press 'Keep these sounds for this song' on the Songs tab if the song's file should forget them too.";
            noteTicks = 0;
            later();
        });
    }
    else if (s.kind == Kind::fx)
    {
        const auto& f = now.fx[(size_t) index];
        detailTitle.setText ("Mixer effect " + juce::String (index + 1) + ": " + f.name + "   (after the looper: never recorded into a loop)", juce::dontSendNotification);

        const auto change = [this, index] (std::function<void (MixFxSetup&)> edit)
        {
            auto next = mixer.setup().fx[(size_t) index];
            edit (next);
            mixer.setFx (index, next);
            refreshStrips();
        };

        addText (0, 140, "Name", f.name, [change, later] (juce::String t) { change ([&t] (MixFxSetup& n) { n.name = t; }); });

        std::vector<std::pair<int, juce::String>> kinds;
        int selected = 0;
        for (size_t k = 0; k < mixerFxChoices().size(); ++k)
        {
            kinds.push_back ({ (int) k + 1, blockName (mixerFxChoices()[k]) });
            if (! mixer.effectNow (index).slots.empty() && mixer.effectNow (index).slots[0].type == mixerFxChoices()[k])
                selected = (int) k + 1;
        }

        addCombo (0, 200, mixer.hasSongFx (index) ? "Effect (this song's)" : "Effect", kinds, selected, [this, index, change, later] (int id)
        {
            if (id >= 1 && id <= (int) mixerFxChoices().size())
            {
                fx::SlotModel slot;
                slot.type = mixerFxChoices()[(size_t) id - 1];

                if (mixer.hasSongFx (index))
                {
                    fx::ChainModel m;
                    m.slots = { slot };
                    mixer.setSongFx (index, m);
                    refreshStrips();
                }
                else
                    change ([slot] (MixFxSetup& n) { n.effect.slots = { slot }; });

                later();
            }
        });

        addToggle (2, 360, "Which reverb or delay this is, and its own settings", "Belong to this song, not the venue", mixer.hasSongFx (index), [this, index, later] (bool on)
        {
            if (on)     mixer.setSongFx (index, mixer.effectNow (index));
            else        mixer.clearSongFx (index);

            note = on ? "This effect's own settings now belong to the song that is loaded. Set them, then press 'Keep these sounds for this song' on the Songs tab. Another song puts the venue's back."
                      : "This effect is back to the venue's own. Press 'Keep these sounds for this song' on the Songs tab if the song's file should forget its own too.";
            noteTicks = 0;
            later();
        });

        for (const bool asDefault : { false, true })
            addButton (2, asDefault ? 130 : 140, asDefault ? "Save as default" : "Save to this song", [this, index, asDefault, later]
            {
                note = onKeepEffect != nullptr ? onKeepEffect (index, asDefault) : juce::String();
                noteTicks = 0;
                later();
            });

        addButton (2, 200, "Back to default for this song", [this, index, later]
        {
            note = onDropEffect != nullptr ? onDropEffect (index) : juce::String();
            noteTicks = 0;
            later();
        });

        auto* low = addSlider (0, 150, "Low cut on the wet sound", 20.0, 1000.0, 1.0, " Hz", f.lowCutHz, 20.0, [change] (float v) { change ([v] (MixFxSetup& n) { n.lowCutHz = v; }); });
        low->setSkewFactorFromMidPoint (140.0);
        auto* high = addSlider (0, 150, "High cut on the wet sound", 1000.0, 20000.0, 10.0, " Hz", f.highCutHz, 20000.0, [change] (float v) { change ([v] (MixFxSetup& n) { n.highCutHz = v; }); });
        high->setSkewFactorFromMidPoint (4500.0);

        for (int m = 0; m < numMixMonitors; ++m)
            addSend (0, 140, "Into monitor " + juce::String (m + 1), f.monDb[(size_t) m], [change, m] (float v) { change ([v, m] (MixFxSetup& n) { n.monDb[(size_t) m] = v; }); });

        // The EQ on what goes into the effect (Tom, 27 Nov 2026): the low and high cuts above do the
        // filtering, these shape what the reverb or delay is fed.
        const auto eqNow = [&f] (const char* id, float fallback) { return f.eq.slots.empty() ? fallback : f.eq.slots[0].param (id, fallback); };
        const auto eqSet = [change] (const char* id)
        {
            return [change, id] (float v)
            {
                change ([v, id] (MixFxSetup& n)
                {
                    if (n.eq.slots.empty())
                    {
                        fx::SlotModel slot;
                        slot.type = "mixeq";
                        slot.on = true;
                        n.eq.slots = { slot };
                    }

                    n.eq.slots[0].setParam (id, v);
                });
            };
        };

        addSlider (1, 130, "Into it: low shelf", -15.0, 15.0, 0.1, " dB", eqNow ("lsg", 0.0f), 0.0, eqSet ("lsg"));
        addSlider (1, 130, "Into it: band A at", 150.0, 3000.0, 1.0, " Hz", eqNow ("b2f", 800.0f), 800.0, eqSet ("b2f"))->setSkewFactorFromMidPoint (700.0);
        addSlider (1, 130, "Band A", -15.0, 15.0, 0.1, " dB", eqNow ("b2g", 0.0f), 0.0, eqSet ("b2g"));
        addSlider (1, 130, "Into it: band B at", 500.0, 8000.0, 1.0, " Hz", eqNow ("b3f", 2500.0f), 2500.0, eqSet ("b3f"))->setSkewFactorFromMidPoint (2500.0);
        addSlider (1, 130, "Band B", -15.0, 15.0, 0.1, " dB", eqNow ("b3g", 0.0f), 0.0, eqSet ("b3g"));
        addSlider (1, 130, "Into it: high shelf", -15.0, 15.0, 0.1, " dB", eqNow ("hsg", 0.0f), 0.0, eqSet ("hsg"));
    }
    else
    {
        const auto& o = now.output[(size_t) index];
        detailTitle.setText (mixOutputName (index) + " output" + (index == 0 ? "   (its processing and level, and the mixer effects, are the venue preset)" : "   (both monitors are the monitor set-up)"),
                             juce::dontSendNotification);

        const auto change = [this, index] (std::function<void (MixOutputSetup&)> edit)
        {
            auto next = mixer.setup().output[(size_t) index];
            edit (next);
            mixer.setOutput (index, next);
            refreshStrips();
        };

        if (index > 0)
            addText (0, 110, "Whose it is", o.name, [change] (juce::String t) { change ([&t] (MixOutputSetup& n) { n.name = t; }); });

        returns[0].second = "Not connected";
        addCombo (0, 200, "Goes out on", returns, o.firstReturn >= 0 ? o.firstReturn + 1 : noneId,
                  [change] (int id) { change ([id] (MixOutputSetup& n) { n.firstReturn = id == noneId ? -1 : id - 1; }); });
        addToggle (0, 150, "Two returns", "And the next one", o.pair, [change] (bool on) { change ([on] (MixOutputSetup& n) { n.pair = on; }); });
        addToggle (0, 80, "Width", "Mono", o.mono, [change] (bool on) { change ([on] (MixOutputSetup& n) { n.mono = on; }); });

        if (index > 0)
        {
            addToggle (0, 150, "Its sends are taken", "Before the faders", o.preFader, [change] (bool on) { change ([on] (MixOutputSetup& n) { n.preFader = on; }); });
            addCombo (0, 190, "From where in each channel", { { 1, "Before its processing" }, { 2, "After its gate and EQ" }, { 3, "After its compressors" } }, (int) o.tap + 1,
                      [change] (int id) { change ([id] (MixOutputSetup& n) { n.tap = (TapPoint) juce::jlimit (0, 2, id - 1); }); });
        }

        std::vector<std::pair<int, juce::String>> names;
        const auto list = mixer.presetNames (Mixer::Preset::output);
        for (int i = 0; i < list.size(); ++i)
            names.push_back ({ i + 1, list[i] });

        outputPresetBox = addCombo (1, 220, "Output processing preset (loads into any output)", names, 0, nullptr);
        outputPresetBox->setEditableText (true);
        addButton (1, 60, "Load", [this, index, later]
        {
            const auto name = outputPresetBox != nullptr ? outputPresetBox->getText().trim() : juce::String();
            const auto error = name.isEmpty() ? juce::String ("Choose an output preset in the list first.") : mixer.loadOutput (index, name);
            note = error.isNotEmpty() ? error : "Output preset '" + name + "' loaded into the " + mixOutputName (index).toLowerCase() + " output.";
            noteTicks = 0;
            later();
        });
        addButton (1, 60, "Save", [this, index, later]
        {
            const auto name = outputPresetBox != nullptr ? outputPresetBox->getText().trim() : juce::String();
            const auto error = name.isEmpty() ? juce::String ("Type a name in the box first.") : mixer.saveOutput (index, name);
            note = error.isNotEmpty() ? error : "This output's processing is saved as '" + name + "'.";
            noteTicks = 0;
            later();
        });
    }

    // ---- which block's sliders are shown ----
    const auto chain = currentChain();
    block = juce::jlimit (0, juce::jmax (0, (int) chain.slots.size() - 1), block);
    blockButtons.clear();

    if (s.kind != Kind::fx)
        for (int b = 0; b < (int) chain.slots.size(); ++b)
        {
            auto* button = blockButtons.add (new juce::TextButton (blockName (chain.slots[(size_t) b].type) + (chain.slots[(size_t) b].on ? ": on" : ": off")));
            styleButton (*button);
            button->setToggleState (b == block, juce::dontSendNotification);
            button->onClick = [this, b]
            {
                block = b;

                for (int k = 0; k < blockButtons.size(); ++k)
                    blockButtons[k]->setToggleState (k == block, juce::dontSendNotification);

                rebuildGrid();
            };
            addAndMakeVisible (button);
        }

    rebuildGrid();
}

void MixerPanel::rebuildGrid()
{
    gridLabels.clear();
    gridControls.clear();

    const auto chain = currentChain();
    const bool isFx = strips[(size_t) chosen].kind == Kind::fx;
    blockOn.setVisible (! isFx && ! chain.slots.empty());

    if (block >= (int) chain.slots.size())
    {
        blockHint.setText ({}, juce::dontSendNotification);
        layoutDetail();
        return;
    }

    const auto& slot = chain.slots[(size_t) block];
    blockOn.setToggleState (slot.on, juce::dontSendNotification);
    blockHint.setText (blockWords (slot.type), juce::dontSendNotification);
    gridColumns = slot.type == "notches" ? 4 : 3;
    const int b = block;

    for (const auto& info : paramsOf (slot.type))
    {
        auto* label = gridLabels.add (new juce::Label());
        small (*label, info.label, style::textMain);
        addAndMakeVisible (label);

        const auto id = info.id;
        const auto set = [this, b, id] (float v)
        {
            auto m = currentChain();

            if (b < (int) m.slots.size())
            {
                m.slots[(size_t) b].setParam (id, v);
                setCurrentChain (m);
            }
        };

        const float value = slot.param (info.id, info.def);

        if (info.kind == fx::ParamInfo::Kind::continuous)
        {
            auto* slider = new juce::Slider();
            gridControls.add (slider);
            styleBar (*slider);
            const auto range = info.max - info.min;
            slider->setRange (info.min, info.max, range > 2000.0f ? 1.0 : range > 20.0f ? 0.1 : range > 2.0f ? 0.05 : 0.005);
            if (info.unit == "Hz" && info.min > 0.0f)
                slider->setSkewFactorFromMidPoint (std::sqrt ((double) info.min * (double) info.max));
            slider->setTextValueSuffix (info.unit.empty() ? "" : " " + juce::String (info.unit));
            slider->setDoubleClickReturnValue (true, info.def);
            slider->setValue (value, juce::dontSendNotification);
            slider->onValueChange = [slider, set] { set ((float) slider->getValue()); };
            addAndMakeVisible (slider);
        }
        else if (info.kind == fx::ParamInfo::Kind::choice)
        {
            auto* box = new juce::ComboBox();
            gridControls.add (box);
            style::combo (*box);
            for (size_t k = 0; k < info.choices.size(); ++k)
                box->addItem (info.choices[k], (int) k + 1);
            box->setSelectedId (juce::roundToInt (value) + 1, juce::dontSendNotification);
            box->onChange = [box, set] { set ((float) (box->getSelectedId() - 1)); };
            addAndMakeVisible (box);
        }
        else
        {
            auto* t = new juce::ToggleButton ("On");
            gridControls.add (t);
            t->setColour (juce::ToggleButton::textColourId, style::textMain);
            t->setToggleState (value >= 0.5f, juce::dontSendNotification);
            t->onClick = [t, set] { set (t->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (t);
        }
    }

    layoutDetail();
}

//==============================================================================
void MixerPanel::timerCallback()
{
    mixer.tick();
    ++ticks;

    const auto db = [] (float peak) { return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f; };

    for (auto& s : strips)
    {
        std::array<float, 2> peak { 0.0f, 0.0f };
        float down = 0.0f;

        if (s.kind == Kind::channel)        { peak[0] = peak[1] = mixer.takeChannelPeak (s.index); down = mixer.channelCompDb (s.index); }
        else if (s.kind == Kind::fx)        { peak[0] = peak[1] = mixer.takeFxPeak (s.index); }
        else
        {
            peak[0] = mixer.takeOutputPeak (s.index, 0);
            peak[1] = mixer.takeOutputPeak (s.index, 1);
            down = mixer.outputCompDb (s.index) + mixer.outputLimiterDb (s.index);
        }

        for (size_t side = 0; side < 2; ++side)
            s.shownDb[side] = juce::jmax (db (peak[side]), s.shownDb[side] - 2.0f);

        s.turnDownDb = std::abs (down);
    }

    repaint (stripsArea);

    if (ticks % 3 == 0)
    {
        const auto l = mixer.load();
        juce::String status = mixer.isOn() ? "Mixing. " : "Off. ";
        status << "Processor: " << juce::String (l.average * 100.0, 1) << "% of each block (busiest " << juce::String (l.busiest * 100.0, 1) << "%).  " << mixer.recorder().statusText();

        recordButton.setButtonText (mixer.recorder().isRecording() ? "Stop recording" : "Record");
        recordButton.setColour (juce::TextButton::buttonColourId, mixer.recorder().isRecording() ? style::red.withAlpha (0.7f) : style::panel);
        resetButton.setVisible (mixer.guardTripped());

        const auto warnings = mixer.warnings ([this] (int socket) { return socketNote (socket); });
        const float stray = mixer.takeStrayPeak();
        auto text = warnings.joinIntoString ("  ");
        if (stray > 0.001f)
            text << (text.isNotEmpty() ? "  " : "") << "A loop is playing that was recorded from a socket no channel listens to now: it is in the main mix at its own level, with no fader.";
        warningLabel.setText (text, juce::dontSendNotification);

        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        venueScreen.setText (nowMs - lastKnobMs < Mixer::browseTimeoutMs && lastKnobLine.isNotEmpty() ? lastKnobLine : mixer.venueScreen (nowMs), juce::dontSendNotification);

        if (++noteTicks > 60)
            note.clear();

        noteLabel.setText (note.isNotEmpty() ? note : status, juce::dontSendNotification);
        noteLabel.setColour (juce::Label::textColourId, note.isNotEmpty() ? style::textMain : style::textDim);

        if (onButton.getToggleState() != mixer.isOn())
            onButton.setToggleState (mixer.isOn(), juce::dontSendNotification);

        // The stars beside the preset names follow every change.
        static const char* const words[3] { "Act", "Venue / PA", "Monitors" };
        const bool changed[3] { mixer.actChanged(), mixer.venueChanged(), mixer.monitorsChanged() };
        for (int i = 0; i < 3; ++i)
        {
            presets[(size_t) i].label.setText (juce::String (words[i]) + (changed[i] ? " *" : ""), juce::dontSendNotification);
            presets[(size_t) i].label.setColour (juce::Label::textColourId, changed[i] ? style::amber : style::textMain);
        }
    }

    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy mixer results");
}

//==============================================================================
void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);

    // Group headings over the strips.
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.setColour (style::textDim);
    const auto heading = [&] (int first, int last, const juce::String& text)
    {
        const auto a = strips[(size_t) first].area.getUnion (strips[(size_t) last].area);
        g.drawText (text, a.getX(), stripsArea.getY(), a.getWidth(), 14, juce::Justification::centredLeft);
        g.setColour (style::panel.brighter (0.05f));
        g.fillRoundedRectangle (a.toFloat().expanded (2.0f, 2.0f), 4.0f);
        g.setColour (style::textDim);
    };
    heading (0, numMixChannels - 1, "CHANNELS (each with its own loops)");
    heading (numMixChannels, numMixChannels + numMixFx - 1, "REVERBS AND DELAYS");
    heading (numMixChannels + numMixFx, numStrips - 1, "OUTPUTS");

    for (int i = 0; i < numStrips; ++i)
    {
        const auto& s = strips[(size_t) i];

        if (i == chosen)
        {
            g.setColour (style::green.withAlpha (0.8f));
            g.drawRoundedRectangle (s.area.toFloat().expanded (1.0f, 1.0f), 4.0f, 1.5f);
        }

        // The meter: one bar (two for an output), green to -12, amber to -3, red above; the amber bar from the top is how far the compressor and limiter turn down.
        const int bars = s.kind == Kind::output ? 2 : 1;
        const float barWidth = (float) (s.meter.getWidth() - 4) / (float) bars;

        for (int b = 0; b < bars; ++b)
        {
            const auto bar = juce::Rectangle<float> ((float) s.meter.getX() + (float) b * barWidth, (float) s.meter.getY(), barWidth - 1.0f, (float) s.meter.getHeight());
            g.setColour (style::background);
            g.fillRect (bar);
            const float dbNow = s.shownDb[(size_t) b];
            const float frac = juce::jlimit (0.0f, 1.0f, (dbNow + 60.0f) / 66.0f);
            g.setColour (dbNow > -3.0f ? style::red : dbNow > -12.0f ? style::amber : style::green);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * frac));
        }

        if (s.kind == Kind::channel)
        {
            // The level guide: with Trim right, loud playing peaks between the two marks
            // (-18 and -9 dB), which is where the compressors, tape and limiter are made to work.
            const auto y = [&s] (float dbAt) { return (float) s.meter.getBottom() - (float) s.meter.getHeight() * (dbAt + 60.0f) / 66.0f; };
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> ((float) s.meter.getX(), y (-9.0f), (float) s.meter.getWidth() - 4.0f, y (-18.0f) - y (-9.0f)));
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect ((float) s.meter.getX(), y (-9.0f), (float) s.meter.getWidth() - 4.0f, 1.0f);
            g.fillRect ((float) s.meter.getX(), y (-18.0f), (float) s.meter.getWidth() - 4.0f, 1.0f);
        }

        if (s.kind != Kind::fx)
        {
            const auto bar = juce::Rectangle<float> ((float) s.meter.getRight() - 3.0f, (float) s.meter.getY(), 3.0f, (float) s.meter.getHeight());
            g.setColour (style::background);
            g.fillRect (bar);
            g.setColour (style::amber);
            g.fillRect (bar.withHeight (bar.getHeight() * juce::jlimit (0.0f, 1.0f, s.turnDownDb / 20.0f)));
        }
    }
}

void MixerPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 4);

    auto top = area.removeFromTop (26);
    onButton.setBounds (top.removeFromLeft (280));
    copyButton.setBounds (top.removeFromRight (140));
    top.removeFromRight (6);
    folderButton.setBounds (top.removeFromRight (140));
    top.removeFromRight (6);
    recordFolderButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (6);
    recordButton.setBounds (top.removeFromRight (110));

    area.removeFromTop (4);
    auto row = area.removeFromTop (26);
    const int each = (row.getWidth() - 16) / 3;

    for (auto& p : presets)
    {
        auto r = row.removeFromLeft (each);
        row.removeFromLeft (8);
        p.label.setBounds (r.removeFromLeft (86));
        p.save.setBounds (r.removeFromRight (48));
        r.removeFromRight (4);
        p.load.setBounds (r.removeFromRight (48));
        r.removeFromRight (4);
        p.box.setBounds (r);
    }

    area.removeFromTop (4);
    row = area.removeFromTop (26);
    venueKnobLabel.setBounds (row.removeFromLeft (140));
    turnLeft.setBounds (row.removeFromLeft (78));
    row.removeFromLeft (4);
    turnRight.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    push.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (8);
    venueScreen.setBounds (row);

    area.removeFromTop (2);
    noteLabel.setBounds (area.removeFromTop (18));
    auto warn = area.removeFromTop (30);
    resetButton.setBounds (warn.removeFromRight (70).reduced (0, 3));
    warningLabel.setBounds (warn);

    // ---- the strips ----
    area.removeFromTop (2);
    stripsArea = area.removeFromTop (juce::jmax (150, area.getHeight() - 330));
    auto stripRow = stripsArea.withTrimmedTop (18).reduced (2, 2);
    const int groupGap = 12;
    const int width = (stripRow.getWidth() - 2 * groupGap) / numStrips;

    for (int i = 0; i < numStrips; ++i)
    {
        if (i == numMixChannels || i == numMixChannels + numMixFx)
            stripRow.removeFromLeft (groupGap);

        auto& s = strips[(size_t) i];
        s.area = stripRow.removeFromLeft (width).reduced (2, 0);
        auto a = s.area.reduced (2, 2);
        s.select.setBounds (a.removeFromTop (24));
        s.mute.setBounds (a.removeFromBottom (22));
        a.removeFromTop (2);
        a.removeFromBottom (2);
        s.meter = a.removeFromRight (13).withTrimmedBottom (18);
        s.fader.setBounds (a);
    }

    area.removeFromTop (6);
    detailArea = area;
    layoutDetail();
}

void MixerPanel::layoutDetail()
{
    auto area = detailArea;

    if (area.isEmpty())
        return;

    detailTitle.setBounds (area.removeFromTop (20));

    int rows = 0;
    for (const auto& f : fields)
        rows = juce::jmax (rows, f.row + 1);

    for (int r = 0; r < rows; ++r)
    {
        auto row = area.removeFromTop (40);

        for (auto& f : fields)
            if (f.row == r)
            {
                auto cell = row.removeFromLeft (f.width);
                row.removeFromLeft (8);
                f.label->setBounds (cell.removeFromTop (15));
                f.control->setBounds (cell.reduced (0, 1));
            }

        area.removeFromTop (2);
    }

    area.removeFromTop (2);
    auto blockRow = area.removeFromTop (24);

    for (auto* b : blockButtons)
    {
        b->setBounds (blockRow.removeFromLeft (blockButtons.size() > 6 ? 112 : blockButtons.size() > 4 ? 128 : 150));
        blockRow.removeFromLeft (4);
    }

    if (blockOn.isVisible())
    {
        blockRow.removeFromLeft (8);
        blockOn.setBounds (blockRow.removeFromLeft (130));
    }

    blockHint.setBounds (area.removeFromTop (18));
    area.removeFromTop (2);

    const int columns = juce::jmax (1, gridColumns);
    const int cellWidth = area.getWidth() / columns;
    const int labelWidth = columns == 4 ? 96 : 150;

    for (int i = 0; i < gridControls.size(); ++i)
    {
        juce::Rectangle<int> cell (area.getX() + (i % columns) * cellWidth, area.getY() + (i / columns) * 23, cellWidth - 10, 21);
        gridLabels[i]->setBounds (cell.removeFromLeft (labelWidth));
        gridControls[i]->setBounds (cell);
    }
}

} // namespace gigrig
