#include "Style.h"

namespace gigrig
{

namespace style
{
    juce::Font monoFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }

    void readOnlyEditor (juce::TextEditor& editor)
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

    void heading (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, textDim);
    }

    void body (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (14.0f)));
        label.setColour (juce::Label::textColourId, textMain);
        label.setMinimumHorizontalScale (0.8f);
    }

    void combo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, panel);
        box.setColour (juce::ComboBox::textColourId, textMain);
        box.setColour (juce::ComboBox::outlineColourId, textDim.withAlpha (0.4f));
    }
}

//==============================================================================
void StatusRow::set (Tone newTone, const juce::String& newTag, const juce::String& newHeadline, const juce::String& newDetail)
{
    tone = newTone;
    tag = newTag;
    headline = newHeadline;
    detail = newDetail;
    repaint();
}

void StatusRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (area, 6.0f);

    const auto colour = tone == Tone::good ? style::green
                      : tone == Tone::caution ? style::amber
                      : tone == Tone::bad ? style::red
                                          : style::textDim;

    auto tagArea = area.removeFromLeft (112.0f).reduced (10.0f, 14.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (tagArea, 4.0f);
    g.setColour (style::background);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (tag, tagArea, juce::Justification::centred);

    auto textArea = area.reduced (6.0f, 8.0f);
    g.setColour (style::textMain);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (headline, textArea.removeFromTop (22.0f), juce::Justification::centredLeft);

    g.setColour (style::textDim);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawFittedText (detail, textArea.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
}

//==============================================================================
namespace
{
    constexpr float meterFloorDb = -60.0f;
}

MeterStrip::MeterStrip (int count, int first)
    : shownDb ((size_t) count, meterFloorDb), firstNumber (first)
{
}

void MeterStrip::setLevel (int index, float peak)
{
    if (index < 0 || index >= (int) shownDb.size())
        return;

    // Jump up at once, fall back slowly, so a short peak can still be seen.
    const auto db = juce::jlimit (meterFloorDb, 0.0f, juce::Decibels::gainToDecibels (peak, meterFloorDb));
    auto& shown = shownDb[(size_t) index];
    const auto next = juce::jmax (db, shown - 3.0f);

    if (! juce::approximatelyEqual (next, shown))
    {
        shown = next;
        repaint();
    }
}

void MeterStrip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto slot = area.getWidth() / (float) shownDb.size();

    for (size_t i = 0; i < shownDb.size(); ++i)
    {
        auto column = area.withX (area.getX() + slot * (float) i).withWidth (slot).reduced (2.0f, 0.0f);
        auto number = column.removeFromBottom (16.0f);

        g.setColour (style::panel);
        g.fillRoundedRectangle (column, 2.0f);

        const auto db = shownDb[i];
        const auto fraction = (db - meterFloorDb) / -meterFloorDb;

        if (fraction > 0.0f)
        {
            g.setColour (db > -1.0f ? style::red : db > -12.0f ? style::amber : style::green);
            g.fillRoundedRectangle (column.withTop (column.getBottom() - column.getHeight() * fraction), 2.0f);
        }

        g.setColour (style::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (juce::String (firstNumber + (int) i), number, juce::Justification::centred);
    }
}

} // namespace gigrig
