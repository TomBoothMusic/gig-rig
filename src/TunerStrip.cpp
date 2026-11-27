#include "TunerStrip.h"

#include "Style.h"

namespace gigrig
{

TunerStrip::TunerStrip (AudioEngine& e) : engine (e)
{
    onButton.setClickingTogglesState (true);
    onButton.setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.4f));
    onButton.setTooltip ("Switches the tuner on and off. While it is on, play one string: the note and how far out it is show here. With the mixer on, the main mix is silent while you tune.");
    onButton.onClick = [this]
    {
        engine.tuner().setOn (onButton.getToggleState());
        repaint();
    };

    optionsButton.setTooltip ("Which input the tuner listens to, what A is tuned to, and whether the main mix goes silent while tuning");
    optionsButton.onClick = [this] { showOptions(); };

    addAndMakeVisible (onButton);
    addAndMakeVisible (optionsButton);
    startTimerHz (20);
}

void TunerStrip::refresh()
{
    onButton.setToggleState (engine.tuner().isOn(), juce::dontSendNotification);
    repaint();
}

void TunerStrip::timerCallback()
{
    auto& tuner = engine.tuner();
    tuner.tick();

    if (onButton.getToggleState() != tuner.isOn())
        onButton.setToggleState (tuner.isOn(), juce::dontSendNotification);

    if (tuner.isOn())
        repaint();
}

void TunerStrip::showOptions()
{
    auto& tuner = engine.tuner();
    juce::PopupMenu menu, inputs, pitch;

    inputs.addItem (1, "Whichever input is being played", true, tuner.socket() < 0);

    for (int s = 0; s < 16; ++s)
        inputs.addItem (2 + s, "Input " + juce::String (s + 1), true, tuner.socket() == s);

    for (int hz = 436; hz <= 444; ++hz)
        pitch.addItem (100 + hz, "A = " + juce::String (hz) + " Hz" + (hz == 440 ? " (the usual)" : ""), true, std::abs (tuner.reference() - (double) hz) < 0.05);

    pitch.addItem (100 + 432, "A = 432 Hz", true, std::abs (tuner.reference() - 432.0) < 0.05);

    menu.addSubMenu ("Listen to", inputs);
    menu.addSubMenu ("Tune to", pitch);
    menu.addItem (50, "Silence the main mix while tuning (mixer on)", true, tuner.mutes());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (optionsButton), [this] (int picked)
    {
        auto& t = engine.tuner();

        if (picked == 50)               t.setMutes (! t.mutes());
        else if (picked >= 100)         t.setReference ((double) (picked - 100));
        else if (picked >= 1)           t.setSocket (picked - 2);

        repaint();
    });
}

void TunerStrip::resized()
{
    auto area = getLocalBounds();
    optionsButton.setBounds (area.removeFromRight (78).reduced (0, 9));
    area.removeFromRight (6);
    onButton.setBounds (area.removeFromRight (78).reduced (0, 9));
}

void TunerStrip::paint (juce::Graphics& g)
{
    auto& tuner = engine.tuner();

    if (! tuner.isOn())
        return;

    auto area = getLocalBounds().withTrimmedRight (78 + 6 + 78 + 10).reduced (0, 4).toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (area, 6.0f);

    const auto r = tuner.reading();
    const bool close = r.heard && std::abs (r.cents) < 3.0;
    auto note = area.removeFromLeft (76.0f);

    g.setColour (! r.heard ? style::textDim : close ? style::green : style::textMain);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText (r.heard ? r.name : juce::String ("--"), note, juce::Justification::centred);

    auto words = area.removeFromRight (112.0f).reduced (6.0f, 0.0f);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.setColour (style::textDim);
    const int c = (int) std::floor (std::abs (r.cents) + 0.5);
    g.drawFittedText (! r.heard ? "Play one string" + (r.socket >= 0 ? " (input " + juce::String (r.socket + 1) + ")" : juce::String())
                                : (c == 0 ? juce::String ("In tune") : juce::String (c) + (r.cents < 0.0 ? " flat" : " sharp")) + "\n" + juce::String (r.hz, 1) + " Hz, input " + juce::String (r.socket + 1),
                      words.toNearestInt(), juce::Justification::centredLeft, 2);

    // The needle: the middle is in tune, the ends are 50 cents flat and sharp.
    auto bar = area.reduced (8.0f, 14.0f);
    g.setColour (style::background);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (style::green.withAlpha (0.35f));
    g.fillRect (bar.withSizeKeepingCentre (bar.getWidth() * 0.06f, bar.getHeight()));
    g.setColour (style::textDim);

    for (int t = -4; t <= 4; ++t)
        g.fillRect (bar.getCentreX() + bar.getWidth() * 0.5f * (float) t / 5.0f - 0.5f, bar.getY(), 1.0f, t == 0 ? bar.getHeight() : 4.0f);

    if (r.heard)
    {
        const float x = bar.getCentreX() + bar.getWidth() * 0.5f * (float) juce::jlimit (-50.0, 50.0, r.cents) / 50.0f;
        g.setColour (close ? style::green : std::abs (r.cents) < 15.0 ? style::amber : style::red);
        g.fillRoundedRectangle (x - 3.0f, bar.getY() - 4.0f, 6.0f, bar.getHeight() + 8.0f, 2.0f);
    }
}

} // namespace gigrig
