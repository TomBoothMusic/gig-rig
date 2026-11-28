// Draws every sample tile of the footswitch screens into one picture, using the very code the
// Teensy and the app draw with (firmware/shared/SwitchTile.cpp). For looking at a change to the
// tiles without a Teensy or the app:
//
//   g++ -std=c++17 -O2 -o tile_sheet tools/tile_sheet.cpp firmware/shared/SwitchTile.cpp
//   ./tile_sheet sheet.ppm          (then open sheet.ppm, or: python3 -c "from PIL import Image; Image.open('sheet.ppm').save('sheet.png')")
//
// Each row is one sample page (three tiles); a flashing tile is drawn in the first half of its flash.
#include "../firmware/shared/SwitchTile.h"

#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    using namespace gigrig::tile;
    const int pages = numDemoPages(), gap = 12, cols = demoTilesPerPage;
    const int w = cols * side + (cols + 1) * gap, h = pages * side + (pages + 1) * gap;
    std::vector<unsigned char> image ((size_t) (w * h * 3), 0x20);
    std::vector<Colour> dots ((size_t) (side * side));
    bool allFit = true;

    for (int page = 0; page < pages; ++page)
        for (int p = 0; p < cols; ++p)
        {
            if (! draw (demoTile (page, p), false, dots.data()))
            {
                allFit = false;
                std::printf ("does not fit: page %d (%s), tile %d\n", page, demoPageName (page), p + 1);
            }

            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x)
                {
                    const Colour c = dots[(size_t) (y * side + x)];
                    unsigned char* o = &image[(size_t) (((gap + page * (side + gap) + y) * w + gap + p * (side + gap) + x) * 3)];
                    o[0] = (unsigned char) (((c >> 11) & 31) * 255 / 31);
                    o[1] = (unsigned char) (((c >> 5) & 63) * 255 / 63);
                    o[2] = (unsigned char) ((c & 31) * 255 / 31);
                }
        }

    if (FILE* f = std::fopen (argc > 1 ? argv[1] : "sheet.ppm", "wb"))
    {
        std::fprintf (f, "P6\n%d %d\n255\n", w, h);
        std::fwrite (image.data(), 1, image.size(), f);
        std::fclose (f);
    }

    return allFit ? 0 : 1;
}
