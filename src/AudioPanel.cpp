#include "AudioPanel.h"
#include "DeviceReport.h"

namespace gigrig
{

namespace
{
    constexpr int timerHz = 20;
    constexpr int noDeviceId = 1;          // ComboBox ids start at 1; the first entry is "audio off"
    constexpr int usbStereoMask = 0b00110000;
    constexpr double targetMs = 10.0;

    const std::vector<int> sweepSizes { 8, 16, 32, 64, 128, 256 };

    void styleBox (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, style::panel);
        box.setColour (juce::ComboBox::textColourId, style::textMain);
        box.setColour (juce::ComboBox::outlineColourId, style::textDim.withAlpha (0.4f));
    }

    juce::String oneDecimal (double value)     { return juce::String (value, 1); }
}

AudioPanel::AudioPanel (AudioEngine& e)
    : engine (e)
{
    style::body (deviceLabel, "Audio device");
    style::body (bufferLabel, "Buffer");
    style::body (pingLabel, "Send the ping to");
    style::body (playLabel, "Play-through");
    style::body (toneLabel, "Test tone on return");
    style::body (dropoutLabel, {});
    style::body (glitchLabel, {});
    style::body (inputsLabel, "Inputs");
    style::body (returnsLabel, "Returns");
    inputsLabel.setColour (juce::Label::textColourId, style::textDim);
    returnsLabel.setColour (juce::Label::textColourId, style::textDim);

    for (auto* box : { &deviceBox, &bufferBox, &pingBox, &playBox })
        styleBox (*box);

    pingBox.addItem ("The CQ's USB stereo channel (returns 5 and 6)", 1);

    for (int r = 0; r < numReturns; ++r)
        pingBox.addItem (returnName (r), 2 + r);

    pingBox.addItem ("All eight returns", 2 + numReturns);
    pingBox.setSelectedId (1, juce::dontSendNotification);

    pingBox.onChange = [this]
    {
        const auto id = pingBox.getSelectedId();
        engine.setPingReturns (id == 1 ? usbStereoMask : id == 2 + numReturns ? 0xff : 1 << (id - 2));
    };

    playBox.addItem ("Off", 1);
    playBox.addItem ("Each input to its own return (input 1 to return 1, and so on)", 2);
    playBox.addItem ("Inputs 1 to 10 mixed to the CQ's USB stereo channel", 3);
    playBox.setSelectedId (1, juce::dontSendNotification);

    playBox.onChange = [this]
    {
        engine.setPlayThrough ((PlayThrough) (playBox.getSelectedId() - 1));
        engine.resetDropoutStats();
    };

    deviceBox.onChange = [this]
    {
        const auto index = deviceBox.getSelectedId() - noDeviceId - 1;

        if (index >= 0 && index < choices.size())
        {
            openDevice (choices[index], 0);
        }
        else
        {
            engine.close();
            syncToEngine();
        }
    };

    bufferBox.onChange = [this]
    {
        if (engine.isOpen() && bufferBox.getSelectedId() > 0 && bufferBox.getSelectedId() != engine.bufferSize())
            openDevice (engine.currentChoice(), bufferBox.getSelectedId());
    };

    panelButton.onClick   = [this] { engine.showControlPanel(); };
    measureButton.onClick = [this] { engine.measure ({}); };
    sweepButton.onClick   = [this] { engine.measure (sweepSizes); };
    resetButton.onClick   = [this] { engine.resetDropoutStats(); updateDropoutLine(); };

    glitchButton.setTooltip ("Sends a steady tone out to the mixer and counts every break in it as it comes back. "
                             "Play-through is silent while this is on.");
    glitchButton.setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.3f));
    glitchButton.onClick  = [this] { engine.setGlitchCheck (! engine.glitchCheckOn()); updateGlitchLine(); };

    for (int r = 0; r < numReturns; ++r)
    {
        auto* button = toneButtons.add (new juce::TextButton (juce::String (r + 1)));
        button->setTooltip (returnName (r));
        button->setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.3f));

        button->onClick = [this, r]
        {
            engine.setToneReturn (engine.toneReturnIndex() == r ? -1 : r);
        };

        addAndMakeVisible (button);
    }

    style::readOnlyEditor (resultsView);

    for (auto* c : std::initializer_list<juce::Component*> { &deviceLabel, &deviceBox, &bufferLabel, &bufferBox, &panelButton,
                                                             &verdict, &measureButton, &sweepButton, &pingLabel, &pingBox,
                                                             &playLabel, &playBox, &resetButton, &dropoutLabel,
                                                             &glitchButton, &glitchLabel,
                                                             &toneLabel, &inputsLabel, &inputMeters, &returnsLabel,
                                                             &returnMeters, &resultsView })
        addAndMakeVisible (c);

    syncToEngine();
    startTimerHz (timerHz);
}

