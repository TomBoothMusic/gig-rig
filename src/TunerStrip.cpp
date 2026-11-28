#include "TunerStrip.h"

#include "Style.h"

namespace gigrig
{

TunerStrip::TunerStrip (AudioEngine& e) : engine (e)
{
    onButton.setClickingTogglesState (true);
    onButton.setColour (juce::TextButton::buttonOnColourId, style::green.darker (0.4f));
    onButton.setTooltip ("Switches the tuner on and off. While it is on, play one string. The tuner fills the screen; with the mixer on, the guitars are silent while you tune and the microphone stays live.");
    onButton.onClick = [this]
    {
        engine.tuner().setOn (onButton.getToggleState());
        repaint();
    };

    optionsButton.setTooltip ("What goes silent while tuning, which input the tuner listens to, what A is tuned to, and whether the tuner fills the screen");
    optionsButton.onClick = [this] { showOptions (engine, optionsButton); };

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

void TunerStrip::showOptions (AudioEngine& engine, juce::Component& target)
{
    auto& tuner = engine.tuner();
    juce::PopupMenu menu, quiet, guitars, inputs, pitch;
    const auto how = tuner.silence();

    quiet.addItem (60, "The guitars (voices and the stomp stay live)", true, how == Tuner::Silence::guitars);
    quiet.addItem (61, "The whole main mix (monitors stay live)", true, how == Tuner::Silence::mainMix);
    quiet.addItem (62, "Nothing", true, how == Tuner::Silence::nothing);

    // Which mixer channels count as guitars. The names decide to start with; a tick here wins.
    juce::StringArray channels;

    for (const auto& ch : engine.mixer().setup().channel)
        if (ch.on && ch.name.isNotEmpty())
            channels.addIfNotAlreadyThere (ch.name);

    for (int i = 0; i < channels.size(); ++i)
        guitars.addItem (200 + i, channels[i], true, tuner.goesSilent (channels[i]));

    if (channels.isEmpty())
        guitars.addItem (199, "The mixer has no channels switched on", false, false);

    inputs.addItem (1, "Whichever guitar is being played", true, tuner.socket() < 0);

    for (int s = 0; s < 16; ++s)
        inputs.addItem (2 + s, "Input " + juce::String (s + 1), true, tuner.socket() == s);

    for (int hz = 436; hz <= 444; ++hz)
        pitch.addItem (500 + hz, "A = " + juce::String (hz) + " Hz" + (hz == 440 ? " (the usual)" : ""), true, std::abs (tuner.reference() - (double) hz) < 0.05);

    pitch.addItem (500 + 432, "A = 432 Hz", true, std::abs (tuner.reference() - 432.0) < 0.05);

    menu.addSubMenu ("While tuning, silence (mixer on)", quiet);
    menu.addSubMenu ("Guitar channels (the ones that go silent)", guitars);
    menu.addItem (51, "Keep the guitars in the monitors while tuning", how == Tuner::Silence::guitars, tuner.monitorsKeepGuitars());
    menu.addSubMenu ("Listen to", inputs);
    menu.addSubMenu ("Tune to", pitch);
    menu.addItem (50, "Fill the screen while tuning", true, tuner.fillsScreen());

    juce::Component::SafePointer<juce::Component> safe (&target);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withStandardItemHeight (34), [&engine, channels, safe] (int picked)
    {
        auto& t = engine.tuner();

        if (picked == 50)               t.setFillsScreen (! t.fillsScreen());
        else if (picked == 51)          t.setMonitorsKeepGuitars (! t.monitorsKeepGuitars());
        else if (picked == 60)          t.setSilence (Tuner::Silence::guitars);
        else if (picked == 61)          t.setSilence (Tuner::Silence::mainMix);
        else if (picked == 62)          t.setSilence (Tuner::Silence::nothing);
        else if (picked >= 500)         t.setReference ((double) (picked - 500));
        else if (picked >= 200)         t.setGoesSilent (channels[picked - 200], ! t.goesSilent (channels[picked - 200]));
        else if (picked >= 1)           t.setSocket (picked - 2);

        if (safe != nullptr)
            if (auto* top = safe->getTopLevelComponent())
                top->repaint();
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
