#include "MainComponent.h"

namespace gigrig
{

MainComponent::MainComponent()
{
    title.setText ("Gig Rig", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, style::textMain);

    subtitle.setText ("Stage 1: audio through the CQ and round-trip latency.   Build " + juce::String (GIGRIG_BUILD_LABEL),
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
    tabs.addTab ("Device check", style::background, &devicePanel, false);

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
