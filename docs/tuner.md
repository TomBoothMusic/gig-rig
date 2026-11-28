# The tuner

Built 27 Nov 2026; the full-screen strobe and "only the guitars go silent" on 28 Nov 2026. A
tuner inside the rig, so the pedal tuner can come off the board. The design doc has it on
switch 8 of the finished board. **Keep this page true when the code changes.**

## What it does, in short

- **Press Tuner and the whole screen becomes the tuner**: dark, with purple lights, like a
  strobe pedal tuner. A big note, and under it four rows of purple bands.
- **The bands are a strobe.** They **stand still when the string is in tune**, slide **right
  when it is sharp** and **left when it is flat**, slower the closer you get. The top row is
  the finest: when even that one has stopped, the string is within a small fraction of a cent.
- It also shows the **cents** as a figure (hundredths of a semitone, to one decimal place) and
  lights **FLAT** or **SHARP**. Within one cent the note turns bright and says **In tune**.
- It listens to **one input socket as it arrives, before any effect**, so drives, the Bass9
  copy and the harmony never confuse it.
- **With the mixer on, only the guitars go silent in the PA while the tuner is on** (Tom, 28
  Nov 2026). The **microphone and the stomp box stay live**, so you can talk to the room while
  you tune. **Loops that are already playing carry on.**
- **The monitors keep the guitar**, so you can still hear it in your in-ears (Tom, 19 Nov
  2026). Untick **Keep the guitars in the monitors while tuning** and the guitar is silent
  everywhere, the way a tuner pedal at the front of a pedalboard does it.
- **A sound crew's direct outs lose the guitar too.** When an outside sound company takes a
  direct out from each channel, the guitar's direct out goes silent while you tune and the
  voice's does not (Tom, 28 Nov 2026). Loops on that direct out carry on.
- **It listens to the guitars only**, so talking into the microphone never takes the tuner off
  the string.
- **With the mixer off it changes no sound at all.** Mute the guitar on the real mixer, as now.
- Off, it does nothing and costs nothing.

## Trying it (15 minutes)

1. Open the app and start the audio as usual. Plug in the electric guitar and a microphone.
2. On the **Mixer** tab tick **Mixer on**. Play and talk so the **Main** meter moves.
3. At the top right of the window, next to **Copy report**, click **Tuner**. The whole window
   goes dark and shows the tuner. At the bottom it says which channels are silent, for example
   **Silent in the PA (still in the monitors): Acoustic, Electric, Bass. Still live: Vocal, Stomp.**
   The **Main** meter on the Mixer tab would now show only your voice.
4. Play the open low E string and let it ring. The big letter shows **E** with a small **2**.
   The purple bands slide left if the string is flat, right if it is sharp.
5. Turn the peg until the bands stop. Watch the top (finest) row for the last little bit.
6. Do the other strings: **A2, D3, G3, B3, E4**.
7. While a string rings, **talk into the microphone**. You should hear your voice in the PA
   and not the guitar, and the tuner should stay on the string.
8. Check it against your pedal strobe tuner: tune a string with the pedal, then look at the
   rig. The bands should be still, or very nearly. Tell Claude what you saw.
9. Try the acoustic and the bass the same way (bass strings: **E1, A1, D2, G2**).
10. Press **Done** (top right). The screen you were on comes back and the guitar is heard
    again, with no click.
11. Click **Copy report** and paste it into the chat.

**Options** (on the tuner screen, and next to the Tuner button):

