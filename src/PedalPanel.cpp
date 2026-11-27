#include "PedalPanel.h"

namespace gigrig
{

namespace
{
    const int windowChoices[] = { 250, 300, 350, 400, 450, 500, 600, 700, 800 };
    constexpr int followBeatId = 5000;
    constexpr int maxLogLines = 400;
}

//==============================================================================
void SwitchTile::setText (const juce::String& newTitle, const juce::String& newDetail)
{
    if (title != newTitle || detail != newDetail)
    {
        title = newTitle;
        detail = newDetail;
        repaint();
    }
}

void SwitchTile::setDown (bool isDown)
{
    if (down != isDown)
    {
        down = isDown;
        repaint();
    }
}

void SwitchTile::setChecks (bool press, bool hold, bool doublePress)
{
    if (! showChecks || seenPress != press || seenHold != hold || seenDouble != doublePress)
    {
        showChecks = true;
        seenPress = press;
        seenHold = hold;
        seenDouble = doublePress;
        repaint();
    }
}

void SwitchTile::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (down ? style::textMain.withAlpha (0.28f) : style::panel);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (down ? style::textMain : style::textDim.withAlpha (0.45f));
    g.drawRoundedRectangle (area, 6.0f, down ? 2.0f : 1.0f);

    area = area.reduced (8.0f, 5.0f);

    if (showChecks)
    {
        auto checks = area.removeFromBottom (18.0f);
        const auto width = checks.getWidth() / 3.0f;
        const bool seen[] = { seenPress, seenHold, seenDouble };
        const char* words[] = { "press", "hold", "double" };

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

        for (int i = 0; i < 3; ++i)
        {
            auto pill = checks.removeFromLeft (width).reduced (2.0f, 0.0f);

            // Filled means seen, an empty outline means not yet: the shape says it, not only the colour.
            if (seen[i])
            {
                g.setColour (style::green);
                g.fillRoundedRectangle (pill, 4.0f);
                g.setColour (style::background);
            }
            else
            {
                g.setColour (style::textDim.withAlpha (0.5f));
                g.drawRoundedRectangle (pill.reduced (0.5f), 4.0f, 1.0f);
            }

            g.drawText (words[i], pill, juce::Justification::centred);
        }

        area.removeFromBottom (3.0f);
    }

    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (title, area.removeFromTop (19.0f), juce::Justification::centredLeft);

