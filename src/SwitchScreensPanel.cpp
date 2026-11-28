#include "SwitchScreensPanel.h"

namespace gigrig
{

SwitchScreensPanel::SwitchScreensPanel (SwitchScreens& s) : screens (s)
{
    style::heading (heading, "Switch screens: the small screen on each footswitch");
    style::body (explain,
                 "These are the pictures the board's footswitch screens show, drawn here exactly as the controller draws them, so the looks can be "
                 "judged before the board exists. They follow the rig: load a song and press the switches (or click them on the Looper tab) and watch the colours change.\n\n"
                 "With the controller (the Teensy) and its three bench screens plugged in, tick the box and the real screens show the same. "
                 "Unticked, nothing is opened or sent, and the controller turns through its sample pages by itself.");
    explain.setJustificationType (juce::Justification::topLeft);
    style::body (lookLabel, "An effect switch that is off looks like");
    style::body (showLabel, "The screens show");
    style::body (pageLabel, "");
    style::body (benchLabel, "The three bench screens show");

    onBox.setColour (juce::ToggleButton::textColourId, style::textMain);
    onBox.setColour (juce::ToggleButton::tickColourId, style::green);

    lookBox.addItem ("A border in the chain's colour, black inside", 1);
    lookBox.addItem ("A white outline, black inside", 2);
    lookBox.addItem ("White all over", 3);
    showBox.addItem ("What the rig is doing", 1);
    showBox.addItem ("The sample pages, turning by themselves", 2);
    showBox.addItem ("One sample page, held", 3);

    for (auto* box : { &lookBox, &showBox })
        style::combo (*box);

    for (auto& box : benchBox)
    {
        style::combo (box);
        for (int sw = 0; sw < numPedalSwitches; ++sw)
            box.addItem (pedalSwitchName (sw), sw + 1);
        box.onChange = [this] { apply(); };
        addAndMakeVisible (box);
    }

    onBox.onClick = [this] { apply(); };
    lookBox.onChange = [this] { apply(); };
    showBox.onChange = [this] { apply(); };
    previousButton.onClick = [this]
    {
        auto st = screens.settings();
        st.page = (samplePageNow() + tile::numDemoPages() - 1) % tile::numDemoPages();
        st.show = tile::showDemoPage;
        screens.setSettings (st);
        screens.saveState();
        showSettings();
    };
    nextButton.onClick = [this]
    {
        auto st = screens.settings();
        st.page = (samplePageNow() + 1) % tile::numDemoPages();
        st.show = tile::showDemoPage;
        screens.setSettings (st);
        screens.saveState();
        showSettings();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &explain, &onBox, &status, &lookLabel, &lookBox, &showLabel, &showBox,
                                                             &previousButton, &nextButton, &pageLabel, &benchLabel })
        addAndMakeVisible (c);

    startedAt = juce::Time::getMillisecondCounter();
    showSettings();
    timerCallback();
    startTimer (100);
}

SwitchScreensPanel::~SwitchScreensPanel()
{
    stopTimer();
}

void SwitchScreensPanel::showSettings()
{
    const auto st = screens.settings();
    onBox.setToggleState (st.send, juce::dontSendNotification);
    lookBox.setSelectedId (st.offLook + 1, juce::dontSendNotification);
    showBox.setSelectedId (st.show + 1, juce::dontSendNotification);

    for (int i = 0; i < (int) benchBox.size(); ++i)
        benchBox[(size_t) i].setSelectedId (st.bench[(size_t) i] + 1, juce::dontSendNotification);
}

void SwitchScreensPanel::apply()
{
    auto st = screens.settings();
    st.send = onBox.getToggleState();
    st.offLook = juce::jmax (0, lookBox.getSelectedId() - 1);
    st.show = juce::jmax (0, showBox.getSelectedId() - 1);

    if (st.show == tile::showDemoPage && screens.settings().show != tile::showDemoPage)
        st.page = samplePageNow();      // holds the page that was showing

    for (int i = 0; i < (int) benchBox.size(); ++i)
        st.bench[(size_t) i] = juce::jmax (0, benchBox[(size_t) i].getSelectedId() - 1);

    screens.setSettings (st);
    screens.saveState();
    timerCallback();
}

int SwitchScreensPanel::samplePageNow() const
{
    const auto st = screens.settings();

    if (st.show == tile::showDemoPage)
        return st.page;

    // Turning: the controller's own page when it is answering, otherwise this tab keeps its own time.
    if (st.send && screens.controllerAnswering())
        return juce::jlimit (0, tile::numDemoPages() - 1, screens.controllerStatus().page);

    return (int) ((juce::Time::getMillisecondCounter() - startedAt) / 6000u) % tile::numDemoPages();
}

