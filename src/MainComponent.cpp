#include "MainComponent.h"

namespace gigrig
{

MainComponent::MainComponent()
{
    title.setText ("Gig Rig", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, style::textMain);

    subtitle.setText ("Audio through the CQ, the RC-600 as the pedal, the looper and the first effects.   Build " + juce::String (GIGRIG_BUILD_LABEL),
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
    tabs.addTab ("Songs", style::background, &songsPanel, false);
    tabs.addTab ("Effects", style::background, &effectsPanel, false);
    tabs.addTab ("Amp capture", style::background, &ampPanel, false);
    tabs.addTab ("Device check", style::background, &devicePanel, false);
    tabs.addTab ("Mixer", style::background, &mixerPanel, false);       // added last, so the tab numbers before it stay as they were

    // The pedal: MIDI from the RC-600 (and clicks on the on-screen switches) become
    // presses, holds and double-presses, and those go straight to the looper.
    // A switch the Effects tab has taken goes to the effects instead.
    pedal.onEvent = [this] (const PedalEvent& event)
    {
        if (engine.effects().takePedal (event.sw, event.kind == PedalEvent::Kind::down, event.kind == PedalEvent::Kind::hold))
            return;

        engine.looper().pedal (event);
    };
    // A program change from any MIDI device (Songbook Pro through its Bluetooth receiver)
    // loads the song with that number. MIDI arrives on the driver's thread; songs load on this one.
    devicePanel.midi().onMessage = [this] (const juce::String& deviceName, const juce::MidiMessage& message)
    {
        pedal.handleMidi (deviceName, message);
        engine.kick().handleMidi (deviceName, message);     // the kick pad test: a note from the SPD::ONE plays the kick

        if (message.isProgramChange())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), deviceName, program = message.getProgramChangeNumber() + 1]
            {
                if (safe != nullptr)
                    safe->songBook.programChange (program, deviceName);
            });
    };

    songBook.onChanged = [this]
    {
        looperPanel.songChanged();
        effectsPanel.refresh();
        songsPanel.refresh();
    };
    devicePanel.onMidiOpened = [this] (const juce::StringArray& names) { pedal.setOpenDevices (names); };

    // The kick pad test gets its own tab and its own part of the report.
    tabs.addTab ("Kick pad", style::background, &kickPanel, false);

    // The pedal and the looper add their parts to the report, and a glitch check
    // starts a fresh count of what the RC-600 sent while it ran.
    engine.extraReport = [this] { return pedal.reportText() + "\n" + looperPanel.reportText() + "\n" + songBook.reportText() + "\n" + engine.effects().reportText() + "\n" + ampPanel.reportText() + "\n" + mixerPanel.reportText(); };
    engine.onGlitchCheckStarted = [this] { pedal.resetCounts(); };

    // ASIO drivers allow one user at a time, so the engine lets go of its
    // device while the device check looks at every driver, then picks it up again.
    devicePanel.onBeforeScan = [this]
    {
        reopenChoice = engine.isOpen() ? engine.currentChoice() : audioPanel.lostDevice();
        reopenBufferSize = engine.isOpen() ? engine.bufferSize() : 0;
        audioPanel.pauseReconnect();
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

    // For screenshots in the cloud (no mouse there): GIGRIG_TAB=4 opens on the fifth tab (0 audio, 1 pedal, 2 looper, 3 songs, 4 effects, 5 amp capture, 6 device check, 7 mixer).
    if (const auto tab = juce::SystemStats::getEnvironmentVariable ("GIGRIG_TAB", {}); tab.isNotEmpty())
        tabs.setCurrentTabIndex (tab.getIntValue());

    // The same for a program change: GIGRIG_PROGRAM=11 does what Songbook Pro's program 11 does, a moment after the start.
    if (const auto program = juce::SystemStats::getEnvironmentVariable ("GIGRIG_PROGRAM", {}).getIntValue(); program > 0)
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this), program]
        {
            if (safe != nullptr)
                safe->songBook.programChange (program, "GIGRIG_PROGRAM");
        });

    engine.extraReport = [this, rest = engine.extraReport] { return rest() + "\n" + kickPanel.reportText(); };

    songsPanel.onWindowButton = [this] { moveSongsPanel (songsWindow == nullptr); };
    songsPanel.refresh();       // the effects panel has read the rack's memory by now
    if (juce::SystemStats::getEnvironmentVariable ("GIGRIG_SONGS_WINDOW", {}).isNotEmpty())
        juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->moveSongsPanel (true); });

    setSize (980, 790);
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

namespace
{
    constexpr int songsTabIndex = 3;

    /** Gives the Songs panel the same margin in its own window as it has under the tabs. */
    struct Padded final : juce::Component
    {
        explicit Padded (juce::Component& c) : inner (c)   { addAndMakeVisible (inner); setSize (980, 720); }
        void paint (juce::Graphics& g) override            { g.fillAll (style::background); }
        void resized() override                            { inner.setBounds (getLocalBounds().reduced (16, 6)); }
        juce::Component& inner;
    };

    struct SongsWindow final : juce::DocumentWindow
    {
        SongsWindow (juce::Component& panel, std::function<void()> whenClosed)
            : juce::DocumentWindow ("Gig Rig: Songs", style::background, juce::DocumentWindow::allButtons), onClose (std::move (whenClosed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new Padded (panel), true);
            setResizable (true, false);
            setResizeLimits (860, 560, 4000, 3000);
        }

        void closeButtonPressed() override      { juce::MessageManager::callAsync (onClose); }
        std::function<void()> onClose;
    };
}

void MainComponent::moveSongsPanel (bool toOwnWindow)
{
    if (toOwnWindow == (songsWindow != nullptr))
        return;

    if (toOwnWindow)
    {
        tabs.removeTab (songsTabIndex);
        songsWindow = std::make_unique<SongsWindow> (songsPanel, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->moveSongsPanel (false);
        });

        if (auto* top = getTopLevelComponent())
            songsWindow->setTopLeftPosition (top->getX() + 60, top->getY() + 60);

        songsWindow->setVisible (true);
        tabs.setCurrentTabIndex (songsTabIndex);        // the Effects tab has moved up into this place: the two are used together
    }
    else
    {
        songsWindow.reset();
        tabs.addTab ("Songs", style::background, &songsPanel, false, songsTabIndex);
        tabs.setCurrentTabIndex (songsTabIndex);
    }

    songsPanel.setInOwnWindow (songsWindow != nullptr);
    songsPanel.refresh();
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
