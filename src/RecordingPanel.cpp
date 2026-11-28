#include "RecordingPanel.h"

#include "effects/Registry.h"

namespace gigrig
{

namespace
{
    constexpr double lowDb = -30.0;         // the bottom of a recording mix level: not in the mix at all

    float fromLevel (double v)      { return v <= lowDb + 0.05 ? mixOffDb : (float) v; }
    double toLevel (float db)       { return juce::jmax (lowDb, (double) db); }

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

    void levelBar (juce::Slider& s, const juce::String& tip)
    {
        styleBar (s);
        s.setRange (lowDb, 12.0, 0.5);
        s.setDoubleClickReturnValue (true, 0.0);
        s.textFromValueFunction = [] (double v) { return v <= lowDb + 0.05 ? juce::String ("not in it") : (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB"; };
        s.setTooltip (tip);
    }

    const char* const blockNames[numRecBlocks] { "EQ", "Bus compressor", "Limiter" };
    const char* const blockWords[numRecBlocks] {
        "The recording mix's own EQ curve: low cut, low shelf, four bells you can sweep, high shelf, high cut. It never touches what the room hears.",
        "The desk-style compressor that glues a mix together. A few dB is plenty; Mix blends the untouched sound back in.",
        "Nothing goes into the file louder than the Ceiling, so a loud chorus cannot clip the recording." };

    juce::String clockText (double seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 3600) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

RecordingPanel::RecordingPanel (AudioEngine& e) : engine (e), mixer (e.mixer())
{
    addAndMakeVisible (status);

    recordButton.setTooltip ("Starts a new folder of WAV files: every channel at its own record point, and the mixes ticked below. Press again to stop.");
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);

    styleButton (folderButton);
    folderButton.onClick = [] { Mixer::recordingFolder().createDirectory(); Mixer::recordingFolder().startAsProcess(); };
    addAndMakeVisible (folderButton);

    styleButton (copyButton);
    copyButton.setTooltip ("Copies this tab's part of the report, ready to paste to Claude");
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (reportText()); note = "Copied."; noteTicks = 0; };
    addAndMakeVisible (copyButton);

    clock.setFont (style::monoFont (34.0f));
    clock.setColour (juce::Label::textColourId, style::textMain);
    clock.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (clock);

    small (roomLabel, {});
    addAndMakeVisible (roomLabel);

    style::body (help, "Every file of a recording starts at the same moment, so they line up in any recording program. "
                       "The recorder only copies sound that is already there: it never changes what the room or the in-ears hear, and the drive is written to away from the sound.");
    help.setColour (juce::Label::textColourId, style::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (help);

    style::heading (tracksHeading, "Channels");
    small (pointHeading, "Record this channel", style::textMain);
    small (levelHeading, "In the recording mix (against the main mix)", style::textMain);
    small (panHeading, "Pan in the recording mix", style::textMain);
    style::heading (mixHeading, "Mixes to write, and the recording mix");
    small (mixLevelLabel, "Recording mix level", style::textMain);
    small (blockHint, {});
    blockHint.setMinimumHorizontalScale (1.0f);

    for (auto* l : { &tracksHeading, &pointHeading, &levelHeading, &panHeading, &mixHeading, &mixLevelLabel, &blockHint })
        addAndMakeVisible (*l);

    for (auto* b : { &mainBox, &mixBox, &fxBox, &checkBox })
    {
        b->setColour (juce::ToggleButton::textColourId, style::textMain);
        b->onClick = [this]
        {
            if (loading)
                return;

            auto r = mixer.setup().rec;
            r.mainMix = mainBox.getToggleState();
            r.mix = mixBox.getToggleState();
            r.fxTracks = fxBox.getToggleState();
            r.checkCopy = checkBox.getToggleState();
            mixer.setRecord (r);
        };
        addAndMakeVisible (*b);
    }

    mainBox.setTooltip ("A stereo file of the main output, with the venue's EQ and limiter, exactly as the PA was sent it");
    mixBox.setTooltip ("A stereo file of the recording mix: the same channels, with the levels, pans and EQ set on this tab");
    checkBox.setTooltip ("Also writes each socket in use plain, before any effect, so the Recordings tab can play this recording back through the rig as a virtual soundcheck");
    fxBox.setTooltip ("Each of the mixer's reverbs and delays alone, so a channel recorded dry can have its reverb put back later");

    for (int c = 0; c < numMixChannels; ++c)
    {
        auto& row = rows[(size_t) c];
        small (row.name, {}, style::textMain);
        style::combo (row.point);
        row.point.addItem ("Nothing", 1);
        row.point.addItem ("The plain socket (no effects)", 2);
        row.point.addItem ("After its effects, before the looper", 3);
        row.point.addItem ("After the looper, before the channel's EQ and compressors", 4);
        row.point.addItem ("After the looper, with the channel's EQ and compressors", 5);
        row.point.setTooltip ("Where in its path this channel is copied to its file. The two 'after the looper' points hold the live sound and its loops together.");
        row.point.onChange = [this, c]
        {
            if (loading)
                return;

            auto ch = mixer.setup().channel[(size_t) c];
            ch.record = (RecordPoint) juce::jlimit (0, numRecordPoints - 1, rows[(size_t) c].point.getSelectedId() - 1);
            mixer.setChannel (c, ch);
        };

        levelBar (row.level, "How much louder or quieter this channel is in the recording mix than in the main mix. It still follows the channel's fader and mute. Double-click for the same as the main mix.");
        row.level.onValueChange = [this, c]
        {
            if (loading)
                return;

            auto ch = mixer.setup().channel[(size_t) c];
            ch.recDb = fromLevel (rows[(size_t) c].level.getValue());
            mixer.setChannel (c, ch);
        };

        styleBar (row.pan);
        row.pan.setRange (-1.0, 1.0, 0.05);
        row.pan.setDoubleClickReturnValue (true, 0.0);
        row.pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.02 ? juce::String ("centre") : juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0.0 ? "% left" : "% right"); };
        row.pan.setTooltip ("Where this channel sits, left to right, in the recording mix only. Double-click for the centre.");
        row.pan.onValueChange = [this, c]
        {
            if (loading)
                return;

            auto ch = mixer.setup().channel[(size_t) c];
            ch.recPan = (float) rows[(size_t) c].pan.getValue();
            mixer.setChannel (c, ch);
        };

        for (auto* comp : std::initializer_list<juce::Component*> { &row.name, &row.point, &row.level, &row.pan })
            addAndMakeVisible (*comp);
    }

