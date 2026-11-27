# Bench tests: the small screens and the piezo kick pad

These tests happen on the bench, before the enclosure exists. They use the board's
controller (a Teensy 4.1) and a small test program for it, which lives in
`firmware/bench-test/` and is built by GitHub into one file, `GigRig-Controller-Test.hex`,
attached to every build on the Releases page.

Tom's step-by-step sheet with the wiring pictures is a page in the project (the "Gig Rig
bench tests" artifact). This file is the short version, kept with the code so the two stay in
step. **If a pin changes in `firmware/bench-test/src/main.cpp`, change it here and on that page.**

## Three rules that keep the Teensy alive

1. Unplug the USB lead before adding, moving or removing any wire.
2. Nothing connects to the pin marked Vin or 5V. Everything runs from the pin marked 3.3V.
3. The piezo never goes straight to a Teensy pin. It goes through the resistors and diodes below.

## Loading the test program (Windows)

1. Download Teensy Loader from pjrc.com/teensy/loader.html and run it.
2. Download `GigRig-Controller-Test.hex` from the newest build on the Releases page.
3. In Teensy Loader: **File**, **Open HEX File**, choose that file.
4. Plug the Teensy in and press its small white button once. Teensy Loader says **Reboot OK**.

The program stays on the Teensy, so after this it can be plugged into the laptop or the rig computer.

## Three screens on shared wires

The screens are 1.54 inch, 240 x 240, ST7789, the eight-pin kind with a CS pin.

| Screen pin | Teensy | Shared? |
|---|---|---|
| GND | GND | all screens |
| VCC | 3.3V | all screens |
| SCL | pin 13 | all screens |
| SDA | pin 11 | all screens |
| RST (or RES) | pin 8 | all screens |
| DC | pin 9 | all screens |
| BL (or BLK) | 3.3V | all screens |
| CS | screen 1: pin 10, screen 2: pin 7, screen 3: pin 6 | one each |

Add one screen at a time. Each shows its own label (REC 1, FX A 2, SONG 3) and changes page
every six seconds: white on black, black on yellow, then a ladder of text sizes for judging
what is readable in the sun.

## The piezo pad on the Teensy

Everything meets at one point, P (one free row of the breadboard):

| Part | One end | Other end |
|---|---|---|
| Piezo red wire | P | |
| Piezo black wire | GND | |
| 1 MΩ resistor | P | GND |
| Diode 1 (1N4148) | plain end at P | stripe end to 3.3V |
| Diode 2 (1N4148) | stripe end at P | plain end to GND |
| 10 kΩ resistor | P | pin 14 (A0) |

A stomp is sent to the computer as MIDI note 60 on channel 10 with a strength from 1 to 127,
which is what the SPD::ONE Kick sends, so the app's Kick pad tab plays it unchanged: on the
**Device check** tab click **Scan again**, then on the **Kick pad** tab set **Piezo pad** to
**Off**, click **Learn pad** and hit the pad once. Screen 1 shows each hit's strength and spike height.

The two levels (the spike that counts, and the spike that is the hardest hit) are fixed numbers
at the top of `main.cpp` in this first program. They are set from Tom's results.

## What is proven and what is not

GitHub proves the program compiles. It has **not** run on a real Teensy, screen or piezo yet:
the first bench test is that proof.
