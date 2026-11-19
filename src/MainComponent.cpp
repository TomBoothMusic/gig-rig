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

    resultsButton.setTooltip ("Copies only the short stage 1 results: latency, dropouts and the glitch check");
    resultsButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard ("GigRig test results, build " + juce::String (GIGRIG_BUILD_LABEL) + "\n\n" + engine.reportText());
        resultsButton.setButtonText ("Copied");
        startTimer (2000);
    };

    copyButton.setTooltip ("Copies everything: the full device list as well as the test results");
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

    for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &copyButton, &resultsButton, &tabs })
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
    header.removeFromRight (8);
    resultsButton.setBounds (header.removeFromRight (150).reduced (0, 9));
    title.setBounds (header.removeFromTop (30));
    subtitle.setBounds (header);

    area.removeFromTop (6);
    tabs.setBounds (area);
}

void MainComponent::timerCallback()
{
    stopTimer();
    copyButton.setButtonText ("Copy report");
    resultsButton.setButtonText ("Copy test results");
}

} // namespace gigrig
