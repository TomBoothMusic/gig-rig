// Bench test for the gig rig's controller (Teensy 4.1). Plain-language guides: docs/bench-test.md
// and docs/switch-screens.md.
//
// It does two jobs at once, and either works without the other being wired:
//   1. The footswitch screens (1.54 inch, 240 x 240, ST7789), all on the same wires, each with its
//      own chip-select wire. That part is in Screens.cpp: sample pages by themselves, or what the
//      app sends once its Switch screens tab is switched on.
//   2. A piezo pad on pin A0. A stomp is sent to the computer as a MIDI note with a strength,
//      exactly as the SPD::ONE Kick sends it, so the app's Kick pad tab plays it with no change.
//
// Wiring (the guide has the pictures):
//   Screens: GND -> GND, VCC -> 3.3V, SCL -> 13, SDA -> 11, RST -> 8, DC -> 9, BL -> 3.3V,
//            CS -> 10 (screen 1), 7 (screen 2), 6 (screen 3).
//   Piezo:   red -> point P, black -> GND. At P: 1 M to GND, a diode up to 3.3V, a diode up
//            from GND, and 10 k from P to pin 14 (A0).

#include <Arduino.h>
#include <math.h>

#include "Screens.h"

namespace
{
constexpr int piezoPin = A0;
constexpr int piezoThreshold = 80;     // of 4095: a spike must pass this to count
constexpr int piezoFull = 3000;        // of 4095: this or more is the hardest hit (127)
constexpr uint32_t scanMicros = 2000;  // look for the top of the spike for 2 ms, as the app does
constexpr uint32_t holdMillis = 40;    // then ignore the pad for 40 ms, so one stomp is one hit
constexpr int midiNote = 60;           // the note and channel the SPD::ONE Kick uses
constexpr int midiChannel = 10;

elapsedMillis sinceHit = 60000;
elapsedMillis sinceNoteOn;
bool noteIsOn = false;
int hits = 0;
int tallestIgnored = 0;

// The hit's numbers across the bottom of screen 1 for a few seconds.
void showHit (int peak, int strength)
{
    char big[16], small[28];
    snprintf (big, sizeof big, "%d", strength);
    snprintf (small, sizeof small, "peak %d  n %d", peak, hits);
    screens::note (big, small);
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

    screens::begin();
}

void loop()
{
    watchPiezo();
    while (usbMIDI.read()) {}   // throw away anything the computer sends

    screens::service();     // the footswitch screens: one small piece of work each pass
}
