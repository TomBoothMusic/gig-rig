#!/usr/bin/env python3
"""Makes firmware/shared/TileArt.h: the letters and the lane pictures for the footswitch screens.

The small screens on the board (and the app's preview of them) draw their words from four sizes
of one typeface and five little pictures. Neither the Teensy nor the app can read a font file
while drawing, so this script turns them into plain numbers once and the result is committed.

Run it only when the sizes, the typeface or a picture changes:

    python3 tools/make_tile_art.py

It needs Python with Pillow (pip install pillow) and nothing else. The typeface is Barlow
Condensed Bold (SIL Open Font Licence, tools/fonts/), the one the mock-up page uses.
Each dot is stored as 16 levels of ink (4 bits), two dots to a byte, so edges are smooth.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "tools" / "fonts" / "BarlowCondensed-Bold.ttf"
OUT = ROOT / "firmware" / "shared" / "TileArt.h"

# name, size in dots, first character, last character
FONTS = [("big", 82, "+", "9"), ("words", 48, " ", "~"), ("small", 36, " ", "~"), ("tiny", 28, " ", "~")]
ICON = 46       # each lane picture is 46 x 46 dots
SS = 6          # pictures are drawn this many times bigger, then shrunk, for smooth edges


def pack(levels):
    """A list of 0..15 values, two to a byte, first value in the high half."""
    if len(levels) % 2:
        levels = levels + [0]
    return [(levels[i] << 4) | levels[i + 1] for i in range(0, len(levels), 2)]


def font_tables(name, size, first, last):
    font = ImageFont.truetype(str(FONT), size)
    ascent, descent = font.getmetrics()
    glyphs, data = [], []
    for code in range(ord(first), ord(last) + 1):
        ch = chr(code)
        left, top, right, bottom = font.getbbox(ch, anchor="ls")
        w, h = max(0, right - left), max(0, bottom - top)
        advance = int(round(font.getlength(ch)))
        offset = len(data)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255, anchor="ls")
            for y in range(h):
                data += pack([(img.getpixel((x, y)) + 8) // 17 for x in range(w)])
        glyphs.append((offset, w, h, left, top, advance))
    return ascent, descent, glyphs, data


def cubic(p0, p1, p2, p3, steps=24):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
        pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return pts


class Pen:
    """Draws in the mock-up's 24 x 24 units."""
    def __init__(self):
        self.k = ICON * SS / 24.0
        self.img = Image.new("L", (ICON * SS, ICON * SS), 0)
        self.d = ImageDraw.Draw(self.img)

    def p(self, pt):
        return (pt[0] * self.k, pt[1] * self.k)

    def circle(self, c, r, fill=255):
        x, y = self.p(c)
        self.d.ellipse((x - r * self.k, y - r * self.k, x + r * self.k, y + r * self.k), fill=fill)

    def ring(self, c, r, width):
        self.circle(c, r + width / 2)
        self.circle(c, r - width / 2, fill=0)

    def line(self, pts, width):
        pts = [self.p(q) for q in pts]
        self.d.line(pts, fill=255, width=int(round(width * self.k)), joint="curve")
        for q in (pts[0], pts[-1]):     # round ends
            r = width * self.k / 2
            self.d.ellipse((q[0] - r, q[1] - r, q[0] + r, q[1] + r), fill=255)

    def poly(self, pts):
        self.d.polygon([self.p(q) for q in pts], fill=255)

    def rounded(self, box, radius, fill=255):
        x0, y0 = self.p(box[:2])
        x1, y1 = self.p(box[2:])
        self.d.rounded_rectangle((x0, y0, x1, y1), radius=radius * self.k, fill=fill)

    def done(self):
        small = self.img.resize((ICON, ICON), Image.LANCZOS)
        return pack([(small.getpixel((x, y)) + 8) // 17 for y in range(ICON) for x in range(ICON)])


def icon_mic():
    p = Pen()
    p.rounded((9, 2.5, 15, 13.5), 3)
    import math
    arc = [(12 + 6.5 * math.cos(math.radians(a)), 11 + 6.5 * math.sin(math.radians(a))) for a in range(0, 181, 6)]
    p.line(arc, 2)
    p.line([(12, 17.5), (12, 21)], 2)
    p.line([(8.5, 21), (15.5, 21)], 2)
    return p.done()


def icon_acoustic():
    p = Pen()
    p.d.rectangle((*p.p((10.8, -3)), *p.p((13.2, 10))), fill=255)
    p.rounded((9.8, -5, 14.2, -1.6), 1)
    p.circle((12, 11.5), 4.2)
    p.circle((12, 18), 6)
    p.circle((12, 13.5), 1.9, fill=0)
    # The mock-up turns the guitar 45 degrees and it runs a little off the square: shrink it to fit.
    turned = p.img.rotate(-45, resample=Image.BICUBIC, center=(p.img.width / 2, p.img.height / 2), expand=True)
    box = turned.getbbox()
    turned = turned.crop(box)
    side = max(turned.size)
    square = Image.new("L", (side, side), 0)
    square.paste(turned, ((side - turned.width) // 2, (side - turned.height) // 2))
    inner = int(ICON * 0.96)
    small = square.resize((inner, inner), Image.LANCZOS)
    out = Image.new("L", (ICON, ICON), 0)
    out.paste(small, ((ICON - inner) // 2, (ICON - inner) // 2))
    return pack([(out.getpixel((x, y)) + 8) // 17 for y in range(ICON) for x in range(ICON)])


def icon_electric():
    p = Pen()
    p.poly([(14, 1), (4, 13.5), (10, 13.5), (8.5, 23), (20, 9.5), (13.5, 9.5)])
    return p.done()


def icon_bass():
    p = Pen()
    curve = cubic((5, 21), (12, 18.5), (15.5, 14), (15.5, 9.5)) + cubic((15.5, 9.5), (15.5, 5.5), (13, 3), (9.8, 3)) \
          + cubic((9.8, 3), (6.8, 3), (5, 5), (5, 7.3))
    p.line(curve, 2.6)
    p.circle((6.8, 8.3), 2.3)
    p.circle((19.5, 6.5), 1.5)
    p.circle((19.5, 12), 1.5)
    return p.done()


def icon_kick():
    p = Pen()
    p.ring((12, 11), 8.5, 2)
    p.circle((12, 11), 3)
    p.line([(5.5, 17.5), (3.5, 22)], 2)
    p.line([(18.5, 17.5), (20.5, 22)], 2)
    return p.done()


def c_bytes(values, per_line=24):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ",".join(str(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def main():
    out = ["// Made by tools/make_tile_art.py. Do not edit by hand: change the script and run it again.",
           "//",
           "// The letters (Barlow Condensed Bold, SIL Open Font Licence, see tools/fonts/) and the lane",
           "// pictures for the footswitch screens, as plain numbers: 16 levels of ink per dot, two dots",
           "// to a byte (the first dot in the high half), each row starting on a fresh byte.",
           "// Included by SwitchTile.cpp only.",
           "#pragma once",
           "",
           "#include <stdint.h>",
           "",
           "namespace gigrig { namespace tile { namespace art",
           "{",
           "",
           "struct Glyph { uint32_t offset; uint8_t w, h; int8_t left, top; uint8_t advance; };",
           "struct Font  { uint8_t size, ascent, descent, first, count; const Glyph* glyphs; const uint8_t* data; };",
           ""]
    for name, size, first, last in FONTS:
        ascent, descent, glyphs, data = font_tables(name, size, first, last)
        out.append(f"static const uint8_t {name}Data[] = {{\n{c_bytes(data)}\n}};")
        out.append(f"static const Glyph {name}Glyphs[] = {{")
        for g in glyphs:
            out.append("    { %d, %d, %d, %d, %d, %d }," % g)
        out.append("};")
        out.append(f"static const Font {name}Font = {{ {size}, {ascent}, {descent}, {ord(first)}, {len(glyphs)}, {name}Glyphs, {name}Data }};")
        out.append("")
    icons = [("mic", icon_mic), ("acoustic", icon_acoustic), ("electric", icon_electric), ("bass", icon_bass), ("kick", icon_kick)]
    out.append(f"constexpr int iconSize = {ICON};")
    out.append(f"constexpr int numIcons = {len(icons)};")
    out.append(f"// In the order of tile::Icon: {', '.join(n for n, _ in icons)}. {ICON} x {ICON} dots each, {ICON // 2} bytes a row.")
    out.append("static const uint8_t iconData[numIcons][iconSize * (iconSize / 2)] = {")
    for name, fn in icons:
        out.append("  {\n" + c_bytes(fn()) + "\n  },")
    out.append("};")
    out.append("")
    out.append("} } } // namespace gigrig::tile::art")
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
