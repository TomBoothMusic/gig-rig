# The kick pad test

Built 26 Nov 2026. This is a **test**, not a finished feature. It answers one question: can a
pad built into the board take over from the Roland SPD::ONE Kick's own sounds?

Tom uses the SPD::ONE Kick as his stomp because it follows his foot: a light tap is quiet, a
full stomp is loud. He wants the pad built into the board, with the sound made inside the rig,
so there is no bank to check before a gig and no button to wear out. That only works if the
pad tells the computer **how hard** each hit was. This test shows whether it does.

## What it does

- The **Kick pad** tab listens for the SPD::ONE Kick over its USB socket. Each hit arrives as a
  message with a strength from 1 (the lightest touch) to 127 (the hardest stomp).
- The app plays its own kick for each hit: quieter for a soft hit, and with less click as
  well, the way a beater that lands gently makes less of a click.
- Every hit is counted. The report says the softest and hardest strength seen, how many
  different strengths there were, whether any stomp was counted twice, and how long each hit
  waited before its kick started.

## Trying it (about 10 minutes)

1. Start the app and get audio running on the first tab, as for any other test.
2. Give the SPD::ONE Kick its **power adaptor or batteries**, and switch it on. It will not run
   from the USB cable alone.
3. Plug a **micro-USB cable** from the SPD::ONE Kick into the computer. It must be a cable that
   carries data. A cable that only charges will show nothing.
4. Click the **Device check** tab, then **Scan again**.
5. Click the **Kick pad** tab. The box at the top should say **PAD FOUND**. If it says
   **NO PAD**, try another cable first.
6. Stomp about twenty times, from the lightest touch you can manage up to a full stomp. Watch
   the bars: a light touch should be a short bar, a full stomp a tall one.
7. Try what you would do at a gig: a steady four on the floor, some ghost taps, a fast double.
8. Click **Copy kick pad results** and paste them to Claude. Say how it felt next to the
   SPD::ONE's own sound: does a light tap come out light, and does the kick land with your foot
   or behind it?

If the box says NO PAD but the pad shows on the Device check tab under another name, click
**Learn pad** and hit the pad once.

Worth trying while you are there: turn the SPD::ONE's **SENS** and **THRES** knobs and see
whether the strengths change, and press its **INST VARIATION** button and see whether the pad
still sends the same thing.

## The settings

| Setting | What it does |
| --- | --- |
| Kick pad on | Off, hits are still counted and shown, but no kick is played. |
| Level | How loud the hardest hit is. |
| Touch | How much quieter a soft hit is than a hard one. 1 is even steps. Higher makes soft hits softer still. |
| Click | How much beater click the kick has. A soft hit always has less than a hard one. |
| Pitch | Built-in kick only: the note the drum settles on. 55 Hz sits under the bass guitar. |
| Length | Built-in kick only: how long the drum rings. Shorter is tighter. |
| Sound | The built-in kick, or a recording of your own (below). |
| Plays on | Which pair of returns the kick comes out of. Returns 5 and 6 are where the loops play. |

The settings are remembered between runs (`kick-pad.txt` in the app's data folder).

## The built-in kick

It is made by the app from scratch, not taken from anyone's recording, to Tom's description of
the kick he uses: a real-drum kind of kick, strongest around 55 Hz so it stays under the bass
guitar, very little in the middle, and a click on top so it still tells on speakers with no
bass. A harder hit is louder, has more click, a brighter click, and a slightly deeper thump.

## Using your own kick recording

Click **Open kick sounds folder** (`Documents/GigRig/Kick sounds`) and put a WAV of **one hard
kick** in it. Within two seconds it appears in the **Sound** box. The app plays it quieter for
softer hits and takes some of its click away, the same as the built-in kick. Recordings stay on
your computer. They are never part of the app's code, which may be public.

## What this test leaves out, on purpose

- The kick plays straight onto a pair of returns. It does not go into the looper or through
  the mixer yet, so it cannot be looped. That comes if the test passes.
- Only one pad, one sound.
- Nothing here changes the looper, the effects or the songs. With no pad plugged in and no hit
  button pressed, the app plays exactly what it played before.

## For a future Claude

- Logic: `src/KickPad.*` (no screen). Screen: `src/KickPadPanel.*`. Checks: `src/KickPadSelfTest.cpp`.
- MIDI reaches it from `MainComponent` (the same place the pedal gets it). Hits cross to the
  audio thread through a small queue and start on the first sample of the next block.
- The engine calls `KickPad::process` just before the effects, adding to the chosen returns.
- Design doc: Effects, "Built-in kick pad" (undecided). The plan is this test first, then one
  piezo disc into a spare mixer socket, then a choice between the SPD::ONE inside the box and a
  bare sensor read by the board's controller.
- Not checked from Roland's documents: that the pad sends strength at all, which note it
  sends, and whether the note changes with the sound chosen on the pad. The report answers all three.
