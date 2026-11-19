#include "MainComponent.h"

namespace gigrig
{

namespace
{
    const juce::Colour background { 0xff15181c };
    const juce::Colour panel      { 0xff1e2329 };
    const juce::Colour textMain   { 0xffe6e9ed };
    const juce::Colour textDim    { 0xff9aa4af };
    const juce::Colour green      { 0xff3fb950 };
    const juce::Colour amber      { 0xffd29922 };
    const juce::Colour red        { 0xfff0605a };

    constexpr int timerHz = 20;
    constexpr int maxMidiLinesShown = 500;

    juce::Font monoFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }

    void styleReadOnlyEditor (juce::TextEditor& editor)
    {
        editor.setMultiLine (true, false);
        editor.setReadOnly (true);
        editor.setCaretVisible (false);
        editor.setScrollbarsShown (true);
        editor.setFont (monoFont (14.0f));
        editor.setColour (juce::TextEditor::backgroundColourId, panel);
        editor.setColour (juce::TextEditor::textColourId, textMain);
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    }

    void styleHeading (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, textDim);
    }
}

//==============================================================================
void VerdictRow::setVerdict (const Verdict& newVerdict)
{
    verdict = newVerdict;
    scanning = false;
    repaint();
}

void VerdictRow::setScanning (const juce::String& what)
{
    verdict = { Verdict::State::missing, what, {} };
    scanning = true;
    repaint();
}

void VerdictRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);

    juce::Colour colour = textDim;
    juce::String tag = "LOOKING";

    if (! scanning)
    {
        switch (verdict.state)
        {
            case Verdict::State::found:   colour = green; tag = "FOUND";     break;
            case Verdict::State::partial: colour = amber; tag = "PARTLY";    break;
            case Verdict::State::missing: colour = red;   tag = "NOT FOUND"; break;
        }
    }

    // Colour is backed by a word, so the answer never depends on telling red from green.
    auto tagArea = area.removeFromLeft (112.0f).reduced (10.0f, 14.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (tagArea, 4.0f);
    g.setColour (background);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (tag, tagArea, juce::Justification::centred);

    auto textArea = area.reduced (6.0f, 8.0f);
    g.setColour (textMain);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (verdict.headline, textArea.removeFromTop (22.0f), juce::Justification::centredLeft);

    g.setColour (textDim);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawFittedText (verdict.detail, textArea.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
}

//==============================================================================
MainComponent::MainComponent()
{
    title.setText ("Gig Rig: device check", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, textMain);

    subtitle.setText ("Stage 0. Does this computer see the CQ mixer and the RC-600?   Build "
                          + juce::String (GIGRIG_BUILD_LABEL),
                      juce::dontSendNotification);
    subtitle.setFont (juce::Font (juce::FontOptions (14.0f)));
    subtitle.setColour (juce::Label::textColourId, textDim);

    styleHeading (reportHeading, "Full report (press Copy report, then paste it to Claude)");
    styleHeading (midiHeading, "Live MIDI monitor (anything a connected MIDI device sends shows here)");

    styleReadOnlyEditor (reportView);
    styleReadOnlyEditor (midiView);

    scanButton.onClick = [this] { requestScan(); };

    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (reportText);
        copyButton.setButtonText ("Copied");
        copiedCountdown = timerHz * 2;
    };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &mixerRow, &looperRow,
                                                             &scanButton, &copyButton, &reportHeading,
                                                             &reportView, &midiHeading, &midiView })
        addAndMakeVisible (c);

    setSize (920, 780);
    startTimerHz (timerHz);
    requestScan();
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (56);
    auto buttons = header.removeFromRight (250);
    copyButton.setBounds (buttons.removeFromRight (120).reduced (0, 10));
    buttons.removeFromRight (10);
    scanButton.setBounds (buttons.removeFromRight (120).reduced (0, 10));
    title.setBounds (header.removeFromTop (32));
    subtitle.setBounds (header);

    area.removeFromTop (8);
    mixerRow.setBounds (area.removeFromTop (72));
    area.removeFromTop (8);
    looperRow.setBounds (area.removeFromTop (72));

    area.removeFromTop (12);
    auto midiArea = area.removeFromBottom (juce::jmax (140, area.getHeight() / 4));
    midiHeading.setBounds (midiArea.removeFromTop (24));
    midiView.setBounds (midiArea);

    reportHeading.setBounds (area.removeFromTop (24));
    area.removeFromBottom (10);
    reportView.setBounds (area);
}

void MainComponent::requestScan()
{
    // Draw "Looking..." first, then scan a moment later: loading audio
    // drivers can hold the window still for a few seconds.
    mixerRow.setScanning ("Looking for the CQ mixer...");
    looperRow.setScanning ("Looking for the RC-600...");
    reportView.setText ("Scanning audio and MIDI devices. This can take a few seconds.", false);
    scanButton.setEnabled (false);
    copyButton.setEnabled (false);
    scanCountdown = 3;
}

void MainComponent::runScan()
{
    const auto report = scanDevices();
    reportText = report.toText();

    mixerRow.setVerdict (report.mixer);
    looperRow.setVerdict (report.looper);
    reportView.setText (reportText, false);
    reportView.setCaretPosition (0);

    const auto opened = midiMonitor.openAllInputs();
    appendMidiLines ({ opened.isEmpty() ? juce::String ("No MIDI inputs to listen to.")
                                        : "Listening to: " + opened.joinIntoString (", ") });

    scanButton.setEnabled (true);
    copyButton.setEnabled (true);
}

void MainComponent::appendMidiLines (const juce::StringArray& lines)
{
    if (lines.isEmpty())
        return;

    midiLineCount += lines.size();

    if (midiLineCount > maxMidiLinesShown)
    {
        midiView.clear();
        midiLineCount = lines.size();
    }

    midiView.moveCaretToEnd();
    midiView.insertTextAtCaret (lines.joinIntoString ("\n") + "\n");
}

void MainComponent::timerCallback()
{
    if (scanCountdown > 0 && --scanCountdown == 0)
        runScan();

    if (copiedCountdown > 0 && --copiedCountdown == 0)
        copyButton.setButtonText ("Copy report");

    appendMidiLines (midiMonitor.takeLines());
}

} // namespace gigrig
