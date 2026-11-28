# The footswitch screens

Every footswitch on the finished board has its own small colour screen (1.54 inch, 240 x 240
dots). This page is the guide to what those screens show, how to try it today, and how the
parts fit together for whoever works on it next.

**Everything about the looks is undecided until Tom has seen them on real screens, indoors and
in the sun.** That is why every look is a choice in the app and none of it is fixed inside the
controller.

## What is built

1. **The pictures.** One "tile" per switch: a background colour for the state, words on a dark
   patch, lane pictures, a pedal picture. These are Tom's ideas of 28 Nov 2026, as drawn on the
   mock-up page in the project.
2. **The Switch screens tab in the app.** It shows the tile for every switch the app knows,
   live, drawn by the very same code the controller uses. It works today with no parts at all.
3. **The controller's program** (`GigRig-Controller-Test.hex`, the same one file as before). With
   no computer talking to it, it turns through sample pages by itself, which is the bench test
   and the sun test. When the app's box is ticked, each screen shows what its switch is doing.

## Trying it with no parts (today)

1. Open the app and click the **Switch screens** tab (the last tab).
2. Click the **Looper** tab, load a song, and click the on-screen switches (or press the RC-600).
3. Go back to **Switch screens**. Each switch's picture has followed: blue for an empty track,
   flashing red while it records, green playing, yellow overdubbing, steady red stopped.
4. Change **An effect switch that is off looks like** to compare the three off looks.
5. Change **The screens show** to **One sample page, held** and use **Next page** to see every
   look from the mock-up, three at a time.
6. **Copy report** has a "Switch screens" part that says in words what each switch's screen shows.

## Trying it on the bench (needs the Teensy and three screens)

The wiring is the same as the first bench test, so nothing moves: see `docs/bench-test.md` and
the "Gig Rig bench tests" page in the project.

1. Load the newest `GigRig-Controller-Test.hex` onto the Teensy (the steps are in `docs/bench-test.md`).
2. Power the Teensy. With the app closed, or from a USB battery, the three screens turn through
   15 sample pages, six seconds each. That is the sun test: take it outside on a battery.
3. Plug the Teensy into the computer, open the app, click **Device check**, then **Scan again**.
4. On the **Switch screens** tab tick **Send to the board's screens**. The status line should
   turn to **CONNECTED** and name the controller's program number and its three screens.
5. Press switches on the RC-600 (or click them on the Looper tab). The three real screens follow.
   **The three bench screens show** chooses which three switches they are.
6. Untick the box, or close the app: within six seconds the sample pages come back by themselves.

## What each kind of switch shows