    for (int f = 0; f < numMixFx; ++f)
    {
        auto& row = fxRows[(size_t) f];
        small (row.name, {}, style::textMain);
        levelBar (row.level, "How much louder or quieter this reverb or delay is in the recording mix than in the main mix. Double-click for the same.");
        row.level.onValueChange = [this, f]
        {
            if (loading)
                return;

            auto e2 = mixer.setup().fx[(size_t) f];
            e2.recDb = fromLevel (fxRows[(size_t) f].level.getValue());
            mixer.setFx (f, e2);
        };
        addAndMakeVisible (row.name);
        addAndMakeVisible (row.level);
    }

    styleBar (mixLevel);
    mixLevel.setRange (-30.0, 10.0, 0.5);
    mixLevel.setDoubleClickReturnValue (true, 0.0);
    mixLevel.setTextValueSuffix (" dB");
    mixLevel.setTooltip ("The level of the whole recording mix going into its file");
    mixLevel.onValueChange = [this]
    {
        if (loading)
            return;

        auto r = mixer.setup().rec;
        r.levelDb = (float) mixLevel.getValue();
        mixer.setRecord (r);
    };
    addAndMakeVisible (mixLevel);

    for (int b = 0; b < numRecBlocks; ++b)
    {
        auto& button = blockButtons[(size_t) b];
        button.setButtonText (blockNames[b]);
        styleButton (button);
        button.setClickingTogglesState (false);
        button.onClick = [this, b] { block = b; rebuildGrid(); };
        addAndMakeVisible (button);
    }

    blockOn.setColour (juce::ToggleButton::textColourId, style::textMain);
    blockOn.onClick = [this]
    {
        auto r = mixer.setup().rec;
        r.chain.slots[(size_t) block].on = blockOn.getToggleState();
        mixer.setRecord (r);
        rebuildGrid();
    };
    addAndMakeVisible (blockOn);

