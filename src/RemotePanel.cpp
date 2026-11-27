#include "RemotePanel.h"

namespace gigrig
{

RemotePanel::RemotePanel (RemoteServer& s) : server (s)
{
    style::heading (heading, "Remote screen: the rig on a phone or tablet");
    style::body (portLabel, "Port");
    style::body (explain,
                 "Tick the box, then type one of the addresses below into the browser of a phone, tablet or laptop that is on the same "
                 "Wi-Fi or network cable as this computer. The page shows the looper, the songs, the looper builder, the effects and the mixer, "
                 "and it follows whatever is done here too.\n\n"
                 "The first time, Windows may ask whether GigRig may use the network: answer Allow. There is no password, so anyone on the "
                 "same network can open the page: use it on your own Wi-Fi, not a venue's guest Wi-Fi.\n\n"
                 "Switched off, nothing is open to the network and the app runs exactly as it did before this tab existed. "
                 "Timing-critical presses still belong on the foot switches: a tap on a phone travels over Wi-Fi and can arrive a few hundredths of a second late.");
    explain.setJustificationType (juce::Justification::topLeft);

    portBox.setInputRestrictions (5, "0123456789");
    portBox.setColour (juce::TextEditor::backgroundColourId, style::panel);
    portBox.setColour (juce::TextEditor::textColourId, style::textMain);
    style::readOnlyEditor (addressView);
    addressView.setFont (style::monoFont (20.0f));
    onBox.setColour (juce::ToggleButton::textColourId, style::textMain);
    onBox.setColour (juce::ToggleButton::tickColourId, style::green);

    const auto settings = RemoteServer::loadSettings();
    portBox.setText (juce::String (settings.port), false);
    onBox.setToggleState (settings.on, juce::dontSendNotification);

    onBox.onClick = [this] { apply (true); };
    portBox.onReturnKey = [this] { apply (true); };
    portBox.onFocusLost = [this] { if (portBox.getText().getIntValue() != server.port() && server.isRunning()) apply (true); };
    openButton.onClick = [this] { juce::URL ("http://localhost:" + juce::String (server.port()) + "/").launchInDefaultBrowser(); };

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &onBox, &portLabel, &portBox, &openButton, &status, &addressView, &explain })
        addAndMakeVisible (c);

    // For checks in the cloud (no mouse there): GIGRIG_REMOTE=8080 switches it on at that port without saving the choice.
    if (const int port = juce::SystemStats::getEnvironmentVariable ("GIGRIG_REMOTE", {}).getIntValue(); port > 0)
    {
        portBox.setText (juce::String (port), false);
        onBox.setToggleState (true, juce::dontSendNotification);
        apply (false);
    }
    else if (settings.on)
    {
        apply (false);
    }

    timerCallback();
    startTimer (500);
}

RemotePanel::~RemotePanel()
{
    stopTimer();
}

void RemotePanel::apply (bool save)
{
    const int port = juce::jlimit (1024, 65535, portBox.getText().getIntValue() > 0 ? portBox.getText().getIntValue() : RemoteServer::defaultPort);
    portBox.setText (juce::String (port), false);
    error.clear();

    if (onBox.getToggleState())
    {
        error = server.start (port);

        if (error.isNotEmpty())
            onBox.setToggleState (false, juce::dontSendNotification);
    }
    else
    {
        server.stop();
    }

    if (save)
        RemoteServer::saveSettings ({ onBox.getToggleState(), port });

    timerCallback();
}

void RemotePanel::timerCallback()
{
    using Tone = StatusRow::Tone;
    const auto since = server.secondsSinceRequest();

    if (error.isNotEmpty())
        status.set (Tone::bad, "PROBLEM", "The remote screen could not be switched on", error);
    else if (! server.isRunning())
        status.set (Tone::neutral, "OFF", "The remote screen is off", "Nothing is open to the network.");
    else if (since >= 0.0 && since < 3.0)
        status.set (Tone::good, "CONNECTED", "A phone, tablet or browser is connected", juce::String (server.requestsServed()) + " requests answered so far.");
    else
        status.set (Tone::caution, "WAITING", "On, and waiting for a phone or tablet", "Type one of the addresses below into its browser.");

    const auto text = ! server.isRunning() ? juce::String ("(switched off)")
                                           : server.addresses().isEmpty() ? juce::String ("This computer is not on a network yet.\nOn this computer itself: http://localhost:" + juce::String (server.port()))
                                                                          : server.addresses().joinIntoString ("\n");
    if (addressView.getText() != text)
        addressView.setText (text, false);

    openButton.setEnabled (server.isRunning());
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void RemotePanel::resized()
{
    auto area = getLocalBounds().reduced (4, 8);
    heading.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    auto row = area.removeFromTop (30);
    onBox.setBounds (row.removeFromLeft (470));
    portLabel.setBounds (row.removeFromLeft (40));
    portBox.setBounds (row.removeFromLeft (70).reduced (0, 2));
    row.removeFromLeft (16);
    openButton.setBounds (row.removeFromLeft (200).reduced (0, 1));
    area.removeFromTop (10);
    status.setBounds (area.removeFromTop (54));
    area.removeFromTop (10);
    addressView.setBounds (area.removeFromTop (110));
    area.removeFromTop (12);
    explain.setBounds (area);
}

} // namespace gigrig
