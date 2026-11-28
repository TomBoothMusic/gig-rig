#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "effects/mixer/EqCurveMath.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gigrig
{

/** The mixer EQ as a curve with a point to drag for each band, on the laptop's Mixer tab
    (29 Nov 2026, Tom's ask: he wants to dial in his mix on the laptop before the board exists).
    It is the phone and tablet page's curve (remote/eq-curve.js) again: the same eight points, the
    same marks and colours, the same graph edges, and the same sums (EqCurveMath.h, held to the
    real EQ by a check).

    It only draws and reports: every change goes out through onChange with the setting's id, and
    the Mixer tab writes it to the mixer the way its sliders always have. The live picture (RTA)
    is handed in by the tab; nothing here touches the sound. */
class EqCurveView final : public juce::Component
{
public:
    static constexpr int numPoints = 8;

    struct Point
    {
        const char* mark;       // on the dot
        const char* name;       // in lists
        const char* f;          // setting ids: frequency, gain (or nullptr), Q (or nullptr)
        const char* g;
        const char* q;
        juce::uint32 colour;
    };

    static const std::array<Point, numPoints>& points();

    EqCurveView();

    /** A setting was changed on the graph (called for every step of a drag). */
    std::function<void (const std::string& id, float value)> onChange;
    /** Another point was picked, or the picked point's settings moved: the tab brings its sliders in line. */
    std::function<void()> onPicked;

    void setValue (const std::string& id, float value);
    float value (const std::string& id) const;
    void setRange (const std::string& id, float min, float max);
    void setRate (double sampleRate);
    void setDimmed (bool blockIsOff);

    int picked() const noexcept                 { return pickedPoint; }
    void pick (int point);
    /** Puts one point back: a band to no boost or cut, a cut to off. */
    void resetPoint (int point);
    int changesMade() const noexcept            { return changes; }

    /** The live picture: one level in dB per sixth of an octave from 20 Hz (Spectrum.h), or empty for none.
        It jumps up at once and falls back slowly, as on the phone. */
    void setPicture (const std::vector<float>& db);

    // The graph's own sums, public so the self-test can hold them (no screen needed).
    static double xOfHz (double hz, juce::Rectangle<float> graph);
    static double hzOfX (double x, juce::Rectangle<float> graph);
    static double yOfDb (double db, juce::Rectangle<float> graph);
    static double dbOfY (double y, juce::Rectangle<float> graph);
    fx::EqCurveValues curveValues() const;
    juce::Point<float> placeOf (int point, juce::Rectangle<float> graph) const;
    int nearestPoint (juce::Point<float> at, juce::Rectangle<float> graph) const;
    /** What a drag of one point to a place on the graph sets (the tab's sliders see the same numbers). */
    void dragPointTo (int point, juce::Point<float> at, juce::Rectangle<float> graph);
    /** Two fingers or the mouse wheel: the picked bell gets wider (below 1) or narrower (above 1). */
    void scaleWidth (double factor);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    juce::Rectangle<float> graphArea() const;
    bool isOff (int point) const;
    float limited (const std::string& id, float value) const;
    void change (const std::string& id, float value);

    std::map<std::string, float> values;
    std::map<std::string, std::pair<float, float>> ranges;
    double rate = 96000.0;
    int pickedPoint = 3, held = -1, changes = 0;
    juce::Point<float> holdOffset;
    bool dimmed = false;

    std::vector<float> shown;
    juce::uint32 shownAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurveView)
};

} // namespace gigrig