    style::readOnlyEditor (reportView);
    addAndMakeVisible (reportView);

    refresh();
    startTimerHz (10);
}

RecordingPanel::~RecordingPanel()
{
    stopTimer();
}

const std::vector<fx::ParamInfo>& RecordingPanel::paramsOf (const std::string& type)
{
    auto& example = examples[type];

    if (example == nullptr)
        example = fx::makeEffect (type);

    static const std::vector<fx::ParamInfo> none;
    return example != nullptr ? example->params() : none;
}

void RecordingPanel::toggleRecording()
{
    noteTicks = 0;

    if (mixer.recorder().isRecording())
    {
        mixer.stopRecording();
        note = "Stopped. " + juce::String (mixer.recorder().fileNames().size()) + " files are in " + mixer.recorder().folder().getFullPathName();
    }
    else if (const auto error = mixer.startRecording (Mixer::recordingFolder()); error.isNotEmpty())
        note = error;
    else
        note.clear();

    shownSignature.clear();
}

juce::String RecordingPanel::signature() const
{
    const auto& now = mixer.setup();
    juce::String s;
    s << (int) mixer.isOn() << (int) now.rec.mainMix << (int) now.rec.mix << (int) now.rec.fxTracks << (int) now.rec.checkCopy << now.rec.levelDb << juce::String (now.rec.chain.toText());

    for (const auto& ch : now.channel)
        s << ch.name << (int) ch.on << (int) ch.record << ch.recDb << "/" << ch.recPan << (int) ch.link << "|";

    for (const auto& f : now.fx)
        s << f.name << (int) f.on << f.recDb << "|";

    return s;
}

void RecordingPanel::refresh()
{
    const auto& now = mixer.setup();
    loading = true;

    mainBox.setToggleState (now.rec.mainMix, juce::dontSendNotification);
    mixBox.setToggleState (now.rec.mix, juce::dontSendNotification);
    fxBox.setToggleState (now.rec.fxTracks, juce::dontSendNotification);
    checkBox.setToggleState (now.rec.checkCopy, juce::dontSendNotification);
    mixLevel.setValue (now.rec.levelDb, juce::dontSendNotification);

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = now.channel[(size_t) c];
        auto& row = rows[(size_t) c];
        const bool rightSide = c > 0 && now.channel[(size_t) c - 1].link;
        row.name.setText (juce::String (c + 1) + "  " + ch.name + (ch.on ? "" : " (off)"), juce::dontSendNotification);
        row.name.setColour (juce::Label::textColourId, ch.on ? style::textMain : style::textDim);
        row.point.setSelectedId ((int) ch.record + 1, juce::dontSendNotification);
        row.level.setValue (toLevel (ch.recDb), juce::dontSendNotification);
        row.pan.setValue (ch.recPan, juce::dontSendNotification);

        for (auto* comp : std::initializer_list<juce::Component*> { &row.point, &row.level, &row.pan })
            comp->setEnabled (ch.on && ! rightSide);

        row.pan.setEnabled (ch.on && ! rightSide && ! ch.link);     // a stereo pair sits hard left and right
    }

    for (int f = 0; f < numMixFx; ++f)
    {
        const auto& e2 = now.fx[(size_t) f];
        fxRows[(size_t) f].name.setText (e2.name + (e2.on ? "" : " (off)"), juce::dontSendNotification);
        fxRows[(size_t) f].name.setColour (juce::Label::textColourId, e2.on ? style::textMain : style::textDim);
        fxRows[(size_t) f].level.setValue (toLevel (e2.recDb), juce::dontSendNotification);
        fxRows[(size_t) f].level.setEnabled (e2.on);
    }

    loading = false;
    shownSignature = signature();
    rebuildGrid();
}

