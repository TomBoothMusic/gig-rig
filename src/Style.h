#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace gigrig
{

namespace style
{
    const juce::Colour background { 0xff15181c };
    const juce::Colour panel      { 0xff1e2329 };
    const juce::Colour textMain   { 0xffe6e9ed };
    const juce::Colour textDim    { 0xff9aa4af };
    const juce::Colour green      { 0xff3fb950 };
    const juce::Colour amber      { 0xffd29922 };
    const juce::Colour red        { 0xfff0605a };

    juce::Font monoFont (float height);
    void readOnlyEditor (juce::TextEditor&);
    void heading (juce::Label&, const juce::String& text);
    void body (juce::Label&, const juce::String& text);
    void combo (juce::ComboBox&);
}

/** One line of an answer: a coloured tag with a word in it, a headline and a
    sentence on what to do next. The word means the answer never depends on
    telling red from green. */
class StatusRow final : public juce::Component
{
public:
    enum class Tone { neutral, good, caution, bad };

    void set (Tone tone, const juce::String& tag, const juce::String& headline, const juce::String& detail);
    void paint (juce::Graphics&) override;

private:
    Tone tone = Tone::neutral;
    juce::String tag, headline, detail;
};

/** A row of small level meters with a number under each. */
class MeterStrip final : public juce::Component
{
public:
    MeterStrip (int count, int firstNumber);

    /** peak is 0..1 for the time since the last call. */
    void setLevel (int index, float peak);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> shownDb;
    int firstNumber;
};

} // namespace gigrig
