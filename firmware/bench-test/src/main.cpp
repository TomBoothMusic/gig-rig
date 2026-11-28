// Bench test for the gig rig's controller (Teensy 4.1). Plain-language guide: docs/bench-test.md.
//
// It does two jobs at once, and either works without the other being wired:
//   1. Up to three 1.54 inch screens (240 x 240, ST7789) on the same wires, each with its own
//      chip-select wire, each showing its own label. Proves that many screens can share wires.
//   2. A piezo pad on pin A0. A stomp is sent to the computer as a MIDI note with a strength,
//      exactly as the SPD::ONE Kick sends it, with the height of the spike beside it for the
//      app's report. The app (build 160 on) knows this controller by its name, "Gig Rig
//      Controller", plays its hits as it plays the SPD::ONE's, and sets its two levels from the
//      Kick pad tab; the controller remembers them. What the two say to each other is all in
//      firmware/shared/PadMessages.h, which both programs are built from.
//
// Wiring (the guide has the pictures):
//   Screens: GND -> GND, VCC -> 3.3V, SCL -> 13, SDA -> 11, RST -> 8, DC -> 9, BL -> 3.3V,
//            CS -> 10 (screen 1), 7 (screen 2), 6 (screen 3).
//   Piezo:   red -> point P, black -> GND. At P: 1 M to GND, a diode up to 3.3V, a diode up
//            from GND, and 10 k from P to pin 14 (A0).

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <EEPROM.h>
#include <math.h>

#include "PadMessages.h"

namespace padmsg = gigrig::padmsg;
using padmsg::PadLevels;
using padmsg::PadMessage;
using padmsg::PadWatcher;

namespace
{
constexpr int numScreens = 3;
constexpr int csPins[numScreens] = { 10, 7, 6 };
constexpr int dcPin = 9;
constexpr int resetPin = 8;
constexpr uint32_t screenSpeed = 16000000; // gentle enough for breadboard jumper wires

constexpr int piezoPin = A0;
constexpr int midiNote = padmsg::note;         // the note and channel the SPD::ONE Kick uses
constexpr int midiChannel = padmsg::channel;

// The two levels (of 4095: a spike must pass the first to count, the second or more is the
// hardest hit). They start as the guesses in PadMessages.h; the app's Kick pad tab changes
// them, and they are kept in the controller's own memory so they survive the power going off.
PadLevels levels;

// The pad's pin is read by a timer, ten thousand times a second, whatever else is going on
// (drawing a screen takes far longer than a spike lasts). The timer only measures: what it
// finds waits in a short queue for loop() to send. All the thinking about what a reading
// means is in PadMessages.h, where the app's self-test checks it.
IntervalTimer padTimer;
PadWatcher watcher;                            // used by the timer only
volatile int timerThreshold = padmsg::thresholdDefault, timerFull = padmsg::fullDefault;
constexpr int queueSize = 8;
PadWatcher::Event queued[queueSize];
volatile int queueIn = 0, queueOut = 0;        // the timer moves In, loop() moves Out

constexpr uint32_t memoryMark = 0x47525032;    // "GRP2": says the memory holds our two levels
struct Remembered { uint32_t mark; uint16_t threshold, full; };
bool levelsChanged = false;
elapsedMillis sinceLevelsChanged;

const char* const labels[numScreens] = { "REC", "FX A", "SONG" };
const uint16_t colours[numScreens] = { ST77XX_RED, ST77XX_GREEN, ST77XX_CYAN };

// Only the first screen object drives the shared reset wire.
Adafruit_ST7789 screens[numScreens] = {
    Adafruit_ST7789 (csPins[0], dcPin, resetPin),
    Adafruit_ST7789 (csPins[1], dcPin, -1),
    Adafruit_ST7789 (csPins[2], dcPin, -1),
};

int page = 0;
elapsedMillis sincePage;
elapsedMillis sinceHit = 60000;
elapsedMillis sinceNoteOn;
bool noteIsOn = false;
int hits = 0;

void centred (Adafruit_ST7789& s, const char* text, int size, int y, uint16_t colour)
{
    int16_t x1, y1;
    uint16_t w, h;
    s.setTextSize (size);
    s.getTextBounds (text, 0, 0, &x1, &y1, &w, &h);
    s.setCursor ((240 - (int) w) / 2, y);
    s.setTextColor (colour);
    s.print (text);
}

// Three pages, shown in turn, so label size, colour and sunlight can all be judged.
void drawPage (int which)
{
    for (int i = 0; i < numScreens; ++i)
    {
        auto& s = screens[i];
        char number[4];
        snprintf (number, sizeof number, "%d", i + 1);

        if (which == 0)        // white label on black, with a coloured bar
        {
            s.fillScreen (ST77XX_BLACK);
            s.fillRect (0, 0, 240, 36, colours[i]);
            centred (s, labels[i], 6, 80, ST77XX_WHITE);
            centred (s, number, 4, 180, colours[i]);
        }
        else if (which == 1)   // black label on yellow: the strongest contrast for sunlight
        {
            s.fillScreen (ST77XX_YELLOW);
            centred (s, labels[i], 6, 80, ST77XX_BLACK);
            centred (s, number, 4, 180, ST77XX_BLACK);
        }
        else                   // a ladder of text sizes, to find the smallest readable one
        {
            s.fillScreen (ST77XX_BLACK);
            int y = 4;
            for (int size = 1; size <= 5; ++size)
            {
                char line[16];
                snprintf (line, sizeof line, "Size %d", size);
                s.setTextSize (size);
                s.setTextColor (ST77XX_WHITE);
                s.setCursor (4, y);
                s.print (line);
                y += size * 8 + 10;
            }
            centred (s, number, 4, 200, colours[i]);
        }
    }
}

// A small, quick update on screen 1 only, so the pad is not left unwatched for long.
void showHit (int peak, int strength)
{
    auto& s = screens[0];
    s.fillRect (0, 150, 240, 90, ST77XX_BLACK);
    char line[24];
    snprintf (line, sizeof line, "%d", strength);
    centred (s, line, 5, 156, ST77XX_YELLOW);
    snprintf (line, sizeof line, "peak %d  n %d", peak, hits);
    centred (s, line, 2, 212, ST77XX_WHITE);
}

void send (const PadMessage* messages, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (messages[i].isNote)
            usbMIDI.sendNoteOn (messages[i].number, messages[i].value, midiChannel);
        else
            usbMIDI.sendControlChange (messages[i].number, messages[i].value, midiChannel);
    }