| Switch | Its screen |
|---|---|
| Loop track | The track number, big, and up to three lane pictures (microphone = vocal, acoustic guitar, lightning bolt = electric, bass clef = bass, kick drum = stomp; more than three shows "+2"). Blue empty, flashing red recording, green playing, yellow overdubbing, steady red stopped. |
| Stop (and play/stop) | "Stop 3". Red while its track plays or records, blue while it is empty, dim when it is recorded but stopped (a play/stop switch then says "Play 3"). |
| Undo | White "Undo" when unused, red "Redo" after an undo, green "Undo" after a redo. Back to white when a new loop or overdub is recorded. |
| All start/stop | Green "All stop" while anything plays or records, red "All start" when everything is stopped, dim "All start" when nothing is recorded. |
| Bank | White "Bank 1", purple "Bank 2". |
| Clear | White when there is something to clear, dim when there is not. (Claude's guess; Tom has not said.) |
| An effect on and off | Purple when on. A small drawing of the kind of pedal, and its name. Off follows the look chosen (below). |
| A hold effect (delay throw, harmony, wah, whammy) | Words only, and the key for the harmony. Purple while the switch is held. |
| Tap tempo | "Tap" and the tempo, blinking in time. |
| A switch with no job in this song | Dim, with the switch's name. |

"Dim" is a dark grey background with paler words: readable but clearly less bright. It is done
with colour because the screens share one backlight.

**The three off looks** (Tom is choosing between them on the bench):

- **A border in the chain's colour, black inside.** Red vocal, yellow acoustic, blue electric,
  orange bass (green stomp and purple duo partner are stand-ins). The border stays when the
  effect is on. The chain's colour comes from the chain's name on the Effects tab.
- **A white outline, black inside.**
- **White all over.**

**The pedal pictures are plain stand-ins**: a stomp box, a rocker pedal, a rack unit, an amp or
a slider box, in a colour per effect. The looks of Tom's real pedals are in his private file and
are not in this repository.

## What is not built yet (on purpose)

- **The board's 17 switches.** The app still knows the RC-600's nine switches and CTL3 and CTL4,
  because that is the pedal in use. Each of those has a tile. Which of the board's 17 screens
  shows which job waits for the switch assignment screen Tom decided on (28 Nov 2026), which is
  built once the enclosure and switches exist. Until then screen N shows switch N.
- **Secondary jobs** (hold and double-press) are not shown, by Tom's choice: he plays those from memory.
- **Real pedal pictures** sent from the computer. The messages have room for new kinds of item.
- **The tuner and bass switches**, which the app does not yet have as switch jobs of their own.

## For whoever works on this next

**Where things live**

| What | Where |
|---|---|
| The tile, its drawing, the sample pages, the messages | `firmware/shared/SwitchTile.h` and `.cpp`: plain C++14, no Arduino and no JUCE, compiled into both programs |
| The letters and lane pictures as numbers | `firmware/shared/TileArt.h`, made by `tools/make_tile_art.py` (never edit it by hand) |
| The controller's side | `firmware/bench-test/src/Screens.cpp` |
| The app's side: tiles from the rig's state, sending | `src/SwitchScreens.*` |
| The tab | `src/SwitchScreensPanel.*` |
| The checks | `src/SwitchScreensSelfTest.cpp` (`GIGRIG_SELFTEST_ONLY=screens`) |
| The choices, remembered | `switch-screens.txt` in the app's settings folder |

**The rule that makes looks easy to change:** the controller knows nothing about loopers or
effects. The app sends each screen a finished description (background, flash, border, and up to
four items: words, lane pictures or a pedal drawing) and the controller draws it. A new colour
rule or wording is a change to `SwitchScreens.cpp` only, with no new hex file. A new *kind of
drawing* is a change to `SwitchTile.cpp`, which needs a new hex file too.

**The messages** are MIDI system exclusive over the Teensy's USB lead, so the app needs nothing
but the MIDI it already has: `F0 7D 47 52 01 <command> ... <check> F7`. Command 1 is a tile for
one screen, 2 says what to show (the rig, the sample pages, or one sample page), 3 is "are you
there", 4 is the controller's answer. The check byte means a damaged message is thrown away.
**The numbers of commands, item kinds, sizes, pictures and pedal kinds are never renumbered,
only added to**, because the app and the controller are updated separately. An item of a kind
the controller does not know is skipped and the rest of the tile is still drawn.

**Never put at risk:** sending is off by default, and off opens no MIDI output and sends
nothing (a check). Everything runs on the message thread, twenty times a second; nothing here
is on the audio thread and the looper was not changed. A tile goes out when it changes, and one
screen in turn is sent again four times a second so a controller that was unplugged catches up.

**The pin budget (undecided, written but untried).** Seventeen screens each need their own
"this is for you" wire (CS). Seventeen Teensy pins is too many: the switches and encoders
already fill it. `Screens.cpp` has a second way, `selectByChips`: three 74HC595 chips in a chain
make the 17 CS wires and listen to the screens' own clock and data wires, so all 17 screens cost
five Teensy pins in total (clock 13, data 11, DC 9, reset 8, latch 10). The bench uses one pin
per screen (10, 7, 6) as before. The chips are a few cents each; they are worth adding to the
next small parts order (five, for spares) so this can be tried before the final wiring.

**Speed.** A tile is drawn in memory, then sent eight rows at a time, one slice per pass of the
controller's loop, so the kick pad is never kept waiting more than about 2 ms. A whole screen
takes about 60 ms at the bench speed (16 MHz). A song change repaints all 17 in about a second
at that speed; the finished board's short wires should allow a faster clock.

**Looking at tiles without a Teensy or the app:** `tools/tile_sheet.cpp` (the two lines to build
and run it are at its top) draws every sample page into one picture with the shared code.

**What is proven and what is not.** The app's self-test proves the drawing, the messages and
the rules above, with the same code the controller compiles. GitHub proves the controller's
program compiles. **Nothing here has run on a real Teensy or screen yet.**
