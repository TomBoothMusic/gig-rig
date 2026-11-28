// See SwitchTile.h. Plain C++, shared by the controller's program and the app.
#include "SwitchTile.h"
#include "TileArt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gigrig { namespace tile
{

namespace
{
//==============================================================================
// Dots

inline Colour mix (Colour under, Colour over, int a)      // a: 0 (all under) to 255 (all over)
{
    if (a >= 255) return over;
    if (a <= 0)   return under;

    const int r = (((under >> 11) & 31) * (255 - a) + ((over >> 11) & 31) * a + 127) / 255;
    const int g = (((under >> 5) & 63) * (255 - a) + ((over >> 5) & 63) * a + 127) / 255;
    const int b = ((under & 31) * (255 - a) + (over & 31) * a + 127) / 255;
    return (Colour) ((r << 11) | (g << 5) | b);
}

struct Canvas
{
    Colour* dots;

    void dot (int x, int y, Colour c, int a)
    {
        if (x >= 0 && y >= 0 && x < side && y < side)
            dots[y * side + x] = mix (dots[y * side + x], c, a);
    }

    void fill (int x, int y, int w, int h, Colour c, int a = 255)
    {
        const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
        const int x1 = x + w > side ? side : x + w, y1 = y + h > side ? side : y + h;

        for (int py = y0; py < y1; ++py)
        {
            Colour* row = dots + py * side;

            if (a >= 255)
                for (int px = x0; px < x1; ++px) row[px] = c;
            else
                for (int px = x0; px < x1; ++px) row[px] = mix (row[px], c, a);
        }
    }

    /** A rectangle with round corners and smooth edges. Any position and size, whole dots or not. */
    void roundRect (float x, float y, float w, float h, float r, Colour c, int a = 255)
    {
        if (w <= 0.0f || h <= 0.0f)
            return;

        const float hx = w * 0.5f, hy = h * 0.5f, cx = x + hx, cy = y + hy;
        r = fminf (r, fminf (hx, hy));

        int x0 = (int) floorf (x), x1 = (int) ceilf (x + w), y0 = (int) floorf (y), y1 = (int) ceilf (y + h);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > side) x1 = side;
        if (y1 > side) y1 = side;

        for (int py = y0; py < y1; ++py)
        {
            const float qy = fabsf ((float) py + 0.5f - cy) - (hy - r);
            Colour* row = dots + py * side;

            // The straight middle of a row well inside the shape needs no sums.
            int plainFrom = x1, plainTo = x1;
            if (qy <= -1.0f || (qy <= 0.0f && r >= 1.0f && qy - r <= -1.0f))
            {
                plainFrom = (int) ceilf (x + 1.0f);
                plainTo = (int) floorf (x + w - 1.0f);
                if (plainFrom < x0) plainFrom = x0;
                if (plainTo > x1) plainTo = x1;
                if (plainTo < plainFrom) plainTo = plainFrom;
            }

            for (int px = x0; px < x1; ++px)
            {
                if (px >= plainFrom && px < plainTo)
                {
                    row[px] = a >= 255 ? c : mix (row[px], c, a);
                    continue;
                }

                const float qx = fabsf ((float) px + 0.5f - cx) - (hx - r);
                float d;

                if (qx > 0.0f && qy > 0.0f)     d = sqrtf (qx * qx + qy * qy) - r;
                else                            d = (qx > qy ? qx : qy) - r;

                const float cover = 0.5f - d;

                if (cover >= 1.0f)              row[px] = a >= 255 ? c : mix (row[px], c, a);
                else if (cover > 0.0f)          row[px] = mix (row[px], c, (int) (cover * (float) a));
            }
        }
    }

    void circle (float cx, float cy, float r, Colour c, int a = 255)
    {
        roundRect (cx - r, cy - r, r * 2.0f, r * 2.0f, r, c, a);
    }
};

//==============================================================================
// Letters

const art::Font& fontFor (int size)
{
    return size <= textBig ? art::bigFont : size == textWords ? art::wordsFont : size == textSmall ? art::smallFont : art::tinyFont;
}

const art::Glyph* glyphFor (const art::Font& f, char ch)
{
    const int c = (unsigned char) ch;
    return c >= f.first && c < f.first + f.count ? f.glyphs + (c - f.first) : nullptr;
}

bool hasEveryLetter (const art::Font& f, const char* text, int n)
{
    for (int i = 0; i < n; ++i)
        if (glyphFor (f, text[i]) == nullptr)
            return false;

    return true;
}

int widthOf (const art::Font& f, const char* text, int n)
{
    int w = 0;

    for (int i = 0; i < n; ++i)
        if (const art::Glyph* g = glyphFor (f, text[i]))
            w += g->advance;

    return w;
}

void drawText (Canvas& canvas, const art::Font& f, const char* text, int n, int x, int baseline, Colour ink)
{
    for (int i = 0; i < n; ++i)
    {
        const art::Glyph* g = glyphFor (f, text[i]);

        if (g == nullptr)
            continue;

        const int rowBytes = (g->w + 1) / 2;

        for (int gy = 0; gy < g->h; ++gy)
        {
            const uint8_t* row = f.data + g->offset + gy * rowBytes;

            for (int gx = 0; gx < g->w; ++gx)
            {
                const int level = (gx & 1) ? (row[gx >> 1] & 15) : (row[gx >> 1] >> 4);

                if (level > 0)
                    canvas.dot (x + g->left + gx, baseline + g->top + gy, ink, level * 17);
            }
        }

        x += g->advance;
    }
}

/** The height of a capital letter: what words are centred by. */
int capHeight (const art::Font& f)      { return f.size * 70 / 100; }
int lineHeight (const art::Font& f)     { return f.size * 105 / 100; }

constexpr int padAcross = 17, padAcrossLeast = 8, padDown = 5;

struct WordsFit
{
    int size = textTiny, lines = 1, split = 0;      // split: where the second line starts (after the space)
    int length = 0, firstLength = 0;
    int w = 0, h = 0;
    bool fits = false;
};

/** Finds how some words go into a width: one line at the size asked for, else two lines, else a size down. */
WordsFit fitWords (const Item& item, int widest, int smallestAllowed)
{
    WordsFit fit;
    const int n = (int) strnlen (item.text, maxText);
    fit.length = fit.firstLength = n;
    int start = item.size > smallestAllowed ? item.size : smallestAllowed;
    if (start > textTiny) start = textTiny;

    for (int size = start; size <= textTiny; ++size)
    {
        const art::Font& f = fontFor (size);

        if (! hasEveryLetter (f, item.text, n))
            continue;

        const int whole = widthOf (f, item.text, n);

        if (whole + 2 * padAcrossLeast <= widest)
        {
            fit.size = size; fit.lines = 1;
            const int room = (widest - whole) / 2;
            fit.w = whole + 2 * (room < padAcross ? room : padAcross);
            fit.h = lineHeight (f) + 2 * padDown;
            fit.fits = true;
            return fit;
        }

        // Two lines, broken at the space that leaves the two most alike in width. Never for the big numbers.
        int best = -1, bestWidth = 1 << 20;

        for (int i = 1; i < n - 1 && size != textBig; ++i)
            if (item.text[i] == ' ')
            {
                const int a = widthOf (f, item.text, i), b = widthOf (f, item.text + i + 1, n - i - 1);
                const int wider = a > b ? a : b;
                if (wider < bestWidth) { bestWidth = wider; best = i; }
            }

        if (best > 0 && bestWidth + 2 * padAcrossLeast <= widest)
        {
            fit.size = size; fit.lines = 2; fit.firstLength = best; fit.split = best + 1;
            const int room = (widest - bestWidth) / 2;
            fit.w = bestWidth + 2 * (room < padAcross ? room : padAcross);
            fit.h = 2 * lineHeight (f) + 2 * padDown;
            fit.fits = true;
            return fit;
        }
    }

    // Too long even at the smallest size: one line, cut off by the tile's edge.
    const art::Font& f = fontFor (textTiny);
    fit.size = textTiny; fit.lines = 1;
    fit.w = widest;
    fit.h = lineHeight (f) + 2 * padDown;
    fit.fits = false;
    return fit;
}

void drawWords (Canvas& canvas, const Item& item, const WordsFit& fit, int centreX, int top)
{
    const art::Font& f = fontFor (fit.size);

    if (item.backing)
        canvas.roundRect ((float) (centreX - fit.w / 2), (float) top, (float) fit.w, (float) fit.h, 14.0f, colours::backing, 209);

    for (int line = 0; line < fit.lines; ++line)
    {
        const char* text = item.text + (line == 0 ? 0 : fit.split);
        const int n = line == 0 ? (fit.lines == 2 ? fit.firstLength : fit.length) : fit.length - fit.split;
        const int w = widthOf (f, text, n);
        const int lineTop = top + padDown + line * lineHeight (f);
        const int baseline = lineTop + (lineHeight (f) + capHeight (f)) / 2;
        drawText (canvas, f, text, n, centreX - w / 2, baseline, item.ink);
    }
}

//==============================================================================
// Lane pictures

void drawPicture (Canvas& canvas, int picture, int x, int y, int size, Colour ink)
{
    if (picture < 0 || picture >= art::numIcons)
        return;

    const uint8_t* data = art::iconData[picture];
    const int n = art::iconSize;
    const auto level = [data, n] (int px, int py) -> int
    {
        if (px < 0 || py < 0 || px >= n || py >= n) return 0;
        const uint8_t b = data[py * (n / 2) + (px >> 1)];
        return (px & 1) ? (b & 15) : (b >> 4);
    };

    for (int dy = 0; dy < size; ++dy)
        for (int dx = 0; dx < size; ++dx)
        {
            int a;

            if (size == n)
            {
                a = level (dx, dy) * 17;
            }
            else            // a smaller copy: each dot takes the average of the patch it covers
            {
                const float fx = ((float) dx + 0.5f) * (float) n / (float) size - 0.5f, fy = ((float) dy + 0.5f) * (float) n / (float) size - 0.5f;
                const int ix = (int) floorf (fx), iy = (int) floorf (fy);
                const float tx = fx - (float) ix, ty = fy - (float) iy;
                const float top = (float) level (ix, iy) * (1.0f - tx) + (float) level (ix + 1, iy) * tx;
                const float bottom = (float) level (ix, iy + 1) * (1.0f - tx) + (float) level (ix + 1, iy + 1) * tx;
                a = (int) ((top * (1.0f - ty) + bottom * ty) * 17.0f);
            }

            if (a > 0)
                canvas.dot (x + dx, y + dy, ink, a);
        }
}

int lanesCount (const Item& item)
{
    const int pictures = item.numPictures > maxLanePictures ? maxLanePictures : item.numPictures;
    return pictures + (item.more > 0 ? 1 : 0);
}

void lanesSize (const Item& item, int& box, int& gap)
{
    const bool roomy = lanesCount (item) <= 3;
    box = roomy ? 62 : 48;
    gap = roomy ? 10 : 8;
}

void drawLanes (Canvas& canvas, const Item& item, int centreX, int top)
{
    int box, gap;
    lanesSize (item, box, gap);
    const int n = lanesCount (item), pictures = n - (item.more > 0 ? 1 : 0);
    int x = centreX - (n * box + (n - 1) * gap) / 2;
    const int inner = box == 62 ? art::iconSize : 36;

    for (int i = 0; i < n; ++i, x += box + gap)
    {
        canvas.roundRect ((float) x, (float) top, (float) box, (float) box, 12.0f, colours::backing, 209);

        if (i < pictures)
        {
            drawPicture (canvas, item.pictures[i], x + (box - inner) / 2, top + (box - inner) / 2, inner, colours::ink);
        }
        else
        {
            char count[8];
            snprintf (count, sizeof count, "+%d", (int) item.more);
            const int length = (int) strlen (count);
            const art::Font& f = fontFor (widthOf (fontFor (textSmall), count, length) <= box - 6 ? textSmall : textTiny);
            drawText (canvas, f, count, length, x + (box - widthOf (f, count, length)) / 2, top + (box + capHeight (f)) / 2, colours::ink);
        }
    }
}

//==============================================================================
// Pedal drawings: plain stand-ins for a kind of gear, in the colours the app sends.
// (The looks of Tom's real pedals are in his private file and are not in this program.)

void pedalShape (int kind, int& w, int& h)
{
    if (kind == pedalRack)      { w = 190; h = 86; }
    else if (kind == pedalAmp)  { w = 156; h = 126; }
    else                        { w = 110; h = 153; }
}

Colour shade (Colour c, int towardsWhite)       // -255 (black) .. 255 (white)
{
    return towardsWhite >= 0 ? mix (c, colours::ink, towardsWhite) : mix (c, colours::black, -towardsWhite);
}

void drawPedal (Canvas& canvas, const Item& item, float x, float y, float w, float h)
{
    const Colour outline = rgb (0x0b, 0x0c, 0x0d), lampOn = rgb (0xff, 0x3b, 0x30), lampOff = rgb (0x55, 0x16, 0x12);
    const Colour lamp = item.lit ? lampOn : lampOff;
    const int kind = item.pedalKind;

    if (kind == pedalRack)
    {
        const float s = w / 190.0f;
        canvas.roundRect (x, y, w, h, 7.0f * s, outline);
        canvas.roundRect (x + 4.0f * s, y + 4.0f * s, w - 8.0f * s, h - 8.0f * s, 4.0f * s, item.body);

        for (int i = 0; i < 4; ++i)     // rack ears' screws
            canvas.circle (x + (i < 2 ? 12.0f : 178.0f) * s, y + (i % 2 == 0 ? 16.0f : 70.0f) * s, 3.2f * s, outline);

        for (int i = 0; i < 3; ++i)
        {
            canvas.circle (x + (40.0f + 28.0f * (float) i) * s, y + 46.0f * s, 11.5f * s, outline);
            canvas.circle (x + (40.0f + 28.0f * (float) i) * s, y + 46.0f * s, 9.0f * s, item.knob);
        }

        canvas.roundRect (x + 118.0f * s, y + 20.0f * s, 46.0f * s, 34.0f * s, 3.0f * s, outline);
        canvas.roundRect (x + 121.0f * s, y + 23.0f * s, 40.0f * s, 28.0f * s, 2.0f * s, item.lit ? rgb (0xf2, 0xdc, 0x9a) : rgb (0x8a, 0x7e, 0x5c));
        canvas.circle (x + 141.0f * s, y + 66.0f * s, 4.0f * s, lamp);
        return;
    }

    if (kind == pedalAmp)
    {
        const float s = w / 156.0f;
        canvas.roundRect (x, y, w, h, 9.0f * s, outline);
        canvas.roundRect (x + 4.0f * s, y + 4.0f * s, w - 8.0f * s, h - 8.0f * s, 6.0f * s, item.body);
        canvas.roundRect (x + 12.0f * s, y + 12.0f * s, w - 24.0f * s, 30.0f * s, 3.0f * s, shade (item.body, 150));

        for (int i = 0; i < 4; ++i)
        {
            canvas.circle (x + (30.0f + 25.0f * (float) i) * s, y + 27.0f * s, 8.5f * s, outline);
            canvas.circle (x + (30.0f + 25.0f * (float) i) * s, y + 27.0f * s, 6.5f * s, item.knob);
        }

        canvas.circle (x + 134.0f * s, y + 27.0f * s, 4.0f * s, lamp);
        canvas.roundRect (x + 12.0f * s, y + 50.0f * s, w - 24.0f * s, 64.0f * s, 3.0f * s, shade (item.body, 70));

        for (int i = 0; i < 7; ++i)     // the speaker cloth
            canvas.roundRect (x + 16.0f * s, y + (56.0f + 8.0f * (float) i) * s, w - 32.0f * s, 2.4f * s, 1.0f * s, shade (item.body, 20));
        return;
    }

    // The three that stand on end: a stomp box, a rocker (wah, whammy, volume) and a slider box (graphic EQ).
    const float s = h / 64.0f;                                  // the mock-up's drawing is 46 x 64
    const float left = x + (w - 46.0f * s) * 0.5f;
    canvas.roundRect (left + 0.75f * s, y + 0.75f * s, 44.5f * s, 62.5f * s, 6.0f * s, outline);
    canvas.roundRect (left + 3.25f * s, y + 3.25f * s, 39.5f * s, 57.5f * s, 3.8f * s, item.body);

    if (kind == pedalRocker)
    {
        canvas.roundRect (left + 9.0f * s, y + 9.0f * s, 28.0f * s, 46.0f * s, 3.0f * s, outline);

        for (int i = 0; i < 6; ++i)
            canvas.roundRect (left + 12.0f * s, y + (13.5f + 7.0f * (float) i) * s, 22.0f * s, 2.6f * s, 1.2f * s, shade (item.body, 90));

        canvas.circle (left + 23.0f * s, y + 6.0f * s, 1.8f * s, lamp);
        return;
    }

    if (kind == pedalSliders)
    {
        static const float at[7] = { 0.55f, 0.35f, 0.20f, 0.30f, 0.50f, 0.40f, 0.60f };

        for (int i = 0; i < 7; ++i)
        {
            const float sx = left + (8.5f + 4.85f * (float) i) * s;
            canvas.roundRect (sx - 0.6f * s, y + 8.0f * s, 1.2f * s, 20.0f * s, 0.6f * s, outline);
            canvas.roundRect (sx - 1.7f * s, y + (8.0f + 17.0f * at[i]) * s, 3.4f * s, 3.2f * s, 0.8f * s, item.knob);
        }
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            canvas.circle (left + (12.0f + 11.0f * (float) i) * s, y + 14.0f * s, 5.25f * s, outline);
            canvas.circle (left + (12.0f + 11.0f * (float) i) * s, y + 14.0f * s, 3.75f * s, item.knob);
        }
    }

    canvas.circle (left + 23.0f * s, y + (kind == pedalSliders ? 32.0f : 27.5f) * s, 2.2f * s, lamp);
    canvas.roundRect (left + 8.0f * s, y + 36.0f * s, 30.0f * s, 20.0f * s, 3.0f * s, outline, 140);
    canvas.circle (left + 23.0f * s, y + 46.0f * s, 6.75f * s, outline);
    canvas.circle (left + 23.0f * s, y + 46.0f * s, 5.25f * s, rgb (0xc9, 0xce, 0xd3));
}