bool SwitchScreensPanel::refresh (Shown& s, const tile::Tile& t, const juce::String& caption, const juce::String& under)
{
    const bool flash = t.flashCentis > 0 && ((juce::Time::getMillisecondCounter() / ((juce::uint32) t.flashCentis * 10u)) & 1u) != 0;
    const bool changed = ! s.drawn || flash != s.flashOn || ! tile::sameTile (t, s.tile) || caption != s.caption || under != s.under;

    if (changed)
    {
        s.tile = t;
        s.flashOn = flash;
        s.drawn = true;
        s.caption = caption;
        s.under = under;
        s.image = SwitchScreens::picture (t, flash);
    }

    return changed;
}

void SwitchScreensPanel::timerCallback()
{
    using Tone = StatusRow::Tone;
    const auto st = screens.settings();

    if (! st.send)
        status.set (Tone::neutral, "OFF", "Nothing is sent to the board", "The pictures below still follow the rig. The controller shows its own sample pages.");
    else if (screens.controllerAnswering())
        status.set (Tone::good, "CONNECTED", "The board's controller is showing these", screens.statusLine());
    else if (screens.outputIsOpen())
        status.set (Tone::caution, "NO ANSWER", "The controller is plugged in but has not answered", screens.statusLine());
    else
        status.set (Tone::caution, "WAITING", "Waiting for the board's controller", "Plug the Teensy in by USB. It is found by itself within a couple of seconds.");

    bool changed = false;
    int count = 0;

    if (st.show == tile::showRig)
    {
        for (int sw = 0; sw < numPedalSwitches; ++sw)
        {
            juce::String under;
            for (int b = 0; b < (int) st.bench.size(); ++b)
                if (st.bench[(size_t) b] == sw)
                    under << (under.isEmpty() ? "bench screen " : " and ") << (b + 1);

            changed = refresh (shown[(size_t) count++], screens.face (sw).tile, pedalSwitchName (sw), under) || changed;
        }

        pageLabel.setText ({}, juce::dontSendNotification);
    }
    else
    {
        const int page = samplePageNow();

        for (int i = 0; i < tile::demoTilesPerPage; ++i)
            changed = refresh (shown[(size_t) count++], tile::demoTile (page, i), "Bench screen " + juce::String (i + 1), {}) || changed;

        pageLabel.setText ("Sample page " + juce::String (page + 1) + " of " + juce::String (tile::numDemoPages()) + ": " + tile::demoPageName (page), juce::dontSendNotification);
    }

    if (count != numShown)
    {
        numShown = count;
        changed = true;
    }

    const bool samples = st.show != tile::showRig;
    previousButton.setVisible (samples);
    nextButton.setVisible (samples);

    for (auto& box : benchBox)
        box.setEnabled (! samples);

    if (changed)
        repaint (tileArea);
}

void SwitchScreensPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::background);

    if (numShown <= 0 || tileArea.isEmpty())
        return;

    const int perRow = numShown <= 3 ? 3 : 6, rows = (numShown + perRow - 1) / perRow, gap = 12, captionHeight = 38;
    const int size = juce::jmin (numShown <= 3 ? 240 : 170, (tileArea.getWidth() - gap * (perRow - 1)) / perRow, (tileArea.getHeight() - rows * captionHeight - gap * (rows - 1)) / rows);

    if (size < 40)
        return;

    for (int i = 0; i < numShown; ++i)
    {
        const auto& s = shown[(size_t) i];
        const int x = tileArea.getX() + (i % perRow) * (size + gap), y = tileArea.getY() + (i / perRow) * (size + captionHeight + gap);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (s.image, juce::Rectangle<float> ((float) x, (float) y, (float) size, (float) size));
        g.setColour (juce::Colours::black);
        g.drawRect (x, y, size, size, 1);
        g.setColour (style::textMain);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (s.caption, x, y + size + 2, size, 18, juce::Justification::centredLeft);
        g.setColour (style::textDim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (s.under, x, y + size + 19, size, 16, juce::Justification::centredLeft);
    }
}

void SwitchScreensPanel::resized()
{
    auto area = getLocalBounds().reduced (4, 8);
    heading.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    explain.setBounds (area.removeFromTop (84));
    area.removeFromTop (4);
    onBox.setBounds (area.removeFromTop (28).removeFromLeft (560));
    area.removeFromTop (6);
    status.setBounds (area.removeFromTop (54));
    area.removeFromTop (8);

    auto row = area.removeFromTop (28);
    lookLabel.setBounds (row.removeFromLeft (260));
    lookBox.setBounds (row.removeFromLeft (330));
    area.removeFromTop (6);

    row = area.removeFromTop (28);
    showLabel.setBounds (row.removeFromLeft (260));
    showBox.setBounds (row.removeFromLeft (330));
    row.removeFromLeft (12);
    previousButton.setBounds (row.removeFromLeft (120).reduced (0, 1));
    row.removeFromLeft (8);
    nextButton.setBounds (row.removeFromLeft (100).reduced (0, 1));
    area.removeFromTop (6);

    row = area.removeFromTop (28);
    benchLabel.setBounds (row.removeFromLeft (260));
    for (auto& box : benchBox)
    {
        box.setBounds (row.removeFromLeft (104));
        row.removeFromLeft (9);
    }

    area.removeFromTop (4);
    pageLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    tileArea = area;
}

} // namespace gigrig
