// The footswitch screens on the controller (Teensy 4.1). Guide: docs/switch-screens.md.
//
// What it does:
//   - With no computer talking to it, it turns through sample pages by itself: every look from
//     the mock-up, three tiles to a page. That is the bench test and the sun test, and it needs
//     only power (a USB battery will do).
//   - When the app sends tiles (its Switch screens tab), each screen shows what its footswitch
//     is doing right now: loop track colours, effect on and off, and so on. The app decides
//     every colour, word and picture, so a look can be changed without loading this program again.
//   - If the app goes quiet for six seconds the sample pages come back, so a stale picture is
//     never left looking live.
//
// How a tile reaches a screen: it is drawn into memory by the code shared with the app
// (firmware/shared/SwitchTile.cpp), then sent to the screen eight rows at a time, one slice per
// pass of loop(). A whole screen takes about 60 ms; nothing here ever waits longer than 2 ms.
//
// Rules agreed with the kick pad's part of this program: nothing here reads an analogue pin,
// switches interrupts off, or holds loop() up.
//
// NOT YET RUN ON HARDWARE. GitHub proves this compiles; the shared drawing code is also checked
// by the app's self-test. The wiring, the screen speed and the select-chip option are unproven
// until Tom's bench test says otherwise.
#include "Screens.h"

#include "../../shared/SwitchTile.h"

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

