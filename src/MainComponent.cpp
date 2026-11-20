#include "MainComponent.h"

namespace gigrig
{

MainComponent::MainComponent()
{
    title.setText ("Gig Rig", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, style::textMain);

    subtitle.setText ("Stages 1 to 3 and the Bass9 test: audio through the CQ, the RC-600 as the pedal, the looper and the bass.   Build " + juce::String (GIGRIG_BUILD_LABEL),
                      juce::dontSendNotification);
    subtitle.setFont (juce::Font (juce::FontOptions (14.0f)));
    subtitle.setColour (juce::Label::textColourId, style::textDim);

    copyButton.setTooltip ("Copies the latency results and the device list, ready to paste to Claude");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (devicePanel.reportText() + "\n" + engine.reportText());
        copyButton.setButtonText ("Copied");
        startTimer (2000);
    };

    tabs.setColour (juce::TabbedComponent::backgroundColourId, style::background);
    tabs.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    tabs.setOutline (0);
    tabs.addTab ("Latency and audio test", style::background, &audioPanel, false);
    tabs.addTab ("Pedal (RC-600)", style::background, &pedalPanel, false);
    tabs.addTab ("Looper", style::background, &looperPanel, false);
    tabs.addTab ("Bass (Bass9 A/B)", style::background, &bassPanel, false);
    tabs.addTab ("Device check", style::background, &devicePanel, false);

    // The pedal: MIDI from the RC-600 (and clicks on the on-screen switches) become
    // presses, holds and double-presses, and those go straight to the looper.
    pedal.onEvent = [this] (const PedalEvent& event)
    {
        if (! bassPanel.handlePedal (event))     // the bass A/B switch, if one is chosen, is not the looper's
            engine.looper().pedal (event);
    };
    devicePanel.midi().onMessage = [this] (const juce::String& deviceName, const juce::MidiMessage& message) { pedal.handleMidi (deviceName, message); };
    devicePanel.onMidiOpened = [this] (const juce::StringArray& names) { pedal.setOpenDevices (names); };

    // The pedal, the looper and the bass add their parts to the report, and a glitch check
    // starts a fresh count of what the RC-600 sent while it ran.
    engine.extraReport = [this] { return pedal.reportText() + "\n" + looperPanel.reportText() + "\n" + bassPanel.reportText(); };
    engine.onGlitchCheckStarted = [this] { pedal.resetCounts(); };

    // ASIO drivers allow one user at a time, so the engine lets go of its
    // device while the device check looks at every driver, then picks it up again.
    devicePanel.onBeforeScan = [this]
    {
        reopenChoice = engine.isOpen() ? engine.currentChoice() : DeviceChoice();
        reopenBufferSize = engine.bufferSize();
        engine.close();
    };

    devicePanel.onScanned = [this] (const DeviceReport&)
    {
        audioPanel.refreshDevices (firstScan);

        if (reopenChoice.deviceName.isNotEmpty())
            audioPanel.openDevice (reopenChoice, reopenBufferSize);

        firstScan = false;
    };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &copyButton, &tabs })
        addAndMakeVisible (c);

    setSize (980, 740);
    devicePanel.requestScan();
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.close();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    auto header = area.removeFromTop (52);
    copyButton.setBounds (header.removeFromRight (120).reduced (0, 9));
    title.setBounds (header.removeFromTop (30));
    subtitle.setBounds (header);

    area.removeFromTop (6);
    tabs.setBounds (area);
}

void MainComponent::timerCallback()
{
    stopTimer();
    copyButton.setButtonText ("Copy report");
}

} // namespace gigrig