void copyText (char* to, const char* from)
{
    int i = 0;

    for (; from != nullptr && from[i] != 0 && i < maxText; ++i)
        to[i] = (from[i] >= 32 && from[i] <= 126) ? from[i] : '?';

    for (; i <= maxText; ++i)
        to[i] = 0;
}

Item wordsItem (const char* text, int size, Colour ink = colours::ink, bool backing = true)
{
    Item item;
    item.kind = itemWords;
    item.size = (uint8_t) size;
    item.backing = backing ? 1 : 0;
    item.ink = ink;
    copyText (item.text, text);
    return item;
}

constexpr int borderDots = 10;      // thin, so a pedal picture fits inside at full size (Tom, 28 Nov 2026)
} // namespace

//==============================================================================
bool sameTile (const Tile& a, const Tile& b)
{
    if (a.background != b.background || a.flashTo != b.flashTo || a.flashCentis != b.flashCentis
        || a.border != b.border || a.borderWidth != b.borderWidth || a.numItems != b.numItems)
        return false;

    for (int i = 0; i < a.numItems && i < maxItems; ++i)
    {
        const Item& x = a.items[i];
        const Item& y = b.items[i];

        if (x.kind != y.kind || x.size != y.size || x.backing != y.backing || x.ink != y.ink
            || strncmp (x.text, y.text, maxText) != 0 || x.numPictures != y.numPictures || x.more != y.more
            || memcmp (x.pictures, y.pictures, sizeof x.pictures) != 0
            || x.pedalKind != y.pedalKind || x.lit != y.lit || x.body != y.body || x.knob != y.knob)
            return false;
    }

    return true;
}

