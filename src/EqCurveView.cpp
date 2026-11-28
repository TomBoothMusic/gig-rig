#include "EqCurveView.h"

#include "Spectrum.h"
#include "Style.h"

#include <cmath>

namespace gigrig
{

namespace
{
    // The graph's edges, the same as the phone's: 20 Hz to 20 kHz, 18 dB up and down.
    constexpr double lowHz = 20.0, highHz = 20000.0, topDb = 18.0;
    constexpr double rtaTop = -6.0, rtaBottom = -90.0;      // the live picture's own scale, top and bottom of the graph
    constexpr double cutRow = -12.0;                        // the two cuts have no gain, so they sit on a row of their own
    constexpr float grabDistance = 34.0f;

    juce::String hzText (double f)
    {
        return f >= 10000.0 ? juce::String (f / 1000.0, 1) + " kHz" : f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    juce::String dbText (double g)
    {
        return (g > 0.04 ? "+" : "") + juce::String (std::abs (g) < 0.05 ? 0.0 : g, 1) + " dB";
    }

    double tidyHz (double f)    { return f >= 1000.0 ? std::round (f / 10.0) * 10.0 : std::round (f); }
}

const std::array<EqCurveView::Point, EqCurveView::numPoints>& EqCurveView::points()
{
    // Marks and colours as on the phone and tablet page (remote/eq-curve.js).
    static const std::array<Point, numPoints> all { {
        { "HP", "HPF", "lowcut",  nullptr, nullptr, 0xff9aa7b4 },
        { "L",  "LS",  "lsf",     "lsg",   nullptr, 0xffff7a45 },
        { "1",  "B1",  "b1f",     "b1g",   "b1q",   0xffffd23f },
        { "2",  "B2",  "b2f",     "b2g",   "b2q",   0xff34d27b },
        { "3",  "B3",  "b3f",     "b3g",   "b3q",   0xff46b3ff },
        { "4",  "B4",  "b4f",     "b4g",   "b4q",   0xffa98bff },
        { "H",  "HS",  "hsf",     "hsg",   nullptr, 0xffff6fb1 },
        { "LP", "LPF", "highcut", nullptr, nullptr, 0xff9aa7b4 } } };
    return all;
}

EqCurveView::EqCurveView()
{
    const fx::EqCurveValues d;
    values = { { "lowcut", (float) d.lowcut }, { "highcut", (float) d.highcut }, { "lsf", (float) d.lsf }, { "lsg", 0.0f }, { "hsf", (float) d.hsf }, { "hsg", 0.0f } };

    for (size_t b = 0; b < 4; ++b)
    {
        const auto n = std::to_string (b + 1);
        values["b" + n + "f"] = (float) d.bf[b];
        values["b" + n + "g"] = 0.0f;
        values["b" + n + "q"] = 1.0f;
    }

    setRepaintsOnMouseActivity (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//==============================================================================
float EqCurveView::value (const std::string& id) const
{
    const auto it = values.find (id);
    return it != values.end() ? it->second : 0.0f;
}

void EqCurveView::setValue (const std::string& id, float v)
{
    values[id] = v;
    repaint();
}

void EqCurveView::setRange (const std::string& id, float min, float max)    { ranges[id] = { min, max }; }
void EqCurveView::setRate (double sampleRate)                               { rate = sampleRate > 8000.0 ? sampleRate : 96000.0; repaint(); }
void EqCurveView::setDimmed (bool off)                                      { dimmed = off; repaint(); }

float EqCurveView::limited (const std::string& id, float v) const
{
    const auto it = ranges.find (id);
    return it != ranges.end() ? juce::jlimit (it->second.first, it->second.second, v) : v;
}

void EqCurveView::change (const std::string& id, float v)
{
    v = limited (id, v);

    if (std::abs (value (id) - v) < 1.0e-6f)
        return;

    values[id] = v;
    ++changes;

    if (onChange != nullptr)
        onChange (id, v);
}

void EqCurveView::pick (int point)
{
    pickedPoint = juce::jlimit (0, numPoints - 1, point);
    repaint();
}

bool EqCurveView::isOff (int point) const
{
    return (point == 0 && value ("lowcut") <= 20.5f) || (point == numPoints - 1 && value ("highcut") >= 19950.0f);
}

void EqCurveView::resetPoint (int point)
{
    if (point < 0 || point >= numPoints)
        return;

    const auto& p = points()[(size_t) point];

    if (p.g != nullptr)     change (p.g, 0.0f);
    else                    change (p.f, point == 0 ? 20.0f : 20000.0f);

    repaint();

    if (onPicked != nullptr)
        onPicked();
}

//==============================================================================
double EqCurveView::xOfHz (double hz, juce::Rectangle<float> g)
{
    return g.getX() + std::log (juce::jlimit (lowHz, highHz, hz) / lowHz) / std::log (highHz / lowHz) * g.getWidth();
}

double EqCurveView::hzOfX (double x, juce::Rectangle<float> g)
{
    return lowHz * std::pow (highHz / lowHz, juce::jlimit (0.0, 1.0, (x - g.getX()) / juce::jmax (1.0f, g.getWidth())));
}

double EqCurveView::yOfDb (double db, juce::Rectangle<float> g)
{
    return g.getY() + (topDb - juce::jlimit (-topDb, topDb, db)) / (2.0 * topDb) * g.getHeight();
}

double EqCurveView::dbOfY (double y, juce::Rectangle<float> g)
{
    return topDb - juce::jlimit (0.0, 1.0, (y - g.getY()) / juce::jmax (1.0f, g.getHeight())) * 2.0 * topDb;
}

fx::EqCurveValues EqCurveView::curveValues() const
{
    fx::EqCurveValues v;
    v.lowcut = value ("lowcut");    v.highcut = value ("highcut");
    v.lsf = value ("lsf");          v.lsg = value ("lsg");
    v.hsf = value ("hsf");          v.hsg = value ("hsg");

    for (size_t b = 0; b < 4; ++b)
    {
        const auto n = std::to_string (b + 1);
        v.bf[b] = value ("b" + n + "f");
        v.bg[b] = value ("b" + n + "g");
        v.bq[b] = juce::jmax (0.05f, value ("b" + n + "q"));
    }

    return v;
}

juce::Rectangle<float> EqCurveView::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (8.0f).withTrimmedRight (8.0f).withTrimmedTop (18.0f).withTrimmedBottom (16.0f);
}

juce::Point<float> EqCurveView::placeOf (int point, juce::Rectangle<float> g) const
{
    const auto& p = points()[(size_t) juce::jlimit (0, numPoints - 1, point)];
    return { (float) xOfHz (value (p.f), g), (float) yOfDb (p.g != nullptr ? value (p.g) : cutRow, g) };
}

int EqCurveView::nearestPoint (juce::Point<float> at, juce::Rectangle<float> g) const
{
    int best = -1;
    float bestDistance = grabDistance;

    for (int i = 0; i < numPoints; ++i)
    {
        const float d = placeOf (i, g).getDistanceFrom (at) - (i == pickedPoint ? 6.0f : 0.0f);     // the picked point wins a near tie

        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }

    return best;
}

void EqCurveView::dragPointTo (int point, juce::Point<float> at, juce::Rectangle<float> g)
{
    if (point < 0 || point >= numPoints)
        return;

    const auto& p = points()[(size_t) point];
    change (p.f, (float) tidyHz (hzOfX (at.x, g)));

    if (p.g != nullptr)
        change (p.g, (float) (std::round (dbOfY (at.y, g) * 10.0) / 10.0));

    repaint();

    if (onPicked != nullptr)
        onPicked();
}

void EqCurveView::scaleWidth (double factor)
{
    const auto& p = points()[(size_t) pickedPoint];

    if (p.q == nullptr || factor <= 0.0)
        return;

    change (p.q, (float) (std::round ((double) value (p.q) * factor * 100.0) / 100.0));
    repaint();

    if (onPicked != nullptr)
        onPicked();
}

//==============================================================================
void EqCurveView::setPicture (const std::vector<float>& db)
{
    const auto now = juce::Time::getMillisecondCounter();

    if (db.empty())
    {
        if (! shown.empty())
        {
            shown.clear();
            repaint();
        }

        return;
    }

    const float fall = shown.size() == db.size() ? 30.0f * (float) juce::jmin (500u, now - shownAt) / 1000.0f : 1000.0f;       // 30 dB a second
    shown.resize (db.size(), (float) rtaBottom);

    for (size_t i = 0; i < db.size(); ++i)
        shown[i] = juce::jmax (db[i], shown[i] - fall);

    shownAt = now;
    repaint();
}

//==============================================================================
void EqCurveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = graphArea();

    g.setColour (style::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    // ---- the grid
    g.setFont (juce::Font (juce::FontOptions (10.5f)));

    for (const double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = (float) xOfHz (f, area);
        g.setColour (style::textDim.withAlpha (0.16f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (style::textDim);
        g.drawText (f >= 1000.0 ? juce::String (juce::roundToInt (f / 1000.0)) + "k" : juce::String (juce::roundToInt (f)),
                    juce::Rectangle<float> (x - 20.0f, area.getBottom() + 1.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    for (const double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        const float y = (float) yOfDb (db, area);
        g.setColour (style::textDim.withAlpha (db == 0.0 ? 0.45f : 0.16f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        g.setColour (style::textDim);
        g.drawText ((db > 0.0 ? "+" : "") + juce::String (juce::roundToInt (db)), juce::Rectangle<float> (area.getX() + 2.0f, y - 13.0f, 30.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    // ---- the live picture, behind the curve
    if (shown.size() >= 2)
    {
        juce::Path live;
        live.startNewSubPath (area.getX(), area.getBottom());

        for (size_t i = 0; i < shown.size(); ++i)
        {
            const double hz = spectrumBandHz ((int) i);

            if (hz > highHz * 1.02)
                break;

            const double level = juce::jlimit (0.0, 1.0, ((double) shown[i] - rtaBottom) / (rtaTop - rtaBottom));
            live.lineTo ((float) xOfHz (hz, area), area.getBottom() - (float) level * area.getHeight());
        }

        live.lineTo (live.getCurrentPosition().x, area.getBottom());
        live.closeSubPath();
        g.setColour (juce::Colour (0xff46b3ff).withAlpha (0.22f));
        g.fillPath (live);
    }

    // ---- the curve
    {
        const auto v = curveValues();
        const int steps = juce::jmax (60, juce::roundToInt (area.getWidth() / 2.0f));
        const float zero = (float) yOfDb (0.0, area);
        juce::Path line;

        for (int i = 0; i <= steps; ++i)
        {
            const double hz = lowHz * std::pow (highHz / lowHz, (double) i / steps);
            const juce::Point<float> at ((float) xOfHz (hz, area), (float) yOfDb (fx::eqCurveDb (v, hz, rate), area));

            if (i == 0)     line.startNewSubPath (at);
            else            line.lineTo (at);
        }

        auto fill = line;
        fill.lineTo (area.getRight(), zero);
        fill.lineTo (area.getX(), zero);
        fill.closeSubPath();

        const auto colour = dimmed ? style::textDim : style::green;
        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }

    // ---- the points, the picked one last so it is on top
    const auto drawPoint = [&] (int i)
    {
        const auto& p = points()[(size_t) i];
        const auto at = placeOf (i, area);
        const bool on = i == pickedPoint;
        const float r = on ? 12.0f : 10.0f;
        const auto colour = juce::Colour (p.colour).withAlpha (isOff (i) || dimmed ? 0.4f : 1.0f);

        g.setColour (colour);
        g.fillEllipse (at.x - r, at.y - r, r * 2.0f, r * 2.0f);

        if (on)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (at.x - r, at.y - r, r * 2.0f, r * 2.0f, 2.0f);
        }

        g.setColour (juce::Colour (0xff15181c));
        g.setFont (juce::Font (juce::FontOptions (juce::String (p.mark).length() > 1 ? 9.5f : 12.0f, juce::Font::bold)));
        g.drawText (p.mark, juce::Rectangle<float> (at.x - r, at.y - r, r * 2.0f, r * 2.0f), juce::Justification::centred, false);
    };

    for (int i = 0; i < numPoints; ++i)
        if (i != pickedPoint)
            drawPoint (i);

    drawPoint (pickedPoint);

    // ---- the picked point in words, along the top
    {
        const auto& p = points()[(size_t) pickedPoint];
        juce::String text (p.name);
        text << "   " << (isOff (pickedPoint) ? juce::String ("off") : hzText (value (p.f)));

        if (p.g != nullptr)     text << "   " << dbText (value (p.g));
        if (p.q != nullptr)     text << "   Q " << juce::String (value (p.q), 2);
        if (dimmed)             text << "   (EQ is off)";

        g.setColour (juce::Colour (p.colour));
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText (text, bounds.withHeight (17.0f).withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void EqCurveView::mouseDown (const juce::MouseEvent& e)
{
    const auto area = graphArea();
    held = nearestPoint (e.position, area);

    if (held < 0)
        return;

    holdOffset = placeOf (held, area) - e.position;
    pickedPoint = held;
    repaint();

    if (onPicked != nullptr)
        onPicked();
}

void EqCurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (held >= 0 && e.getDistanceFromDragStart() >= 3)
        dragPointTo (held, e.position + holdOffset, graphArea());
}

void EqCurveView::mouseUp (const juce::MouseEvent&)     { held = -1; }

void EqCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    resetPoint (nearestPoint (e.position, graphArea()));
}

void EqCurveView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 1.0e-4f)
        scaleWidth (std::pow (1.12, wheel.deltaY > 0.0f ? 1.0 : -1.0));
}

void EqCurveView::mouseMagnify (const juce::MouseEvent&, float scaleFactor)
{
    // Fingers apart (above 1) is a wider bell, which is a lower Q, as on the phone.
    if (scaleFactor > 0.01f)
        scaleWidth (1.0 / (double) scaleFactor);
}

} // namespace gigrig