    g.setColour (style::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText (detail, area.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
}

void SwitchTile::mouseDown (const juce::MouseEvent&)
{
    if (onSwitch != nullptr)
        onSwitch (true);
}

void SwitchTile::mouseUp (const juce::MouseEvent&)
{
    if (onSwitch != nullptr)
        onSwitch (false);
}

void layOutSwitches (juce::Rectangle<int> area, juce::OwnedArray<SwitchTile>& tiles)
{
    if (tiles.size() < numPedalSwitches)
        return;

    const int gap = 6;
    const auto columnWidth = (area.getWidth() - 3 * gap) / 4;
    const auto rowHeight = (area.getHeight() - 2 * gap) / 3;

    for (int sw = 0; sw < 9; ++sw)
        tiles[sw]->setBounds (area.getX() + (sw % 3) * (columnWidth + gap), area.getY() + (sw / 3) * (rowHeight + gap), columnWidth, rowHeight);

    for (int i = 0; i < 2; ++i)
        tiles[9 + i]->setBounds (area.getX() + 3 * (columnWidth + gap), area.getY() + i * (rowHeight + gap), columnWidth, rowHeight);
}

//==============================================================================
PedalPanel::PedalPanel (PedalInput& pedalToUse, ExpressionInput& expressionToUse)
    : pedal (pedalToUse), expression (expressionToUse)
{
    // ---- a real expression pedal (docs/songs.md, "The expression pedal") ----
    style::body (expLabel, "Expression pedal");
    expLabel.setTooltip ("Any pedal that arrives as MIDI: one in the RC-600's EXP jack given a CC number there, or a USB one. It works the blend (every Volume pedal effect), and the wah or whammy while the toe switch is on.");
    expStatus.setFont (style::monoFont (13.0f));
    expStatus.setColour (juce::Label::textColourId, style::textMain);
    expStatus.setColour (juce::Label::backgroundColourId, style::panel);
    expStatus.setMinimumHorizontalScale (0.6f);
    learnButton.setTooltip ("Press, rock the pedal all the way back and all the way forward a couple of times, then press Done");
    learnButton.onClick = [this]
    {
        if (expression.isLearning())
        {
            expression.finishLearning();
            expression.saveState();
        }
        else
            expression.startLearning();

        shownExp.clear();
    };
    swapButton.setTooltip ("For a pedal that works backwards: toe down becomes heel down");
    swapButton.onClick = [this]
    {
        auto s = expression.setup();
        std::swap (s.heel, s.toe);
        expression.setSetup (s);
        expression.saveState();
        shownExp.clear();
    };
    forgetButton.setTooltip ("Forgets the pedal, so no MIDI controller moves the blend");
    forgetButton.onClick = [this]
    {
        expression.setSetup ({});
        expression.saveState();
        shownExp.clear();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &expLabel, &expStatus, &learnButton, &swapButton, &forgetButton })
        addAndMakeVisible (c);

    style::body (help, "On the RC-600 choose memory 65 (MIDI CTRL). Then, one switch at a time: press it once, press it twice "
                       "quickly, and hold it down for 2 seconds. Each tile fills in as the app sees it. You can also click a tile with the mouse.");
    help.setColour (juce::Label::textColourId, style::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setMinimumHorizontalScale (1.0f);

    for (int sw = 0; sw < numPedalSwitches; ++sw)
    {
        auto* tile = tiles.add (new SwitchTile());
        tile->setText (pedalSwitchName (sw), "CC " + juce::String (firstPedalCC + sw) + "  " + pedalSwitchRole (sw));
        tile->setChecks (false, false, false);
        tile->onSwitch = [this, sw] (bool isDown) { pedal.screenSwitch (sw, isDown); };
        addAndMakeVisible (tile);
    }

    style::body (windowLabel, "Double-press: two presses within");
    style::body (windowNow, {});
    windowNow.setColour (juce::Label::textColourId, style::textDim);
    addAndMakeVisible (windowNow);
    style::combo (windowBox);

    windowBox.addItem ("one beat of the song", followBeatId);

    for (auto ms : windowChoices)
        windowBox.addItem (juce::String (ms) + " ms", ms);

    windowBox.setSelectedId (pedal.followsBeat() ? followBeatId : pedal.doubleWindowMs(), juce::dontSendNotification);
    windowBox.setTooltip ("One beat of the song: two presses a beat apart (say on beats 3 and 4) count as a double-press, whatever the tempo. "
                          "Or choose a fixed time. If double-presses are missed, go longer; if two separate presses are taken as one double-press, go shorter.");
    windowBox.onChange = [this]
    {
        const auto id = windowBox.getSelectedId();
        pedal.setFollowBeat (id == followBeatId);

        if (id != followBeatId && id > 0)
            pedal.setDoubleWindowMs (id);
    };

    resetButton.setTooltip ("Clears the ticks and the counts so the test can be done again");
    resetButton.onClick = [this]
    {
        pedal.resetSeen();
        pedal.resetCounts();
        logView.clear();
        logLines = 0;
    };

    copyButton.setTooltip ("Copies what each switch has shown, ready to paste to Claude");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (pedal.reportText());
        copyButton.setButtonText ("Copied");
        copiedTicks = 40;
    };

    style::heading (logHeading, "What the app saw, as it happened");
    style::readOnlyEditor (logView);

    for (auto* c : std::initializer_list<juce::Component*> { &status, &help, &windowLabel, &windowBox, &resetButton, &copyButton, &logHeading, &logView })
        addAndMakeVisible (c);

    startTimerHz (20);
}

PedalPanel::~PedalPanel()
{
    stopTimer();
}

void PedalPanel::resized()
{
    auto area = getLocalBounds().reduced (0, 10);

    status.setBounds (area.removeFromTop (68));
    area.removeFromTop (6);
    help.setBounds (area.removeFromTop (40));
    area.removeFromTop (4);

    layOutSwitches (area.removeFromTop (3 * 70 + 12), tiles);
    area.removeFromTop (10);

    auto row = area.removeFromTop (28);
    windowLabel.setBounds (row.removeFromLeft (230));
    windowBox.setBounds (row.removeFromLeft (190));
    row.removeFromLeft (8);
    windowNow.setBounds (row.removeFromLeft (150));
    copyButton.setBounds (row.removeFromRight (160));
    row.removeFromRight (8);
    resetButton.setBounds (row.removeFromRight (110));

    area.removeFromTop (8);
    row = area.removeFromTop (26);
    expLabel.setBounds (row.removeFromLeft (120));
    forgetButton.setBounds (row.removeFromRight (104));
    row.removeFromRight (4);
    swapButton.setBounds (row.removeFromRight (136));
    row.removeFromRight (4);
    learnButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    expStatus.setBounds (row);

    area.removeFromTop (8);
    logHeading.setBounds (area.removeFromTop (24));
    logView.setBounds (area);
}

void PedalPanel::timerCallback()
{
    if (const auto line = expression.statusLine() + (expression.isLearning() ? "L" : ""); line != shownExp)
    {
        shownExp = line;
        expStatus.setText (expression.statusLine(), juce::dontSendNotification);
        learnButton.setButtonText (expression.isLearning() ? "Done" : "Learn pedal");
        swapButton.setEnabled (expression.isSetUp() && ! expression.isLearning());
        forgetButton.setEnabled (expression.isSetUp() && ! expression.isLearning());
    }

    const auto seen = pedal.seen();
    int complete = 0, started = 0;

    for (int sw = 0; sw < numPedalSwitches; ++sw)
    {
        const auto& s = seen[(size_t) sw];
        tiles[sw]->setDown (s.down);
        tiles[sw]->setChecks (s.presses > 0, s.holds > 0, s.doubles > 0);
        complete += s.presses > 0 && s.holds > 0 && s.doubles > 0 ? 1 : 0;
        started += s.presses > 0 ? 1 : 0;
    }

    const bool connected = pedal.rc600Connected();
    const auto key = juce::String ((int) connected) + "/" + juce::String (complete) + "/" + juce::String (started);

    if (key != shownStatus)
    {
        shownStatus = key;
        const auto progress = juce::String (complete) + " of " + juce::String (numPedalSwitches) + " switches have shown a press, a hold and a double-press.";

        if (complete == numPedalSwitches)
            status.set (StatusRow::Tone::good, "ALL SEEN", "Every switch has shown a press, a hold and a double-press",
                        "Press Copy pedal results and paste them to Claude.");
        else if (connected)
            status.set (StatusRow::Tone::good, "CONNECTED", "RC-600 is connected over USB",
                        progress + " CTL3 and CTL4 only count if footswitches are plugged into the CTL 3/4 socket.");
        else
            status.set (StatusRow::Tone::caution, "NO RC-600", "The RC-600 is not showing as a MIDI device",
                        "Plug in its USB lead and press Scan again on the Device check tab. The tiles still work with the mouse. " + progress);
    }

    const auto lines = pedal.takeLines();

    if (! lines.isEmpty())
    {
        logLines += lines.size();

        if (logLines > maxLogLines)
        {
            logView.clear();
            logLines = lines.size();
        }

        logView.moveCaretToEnd();
        logView.insertTextAtCaret (lines.joinIntoString ("\n") + "\n");
    }

    const auto nowText = pedal.followsBeat() ? "now " + juce::String (pedal.doubleWindowMs()) + " ms" : juce::String();

    if (windowNow.getText() != nowText)
        windowNow.setText (nowText, juce::dontSendNotification);

    if (copiedTicks > 0 && --copiedTicks == 0)
        copyButton.setButtonText ("Copy pedal results");
}

} // namespace gigrig