int textWidth (const char* text, int size)
{
    if (size < textBig) size = textBig;
    if (size > textTiny) size = textTiny;
    return widthOf (fontFor (size), text, (int) strlen (text));
}

bool draw (const Tile& tile, bool flashOn, Colour* dots)
{
    Canvas canvas { dots };
    canvas.fill (0, 0, side, side, (flashOn && tile.flashCentis > 0) ? tile.flashTo : tile.background);

    const int bw = tile.borderWidth > 40 ? 40 : tile.borderWidth;

    if (bw > 0)
    {
        canvas.fill (0, 0, side, bw, tile.border);
        canvas.fill (0, side - bw, side, bw, tile.border);
        canvas.fill (0, bw, bw, side - 2 * bw, tile.border);
        canvas.fill (side - bw, bw, bw, side - 2 * bw, tile.border);
    }

    const int inset = bw > 0 ? bw + 4 : 8;
    const int room = side - 2 * inset;
    const int n = tile.numItems > maxItems ? maxItems : tile.numItems;
    bool everythingFits = true;

    // Sizes first. Words that make the stack too tall go down a size, biggest first.
    WordsFit words[maxItems];
    int heights[maxItems] = {}, pedalW[maxItems] = {}, smallest[maxItems] = {};
    int gap = 14, used = 0, pedals = 0, total = 0;

    for (int attempt = 0; attempt < 16; ++attempt)
    {
        used = 0; pedals = 0; total = 0;

        for (int i = 0; i < n; ++i)
        {
            const Item& item = tile.items[i];
            heights[i] = 0;

            if (item.kind == itemWords)
            {
                words[i] = fitWords (item, room, smallest[i]);
                heights[i] = words[i].h;
            }
            else if (item.kind == itemLanes && lanesCount (item) > 0)
            {
                int box, laneGap;
                lanesSize (item, box, laneGap);
                heights[i] = box;
            }
            else if (item.kind == itemPedal)
            {
                int w, h;
                pedalShape (item.pedalKind, w, h);
                if (w > room) { h = h * room / w; w = room; }
                heights[i] = h; pedalW[i] = w;
                ++pedals;
            }

            if (heights[i] > 0) { ++used; total += heights[i]; }
        }

        gap = 14;
        int over = total + gap * (used > 1 ? used - 1 : 0) - room;

        // Too tall: a pedal picture gives way first (down to 45% of its size), then the gaps.
        for (int i = 0; i < n && over > 0 && pedals > 0; ++i)
            if (tile.items[i].kind == itemPedal)
            {
                const int least = heights[i] * 45 / 100;
                const int cut = over < heights[i] - least ? over : heights[i] - least;
                pedalW[i] = pedalW[i] * (heights[i] - cut) / heights[i];
                heights[i] -= cut; total -= cut; over -= cut;
            }

        if (over > 0 && used > 1)
        {
            const int fewer = (over + used - 2) / (used - 1);
            gap = gap - fewer < 4 ? 4 : gap - fewer;
            over = total + gap * (used - 1) - room;
        }

        if (over <= 0)
            break;

        int biggest = -1;       // the words item with the most to give
        for (int i = 0; i < n; ++i)
            if (tile.items[i].kind == itemWords && words[i].size < textTiny && (biggest < 0 || words[i].h > words[biggest].h))
                biggest = i;

        if (biggest < 0)
        {
            everythingFits = false;
            break;
        }

        smallest[biggest] = words[biggest].size + 1;
    }

    int y = inset + (room - (total + gap * (used > 1 ? used - 1 : 0))) / 2;
    if (y < 0) y = 0;

    for (int i = 0; i < n; ++i)
    {
        const Item& item = tile.items[i];

        if (heights[i] <= 0)
            continue;

        if (item.kind == itemWords)
        {
            drawWords (canvas, item, words[i], side / 2, y);
            everythingFits = everythingFits && words[i].fits;
        }
        else if (item.kind == itemLanes)
        {
            drawLanes (canvas, item, side / 2, y);
        }
        else if (item.kind == itemPedal)
        {
            drawPedal (canvas, item, (float) (side - pedalW[i]) * 0.5f, (float) y, (float) pedalW[i], (float) heights[i]);
        }

        y += heights[i] + gap;
    }

    return everythingFits && (y - gap) <= side - inset + 1;
}