void RecordingPanel::rebuildGrid()
{
    gridLabels.clear();
    gridControls.clear();

    const auto& chain = mixer.setup().rec.chain;
    block = juce::jlimit (0, numRecBlocks - 1, block);

    for (int b = 0; b < numRecBlocks; ++b)
    {
        blockButtons[(size_t) b].setToggleState (b == block, juce::dontSendNotification);
        blockButtons[(size_t) b].setButtonText (juce::String (blockNames[b]) + (chain.slots[(size_t) b].on ? "" : " (off)"));
    }

    const auto& slot = chain.slots[(size_t) block];
    blockOn.setToggleState (slot.on, juce::dontSendNotification);
    blockHint.setText (blockWords[block], juce::dontSendNotification);
    const int b = block;

    for (const auto& info : paramsOf (slot.type))
    {
        auto* label = gridLabels.add (new juce::Label());
        small (*label, info.label, style::textMain);
        addAndMakeVisible (label);

        const auto id = info.id;
        const auto set = [this, b, id] (float v)
        {
            auto r = mixer.setup().rec;
            r.chain.slots[(size_t) b].setParam (id, v);
            mixer.setRecord (r);
            shownSignature = signature();       // this tab made the change: no need to build the sliders again
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

    layoutGrid();
}

void RecordingPanel::timerCallback()
{
    if (++ticks % 3 != 0)
        return;

    auto& rec = mixer.recorder();
    const bool recording = rec.isRecording();

    recordButton.setButtonText (recording ? "Stop recording" : "Record");
    recordButton.setColour (juce::TextButton::buttonColourId, recording ? style::red : style::red.withAlpha (0.35f));
    recordButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    clock.setText (recording || rec.seconds() > 0.0 ? clockText (rec.seconds()) : juce::String ("0:00:00"), juce::dontSendNotification);
    clock.setColour (juce::Label::textColourId, recording ? style::red : style::textDim);

    if (signature() != shownSignature)
        refresh();

    if (++noteTicks > 40 && ! note.startsWith ("Stopped"))
        note.clear();

    const auto tracks = mixer.recordTracks();
    const auto stopNote = mixer.recordNote();

    if (recording && rec.lostSamples() > 0)
        status.set (StatusRow::Tone::bad, "LOST", "Recording, but the drive is not keeping up", juce::String (rec.lostSamples()) + " samples are missing from the files so far. The sound in the room is not affected. Copy the report for Claude.");
    else if (recording)
        status.set (StatusRow::Tone::good, "REC", "Recording " + juce::String (rec.fileNames().size()) + " files at " + juce::String (rec.fileRate() / 1000.0, 1) + " kHz, 24-bit", "Into " + rec.folder().getFullPathName() + ". Nothing lost.");
    else if (stopNote.isNotEmpty())
        status.set (StatusRow::Tone::bad, "STOPPED", "The recording stopped by itself", stopNote);
    else if (note.isNotEmpty())
        status.set (note.startsWith ("Stopped") ? StatusRow::Tone::good : StatusRow::Tone::caution, note.startsWith ("Stopped") ? "SAVED" : "NOTE", note.startsWith ("Stopped") ? "Recording saved" : "Not recording", note);
    else if (tracks.empty())
        status.set (StatusRow::Tone::caution, "NOTHING", "Nothing is set to be recorded", "Give at least one channel a record point below, or tick a mix.");
    else if (! mixer.isOn())
        status.set (StatusRow::Tone::caution, "READY", "Ready: " + juce::String ((int) tracks.size()) + " files, sockets only", "The mixer is off, so each channel is recorded at its socket (plain, or after its effects). Loops and the mixes need the mixer on.");
    else
        status.set (StatusRow::Tone::neutral, "READY", "Ready: " + juce::String ((int) tracks.size()) + " files", "Press Record. Each channel is written at its own record point, with the mixes ticked below.");

    if (ticks % 30 == 0 || roomLabel.getText().isEmpty())
    {
        const auto room = mixer.recordRoom (Mixer::recordingFolder());
        roomLabel.setText (juce::String (room.freeGb, 1) + " GB free on the recordings drive: about " + juce::String (room.hours, 1) + " hours of these files ("
                           + juce::String (room.gbPerHour, 2) + " GB an hour). A recording stops by itself before the drive is full.", juce::dontSendNotification);
        roomLabel.setColour (juce::Label::textColourId, room.hours < 1.0 ? style::amber : style::textDim);
    }

    if (isShowing())
        if (const auto text = reportText(); text != shownReport)
        {
            shownReport = text;
            reportView.setText (text, false);
        }
}

void RecordingPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void RecordingPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    status.setBounds (area.removeFromTop (58));
    area.removeFromTop (8);

    auto top = area.removeFromTop (64);
    recordButton.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (16);
    clock.setBounds (top.removeFromLeft (190));
    auto buttons = top.removeFromRight (230);
    folderButton.setBounds (buttons.removeFromTop (30));
    buttons.removeFromTop (4);
    copyButton.setBounds (buttons.removeFromTop (30));
    top.removeFromRight (12);
    roomLabel.setBounds (top.removeFromBottom (20));
    help.setBounds (top);
    area.removeFromTop (10);

    leftArea = area.removeFromLeft (juce::jmin (760, area.getWidth() * 58 / 100));
    area.removeFromLeft (16);
    rightArea = area;

    // ---- left: the channels, then the reverbs and delays ----
    auto left = leftArea;
    tracksHeading.setBounds (left.removeFromTop (24));
    auto head = left.removeFromTop (18);
    const int nameWidth = 150, panWidth = 130, levelWidth = 170;
    head.removeFromLeft (nameWidth);
    panHeading.setBounds (head.removeFromRight (panWidth));
    head.removeFromRight (6);
    levelHeading.setBounds (head.removeFromRight (levelWidth));
    head.removeFromRight (6);
    pointHeading.setBounds (head);

    for (auto& row : rows)
    {
        auto r = left.removeFromTop (26).reduced (0, 2);
        row.name.setBounds (r.removeFromLeft (nameWidth));
        row.pan.setBounds (r.removeFromRight (panWidth));
        r.removeFromRight (6);
        row.level.setBounds (r.removeFromRight (levelWidth));
        r.removeFromRight (6);
        row.point.setBounds (r);
    }

    left.removeFromTop (6);

    for (auto& row : fxRows)
    {
        auto r = left.removeFromTop (26).reduced (0, 2);
        r.removeFromRight (panWidth + 6);
        row.level.setBounds (r.removeFromRight (levelWidth));
        r.removeFromRight (6);
        row.name.setBounds (r);
    }

    left.removeFromTop (8);
    reportView.setBounds (left);

    // ---- right: the mixes, and the recording mix's own chain ----
    auto right = rightArea;
    mixHeading.setBounds (right.removeFromTop (24));
    mainBox.setBounds (right.removeFromTop (24));
    mixBox.setBounds (right.removeFromTop (24));
    fxBox.setBounds (right.removeFromTop (24));
    checkBox.setBounds (right.removeFromTop (24));
    right.removeFromTop (8);
    auto levelRow = right.removeFromTop (24);
    mixLevelLabel.setBounds (levelRow.removeFromLeft (150));
    mixLevel.setBounds (levelRow.removeFromLeft (juce::jmin (260, levelRow.getWidth())));
    right.removeFromTop (8);
    auto blockRow = right.removeFromTop (26);

    for (auto& b : blockButtons)
    {
        b.setBounds (blockRow.removeFromLeft (128));
        blockRow.removeFromLeft (4);
    }

    blockRow.removeFromLeft (8);
    blockOn.setBounds (blockRow.removeFromLeft (130));
    blockHint.setBounds (right.removeFromTop (34));
    right.removeFromTop (2);
    gridArea = right;
    layoutGrid();
}

void RecordingPanel::layoutGrid()
{
    if (gridArea.isEmpty())
        return;

    const int columns = gridArea.getWidth() > 520 ? 2 : 1;
    const int cellWidth = gridArea.getWidth() / columns;

    for (int i = 0; i < gridControls.size(); ++i)
    {
        juce::Rectangle<int> cell (gridArea.getX() + (i % columns) * cellWidth, gridArea.getY() + (i / columns) * 24, cellWidth - 10, 22);
        gridLabels[i]->setBounds (cell.removeFromLeft (juce::jmin (140, cell.getWidth() / 2)));
        gridControls[i]->setBounds (cell);
    }
}

} // namespace gigrig
