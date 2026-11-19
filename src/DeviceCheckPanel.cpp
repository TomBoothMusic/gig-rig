#include "DeviceCheckPanel.h"

namespace gigrig
{

namespace
{
    constexpr int timerHz = 20;
    constexpr int maxMidiLinesShown = 500;

    void show (StatusRow& row, const Verdict& verdict)
    {
        switch (verdict.state)
        {
            case Verdict::State::found:   row.set (StatusRow::Tone::good,    "FOUND",     verdict.headline, verdict.detail); break;
            case Verdict::State::partial: row.set (StatusRow::Tone::caution, "PARTLY",    verdict.headline, verdict.detail); break;
            case Verdict::State::missing: row.set (StatusRow::Tone::bad,     "NOT FOUND", verdict.headline, verdict.detail); break;
        }
    }
}

DeviceCheckPanel::DeviceCheckPanel()
{
    style::heading (reportHeading, "Every audio and MIDI device this computer can see");
    style::heading (midiHeading, "Live MIDI monitor (anything a connected MIDI device sends shows here)");

    style::readOnlyEditor (reportView);
    style::readOnlyEditor (midiView);

    scanButton.onClick = [this] { requestScan(); };

    for (auto* c : std::initializer_list<juce::Component*> { &mixerRow, &looperRow, &scanButton, &reportHeading,
                                                             &reportView, &midiHeading, &midiView })
        addAndMakeVisible (c);

    startTimerHz (timerHz);
}

DeviceCheckPanel::~DeviceCheckPanel()
{
    stopTimer();
}

void DeviceCheckPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    mixerRow.setBounds (area.removeFromTop (68));
    area.removeFromTop (6);
    looperRow.setBounds (area.removeFromTop (68));
    area.removeFromTop (8);

    auto midiArea = area.removeFromBottom (juce::jmax (120, area.getHeight() / 3));
    midiHeading.setBounds (midiArea.removeFromTop (24));
    midiView.setBounds (midiArea);

    auto headingRow = area.removeFromTop (30);
    scanButton.setBounds (headingRow.removeFromRight (120).reduced (0, 2));
    reportHeading.setBounds (headingRow);
    area.removeFromBottom (8);
    reportView.setBounds (area);
}

void DeviceCheckPanel::requestScan()
{
    // Draw "Looking..." first, then scan a moment later: loading audio
    // drivers can hold the window still for a few seconds.
    mixerRow.set (StatusRow::Tone::neutral, "LOOKING", "Looking for the CQ mixer...", {});
    looperRow.set (StatusRow::Tone::neutral, "LOOKING", "Looking for the RC-600...", {});
    reportView.setText ("Scanning audio and MIDI devices. This can take a few seconds.", false);
    scanButton.setEnabled (false);
    scanCountdown = 3;
}

void DeviceCheckPanel::runScan()
{
    if (onBeforeScan != nullptr)
        onBeforeScan();

    const auto report = scanDevices();
    text = report.toText();

    show (mixerRow, report.mixer);
    show (looperRow, report.looper);
    reportView.setText (text, false);
    reportView.setCaretPosition (0);

    const auto opened = midiMonitor.openAllInputs();
    appendMidiLines ({ opened.isEmpty() ? juce::String ("No MIDI inputs to listen to.")
                                        : "Listening to: " + opened.joinIntoString (", ") });

    if (onMidiOpened != nullptr)
        onMidiOpened (opened);

    scanButton.setEnabled (true);

    if (onScanned != nullptr)
        onScanned (report);
}

void DeviceCheckPanel::appendMidiLines (const juce::StringArray& lines)
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

void DeviceCheckPanel::timerCallback()
{
    if (scanCountdown > 0 && --scanCountdown == 0)
        runScan();

    appendMidiLines (midiMonitor.takeLines());
}

} // namespace gigrig
