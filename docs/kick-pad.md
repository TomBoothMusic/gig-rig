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
| Length | Built-in kick only: how long the drum rings, from 0.03 to 0.20 seconds. Shorter is tighter. |
| Sound | The built-in kick, or a recording of your own (below). |
| Plays on | Where the kick goes. **Socket 10** (how it starts): the kick joins that socket as a stomp box plugged in there would, so the mixer's Stomp channel plays it, with its EQ and compressor, and the looper can record it. A pair of returns: straight out, never looped. |

The settings are remembered between runs (`kick-pad.txt` in the app's data folder).

A fresh install starts on the sound Tom locked in on 27 Nov 2026 (build 103, on his studio
monitors): Level 0.80, Touch 1.10, Click 0.86, Pitch 49 Hz, Length 0.155 s. He rated it about
95% as good as his favourite SPD::ONE sound, and expects to adjust it on a big PA.

## The built-in kick

It is made by the app from scratch, not taken from anyone's recording, to Tom's description of
the kick he uses: a real-drum kind of kick, strongest around 55 Hz so it stays under the bass
guitar, very little in the middle, and a click on top so it still tells on speakers with no
bass. A harder hit is louder, has more click, a brighter click, and a slightly deeper thump.

It was reworked on the night of 26 Nov 2026, after Tom played the first one from the SPD::ONE
and found the click "like a wooden block" and the body "like an 808":

- **The beater** is now a soft slap, a few thousandths of a second of hiss with no note of its
  own. The first one rang at one pitch, which is what a wooden block does.
- **The drum** now makes more than one note, as a real drum head does: two higher notes sound
  with the hit and die away quickly, and the main note starts a little sharp and settles as
  the stretched head relaxes. The first moment of the hit is louder than the ring after it.
- **Length** runs from 0.03 to 0.20 seconds (it was 0.05 to 0.40, and only the bottom third
  was any use).

It is still built from how drums are known to behave, not copied from a recording. If it is
not yet right, the best next step is a recording of the kick sound Tom likes (soft, medium and
hard hits), so the built-in kick can be matched to it by measurement.

## One stomp counted twice

A stomp sometimes reaches the app as two hits a few thousandths of a second apart (9 times in
Tom's first 370). When two hits from the pad arrive less than 30 ms apart, the app plays only
the stronger one. Both are still counted, and the report says how many were not played.

## Using your own kick recording

Click **Open kick sounds folder** (`Documents/GigRig/Kick sounds`) and put a WAV of **one hard
kick** in it. Within two seconds it appears in the **Sound** box. The app plays it quieter for
softer hits and takes some of its click away, the same as the built-in kick. Recordings stay on
your computer. They are never part of the app's code, which may be public.

## The piezo pad test

A second test of the same idea (27 Nov 2026): a home-built pad in place of the SPD::ONE. It is
a plate with a rubber top and a **piezo disc** glued underneath. A knock makes the disc give out
a short spike of electricity, taller for a harder knock. For this test the disc is plugged into
a spare socket on the mixer with a guitar lead, and the app turns each spike into a hit with a
strength from 1 to 127. In the finished board the disc would be wired to the controller inside
the board, so it would not take up a socket.

**Building the test pad**

1. Glue the piezo disc flat to the middle of the underside of the plate (about 10 by 8 cm).
2. Stick the rubber on top of the plate.
3. Solder the disc's two wires to a 6.5 mm mono socket: one wire to the tip, the other to the
   sleeve. Either way round works.
4. Sit the plate on four small blocks of firm foam, on the floor.

**Running the test**

1. Plug a guitar lead from the pad into a **spare socket** on the mixer: one that nothing else
   uses and that has no mixer channel on it in the app. Turn that socket's gain right down on
   the mixer and its 48 V off.
2. In the **Kick pad** tab, set **Piezo pad** to that socket.
3. Stomp on the pad. The kick should play, and each hit shows in **The last hits** with
   `(piezo ...)` beside it, which is how tall its spike was.
4. If nothing plays, bring the socket's gain up on the mixer a little at a time until soft
   taps start to count.
5. Stomp from the softest tap to the hardest stomp, a fast double, and a steady run.
6. Tap the floor right beside the pad, as hard as a footswitch press. It should **not** play.
7. Click **Copy kick pad results** and paste them into the thread, with how it feels beside
   the SPD::ONE. Click **Start again** between pads so the two sets of results stay apart.

| Setting | What it does |
| --- | --- |
| Piezo pad | The socket the piezo is plugged into, or Off. |
| Counts from | How tall a spike must be to count as a hit. Raise it if a knock beside the pad plays the kick. Lower it if soft taps are missed. |
| Hardest at | A spike this tall is the hardest hit. Set it near the tallest spike your hardest stomp makes. The results list it. |

The app watches each spike for 2 ms to find its top before the kick starts, then looks away
for 40 ms while the plate rings, so one stomp is one hit. The results say how tall the
smallest and tallest counted spikes were, and how tall the tallest ignored one was. Those
three numbers are what the two sliders should be set from.

## The board's own pad (the piezo on the controller)

Built 28 Nov 2026, before the parts arrived. This is how the finished board has it: the same
piezo disc, wired to the board's controller (the Teensy) and not to a mixer socket. The
controller measures each spike and sends the app a hit with a strength over its USB lead,
exactly as the SPD::ONE Kick does. So a hit from the board's pad takes the same road through
the app as the SPD::ONE's: the same kick, the same mixer channel, into loops, and noted in a
recording so a virtual soundcheck makes it again.

**It needs no setting up in the app.** The controller names itself **Gig Rig Controller**, and
the app knows it by that name, as it knows the SPD::ONE by its name. There is no **Learn pad**
step. The SPD::ONE and the board's pad can be plugged in together and both play.

**Running the test** (wiring: `docs/bench-test.md` and the bench test page, Test D)

1. Load the newest `GigRig-Controller-Test.hex` onto the Teensy (bench test page, "Get the
   Teensy ready"). An older one still plays, but cannot be set from the app.
2. Plug the Teensy into the computer. On the **Device check** tab click **Scan again**.
3. On the **Kick pad** tab, the row that starts **Board's pad** should now say
   **Controller connected** at its right-hand end. Leave **Piezo pad** on **Off**.
4. Stomp on the pad. The kick plays, and each hit shows in **The last hits** with
   `(board ...)` beside it: how tall its spike was, out of 4095.
5. Stomp from the softest tap to the hardest stomp, a fast double, and a steady run.
6. Tap the bench right beside the pad, as hard as a footswitch press. It should **not** play.
7. Click **Copy kick pad results** and paste them into the thread.

| Setting | What it does |
| --- | --- |
| Board's pad (tick box) | Off: the controller's hits are counted and shown, but no kick plays. |
| Counts from | How tall a spike must be to count as a hit, out of 4095. Raise it if a knock beside the pad plays the kick. Lower it if soft taps are missed. |
| Hardest at | A spike this tall is the hardest hit. Set it near the tallest spike your hardest stomp makes. The results list it. |

The two levels are sent to the controller the moment a slider moves, and the controller keeps
them in its own memory, so they are still there after the power has been off and on any
computer it is plugged into. The results say what the app asked for and what the controller
says it holds; a line in capitals appears if the two differ.

The controller watches each spike for 2 ms to find its top, sends the hit, then ignores the
pad for 40 ms while the plate rings, the same as the app does for a piezo on a socket. It also
tells the app about knocks that were too small to count (never more than four a second), which
is where **Tallest knock that did not count** in the results comes from.

**Not proven yet:** all of this has only run inside the app's self-test, against a pretend pin.
The first stomp on a real pad is the proof.

## What this test leaves out, on purpose

- (Changed 27 Nov 2026, once Tom was happy with the sound.) The kick now joins a socket, so it
  goes through the mixer and into loops. See "Through the mixer and into loops" below.
- Only one pad, one sound.
- Nothing here changes the looper, the effects or the songs. With no pad plugged in and no hit
  button pressed, the app plays exactly what it played before.

## Through the mixer and into loops

Tom, 27 Nov 2026: the sound is good, fold it in.

1. On the **Kick pad** tab, **Plays on** is **Socket 10: through its mixer channel, and into
   loops**. That is how it starts.
2. On the **Mixer** tab, with **Mixer on**, the **Stomp** channel (socket 10) now carries the
   kick. Its EQ and compressor shape it, its fader sets its level.
3. On the **Looper** tab the **Stomp** input socket follows the kick pad by itself (socket 10)
   while the pad is on, so loops record the kick with nothing to set. Only a Stomp socket chosen
   by hand, different from the acoustic's, is left alone; the Looper's report then says so in a
   line starting **NOTE**.
4. Every track in Tom's songs that hears the acoustic also hears the stomp (his rule). With the
   two on different sockets a loop keeps them apart, a lane each, and each comes back on its own
   mixer channel.

With the mixer off, the kick plays on returns 5 and 6 as before, and the looper still hears it
on its socket.

## For a future Claude

- The engine makes the kick's sound before the effects and adds it to its socket's feed after
  them (`AudioEngine::processBlock`, `kickSock`), ahead of `Mixer::stripChannels` and the looper.
  A hit reaches the looper a few milliseconds earlier than the same stomp would as audio through
  the interface (no input delay); at a 64-sample buffer that is under 5 ms and is left alone.

- The board's pad: what the controller and the app say to each other is one file,
  `firmware/shared/PadMessages.h`, built into both programs. It also holds the controller's own
  thinking (`PadWatcher`, `PadLevels`), so the app's self-test runs the very lines the Teensy
  runs. Change the pad's behaviour there, not in `firmware/bench-test/src/main.cpp`. A
  controller with an older program is still out there on Tom's bench: add new controller
  numbers, never give an old one a new meaning. The app opens a MIDI output only for the
  controller (`KickPadPanel::midiOpened`, after each Scan again), and only to send the two levels.
  `kick-pad.txt` gained `boardPad`, `boardFrom`, `boardFull` (on Tom's disk: never rename).
- Logic: `src/KickPad.*` (no screen). Screen: `src/KickPadPanel.*`. Checks: `src/KickPadSelfTest.cpp`.
- MIDI reaches it from `MainComponent` (the same place the pedal gets it). Hits cross to the
  audio thread through a small queue and start on the first sample of the next block.
- Design doc: Effects, "Built-in kick pad" (undecided). The plan is this test first, then one
  piezo disc into a spare mixer socket, then a choice between the SPD::ONE inside the box and a
  bare sensor read by the board's controller.
- Not checked from Roland's documents: that the pad sends strength at all, which note it
  sends, and whether the note changes with the sound chosen on the pad. The report answers all three.
