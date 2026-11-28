// Bench test for the gig rig's controller (Teensy 4.1). Plain-language guides: docs/bench-test.md
// and docs/switch-screens.md.
//
// It does two jobs at once, and either works without the other being wired:
//   1. The footswitch screens (1.54 inch, 240 x 240, ST7789), all on the same wires, each with its
//      own chip-select wire. That part is in Screens.cpp: sample pages by themselves, or what the
//      app sends once its Switch screens tab is switched on.
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
#include <EEPROM.h>
#include <math.h>

#include "PadMessages.h"
#include "Screens.h"

namespace padmsg = gigrig::padmsg;
using padmsg::PadLevels;
using padmsg::PadMessage;
using padmsg::PadWatcher;

namespace
{
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

elapsedMillis sinceHit = 60000;
elapsedMillis sinceNoteOn;
bool noteIsOn = false;
int hits = 0;

// The hit's numbers across the bottom of screen 1 for a few seconds.
void showHit (int peak, int strength)
{
    char big[16], small[28];
    snprintf (big, sizeof big, "%d", strength);
    snprintf (small, sizeof small, "peak %d  n %d", peak, hits);
    screens::note (big, small);
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

    screens::begin();
}

void loop()
{
    watchPiezo();
    readFromComputer();
    saveLevelsWhenQuiet();

    screens::service();     // the footswitch screens: one small piece of work each pass
}
