# The tuner

Built 27 Nov 2026. A tuner inside the rig, so the pedal tuner can come off the board. The
design doc has it on switch 8 of the finished board: "TUNER: mutes main, in-ears live".
**Keep this page true when the code changes.**

## What it does, in short

- It listens to **one input socket as it arrives, before any effect**, so drives, the Bass9
  copy and the harmony never confuse it.
- It shows the **note**, a **needle**, and how far out the note is in **cents** (hundredths of
  a semitone). Green means within 3 cents.
- By itself it listens to **whichever input is being played**. It can be told to listen to one
  input only.
- **With the mixer on, the main mix goes silent while the tuner is on.** The monitors (your
  in-ears) stay live, and so does the break music. Loops keep running; they are silent in the
  main mix with everything else and come back when the tuner goes off.
- **With the mixer off it changes no sound at all.** Mute on the real mixer, as now.
- Off, it does nothing and costs nothing.

## Trying it (10 minutes)

1. Open the app and start the audio as usual. Plug in the electric guitar.
2. At the top right of the window, next to **Copy report**, click **Tuner**. The button turns
   green and a strip appears beside it: two dashes, a bar with a line down the middle, and
   **Play one string**.
3. Play the open low E string and let it ring. The strip shows **E2**, the needle, and words
   such as **6 flat**. Left of the middle is flat, right is sharp.
4. Tune the string. The note turns green when it is within 3 cents.
5. Do the other strings: **A2, D3, G3, B3, E4**.
6. Check it against the tuner you trust: tune one string with that tuner, then look at what
   the rig says. They should agree within a cent or two. Tell Claude what you saw.
7. Try the acoustic and the bass the same way (bass strings: **E1, A1, D2, G2**).
8. Click **Tuner** again to switch it off.

**Options** (next to the Tuner button):

| Choice | What it does |
| --- | --- |
| **Listen to** | **Whichever input is being played** (the usual), or one input socket. Choose one socket if a microphone near the amp keeps taking over. |
| **Tune to** | What A is tuned to. 440 Hz unless a piano or a recording needs something else. |
| **Silence the main mix while tuning (mixer on)** | Ticked to start with. Untick it to tune with the sound still going to the PA. |

The three choices are remembered.

### Trying the silence (5 minutes, mixer on)

1. On the **Mixer** tab, tick **Mixer on**, and play so that the **Main** meter moves.
2. Click **Tuner**. The Main meter drops to nothing within a blink, with no click. The monitor
   meters keep moving.
3. Click **Tuner** again. The main mix comes back exactly as loud as it was.

## On the phone

The remote screen's **Tools** screen has the same tuner with a big note and needle, the on/off
switch and the three choices. A tuner switched on from the phone shows on the rig's screen too.
The needle on a phone runs a moment behind, because it travels over Wi-Fi.

## What it cannot do yet

- **No foot switch.** It is switched on from the screen or the phone for now. Switch 8 on the
  finished board is the plan; whether a spare RC-600 switch should do it meanwhile is a
  question for Tom.
- It reads one note at a time, not a strummed chord.

## What the report says

**Copy report** has a **TUNER** part: on or off, what it listens to, what A is, whether it
silences the main mix, how many times it has been switched on and how many readings it took,
and the note it is reading if it is on.

## For developers

| File | What it is |
| --- | --- |
| `src/Tuner.h/.cpp` | The tuner. No screen. |
| `src/TunerStrip.*` | The button and strip in the window's header. Its timer calls `Tuner::tick`. |
| `src/TunerSelfTest.cpp` | The checks (`GIGRIG_SELFTEST_ONLY=tuner` runs only these). |

- **Threads.** The audio thread (`Tuner::hear`, called from `AudioEngine::processBlock`) only
  averages the socket down to about 24 kHz and puts it in a queue. Off, it reads one flag and
  returns. The message thread (`Tuner::tick`, about 20 times a second) empties the queue and
  works out the note.
- **The note.** The McLeod pitch method over the last 4096 thinned samples (about a sixth of a
  second), with the peak placed between samples. It reads from about 27 Hz to 1500 Hz. A
  reading needs a clarity of 0.9 and a level above -50 dB, and the figure shown is the middle of
  the last three, so one odd reading never shows. The checks hold it to a cent from a
  five-string bass's low B to a high C, with overtones louder than the note itself.
- **The silence.** `Mixer::setTuning`, a 20 ms fade on output 0 only, after the main mix is
  recorded and before the break music joins. **Mixer off, tuner on must stay sample for sample
  the same as tuner off** (a check).
- **Memory.** `~/.config/GigRig/tuner.txt`: the input, the A and the silence choice.
- **The remote screen:** `tuner.on` and `tuner.set` orders, `tuner` in the state and the live
  part (`docs/remote.md`).