void drawNote (Colour* dots, const char* big, const char* small)
{
    Canvas canvas { dots };
    canvas.fill (0, noteTop, side, side - noteTop, colours::black);
    const art::Font& f = fontFor (textWords);
    const art::Font& t = fontFor (textTiny);
    const int nb = (int) strlen (big), ns = (int) strlen (small);
    drawText (canvas, f, big, nb, (side - widthOf (f, big, nb)) / 2, noteTop + 46, colours::yellow);
    drawText (canvas, t, small, ns, (side - widthOf (t, small, ns)) / 2, noteTop + 78, colours::ink);
}

//==============================================================================
Tile trackTile (int number, int trackLook, const uint8_t* pictures, int numPictures, int more)
{
    Tile t;
    t.background = trackLook == trackEmpty ? colours::blue : trackLook == trackPlaying ? colours::green
                 : trackLook == trackOverdubbing ? colours::yellow : colours::red;

    if (trackLook == trackRecording)
    {
        t.flashTo = colours::redDark;
        t.flashCentis = 50;
    }

    char text[8];
    snprintf (text, sizeof text, "%d", number);
    t.items[t.numItems++] = wordsItem (text, textBig);

    if (numPictures > 0 && pictures != nullptr)
    {
        Item lanes;
        lanes.kind = itemLanes;
        lanes.numPictures = (uint8_t) (numPictures > maxLanePictures ? maxLanePictures : numPictures);
        for (int i = 0; i < lanes.numPictures; ++i)
            lanes.pictures[i] = pictures[i];
        lanes.more = (uint8_t) (more < 0 ? 0 : more > 9 ? 9 : more);
        t.items[t.numItems++] = lanes;
    }

    return t;
}

