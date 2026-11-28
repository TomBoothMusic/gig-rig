#include "StrobeTuner.h"

#include <cmath>

namespace gigrig
{

const juce::Colour StrobeTuner::dark   { 0xff08060d };
const juce::Colour StrobeTuner::purple { 0xffb14dff };
const juce::Colour StrobeTuner::lilac  { 0xfff0e0ff };
const juce::Colour StrobeTuner::dim    { 0xff2a1642 };

StrobeTuner::StrobeTuner (AudioEngine& e) : engine (e)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);

    for (auto* b : { &doneButton, &optionsButton })
    {
        b->setColour (juce::TextButton::buttonColourId, dim);
        b->setColour (juce::TextButton::textColourOffId, lilac);
        addAndMakeVisible (*b);
    }

    doneButton.setTooltip ("Switches the tuner off and goes back to the screen underneath");
    doneButton.onClick = [this]
    {
        pretending = false;
        engine.tuner().setOn (false);
        timerCallback();
    };

    optionsButton.setTooltip ("What goes silent while tuning, which input the tuner listens to, and what A is tuned to");
    optionsButton.onClick = [this]
    {
        if (onOptions != nullptr)
            onOptions (optionsButton);
    };

    setVisible (false);
    startTimerHz (8);
}

void StrobeTuner::pretend (bool on)
{
    pretending = on;
    timerCallback();
}

void StrobeTuner::timerCallback()
{
    auto& tuner = engine.tuner();
    const bool want = pretending || (tuner.isOn() && tuner.fillsScreen());

    if (want != isVisible())
    {
        setVisible (want);
        sinceWords = 1000;
        startTimerHz (want ? 60 : 8);

        if (want)
            toFront (false);
    }

    if (! want)
        return;

    tuner.tick();

    if (pretending)
        pretendTurn += 0.19 / 60.0;     // an A string 3 cents sharp slips about a fifth of a cycle a second

    // Which channels are silent: worked out twice a second, not sixty times.
    if (++sinceWords > 30)
    {
        sinceWords = 0;
        const auto silent = engine.tunerChannels (true), live = engine.tunerChannels (false);
        const auto how = tuner.silence();

        if (! engine.mixer().isOn())
            words = "The mixer is off, so nothing goes silent: mute the guitar on the real mixer.";
        else if (how == Tuner::Silence::mainMix)
            words = "The whole main mix is silent. The monitors and the break music carry on.";
        else if (how == Tuner::Silence::nothing)
            words = "Nothing is silent: the guitar can be heard while you tune.";
        else if (silent.isEmpty())
            words = "No channel is named as a guitar, so nothing is silent. Options, Guitar channels.";
        else
            words = (tuner.monitorsKeepGuitars() ? "Silent in the PA (still in the monitors): " : "Silent everywhere: ") + silent.joinIntoString (", ") + "."
                        + (live.isEmpty() ? juce::String() : "   Still live: " + live.joinIntoString (", ") + ".");
    }

    repaint();
}

void StrobeTuner::resized()
{
    auto top = getLocalBounds().reduced (18, 14).removeFromTop (juce::jmax (48, getHeight() / 12));
    doneButton.setBounds (top.removeFromRight (juce::jmax (130, getWidth() / 8)));
    top.removeFromRight (12);
    optionsButton.setBounds (top.removeFromRight (juce::jmax (130, getWidth() / 8)));
}