AudioPanel::~AudioPanel()
{
    stopTimer();
}

void AudioPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);
    const auto row = [&area] { auto r = area.removeFromTop (30); area.removeFromTop (6); return r; };

    auto r = row();
    deviceLabel.setBounds (r.removeFromLeft (92));
    panelButton.setBounds (r.removeFromRight (120));
    r.removeFromRight (10);
    bufferBox.setBounds (r.removeFromRight (190));
    bufferLabel.setBounds (r.removeFromRight (52));
    r.removeFromRight (10);
    deviceBox.setBounds (r);

    verdict.setBounds (area.removeFromTop (68));
    area.removeFromTop (6);

    r = row();
    measureButton.setBounds (r.removeFromLeft (150));
    r.removeFromLeft (8);
    sweepButton.setBounds (r.removeFromLeft (240));
    r.removeFromLeft (14);
    pingLabel.setBounds (r.removeFromLeft (112));
    pingBox.setBounds (r);

    r = row();
    playLabel.setBounds (r.removeFromLeft (92));
    playBox.setBounds (r.removeFromLeft (juce::jmax (160, r.getWidth() - 92 - 14 - 128 - numReturns * 38)));
    r.removeFromLeft (14);
    toneLabel.setBounds (r.removeFromLeft (128));

    for (auto* button : toneButtons)
    {
        button->setBounds (r.removeFromLeft (34));
        r.removeFromLeft (4);
    }

    r = row();
    resetButton.setBounds (r.removeFromRight (120));
    r.removeFromRight (10);
    dropoutLabel.setBounds (r);

    r = row();
    glitchButton.setBounds (r.removeFromLeft (150));
    r.removeFromLeft (10);
    glitchLabel.setBounds (r);

    auto meters = area.removeFromTop (64);
    area.removeFromTop (8);
    inputsLabel.setBounds (meters.removeFromLeft (52));
    auto returnsArea = meters.removeFromRight (meters.getWidth() / 3);
    returnsLabel.setBounds (returnsArea.removeFromLeft (64).translated (8, 0));
    returnMeters.setBounds (returnsArea);
    inputMeters.setBounds (meters);

    resultsView.setBounds (area);
}

void AudioPanel::refreshDevices (bool openCQ)
{
    choices = engine.listDevices();

    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItem ("None (audio off)", noDeviceId);

    for (int i = 0; i < choices.size(); ++i)
        deviceBox.addItem (choices[i].label(), noDeviceId + 1 + i);

    if (! engine.isOpen() && openCQ)
    {
        // ASIO first: it is the low-latency route on Windows.
        const DeviceChoice* found = nullptr;

        for (const auto& c : choices)
            if (looksLikeCQ (c.deviceName) && (found == nullptr || (c.typeName == "ASIO" && found->typeName != "ASIO")))
                found = &c;

        if (found != nullptr)
        {
            openDevice (*found, 0);
            return;
        }
    }

    syncToEngine();
}

void AudioPanel::openDevice (const DeviceChoice& choice, int bufferSize)
{
    engine.open (choice, bufferSize);
    syncToEngine();
}

void AudioPanel::syncToEngine()
{
    const auto index = engine.isOpen() ? choices.indexOf (engine.currentChoice()) : -1;
    deviceBox.setSelectedId (index >= 0 ? noDeviceId + 1 + index : noDeviceId, juce::dontSendNotification);

    bufferBox.clear (juce::dontSendNotification);

    if (engine.isOpen())
    {
        auto sizes = engine.availableBufferSizes();
        sizes.addIfNotAlreadyThere (engine.bufferSize());
        sizes.sort();

        for (auto size : sizes)
            bufferBox.addItem (juce::String (size) + " samples (" + oneDecimal (size * 1000.0 / engine.sampleRate()) + " ms)", size);

        bufferBox.setSelectedId (engine.bufferSize(), juce::dontSendNotification);
    }

    setControlsEnabled();
    updateVerdict();
    updateDropoutLine();
    updateGlitchLine();
}