namespace screens
{

namespace
{
using namespace gigrig::tile;

constexpr int programVersion = 2;       // 1 was the first bench test (three fixed pages). The app's report shows it.

//==============================================================================
// Wiring. The same as the first bench test, so nothing on the breadboard moves.
//
// Every screen shares the clock (13), data (11), DC (9) and reset (8) wires. What differs is
// how each screen is told "this is for you" (its CS pin):
//
//   selectByChips = false (the bench): one Teensy pin per screen, listed in selectPins.
//       Add a screen by adding its pin to the list.
//   selectByChips = true (the plan for the finished board, untried): the CS pins of all 17
//       screens come from three 74HC595 chips in a chain, which listen to the same clock and
//       data wires as the screens and need ONE Teensy pin (chipLatchPin) between them. That is
//       how 17 screens fit the Teensy's pin budget. Screen 1 is the first chip's output QA,
//       screen 9 the second chip's QA, screen 17 the third chip's QA.
constexpr bool selectByChips = false;
constexpr int selectPins[] = { 10, 7, 6 };
constexpr int chipLatchPin = 10;
constexpr int dcPin = 9;
constexpr int resetPin = 8;
constexpr uint32_t screenSpeed = 16000000;      // gentle enough for breadboard jumper wires

constexpr int numScreens = selectByChips ? maxScreens : (int) (sizeof selectPins / sizeof selectPins[0]);

constexpr int rowsPerSlice = 8;                 // 8 rows = 3,840 bytes = 1.9 ms at 16 MHz
constexpr uint32_t pageMillis = 6000;           // how long each sample page stays
constexpr uint32_t quietMillis = 6000;          // no word from the computer for this long: back to the sample pages
constexpr uint32_t noteMillis = 2500;

// One screen object does the talking; which screen hears it is chosen by hand (select below).
Adafruit_ST7789 tft (-1, dcPin, -1);

DMAMEM Colour frame[side * side];               // the tile being sent, as colours
uint8_t slice[rowsPerSlice * side * 2];         // one slice of it, as the bytes the screen wants

struct Screen
{
    Tile fromRig;               // the last tile the computer sent for this screen
    bool hasRigTile = false;
    Tile showing;               // what it should be showing now
    bool flashOn = false;
    bool stale = true;          // needs sending again
};

Screen all[numScreens];

int mode = showDemo, heldPage = 0;      // what the computer last asked for
bool computerHeard = false;
elapsedMillis sinceComputer;
int tilesHeard = 0;

int demoPage = 0;
elapsedMillis sincePage;
bool wasDemo = true;

// The tile being sent right now, slice by slice.
int sending = -1, sendRow = 0, sendEnd = 0;
int lookNext = 0;

char noteBig[16] = "", noteSmall[28] = "";
bool noteShowing = false, noteStale = false;
elapsedMillis sinceNote;

uint8_t incoming[maxMessage + 2];
int incomingLength = 0;
bool incomingTooLong = false;

//==============================================================================
void chipsWrite (uint32_t pattern)      // one bit per screen, 0 = chosen. Screen 1 is bit 0.
{
    // Mode 3, the same as the screens, so the clock wire never moves while a screen is listening.
    SPI.beginTransaction (SPISettings (4000000, MSBFIRST, SPI_MODE3));
    SPI.transfer ((uint8_t) (pattern >> 16));       // the third chip's byte goes in first and is pushed along the chain
    SPI.transfer ((uint8_t) (pattern >> 8));
    SPI.transfer ((uint8_t) pattern);
    SPI.endTransaction();
    digitalWriteFast (chipLatchPin, HIGH);
    digitalWriteFast (chipLatchPin, LOW);
}

/** 74HC595 outputs run QA..QH from the LAST bit shifted in, so each byte is sent back to front. */
uint8_t backToFront (uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (b & (1 << i))
            r |= (uint8_t) (0x80 >> i);
    return r;
}

void chooseOnly (uint32_t chosenBits)   // bit i set = screen i + 1 listens
{
    if (selectByChips)
    {
        const uint32_t idle = ~chosenBits;
        chipsWrite (((uint32_t) backToFront ((uint8_t) (idle >> 16)) << 16)
                    | ((uint32_t) backToFront ((uint8_t) (idle >> 8)) << 8)
                    | (uint32_t) backToFront ((uint8_t) idle));
        return;
    }

    for (int i = 0; i < numScreens; ++i)
        digitalWriteFast (selectPins[i], (chosenBits & (1u << i)) ? LOW : HIGH);
}

void chooseNone()                       { chooseOnly (0); }

//==============================================================================
bool showingDemo()
{
    return ! computerHeard || sinceComputer > quietMillis || mode != showRig;
}

int pageNow()
{
    return (computerHeard && sinceComputer <= quietMillis && mode == showDemoPage) ? heldPage : demoPage;
}

Tile wantedFor (int screen)
{
    if (showingDemo())
        return demoTile (pageNow() % numDemoPages(), screen % demoTilesPerPage);

    if (all[screen].hasRigTile)
        return all[screen].fromRig;

    char words[16];
    snprintf (words, sizeof words, "Screen %d", screen + 1);
    return dimTile (words);
}

/** Works out what every screen should show, and marks the ones that have changed. */
void decide()
{
    const bool demo = showingDemo();

    if (demo && ! (computerHeard && sinceComputer <= quietMillis && mode == showDemoPage) && sincePage > pageMillis)
    {
        demoPage = (demoPage + 1) % numDemoPages();
        sincePage = 0;
        Serial.printf ("sample page %d of %d: %s\n", demoPage + 1, numDemoPages(), demoPageName (demoPage));
    }

    if (demo != wasDemo)
    {
        wasDemo = demo;
        Serial.println (demo ? "showing the sample pages" : "showing what the computer sends");
    }

    for (int i = 0; i < numScreens; ++i)
    {
        Screen& s = all[i];
        const Tile wanted = wantedFor (i);
        const bool flash = wanted.flashCentis > 0 && ((millis() / ((uint32_t) wanted.flashCentis * 10u)) & 1u) != 0;

        if (! sameTile (wanted, s.showing) || flash != s.flashOn)
        {
            s.showing = wanted;
            s.flashOn = flash;
            s.stale = true;
        }
    }

    if (noteShowing && sinceNote > noteMillis)
    {
        noteShowing = false;
        all[0].stale = true;
    }
}

void sendSlice()
{
    const int rows = sendEnd - sendRow < rowsPerSlice ? sendEnd - sendRow : rowsPerSlice;
    const Colour* from = frame + sendRow * side;

    for (int i = 0; i < rows * side; ++i)       // the screens want the high byte first
    {
        slice[2 * i] = (uint8_t) (from[i] >> 8);
        slice[2 * i + 1] = (uint8_t) from[i];
    }

    chooseOnly (1u << sending);
    tft.startWrite();
    tft.setAddrWindow (0, (uint16_t) sendRow, side, (uint16_t) rows);
    SPI.transfer (slice, nullptr, (size_t) (rows * side * 2));
    tft.endWrite();
    chooseNone();

    sendRow += rows;

    if (sendRow >= sendEnd)
        sending = -1;
}

void startSending (int screen, int firstRow)
{
    draw (all[screen].showing, all[screen].flashOn, frame);

    if (screen == 0 && noteShowing)
        drawNote (frame, noteBig, noteSmall);

    sending = screen;
    sendRow = firstRow;
    sendEnd = side;
}

//==============================================================================
void handle (const Message& m)
{
    computerHeard = true;
    sinceComputer = 0;

    if (m.command == commandTile)
    {
        tilesHeard = (tilesHeard + 1) & 0x3FFF;

        if (m.screen < numScreens)
        {
            all[m.screen].fromRig = m.tile;
            all[m.screen].hasRigTile = true;
        }
    }
    else if (m.command == commandShow)
    {
        mode = m.mode;
        heldPage = m.page;
    }
    else if (m.command == commandHello)
    {
        Status s;
        s.program = programVersion;
        s.screens = numScreens;
        s.tilesHeard = tilesHeard;
        s.mode = mode;
        s.page = pageNow();
        uint8_t reply[maxMessage];
        const int n = encodeStatus (s, reply);
        usbMIDI.sendSysEx ((uint32_t) n, reply, false);
        usbMIDI.send_now();
    }
}

/** MIDI system exclusive from the computer, a piece at a time (usbMIDI.read() in loop() feeds this). */
void onSysEx (const uint8_t* data, uint16_t length, bool last)
{
    for (uint16_t i = 0; i < length; ++i)
    {
        if (incomingLength < (int) sizeof incoming)
            incoming[incomingLength++] = data[i];
        else
            incomingTooLong = true;
    }

    if (! last)
        return;

    Message m;

    if (! incomingTooLong && decode (incoming, incomingLength, m))
        handle (m);

    incomingLength = 0;
    incomingTooLong = false;
}
} // namespace

//==============================================================================
void begin()
{
    if (selectByChips)
    {
        pinMode (chipLatchPin, OUTPUT);
        digitalWriteFast (chipLatchPin, LOW);
    }
    else
    {
        for (int i = 0; i < numScreens; ++i)
        {
            pinMode (selectPins[i], OUTPUT);
            digitalWriteFast (selectPins[i], HIGH);
        }
    }

    SPI.begin();
    chooseNone();

    // One reset wire wakes every screen.
    pinMode (resetPin, OUTPUT);
    digitalWriteFast (resetPin, HIGH);
    delay (10);
    digitalWriteFast (resetPin, LOW);
    delay (10);
    digitalWriteFast (resetPin, HIGH);
    delay (150);

    // Every screen listens while the start-up commands go out once: 17 screens start as fast as one.
    chooseOnly (0xFFFFFFFFu >> (32 - numScreens));
    tft.init (side, side, SPI_MODE3);
    tft.setSPISpeed (screenSpeed);
    tft.setRotation (0);
    chooseNone();

    usbMIDI.setHandleSystemExclusive (onSysEx);
    Serial.printf ("footswitch screens: program %d, %d screens, sample page 1 of %d: %s\n",
                   programVersion, numScreens, numDemoPages(), demoPageName (0));
}

void service()
{
    if (sending >= 0)
    {
        sendSlice();
        return;
    }

    decide();

    // The pad's read-out goes first, and only its band of rows is sent, so it appears at once.
    if (noteStale && noteShowing)
    {
        noteStale = false;
        const bool wholeTile = all[0].stale;
        all[0].stale = false;
        startSending (0, wholeTile ? 0 : noteTop);
        return;
    }

    for (int k = 0; k < numScreens; ++k)
    {
        const int i = (lookNext + k) % numScreens;

        if (all[i].stale)
        {
            all[i].stale = false;
            lookNext = (i + 1) % numScreens;
            startSending (i, 0);
            return;
        }
    }
}

void note (const char* big, const char* small)
{
    snprintf (noteBig, sizeof noteBig, "%s", big);
    snprintf (noteSmall, sizeof noteSmall, "%s", small);
    noteShowing = true;
    noteStale = true;
    sinceNote = 0;
}

} // namespace screens