| Choice | What it does |
| --- | --- |
| **While tuning, silence** | **The guitars** (the usual), **The whole main mix** (the first version's way: everything in the PA goes silent, the monitors stay live), or **Nothing**. Mixer on only. |
| **Guitar channels** | Which mixer channels count as guitars. To start with it goes by the channel's name: a name with *electric*, *acoustic*, *guitar*, *bass*, *uke*, *mandolin* or *banjo* in it is a guitar. Tick or untick a channel to overrule its name. |
| **Keep the guitars in the monitors while tuning** | Ticked to start with: the guitars leave the PA only, and the monitors (your in-ears, a wedge) still carry them. Unticked, the guitars are silent everywhere, and anything looped while tuning has no guitar in it. |
| **Listen to** | **Whichever guitar is being played** (the usual), or one input socket. |
| **Tune to** | What A is tuned to. 440 Hz unless a piano or a recording needs something else. |
| **Fill the screen while tuning** | Ticked to start with. Unticked, the tuner is a small strip with a needle beside the Tuner button, and the rest of the window stays in view. |

The choices are remembered.

## On the phone

The remote screen shows the same full-screen strobe whenever the tuner is on, whichever screen
the phone was on. **Done** switches the tuner off. **Options** goes to the **Tools** screen,
which has the on/off switch, the small needle, and the same choices (the guitar channels are
buttons: tap one to change it). **Fill the screen with the strobe** brings the big tuner back.

A phone hears from the rig about eight times a second over Wi-Fi, so its bands run a moment
behind the rig's own screen and are a little less fine. For the last fraction of a cent, trust
the rig's own screen.

## What it cannot do yet

- **No foot switch.** It is switched on from the screen or the phone for now. Switch 8 on the
  finished board is the plan; whether a spare RC-600 switch should do it meanwhile is a
  question for Tom.
- It reads one note at a time, not a strummed chord.
- With the mixer off nothing goes silent (the rig is not the mixer then).

## What the report says

**Copy report** has a **TUNER** part: on or off, what it listens to, what A is, what goes
silent, whether it fills the screen, any channels chosen by hand, which mixer channels go
silent and which stay live, how many times it has been switched on and how many readings it
took, and the note it is reading if it is on (with **Strobe locked.** when the strobe has it).

## For developers

| File | What it is |
| --- | --- |
| `src/Tuner.h/.cpp` | The tuner and the strobe's measurement. No screen. |
| `src/StrobeTuner.*` | The full-screen strobe, laid over the whole window by `MainComponent` while the tuner is on. Its timer calls `Tuner::tick` 60 times a second. |
| `src/TunerStrip.*` | The button, the small strip and the Options menu in the window's header. Its timer calls `Tuner::tick` too. |
| `src/TunerSelfTest.cpp` | The checks (`GIGRIG_SELFTEST_ONLY=tuner` runs only these). |
| `remote/screens.js` (`GR.strobe`) | The phone's strobe. |

- **Threads.** The audio thread (`Tuner::hear`, called from `AudioEngine::processBlock`) only
  averages the socket down to about 24 kHz and puts it in a queue. Off, it reads one flag and
  returns. The message thread (`Tuner::tick`) empties the queue and works out the note.
- **The note.** The McLeod pitch method over the last 4096 thinned samples (about a sixth of a
  second), at most 30 times a second. It reads from about 27 Hz to 1500 Hz. A reading needs a
  clarity of 0.9 and a level above -50 dB, and the middle of the last three is used.
- **The strobe.** Once the note is known, `Tuner::phaseAgainst` multiplies the last eight cycles
  (at least 1536 samples) by a steady wave of the note's exact frequency, counted from the
  first sample of the stream. The angle left over is how far the string's wave sits ahead of
  the note's; it turns by (string minus note) cycles every second, which is exactly what a
  strobe disc shows. `Tuner::strobeStep` unwraps it (the McLeod figure says roughly how far it
  should have turned, so nothing is lost between ticks) into `Reading::strobe`, and the slope
  of the last 0.4 seconds is the cents shown (`Reading::cents`, `Reading::hz`), so the figure and
  the picture always agree. If the two methods disagree by more than 6 cents the slope starts
  again. A string whose own note is more than 30 dB under its first overtone is followed by
  that overtone. The checks hold the cents to a tenth of a cent and the slip to a twentieth, on
  made-up plucked strings with overtones a little sharp, from a bass's low B to a high E.
- **The picture.** Row *n* (from the bottom) has bands twice as fine as the row below and shows
  `strobe` times 2, 4, 8, so every row slides at the same speed across the screen, as the rings
  of one disc do. A row slipping more than about 7 of its own bands a second fades to an even
  glow (a real strobe blurs too, and a screen cannot show it).
- **The silence.** `Tuner::silence()`: `guitars`, `mainMix` or `nothing`. For `guitars` the
  engine tells the mixer which channels (`AudioEngine::tellTunerTheGuitars`, called at the
  start of every tick; `Mixer::setTunerChannels`). With the monitors kept
  (`Tuner::monitorsKeepGuitars`, the default) the mixer gives the main mix, the sends to the
  reverbs and delays and the channel's direct out each such channel's **loops without its live sound** (`Mixer::process`,
  `tuneFeed` and `tuneDirect`, 20 ms down and back) and the monitors get the channel as always; a loop recorded
  meanwhile does have the guitar in it. Without it the mixer fades each one's **live sound to
  nothing where it enters the channel** (`Mixer::stripChannels`), so it is silent in the main
  mix, the monitors and anything recorded meanwhile. Either way loops and reverb tails carry on.
  The right side of a stereo pair follows its left. The tuner is also told the guitars'
  sockets (`Tuner::setCandidates`), so "whichever is being played" never picks a microphone.
  For `mainMix`, `Mixer::setTuning` fades output 0 only, after the main mix is recorded and
  before the break music joins. **Mixer off, tuner on must stay sample for sample the same as
  tuner off** (a check).
- **Memory.** `~/.config/GigRig/tuner.txt`: `socket`, `reference`, `silence` (`guitars`, `main`,
  `nothing`), `guitars in monitors`, `full screen`, and `silent channels` / `live channels` (names chosen by hand,
  joined with `|`; written only when there are some). `mute main` is still written and read for
  builds before 28 Nov 2026: a file with only `mute main=1` now means the guitars.
- **The remote screen:** `tuner.on` and `tuner.set` orders, `tuner` in the state and the live
  part (`docs/remote.md`).
- **Pictures without a guitar:** `GIGRIG_TUNER=demo` opens the app on the full-screen tuner with
  a pretend A string 3 cents sharp.
- **A real recording:** `GIGRIG_TUNER_TAKE=<a WAV>` in front of the self-test plays it into the
  tuner and writes what it read, twenty times a second, to `tuner-take.txt` beside the WAV.
  Never commit a recording.