void AudioPanel::setControlsEnabled()
{
    const bool open = engine.isOpen();
    const bool free = ! engine.isMeasuring();

    deviceBox.setEnabled (free);
    bufferBox.setEnabled (open && free);
    panelButton.setEnabled (open && free && engine.hasControlPanel());
    measureButton.setEnabled (open && free);
    sweepButton.setEnabled (open && free);
    pingBox.setEnabled (free);
    playBox.setEnabled (open && free);
    resetButton.setEnabled (open);
    glitchButton.setEnabled (open && free);

    for (auto* button : toneButtons)
        button->setEnabled (open && free);
}

void AudioPanel::updateVerdict()
{
    using Tone = StatusRow::Tone;

    if (! engine.isOpen())
    {
        verdict.set (engine.lastError().isNotEmpty() ? Tone::bad : Tone::neutral, "NO AUDIO",
                     "No audio device is running",
                     engine.lastError().isNotEmpty() ? engine.lastError()
                                                     : juce::String ("Choose the CQ in the Audio device list. If it is not listed, "
                                                                     "look at the Device check tab."));
        return;
    }

    if (engine.isMeasuring())
    {
        verdict.set (Tone::neutral, "MEASURING", engine.measuringStatus(),
                     "You will hear a short chirp for each ping. Leave everything as it is until this finishes.");
        return;
    }

    const auto& rows = engine.latencyRows();

    if (rows.empty())
    {
        verdict.set (Tone::neutral, "READY",
                     "Running: " + juce::String (engine.sampleRate() / 1000.0, 0) + " kHz, "
                         + juce::String (engine.bufferSize()) + "-sample buffer. Ready to measure.",
                     "Run a microphone cable from MAIN OUT L to a spare input, pull that input's fader right down on the CQ, "
                     "then press a Measure button.");
        return;
    }

    juce::StringArray parts;
    const LatencyRow* judged = nullptr;
    const LatencyRow* anyHeard = nullptr;
    int dropouts = 0;

    for (const auto& row : rows)
    {
        dropouts += row.dropouts;

        if (row.best < 0)
        {
            parts.add ("at " + juce::String (row.bufferSize) + ": not heard");
            continue;
        }

        const auto time = row.ms (row.heard[(size_t) row.best].medianSamples);
        parts.add ("at " + juce::String (row.bufferSize) + ": " + oneDecimal (time));
        anyHeard = &row;

        // Judge on the 64-sample row when there is one, otherwise the first that was heard.
        if (judged == nullptr || row.bufferSize == 64)
            judged = &row;
    }

    if (anyHeard == nullptr)
    {
        verdict.set (Tone::bad, "NOT HEARD", "The ping went out but did not come back",
                     "On the CQ, check the USB stereo channel's fader is up and it is not muted. "
                     "Then check the cable runs from MAIN OUT L to an input.");
        return;
    }

    const auto& best = judged->heard[(size_t) judged->best];
    const auto time = judged->ms (best.medianSamples);
    const auto headline = "Round trip in ms    " + parts.joinIntoString (",   ");

    if (! judged->bestIsAnalogue)
    {
        verdict.set (Tone::caution, "NO CABLE", headline,
                     "No input socket heard the ping, only the copy inside the mixer, so the true figure is a little "
                     "higher. Check the cable from MAIN OUT L to an input, and that input's Source Point is Post Preamp.");
        return;
    }

    const auto heardOn = "Heard on " + inputName (best.input, engine.isCQ()) + ". ";
    const auto dropoutNote = dropouts > 0 ? juce::String (dropouts) + " dropouts during the test (see below). " : juce::String();

    if (time <= targetMs)
        verdict.set (Tone::good, time <= 5.0 ? "GREAT" : "GOOD", headline,
                     heardOn + dropoutNote + "The target is 10 ms or better, and 3 to 5 ms is the aim. "
                     "Press Copy report and paste it to Claude.");
    else
        verdict.set (Tone::caution, "SLOW", headline,
                     heardOn + dropoutNote + "That is over the 10 ms target. Try a smaller buffer, "
                     "then press Copy report and paste it to Claude.");
}