Tile wordsTile (Colour background, const char* words, const char* second, int offLook)
{
    Tile t;
    t.background = background;

    if (background == colours::white && offLook == lookWhiteOutline)
    {
        t.background = colours::black;
        t.border = colours::white;
        t.borderWidth = borderDots;
    }

    t.items[t.numItems++] = wordsItem (words, textWords);

    if (second != nullptr && second[0] != 0)
        t.items[t.numItems++] = wordsItem (second, textSmall);

    return t;
}

Tile dimTile (const char* words)
{
    Tile t;
    t.background = colours::dim;
    t.items[t.numItems++] = wordsItem (words, textWords, colours::dimInk, false);
    return t;
}

Tile effectTile (const char* name, const char* second, bool on, int offLook, Colour chain,
                 int pedalKind, Colour body, Colour knob)
{
    Tile t;
    t.background = on ? colours::purple : offLook == lookFullWhite ? colours::white : colours::black;

    if (offLook == lookChainBorder)
    {
        t.border = chain;
        t.borderWidth = borderDots;
    }
    else if (offLook == lookWhiteOutline && ! on)
    {
        t.border = colours::white;
        t.borderWidth = borderDots;
    }

    if (pedalKind >= 0)
    {
        Item pedal;
        pedal.kind = itemPedal;
        pedal.pedalKind = (uint8_t) pedalKind;
        pedal.lit = on ? 1 : 0;
        pedal.body = body;
        pedal.knob = knob;
        t.items[t.numItems++] = pedal;
        t.items[t.numItems++] = wordsItem (name, textSmall);
    }
    else
    {
        t.items[t.numItems++] = wordsItem (name, textWords);
    }

    if (second != nullptr && second[0] != 0 && t.numItems < maxItems)
        t.items[t.numItems++] = wordsItem (second, textSmall);

    return t;
}

