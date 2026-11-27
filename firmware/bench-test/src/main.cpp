// Bench test for the gig rig's controller (Teensy 4.1). Plain-language guide: docs/bench-test.md.
//
// It does two jobs at once, and either works without the other being wired:
//   1. Up to three 1.54 inch screens (240 x 240, ST7789) on the same wires, each with its own
//      chip-select wire, each showing its own label. Proves that many screens can share wires.
//   2. A piezo pad on pin A0. A stomp is sent to the computer as a MIDI note with a strength,
//      exactly as the SPD::ONE Kick sends it, so the app's Kick pad tab plays it with no change.
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
#include <math.h>

namespace
{
constexpr int numScreens = 3;
constexpr int csPins[numScreens] = { 10, 7, 6 };
constexpr int dcPin = 9;
constexpr int resetPin = 8;
constexpr uint32_t screenSpeed = 16000000; // gentle enough for breadboard jumper wires

constexpr int piezoPin = A0;
constexpr int piezoThreshold = 80;     // of 4095: a spike must pass this to count
constexpr int piezoFull = 3000;        // of 4095: this or more is the hardest hit (127)
constexpr uint32_t scanMicros = 2000;  // look for the top of the spike for 2 ms, as the app does
constexpr uint32_t holdMillis = 40;    // then ignore the pad for 40 ms, so one stomp is one hit
constexpr int midiNote = 60;           // the note and channel the SPD::ONE Kick uses
constexpr int midiChannel = 10;

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
int tallestIgnored = 0;

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

int strengthFor (int peak)
{
    const float x = float (peak - piezoThreshold) / float (piezoFull - piezoThreshold);
    const float shaped = sqrtf (constrain (x, 0.0f, 1.0f));
    return constrain (1 + (int) lroundf (126.0f * shaped), 1, 127);
}

void watchPiezo()
{
    if (noteIsOn && sinceNoteOn > 20)
    {
        usbMIDI.sendNoteOff (midiNote, 0, midiChannel);
        noteIsOn = false;
    }

    if (sinceHit < holdMillis)
        return;

    int level = analogRead (piezoPin);
    if (level < piezoThreshold)
    {
        tallestIgnored = max (tallestIgnored, level);
        return;
    }

    int peak = level;
    elapsedMicros scan;
    while (scan < scanMicros)
        peak = max (peak, analogRead (piezoPin));

    const int strength = strengthFor (peak);
    usbMIDI.sendNoteOn (midiNote, strength, midiChannel);
    usbMIDI.send_now();
    noteIsOn = true;
    sinceNoteOn = 0;
    sinceHit = 0;
    ++hits;

    Serial.printf ("hit %d: peak %d of 4095, strength %d, tallest ignored since last hit %d\n",
                   hits, peak, strength, tallestIgnored);
    tallestIgnored = 0;
    showHit (peak, strength);
}
}

void setup()
{
    Serial.begin (115200);
    analogReadResolution (12);
    analogReadAveraging (1);
    // A weak pull to ground keeps the pin quiet when no pad is wired, so it sends no false hits.
    pinMode (piezoPin, INPUT_PULLDOWN);

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
    while (usbMIDI.read()) {}   // throw away anything the computer sends

    // The pages change every 6 seconds, but not while the pad is in use: a full redraw
    // takes long enough to miss a stomp.
    if (sincePage > 6000 && sinceHit > 30000)
    {
        page = (page + 1) % 3;
        drawPage (page);
        sincePage = 0;
    }
}