void StrobeTuner::paint (juce::Graphics& g)
{
    g.fillAll (dark);

    auto& tuner = engine.tuner();
    auto r = tuner.reading();

    if (pretending)
    {
        r.heard = r.strobing = true;
        r.name = "A2";
        r.midi = 45;
        r.cents = 3.0;
        r.hz = 110.0 * std::pow (2.0, 3.0 / 1200.0);
        r.strobe = pretendTurn;
        r.strength = 0.9f;
        r.socket = 7;
    }

    const auto all = getLocalBounds().toFloat().reduced (18.0f, 14.0f);
    const float h = all.getHeight(), w = all.getWidth();
    auto area = all;
    auto header = area.removeFromTop (juce::jmax (48.0f, (float) getHeight() / 12.0f));

    g.setColour (purple.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (header.getHeight() * 0.5f, juce::Font::bold)));
    g.drawText ("TUNER", header.removeFromLeft (w * 0.2f), juce::Justification::centredLeft);
    g.setColour (purple.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (13.0f, header.getHeight() * 0.3f))));
    g.drawText ("A = " + juce::String (tuner.reference(), 0) + " Hz" + (r.socket >= 0 ? ",  input " + juce::String (r.socket + 1) : juce::String()),
                header.withTrimmedRight (2.0f * (float) juce::jmax (130, getWidth() / 8) + 30.0f), juce::Justification::centredLeft);

    auto footer = area.removeFromBottom (juce::jmax (26.0f, h * 0.06f));
    g.setColour (purple.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (13.0f, footer.getHeight() * 0.55f))));
    g.drawFittedText (words, footer.toNearestInt(), juce::Justification::centred, 1);

    // ---- the note, with flat on its left and sharp on its right ----
    auto noteArea = area.removeFromTop (area.getHeight() * 0.46f);
    const bool inTune = r.heard && std::abs (r.cents) < 1.0;
    const auto name = r.heard ? r.name.retainCharacters ("ABCDEFG#") : juce::String ("-");
    const auto octave = r.heard ? r.name.retainCharacters ("-0123456789") : juce::String();
    const float big = noteArea.getHeight() * 0.86f;
    juce::Font noteFont (juce::FontOptions (big, juce::Font::bold));
    const float nameWidth = juce::GlyphArrangement::getStringWidth (noteFont, name);

    g.setColour (! r.heard ? dim.brighter (0.25f) : inTune ? lilac : purple);
    g.setFont (noteFont);
    g.drawText (name, noteArea, juce::Justification::centred);

    if (octave.isNotEmpty())
    {
        g.setFont (juce::Font (juce::FontOptions (big * 0.3f, juce::Font::bold)));
        g.setColour ((inTune ? lilac : purple).withAlpha (0.7f));
        g.drawText (octave, juce::Rectangle<float> (noteArea.getCentreX() + nameWidth * 0.5f + 8.0f, noteArea.getBottom() - big * 0.46f, big * 0.5f, big * 0.34f), juce::Justification::centredLeft);
    }

    const auto side = [&] (bool sharp)
    {
        const bool lit = r.heard && ! inTune && (r.cents > 0.0) == sharp;
        auto box = sharp ? noteArea.withTrimmedLeft (w * 0.72f) : noteArea.withTrimmedRight (w * 0.72f);
        const float s = juce::jmin (box.getWidth() * 0.3f, box.getHeight() * 0.3f);
        const auto mid = box.getCentre().translated (0.0f, -s * 0.35f);
        juce::Path arrow;

        // The arrows point the way the bands are sliding: left for flat, right for sharp.
        if (sharp)  arrow.addTriangle (mid.x + s * 0.5f, mid.y, mid.x - s * 0.5f, mid.y - s * 0.6f, mid.x - s * 0.5f, mid.y + s * 0.6f);
        else        arrow.addTriangle (mid.x - s * 0.5f, mid.y, mid.x + s * 0.5f, mid.y - s * 0.6f, mid.x + s * 0.5f, mid.y + s * 0.6f);

        g.setColour (lit ? purple : dim);
        g.fillPath (arrow);
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (14.0f, s * 0.42f), juce::Font::bold)));
        g.drawText (sharp ? "SHARP" : "FLAT", box.withTop (mid.y + s * 0.75f).withHeight (s * 0.6f), juce::Justification::centred);
    };

    side (false);
    side (true);

    // ---- the words under the note ----
    auto line = area.removeFromTop (juce::jmax (30.0f, h * 0.085f));
    g.setFont (juce::Font (juce::FontOptions (line.getHeight() * 0.62f, juce::Font::bold)));
    g.setColour (! r.heard ? purple.withAlpha (0.45f) : inTune ? lilac : purple);
    const double c = std::abs (r.cents);
    g.drawText (! r.heard ? juce::String ("Play one string")
                          : inTune ? juce::String ("In tune")
                                   : juce::String (c, c < 10.0 ? 1 : 0) + (c < 1.05 ? " cent " : " cents ") + (r.cents < 0.0 ? "flat" : "sharp"),
                line, juce::Justification::centred);

    // ---- the strobe ----
    area.removeFromTop (h * 0.02f);
    area.removeFromBottom (h * 0.02f);
    const int rows = 4;
    const float gap = juce::jmax (4.0f, area.getHeight() * 0.035f);
    const float rowHeight = (area.getHeight() - gap * (float) (rows - 1)) / (float) rows;
    const double noteHz = tuner.reference() * std::pow (2.0, (double) (r.midi - 69) / 12.0);
    const double slip = r.heard ? std::abs (r.hz - noteHz) : 0.0;       // cycles a second
    const float glow = ! r.heard ? 0.16f : 0.45f + 0.55f * r.strength;

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    for (int row = 0; row < rows; ++row)
    {
        // The coarse row is at the bottom, as the outer ring of a strobe disc is the finest.
        const auto band = juce::Rectangle<float> (area.getX(), area.getBottom() - rowHeight * (float) (row + 1) - gap * (float) row, area.getWidth(), rowHeight);
        const int times = 1 << row;
        const float period = area.getWidth() / (float) (3 * times);
        const double turn = r.strobe * (double) times;
        const float offset = (float) (turn - std::floor (turn)) * period;

        // A row slipping faster than the eye (or the screen) can follow becomes an even glow, as on a real strobe.
        const float blur = (float) juce::jlimit (0.0, 1.0, (slip * (double) times - 7.0) / 9.0);

        g.setColour (dim.withAlpha (0.55f));
        g.fillRoundedRectangle (band, 5.0f);

        if (blur > 0.0f)
        {
            g.setColour (purple.withAlpha (glow * 0.42f * blur));
            g.fillRoundedRectangle (band, 5.0f);
        }

        if (blur < 1.0f)
        {
            g.setColour (purple.withAlpha (glow * (1.0f - blur)));

            for (float x = band.getX() - period + offset; x < band.getRight(); x += period)
                g.fillRoundedRectangle (x, band.getY(), period * 0.5f, band.getHeight(), juce::jmin (5.0f, period * 0.12f));
        }
    }

    g.restoreState();

    // The middle mark: where a band's edge rests means nothing, only whether it moves.
    g.setColour (lilac.withAlpha (inTune ? 0.9f : 0.25f));
    g.fillRect (area.getCentreX() - 1.0f, area.getY() - 6.0f, 2.0f, 6.0f);
    g.fillRect (area.getCentreX() - 1.0f, area.getBottom(), 2.0f, 6.0f);
}

} // namespace gigrig