//==============================================================================
namespace
{
const char* const demoNames[] =
{
    "Loop tracks: empty, recording, playing",
    "Loop tracks: overdubbing, stopped, more than three lanes",
    "Stop switches: track busy, track empty, track stopped",
    "Effects, chain colour border: drive off, drive on, boost on",
    "Effects, chain colour border: boost off, bass drive off, delay throw",
    "Effects, chain colour border: harmony held, tap tempo, compressor on",
    "Effects, white outline: drive off, drive on, boost on",
    "Effects, white outline: boost off, bass drive off, delay throw",
    "Effects, white outline: harmony held, tap tempo, compressor on",
    "Effects, full white: drive off, drive on, boost on",
    "Effects, full white: boost off, bass drive off, delay throw",
    "Effects, full white: harmony held, tap tempo, compressor on",
    "All start/stop: playing, stopped, nothing recorded",
    "Undo: unused, after an undo, after a redo",
    "Bank 1, bank 2, and three sizes of words"
};

constexpr int numDemoNames = (int) (sizeof demoNames / sizeof demoNames[0]);

Tile demoEffect (int which, int look)
{
    const Colour green = rgb (0x2f, 0x9e, 0x4f), cream = rgb (0xf1, 0xea, 0xd2), gold = rgb (0xc9, 0xa2, 0x27),
                 maroon = rgb (0x7a, 0x1f, 0x1f), orange = rgb (0xe8, 0x76, 0x2c), nearBlack = rgb (0x11, 0x11, 0x11),
                 rackGrey = rgb (0x17, 0x18, 0x1b);

    switch (which)
    {
        case 0:  return effectTile ("Tube drive", nullptr, false, look, colours::electric, pedalStomp, green, cream);
        case 1:  return effectTile ("Tube drive", nullptr, true, look, colours::electric, pedalStomp, green, cream);
        case 2:  return effectTile ("Clean boost", nullptr, true, look, colours::acoustic, pedalStomp, gold, maroon);
        case 3:  return effectTile ("Clean boost", nullptr, false, look, colours::acoustic, pedalStomp, gold, maroon);
        case 4:  return effectTile ("Bass drive", nullptr, false, look, colours::bass, pedalStomp, orange, nearBlack);
        case 5:  return effectTile ("Delay throw", nullptr, false, look, colours::vocal, -1, 0, 0);
        case 6:  return effectTile ("Harmony", "Key of G", true, look, colours::vocal, -1, 0, 0);
        case 7:
        {
            Tile t = effectTile ("Tap", "120", true, look, colours::electric, -1, 0, 0);
            t.flashTo = look == lookFullWhite ? colours::white : colours::black;        // on the beat, then off
            t.flashCentis = 25;
            return t;
        }
        default: return effectTile ("Compressor", nullptr, true, look, colours::vocal, pedalRack, rackGrey, cream);
    }
}
}