void AudioPanel::updateDropoutLine()
{
    if (! engine.isOpen())
    {
        dropoutLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    if (engine.feedbackGuardTripped())
    {
        dropoutLabel.setColour (juce::Label::textColourId, style::red);
        dropoutLabel.setText ("Play-through was switched off: the sound was feeding back. Unplug the loopback cable "
                              "or turn the speakers down before switching it on again.", juce::dontSendNotification);
        return;
    }

    const auto stats = engine.dropoutStats();
    const auto seconds = (int) stats.seconds;
    const bool clean = stats.lateCallbacks == 0 && stats.driverDropouts <= 0;

    dropoutLabel.setColour (juce::Label::textColourId, clean ? style::textMain : style::amber);
    dropoutLabel.setText ("Dropout watch  " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                            + "     Driver dropouts: " + (stats.driverDropouts < 0 ? juce::String ("n/a") : juce::String (stats.driverDropouts))
                            + "     Late blocks: " + juce::String (stats.lateCallbacks)
                            + "     Longest wait: " + oneDecimal (stats.worstGapMs) + " ms"
                            + "     Busiest: " + oneDecimal (stats.peakLoad * 100.0) + "%",
                          juce::dontSendNotification);
}

void AudioPanel::updateGlitchLine()
{
    using State = GlitchStats::State;
    const auto found = engine.glitchStats();
    const bool on = engine.isOpen() && found.state != State::off;

    glitchButton.setToggleState (on, juce::dontSendNotification);
    glitchButton.setButtonText (on ? "Stop glitch check" : "Glitch check");

    auto colour = style::textDim;
    juce::String text;

    if (! engine.isOpen())
    {
    }
    else if (! on)
    {
        const auto seconds = (int) found.seconds;
        text = seconds > 0 ? "Off. Last run: tone watched " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                               + ", glitches: " + juce::String (found.glitches) + ". This stays in Copy report."
                           : juce::String ("Off. Sends a steady tone round the mixer and counts every break in it. Play-through is silent while it runs.");
    }
    else if (found.state == State::notHeard)
    {
        colour = style::amber;
        text = "The tone is not coming back cleanly. On the CQ, check the USB stereo channel's fader is up and not muted.";
    }
    else if (found.state == State::listening)
    {
        colour = found.seconds > 0.0 ? style::amber : style::textMain;
        text = found.seconds > 0.0 ? "The tone has gone missing: listening for it again.     Glitches so far: " + juce::String (found.glitches)
                                   : juce::String ("Listening for the tone...");
    }
    else
    {
        const auto seconds = (int) found.seconds;
        colour = found.glitches == 0 ? style::textMain : style::amber;
        text = "Tone watched  " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                 + "     Glitches: " + juce::String (found.glitches)
                 + "     Heard on " + inputName (found.input, engine.isCQ())
                 + " at " + juce::String (juce::roundToInt (found.levelDb)) + " dB";
    }

    glitchLabel.setColour (juce::Label::textColourId, colour);
    glitchLabel.setText (text, juce::dontSendNotification);
}

void AudioPanel::timerCallback()
{
    engine.tick();

    for (int i = 0; i < numMeteredInputs; ++i)
        inputMeters.setLevel (i, engine.takeInputPeak (i));

    for (int i = 0; i < numReturns; ++i)
        returnMeters.setLevel (i, engine.takeReturnPeak (i));

    for (int r = 0; r < toneButtons.size(); ++r)
        toneButtons[r]->setToggleState (engine.toneReturnIndex() == r, juce::dontSendNotification);

    if (engine.feedbackGuardTripped() && playBox.getSelectedId() != 1)
        playBox.setSelectedId (1, juce::dontSendNotification);

    const bool measuring = engine.isMeasuring();

    if (measuring || wasMeasuring)
    {
        if (! measuring)
            syncToEngine();     // the buffer size is back where it started
        else
            updateVerdict();

        setControlsEnabled();
        wasMeasuring = measuring;
    }

    // Twice a second is plenty for the words.
    if (++ticks % 10 == 0)
    {
        updateDropoutLine();
        updateGlitchLine();

        if (! measuring && engine.isOpen() && engine.bufferSize() != bufferBox.getSelectedId())
            syncToEngine();     // the driver changed its own buffer size (from its settings window)

        const auto text = engine.latencyText();

        if (text != shownResults)
        {
            shownResults = text;
            resultsView.setText (text, false);
        }
    }
}

} // namespace gigrig