    usbMIDI.send_now();
}

// Hands the two levels to the timer that reads the pad.
void useLevels()
{
    timerThreshold = levels.threshold;
    timerFull = levels.full;
}

// Tells the app which program this is and the two levels it holds.
void sendLevels()
{
    PadMessage out[5];
    send (out, levels.answer (out));
}

void loadLevels()
{
    Remembered r;
    EEPROM.get (0, r);

    if (r.mark == memoryMark)
        levels.set (r.threshold, r.full);
}

// Writing to memory takes a moment, so it waits until nothing has changed for two seconds
// and the pad has been still for one.
void saveLevelsWhenQuiet()
{
    if (! levelsChanged || sinceLevelsChanged < 2000 || sinceHit < 1000)
        return;

    const Remembered r { memoryMark, (uint16_t) levels.threshold, (uint16_t) levels.full };
    EEPROM.put (0, r);
    levelsChanged = false;
    Serial.printf ("levels remembered: counts from %d, hardest at %d (of 4095)\n", levels.threshold, levels.full);
}

// What the app sends: the two levels, and "tell me what you hold".
void readFromComputer()
{
    while (usbMIDI.read())
    {
        if (usbMIDI.getType() != usbMIDI.ControlChange || usbMIDI.getChannel() != midiChannel)
            continue;

        const auto took = levels.take (usbMIDI.getData1(), usbMIDI.getData2());

        if (took == PadLevels::changed)
        {
            useLevels();
            levelsChanged = true;      // written to memory once the slider has stopped moving
            sinceLevelsChanged = 0;
        }
        else if (took == PadLevels::asked)
        {
            sendLevels();
        }
    }
}

// Runs ten thousand times a second, in the middle of whatever else is happening. It must stay short.
void readPad()
{
    const auto event = watcher.read (analogRead (piezoPin), micros(), timerThreshold, timerFull);

    if (event.kind == PadWatcher::none)
        return;

    const int next = (queueIn + 1) % queueSize;

    if (next != queueOut)      // a full queue means loop() has been stuck for a long time: the event is dropped
    {
        queued[queueIn] = event;
        queueIn = next;
    }
}

void watchPiezo()
{
    if (noteIsOn && sinceNoteOn > 20)
    {
        usbMIDI.sendNoteOff (midiNote, 0, midiChannel);
        noteIsOn = false;
    }

    while (queueOut != queueIn)
    {
        const PadWatcher::Event event = queued[queueOut];
        queueOut = (queueOut + 1) % queueSize;

        PadMessage out[3];
        send (out, event.messages (out));

        if (event.kind == PadWatcher::knock)
        {
            Serial.printf ("a knock that did not count: %d of 4095 (counts from %d)\n", event.height, levels.threshold);
            continue;
        }

        noteIsOn = true;
        sinceNoteOn = 0;
        sinceHit = 0;
        ++hits;

        Serial.printf ("hit %d: peak %d of 4095, strength %d\n", hits, event.height, event.strength);
        showHit (event.height, event.strength);
    }
}
}

void setup()
{
    Serial.begin (115200);
    analogReadResolution (12);
    analogReadAveraging (1);
    // A weak pull to ground keeps the pin quiet when no pad is wired, so it sends no false hits.
    pinMode (piezoPin, INPUT_PULLDOWN);
    loadLevels();
    useLevels();
    padTimer.begin (readPad, padmsg::readEveryMicros);

    for (int i = 0; i < numScreens; ++i)
    {
        screens[i].init (240, 240, SPI_MODE3);
        screens[i].setSPISpeed (screenSpeed);
        screens[i].setRotation (0);
    }
    drawPage (page);
}

void loop()
{
    watchPiezo();
    readFromComputer();
    saveLevelsWhenQuiet();

    // The pages change every 6 seconds, but not while the pad is in use: a full redraw
    // takes long enough to miss a stomp.
    if (sincePage > 6000 && sinceHit > 30000)
    {
        page = (page + 1) % 3;
        drawPage (page);
        sincePage = 0;
    }
}