int numDemoPages()                      { return numDemoNames; }
const char* demoPageName (int page)     { return page >= 0 && page < numDemoNames ? demoNames[page] : ""; }

Tile demoTile (int page, int position)
{
    static const uint8_t acKick[] = { picAcoustic, picKick }, bassOnly[] = { picBass }, micAcEl[] = { picMic, picAcoustic, picElectric },
                         elOnly[] = { picElectric }, micOnly[] = { picMic };
    const int p = position < 0 ? 0 : position > 2 ? 2 : position;

    if (page >= 3 && page <= 11)
        return demoEffect (((page - 3) % 3) * 3 + p, (page - 3) / 3);

    switch (page * 3 + p)
    {
        case 0:  return trackTile (1, trackEmpty, acKick, 2, 0);
        case 1:  return trackTile (2, trackRecording, bassOnly, 1, 0);
        case 2:  return trackTile (3, trackPlaying, micAcEl, 3, 0);
        case 3:  return trackTile (4, trackOverdubbing, elOnly, 1, 0);
        case 4:  return trackTile (5, trackStopped, micOnly, 1, 0);
        case 5:  return trackTile (6, trackPlaying, micAcEl, 3, 2);
        case 6:  return wordsTile (colours::red, "Stop 3", nullptr, lookFullWhite);
        case 7:  return wordsTile (colours::blue, "Stop 1", nullptr, lookFullWhite);
        case 8:  return dimTile ("Stop 5");
        case 36: return wordsTile (colours::green, "All stop", nullptr, lookFullWhite);
        case 37: return wordsTile (colours::red, "All start", nullptr, lookFullWhite);
        case 38: return dimTile ("All start");
        case 39: return wordsTile (colours::white, "Undo", nullptr, lookFullWhite);
        case 40: return wordsTile (colours::red, "Redo", nullptr, lookFullWhite);
        case 41: return wordsTile (colours::green, "Undo", nullptr, lookFullWhite);
        case 42: return wordsTile (colours::white, "Bank 1", nullptr, lookFullWhite);
        case 43: return wordsTile (colours::purple, "Bank 2", nullptr, lookFullWhite);
        default: break;
    }

    Tile sizes;         // the three sizes of words, to find the smallest that reads from standing height
    sizes.background = colours::black;
    sizes.items[sizes.numItems++] = wordsItem ("Size 48", textWords, colours::ink, false);
    sizes.items[sizes.numItems++] = wordsItem ("Size 36", textSmall, colours::ink, false);
    sizes.items[sizes.numItems++] = wordsItem ("Size 28", textTiny, colours::ink, false);
    return sizes;
}

//==============================================================================
namespace
{
struct Writer
{
    uint8_t* out;
    int n = 0;

    void u7 (int v)             { if (n < maxMessage) out[n] = (uint8_t) (v & 0x7F); ++n; }
    void colour (Colour c)      { u7 (c >> 9); u7 (c >> 2); u7 (c & 3); }
    void start (int command)    { u7 (0x7D); u7 (0x47); u7 (0x52); u7 (1); u7 (command); }

    int finish()
    {
        int sum = 0;
        for (int i = 0; i < n && i < maxMessage; ++i) sum += out[i];
        u7 (sum);
        return n <= maxMessage ? n : 0;
    }
};

struct Reader
{
    const uint8_t* in;
    int length, at = 0;
    bool bad = false;

    int u7()                    { if (at >= length) { bad = true; return 0; } return in[at++] & 0x7F; }
    Colour colour()             { const int a = u7(), b = u7(), c = u7(); return (Colour) ((a << 9) | (b << 2) | (c & 3)); }
};
}

