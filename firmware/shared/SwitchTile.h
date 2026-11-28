// The footswitch screens: what one small screen shows (a "tile"), how it is drawn, and how the
// computer tells the board's controller what to show.
//
// This file and SwitchTile.cpp are used twice, unchanged:
//   - by the controller's program (firmware/bench-test), which draws tiles on the real screens;
//   - by the app (src/SwitchScreens.*), which builds the tiles from what the rig is doing, shows
//     the same drawing on its Switch screens tab, and checks it in the self-test.
// So what the app shows is, dot for dot, what the Teensy draws. Plain C++ with no Arduino and no
// JUCE in it, and nothing newer than C++14, so both compilers take it.
//
// Guide: docs/switch-screens.md.
//
// The numbers of the item kinds, text sizes, pictures, pedal kinds and message commands go down
// the USB lead between two programs that are updated separately: never renumber them, only add.
#pragma once

#include <stdint.h>

namespace gigrig { namespace tile
{

constexpr int side = 240;               // the screens are 240 x 240 dots
constexpr int maxItems = 4;             // things stacked on one tile, top to bottom
constexpr int maxText = 15;             // letters in one item
constexpr int maxLanePictures = 3;
constexpr int maxScreens = 17;          // the board has 17 footswitches, each with its own screen

/** A colour as the screens take it: 5 bits red, 6 green, 5 blue. */
typedef uint16_t Colour;
constexpr Colour rgb (int r, int g, int b)      { return (Colour) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

/** The mock-up's colours (the "Footswitch screen mock-up" page in the project). */
namespace colours
{
    constexpr Colour black   = rgb (0, 0, 0);
    constexpr Colour blue    = rgb (0x1d, 0x5f, 0xd6);     // an empty loop track
    constexpr Colour red     = rgb (0xd4, 0x22, 0x22);     // stopped (steady) or recording (flashing)
    constexpr Colour redDark = rgb (0x3a, 0x0a, 0x0a);     // the other half of the recording flash
    constexpr Colour green   = rgb (0x17, 0xa3, 0x4a);     // playing
    constexpr Colour yellow  = rgb (0xf2, 0xc2, 0x00);     // overdubbing
    constexpr Colour white   = rgb (0xf4, 0xf4, 0xf1);     // off, unused
    constexpr Colour purple  = rgb (0x7a, 0x35, 0xd8);     // on
    constexpr Colour dim     = rgb (0x3a, 0x3d, 0x41);     // nothing to do here just now
    constexpr Colour dimInk  = rgb (0xae, 0xb4, 0xba);     // words on a dim tile
    constexpr Colour ink     = rgb (0xff, 0xff, 0xff);
    constexpr Colour backing = rgb (0x08, 0x09, 0x0a);     // the dark patch behind words

    // The chain colours (Tom, 28 Nov 2026): the border of an effect switch's tile.
    constexpr Colour vocal    = red;
    constexpr Colour acoustic = yellow;
    constexpr Colour electric = rgb (0x2f, 0x7b, 0xff);
    constexpr Colour bass     = rgb (0xf0, 0x7a, 0x1a);
    constexpr Colour stomp    = rgb (0x34, 0xd2, 0x7b);    // a stand-in, undecided
    constexpr Colour duo      = rgb (0xc0, 0x8c, 0xff);    // a stand-in, undecided
}

enum ItemKind  { itemNone = 0, itemWords = 1, itemLanes = 2, itemPedal = 3 };
enum TextSize  { textBig = 0, textWords = 1, textSmall = 2, textTiny = 3 };          // 82, 48, 36 and 28 dots tall
enum Picture   { picMic = 0, picAcoustic = 1, picElectric = 2, picBass = 3, picKick = 4, numPictures = 5 };
enum PedalKind { pedalStomp = 0, pedalRocker = 1, pedalRack = 2, pedalAmp = 3, pedalSliders = 4, numPedalKinds = 5 };

/** One thing on a tile. Which fields count depends on the kind. */
struct Item
{
    uint8_t kind = itemNone;

    // words
    uint8_t size = textWords;       // the size asked for; words that do not fit go onto two lines, then a size down
    uint8_t backing = 1;            // 1 = on a dark patch
    Colour ink = colours::ink;
    char text[maxText + 1] = {};

    // lanes: up to three pictures, and "+2" when the track has more
    uint8_t numPictures = 0;
    uint8_t pictures[maxLanePictures] = {};
    uint8_t more = 0;

    // pedal: a plain drawing of the kind of gear, in its colours
    uint8_t pedalKind = pedalStomp;
    uint8_t lit = 0;                // its lamp
    Colour body = 0, knob = 0;
};

/** Everything one screen shows. */
struct Tile
{
    Colour background = colours::black;
    Colour flashTo = colours::black;    // with flashCentis above 0 the background goes to and fro between the two
    uint8_t flashCentis = 0;            // how long each half of the flash lasts, in hundredths of a second (0 = steady, 127 at most)
    Colour border = colours::black;
    uint8_t borderWidth = 0;            // dots; 0 = no border
    uint8_t numItems = 0;
    Item items[maxItems];
};

bool sameTile (const Tile&, const Tile&);

//==============================================================================
// Drawing

/** Draws a tile into side x side colours (row after row from the top left). flashOn chooses
    the flash's second half. Returns false if something had to be squeezed past its limits or
    cut off to fit, which the app's self-test treats as a mistake in the tile. */
bool draw (const Tile&, bool flashOn, Colour* dots);

/** How wide some words are at a size, in dots (for anyone composing a tile). */
int textWidth (const char* text, int size);

/** A black band over the bottom of a tile with a big line and a small one: the bench test's
    "pad hit" read-out. */
void drawNote (Colour* dots, const char* big, const char* small);
constexpr int noteTop = 150;            // the band's first row

//==============================================================================
// Ready-made tiles: the looks from the mock-up, in one place so the app and the demo agree.

/** How an effect switch that is off looks. All three are there to be compared on the bench. */
enum OffLook { lookChainBorder = 0, lookWhiteOutline = 1, lookFullWhite = 2, numOffLooks = 3 };

enum TrackLook { trackEmpty = 0, trackRecording = 1, trackPlaying = 2, trackOverdubbing = 3, trackStopped = 4 };

/** A loop track's switch: its number, and its lanes as pictures. */
Tile trackTile (int number, int trackLook, const uint8_t* pictures, int numPictures, int more);

/** Words on a coloured tile ("All stop", "Bank 2"); second is a smaller line under them or null.
    A white tile follows the off look (white all over, or a white outline on black). */
Tile wordsTile (Colour background, const char* words, const char* second, int offLook);

/** Dark grey with paler words: readable but clearly less bright. */
Tile dimTile (const char* words);

/** An effect switch. chain is the chain's colour. pedalKind below 0 = words only (a hold effect). */
Tile effectTile (const char* name, const char* second, bool on, int offLook, Colour chain,
                 int pedalKind, Colour body, Colour knob);

/** The sample tiles: every look in the mock-up, for the bench and the sun test with no computer. */
int numDemoPages();
constexpr int demoTilesPerPage = 3;
Tile demoTile (int page, int position);
/** What a sample page is showing, in a few words (for the serial log and the app). */
const char* demoPageName (int page);

//==============================================================================
// Messages between the computer and the controller. They travel as MIDI system exclusive:
// F0, then these bytes, then F7. Every byte is below 128, as MIDI demands.
//
//   7D 47 52 <version 1> <command> ... <check>
//
// 7D is the number MIDI sets aside for private use, 47 52 is "GR". The check byte is the sum of
// every byte before it, kept to 7 bits, so a damaged message is thrown away, never drawn.

constexpr int maxMessage = 160;         // bytes, without the F0 and F7

enum Command
{
    commandTile   = 1,      // computer -> controller: screen number, then the tile
    commandShow   = 2,      // computer -> controller: what to show (ShowMode), sample page
    commandHello  = 3,      // computer -> controller: "are you there?" (also keeps the tiles on screen)
    commandStatus = 4       // controller -> computer: the answer to hello
};

enum ShowMode
{
    showRig = 0,            // what the computer sends
    showDemo = 1,           // the sample pages, turning by themselves
    showDemoPage = 2        // one sample page, held
};

struct Status
{
    int program = 0;        // the controller program's own version number
    int screens = 0;        // how many screens it drives
    int tilesHeard = 0;     // tiles taken in since it was switched on (0 to 16383, then round again)
    int mode = 0, page = 0; // what it is showing
};

struct Message
{
    int command = 0;
    int screen = 0;         // commandTile
    Tile tile;
    int mode = 0, page = 0; // commandShow
    Status status;          // commandStatus
};

int encodeTile (int screen, const Tile&, uint8_t* out);
int encodeShow (int mode, int page, uint8_t* out);
int encodeHello (uint8_t* out);
int encodeStatus (const Status&, uint8_t* out);

/** Reads one message, with or without its F0 and F7. False for anything that is not a whole,
    undamaged message of this kind. */
bool decode (const uint8_t* bytes, int length, Message&);

} } // namespace gigrig::tile