int encodeTile (int screen, const Tile& tile, uint8_t* out)
{
    Writer w { out };
    w.start (commandTile);
    w.u7 (screen);
    w.u7 (12);                  // how many bytes of tile settings follow (a later program may add more at the end)
    w.colour (tile.background);
    w.colour (tile.flashTo);
    w.u7 (tile.flashCentis);
    w.colour (tile.border);
    w.u7 (tile.borderWidth);
    const int n = tile.numItems > maxItems ? maxItems : tile.numItems;
    w.u7 (n);

    for (int i = 0; i < n; ++i)
    {
        const Item& item = tile.items[i];
        w.u7 (item.kind);

        if (item.kind == itemWords)
        {
            const int letters = (int) strnlen (item.text, maxText);
            w.u7 (6 + letters);
            w.u7 (item.size);
            w.u7 (item.backing);
            w.colour (item.ink);
            w.u7 (letters);
            for (int k = 0; k < letters; ++k) w.u7 (item.text[k]);
        }
        else if (item.kind == itemLanes)
        {
            const int pictures = item.numPictures > maxLanePictures ? maxLanePictures : item.numPictures;
            w.u7 (2 + pictures);
            w.u7 (pictures);
            for (int k = 0; k < pictures; ++k) w.u7 (item.pictures[k]);
            w.u7 (item.more);
        }
        else if (item.kind == itemPedal)
        {
            w.u7 (8);
            w.u7 (item.pedalKind);
            w.u7 (item.lit);
            w.colour (item.body);
            w.colour (item.knob);
        }
        else
        {
            w.u7 (0);
        }
    }

    return w.finish();
}

int encodeShow (int mode, int page, uint8_t* out)
{
    Writer w { out };
    w.start (commandShow);
    w.u7 (mode);
    w.u7 (page);
    return w.finish();
}

int encodeHello (uint8_t* out)
{
    Writer w { out };
    w.start (commandHello);
    return w.finish();
}

int encodeStatus (const Status& s, uint8_t* out)
{
    Writer w { out };
    w.start (commandStatus);
    w.u7 (s.program);
    w.u7 (s.screens);
    w.u7 (s.tilesHeard >> 7);
    w.u7 (s.tilesHeard);
    w.u7 (s.mode);
    w.u7 (s.page);
    return w.finish();
}

bool decode (const uint8_t* bytes, int length, Message& m)
{
    if (bytes == nullptr)
        return false;

    if (length > 0 && bytes[0] == 0xF0)             { ++bytes; --length; }
    if (length > 0 && bytes[length - 1] == 0xF7)    --length;

    if (length < 6 || length > maxMessage || bytes[0] != 0x7D || bytes[1] != 0x47 || bytes[2] != 0x52 || bytes[3] != 1)
        return false;

    int sum = 0;
    for (int i = 0; i < length - 1; ++i)
    {
        if (bytes[i] > 0x7F) return false;
        sum += bytes[i];
    }

    if ((sum & 0x7F) != bytes[length - 1])
        return false;

    Reader r { bytes, length - 1 };
    r.at = 4;
    m = Message();
    m.command = r.u7();

    if (m.command == commandHello)
        return ! r.bad;

    if (m.command == commandShow)
    {
        m.mode = r.u7();
        m.page = r.u7();
        return ! r.bad;
    }

    if (m.command == commandStatus)
    {
        m.status.program = r.u7();
        m.status.screens = r.u7();
        const int high = r.u7();
        m.status.tilesHeard = (high << 7) | r.u7();
        m.status.mode = r.u7();
        m.status.page = r.u7();
        return ! r.bad;
    }

    if (m.command != commandTile)
        return false;

    m.screen = r.u7();
    const int settings = r.u7(), settingsEnd = r.at + settings;
    Tile& t = m.tile;
    t.background = r.colour();
    t.flashTo = r.colour();
    t.flashCentis = (uint8_t) r.u7();
    t.border = r.colour();
    t.borderWidth = (uint8_t) r.u7();
    const int items = r.u7();

    if (r.bad || settings < 12 || settingsEnd > r.length || items > maxItems || m.screen >= maxScreens)
        return false;

    r.at = settingsEnd;         // skips anything a newer program added

    for (int i = 0; i < items; ++i)
    {
        Item& item = t.items[i];
        item.kind = (uint8_t) r.u7();
        const int size = r.u7(), end = r.at + size;

        if (r.bad || end > r.length)
            return false;

        if (item.kind == itemWords)
        {
            item.size = (uint8_t) r.u7();
            item.backing = (uint8_t) r.u7();
            item.ink = r.colour();
            const int letters = r.u7();

            if (letters > maxText || r.at + letters > end)
                return false;

            for (int k = 0; k < letters; ++k)
            {
                const int c = r.u7();
                item.text[k] = (char) (c >= 32 && c <= 126 ? c : '?');
            }
        }
        else if (item.kind == itemLanes)
        {
            item.numPictures = (uint8_t) r.u7();

            if (item.numPictures > maxLanePictures)
                return false;

            for (int k = 0; k < item.numPictures; ++k)
                item.pictures[k] = (uint8_t) r.u7();

            item.more = (uint8_t) r.u7();
        }
        else if (item.kind == itemPedal)
        {
            item.pedalKind = (uint8_t) r.u7();
            item.lit = (uint8_t) r.u7();
            item.body = r.colour();
            item.knob = r.colour();
        }
        else
        {
            item.kind = itemNone;       // a kind this program does not know: left out, the rest still drawn
        }

        if (r.bad || r.at > end)
            return false;

        r.at = end;
    }

    t.numItems = (uint8_t) items;
    return ! r.bad && r.at == r.length;
}

} } // namespace gigrig::tile
