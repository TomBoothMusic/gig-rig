# The effects: how they are built, and how to add one

Written 23 November 2026 with the first batch (drives, the Bass9 copy, the amp capture);
the compressor and graphic EQ came on 24 November 2026, the time effects that night, and the
acoustic, vocal and bass batch (reverbs, flanger, Uni-Vibe, channel EQ, acoustic body, response
files, the vocal harmony, the test take) on 25 November 2026.
For a future Claude session: read this, then `CLAUDE.md` "Adding an effect", then the code.

## Where things are

| What | File |
|---|---|
| The interface every effect follows | `src/effects/Effect.h` |
| The list of effects (the registry) | `src/effects/Registry.cpp` |
| A row of effects, with click-free changes | `src/effects/Chain.h` |
| Checks every effect must pass (self-test) | `src/effects/EffectChecks.cpp` |
| Oversampling for clipping stages | `src/effects/Oversampler.h` |
| Clipping curves shared by the drives | `src/effects/Shapers.h` |
| Delay lines, an LFO, note values | `src/effects/DelayLine.h` |
| Running part of an effect at half the rate (48 kHz inside 96 kHz) | `src/effects/HalfRate.h` |
| Response files: WAV in and out, resampling, making one from a capture | `src/effects/ir/IrTools.*` |
| One folder per effect | `src/effects/<id>/` |
| The engine side: chains on sockets, returns, A/B, level match, Capture, amp capture, the test take | `src/EffectsRack.*` |
| The Effects and Amp capture tabs | `src/EffectsPanel.*` |
| The responses folder, the response maker's wording, the starter presets | `src/Responses.*` |
| Starter presets built into the app | `resources/presets/` |
| Checks of the rack inside the engine | `src/EffectsSelfTest.cpp` |

The effect code in `src/effects/` is plain C++ (no JUCE), so it can be built and tested on
its own in seconds:

```
cd src/effects
g++ -O2 -std=c++20 -I. test-main.cpp Effect.cpp Chain.cpp Registry.cpp EffectChecks.cpp */*.cpp
```

with a `test-main.cpp` that calls `gigrig::fx::runEffectChecks` and prints each line (see the
bottom of this file).

## How sound flows

```
input socket --> chain (effect 1 -> effect 2 -> ...) --> chain level --> Listen (effects / dry / reference)
                                                                              |
                                                       +----------------------+-------------------+
                                                       v                                          v
                                          return (or pair) to the mixer          the looper, in place of a socket
```

- Up to 4 chains (`EffectsRack::numChains`), each on its own socket.
- The looper hears the chain's output in place of its own socket (or another socket, or
  not at all), so effects are baked into loops, as the design doc decides.
- The Effects tab starts switched off; then the looper hears the raw inputs exactly as before.
- On the CQ12T only sockets 1 to 10 can feed a chain: USB channels 11 to 16 carry the mixer's
  own outputs and would howl. A feedback guard also switches a chain off if its output sits
  at full level for a quarter of a second.

## The rules (enforced by the self-test where possible)

1. **One effect, one folder**: `src/effects/<id>/` with the DSP and, if useful, its own checks.
   It must not reach into other effects or the engine.
2. **The interface** (`Effect.h`): add settings in the constructor with `addParam`, `addToggle`
   or `addChoice`; build filters in `onPrepare`; clear memory in `onReset`; process in
   `processChunk` (at most 16 samples at a time, in place, 1 or 2 channels). Read settings
   with `value(i)`: it glides, so knobs never click. Recalculate coefficients when
   `anyChanged()` is true.
3. **Audio thread rules**: in `processChunk` and `onReset`, no memory allocation (no
   `std::vector` growth, no `std::string` building), no locks, no files, no logging.
4. **Ids never change**: the effect id and every setting id are saved in presets and, later,
   songs. To change one, add a new setting and leave the old one alone.
5. **Knobs like the pedal**: 0 to 10 for pedal knobs, dB for levels. Defaults roughly as loud
   as the dry signal (checked to within 9 dB), so A/B starts fair.
6. **Latency**: if the effect delays the sound (oversampling, look-ahead), say so in
   `latencySamples()`; the self-test checks it against a pick-like burst.
7. **CPU**: `cpuBudgetPercent()` is the most of one 64-sample block at 96 kHz the effect may
   use on the build machine (5% unless it says otherwise; the Bass9 copy has 20%).

## Adding an effect, step by step

1. Make `src/effects/<id>/<Name>.h` and `.cpp`. Copy `boost/CleanBoost.*` (simplest) or
   `tubescreamer/TubeScreamer.*` (with oversampling) as the starting point.
2. In `src/effects/Registry.cpp`: include the header and add one line to the list:
   `{ "<id>", "<Name on screen>", "<Group>", "<one plain sentence>", [] { return std::make_unique<Name>(); }, <checks or {}> },`
3. In `CMakeLists.txt`: add `src/effects/<id>/<Name>.cpp` under the effects list.
4. Build and run the self-test (see the README, "For developers"). Every standard check
   runs on the new effect automatically: no broken numbers on silence, noise, a sweep and
   every setting jumping between its ends; no sound from silence; sound from sound; no
   clicks when settings jump or it is switched on and off; latency told truthfully; CPU
   inside its budget.
5. If the effect has a character worth guarding (the Tube Screamer's mid hump, the Bass9's
   octave), add checks for it in a `<id>/<Name>Checks.cpp` and point the registry line at them.
   **Anything that follows pitch or splits the sound into bands must also be checked on a
   played instrument, not only on test tones** (see "The bass, reworked" below for why, and
   for a made-up guitar to borrow).
6. Open a PR. CI must be green. Tom tries it on the laptop or on the rig with
   `linux/install.sh --version <build>` between gigs; `linux/rollback.sh` goes back.
7. Keep the design doc's effects list in step.

## Meters, and moving filters without clicks

- An effect with a meter (the gate's and compressor's gain reduction) returns true from
  `hasMeter()` and calls `setMeter()` from the audio thread; the Effects tab shows it as
  "Turning down by:". Gain reduction is negative dB.
- Changing a resonant filter's coefficients quickly can click (the graphic EQ's first version
  failed the "jumping a slider" check at 200 Hz). The EQ (`src/effects/eq/`) avoids it by
  keeping each band a fixed band-pass filter and only changing how much of it is added or
  taken away, as the GE-7's own circuit does. Use the same idea for any EQ-like effect.
- The compressor (`src/effects/comp/`) has no look-ahead, so its latency is 0. Ids: `comp`
  (`input`, `output`, `attack`, `release`, `ratio`, `mix`) and `eq` (`b100` ... `b6k4`, `level`).


## Time effects and the song's tempo

- `src/effects/DelayLine.h`: the delay line (cubic interpolation, `read(0)` is the sample just
  written), a sine LFO, and the note values a synced delay offers (`sync::names`, `sync::seconds`).
- An effect reads the song's tempo with `tempo()` (0 = unknown). The engine sets it every block:
  the looper's tempo once a loop has set it, else the last tap tempo (`EffectsRack::tap`, also a
  pedal action), else 0. A synced delay with no tempo uses its own Time knob.
- Delay time changes: the digital delay crossfades between the old and new tap in 40 ms (no
  pitch bend); the analog and tape delays glide the read point at most 25% off real time (a
  musical pitch bend, and it keeps the "no clicks" check happy). Their `time` setting has
  `smoothingMs` 0 because they handle it themselves.
- Ids: `analogdelay`, `digitaldelay`, `tapedelay`, `spring`, `tremolo`, `vibrato`, `chorus`.
  Vibrato reports its shortest delay (0.5 ms) and only ever wobbles later than that.
- Switching a slot off fades it out in 10 ms, so its delay or reverb tail stops.
- When the whole chain changes (a song or preset loads), tails ring out instead (25 Nov 2026,
  `docs/songs.md`, "Tails ring out"). **A new delay or reverb must add
  `bool hasTail() const noexcept override { return true; }`** and be added to the list in
  `src/SongSelfTest.cpp` that checks which effects say so.

## The whammy (pitch bending)

- `src/effects/whammy/`, id `whammy`: settings `mode` (a choice; **only ever add modes to the
  end**, presets save the number), `pedal` (0 heel to 100 toe), `glide` (ms for a full sweep;
  the effect moves the pedal itself, per sample, so both have `smoothingMs` 0) and `mix`.
- Method: the guitar is written to a ring buffer and read back at the pitch ratio. A read
  point that runs out hops by 11 to 28 ms; `findHop` picks the hop that best lines the waves
  up (a quick search on a copy thinned to about 8 kHz, then to the sample on the real buffer)
  and the two read points are faded across. That is why single notes are clean and chords warble.
- At true pitch (heel in a Whammy mode) it fades to a read with no delay, so its latency is 0
  and the output is the input to the sample. While bending the delay is 2 to about 45 ms.
- The first expression-pedal effect. Until there is a real pedal, `EffectsRack::bendWhammy`
  (pedal action 3, "whammy bend while held") moves `pedal` to 100 on a press and 0 on release.
  A real expression pedal should set `pedal` the same way; the wah copies this pattern.

## The wah

- `src/effects/wah/`, id `wah`, group "Filter": settings `pedal` (0 heel to 100 toe), `heel`
  and `toe` (Hz: where the sweep starts and ends), `q`, `depth` (dB the peak stands above the
  body), `level` and `glide` ("Sweep time", ms for a full sweep). Like the whammy, the effect
  moves the pedal itself per sample, so `pedal` and `glide` have `smoothingMs` 0.
- Method: two state-variable filters at the same frequency, which is
  `heel * (toe / heel) ^ pedal` (equal musical steps). One is a band-pass fed with the guitar
  times 1/Q, so its top is always 1 and Q only changes its width; the other is a gentle
  low-pass for the body under the peak. Output = peak gain x band-pass + body gain x low-pass,
  with peak above body = `depth`. Feed the 1/Q in, not out: scaling the output makes a swell
  when Q drops quickly (the no-click check caught it).
- Voiced from the usual Cry Baby figures (about 400 Hz to 2.2 kHz), not from a measurement of
  Tom's own GCB95F. If he wants it closer, the Amp capture tab can measure his pedal at a few
  treadle positions as response files, and the defaults can be set from those.
- `EffectsRack::sweepWah` (pedal action 6, "wah sweep while held") moves `pedal` to 100 on a
  press and 0 on release, sharing `rockPedal` with the whammy. A real expression pedal should
  set `pedal` directly; the toe switch is the slot's on/off.

## The volume pedal (26 November 2026)

- `src/effects/volume/`, id `volume`, group "Utility": settings `pedal` (0 heel to 100 toe),
  `heel` (the heel's level in dB; -80 is silence) and `reverse` (heel loud, toe silent).
- It is an effect so that it can sit anywhere in a chain. Between the drives and the amp it
  cleans the amp up as it turns down, and wherever it sits it is before the looper, the mixer
  and the direct outs.
- The travel follows the ear (level = position to the power 2.5), and the level glides per
  sample, so a swell never zips.
- `curve` (**Travel**) chooses how the level follows the pedal: `Volume pedal` (the ear's
  curve above) or `Blend (even loudness)`, a quarter of a sine. With the blend travel on both
  pedals of a blend, one rises as the other falls and their loudness together stays the same
  all the way (each is 3 dB down in the middle). Checked in `VolumePedal.cpp`.
- **The blend.** Tom's one pedal fades between the acoustic and the electric: the electric
  chain has a volume pedal as it is, the acoustic chain has one with `reverse` on.
  `EffectsRack::moveExpressionPedal` is where the pedal arrives (the Effects tab calls it
  when either Pedal slider moves; a real expression pedal should call the same thing). With
  the toe switch off it moves the `pedal` of every volume pedal in every chain.
- **A switch as the pedal** (pedal action 90, `EffectsRack::actionPedalToe`, the words `pedal toe` in a
  song file): while the switch is held the expression pedal is at the toe, and at the heel when let
  go (`moveExpressionPedal`). For testing until there is a real pedal.
- **The toe switch** (`EffectsRack::toeSwitch`, pedal action 9, the words `toe switch` in a
  song file; Tom's stand-in is CTL4). On: every volume pedal goes to toe down (electric full,
  the reversed acoustic silent), the first `wah` or `whammy` in the switch's chain is switched
  on, and the expression pedal now moves that effect's `pedal` instead of the blend. Off: the
  effect is switched off and the blend goes back to where the pedal was. A chain with no wah
  or whammy still gets the electric-full, acoustic-muted part. A new expression effect joins
  by being added to `expressionSlot` in `EffectsRack.cpp`.

## The studio-style blocks (26 November 2026)

Five ordinary effects (group "Studio") built for the mixer's channels and outputs, and offered
in the Effects tab too. None adds delay.

| Id | Folder | What | Settings |
|---|---|---|---|
| `la2a` | `src/effects/la2a/` | LA-2A style optical leveller: slow, programme-dependent release | `reduction`, `gain`, `mode` (Compress, Limit) |
| `warmth` | `src/effects/warmth/` | Valve-desk colour with the loudness held steady as Drive rises | `drive`, `colour`, `level`, `mix` |
| `enhancer` | `src/effects/enhancer/` | Source enhancer in the style of the Portico 5045: turns a microphone down by a set depth between phrases, never shut | `threshold`, `depth`, `time` (A to F), `mode` (RMS, Peak) |
| `tape` | `src/effects/tape/` | Tape machine: soft squeeze on peaks, head bump, softer top | `drive`, `speed` (7.5, 15, 30), `level` |
| `buscomp` | `src/effects/buscomp/` | Desk-style bus compressor | `threshold`, `ratio`, `attack`, `release`, `makeup`, `listencut`, `mix` |

Each has its own character checks beside the standard ones. In the mixer, `enhancer`, `warmth` and `la2a`
sit on every channel and `buscomp` and `tape` on every output (`docs/mixer.md`).

## The mixer's own blocks

`src/effects/mixer/` holds the EQ, compressor, notches, limiter, speaker delay and wet filter
the mixer uses (see `docs/mixer.md`). They follow `Effect.h` and pass the standard checks,
but are registered in `mixerEffectTypes()`, not `effectTypes()`, so the Effects tab does not
offer them and pedal-style effects stay out of the mixer.

## The acoustic, vocal and bass batch (25 November 2026)

Ids: `plate`, `room`, `flanger`, `univibe`, `peq`, `body`, `ir`, `harmony`. Each folder's header
says how its effect works; the points a future session needs:

- **Where they belong.** Tom decided (25 Nov 2026) that vocal delays and reverbs live in the
  mixer stage after the looper, and the harmony before the looper. `plate` and `room` (and the
  delays) are ordinary effects here so he can hear them now; the mixer stage reuses the same
  classes on its sends. Keep them free of anything chain-specific.
- **`plate`** is Dattorro's plate (lengths scaled from 29761 Hz), `room` an 8-line feedback
  delay network with a Householder mix. Both add the wet to the untouched dry, so Reverb at 0
  is exactly the input. Their checks measure the ring time (5 to 35 dB down, times two).
- **`peq`** uses state-variable filters (Simper's), which stay stable while their settings
  glide. Low cut at 20 Hz and High cut at 20 kHz move right out of the audible range ("off").
- **`body`** is Claude's own voicing of a BodyRez-style effect: fixed band-passes blended in
  and out (GE-7 style, so the knob never clicks) and a squeeze of the 2 kHz region on hard
  playing. It is not a measured copy. The measured copy is a response file:
- **`ir`** plays a response file ("impulse response"): up to 4096 points at 48 kHz (85 ms),
  a plain sum of products run through `HalfRate` at 96 kHz. Text setting `file` = a name in
  `Documents/GigRig/Responses` (set with `ResponsePlayer::setFolder`, done in
  `responses::setUp`) or a full path. Files at other rates are converted on load with
  `ir::resampleResponse` (a response's points scale with the rate; plain audio does not).
- **Making a response file.** The Amp capture tab's tick box "This is a tone-only pedal" (or
  the button beside it, or `GigRig --make-response <capture.wav> <response.wav>`) runs
  `ir::makeResponse` on a capture: it finds the delay by cross-correlation, averages the
  response over the whole recording (Welch's method, H1), fades out where the test sound had
  nothing, and writes a level-neutral 48 kHz file plus a .txt saying what it found. "Match" is
  how much of what came back a file can explain; a pedal that compresses scores lower.
- **`harmony`** (first version): a pitch tracker (McLeod's normalised difference on a
  decimated, low-passed copy) and PSOLA grains two periods long, so formants stay put. `Key`
  is a major key (shown with its relative minor), `Voice` a number of scale steps, and
  `Harmony::interval()` turns the sung note into semitones. The lead passes untouched with no
  delay; the harmony sings about 24 ms behind (two periods of 85 Hz). It is checked on a
  synthetic vowel only: **Tom's voice is the real test**, and his findings should drive the
  next version (tracking on real consonants and breath, guitar spill, grain artefacts).
  Planned control (design doc): hold the harmony switch and turn the song encoder to change
  key, the key shown on the switch's screen.
  A **second voice** (26 Nov 2026): settings `voice2` (a list that starts with Off, then the
  same choices as `voice`) and `level2`. Both voices share the tracker, the key and the pulse
  marks; each has its own grains (`Harmony::Part`). Never put two harmony effects in a row for
  this: the second would hear the lead and the first harmony mixed.
  **Only while a switch is held** (26 Nov 2026, Tom: "it will always be done by holding a
  switch"): choice setting `hold`: 0 Off (always sings), 1 Ready (silent, still tracking),
  2 Singing. `EffectsRack::holdHarmony` (pedal action 8, "harmony while held", `harmony` in a
  song file) sets it to 2 on the press and 1 on the release, and switches the effect on first if
  the song left it off. The harmony opens in 10 ms and closes in 60. Songs are saved with the
  key set and the harmony switched off; the first press wakes it.
- **`HalfRate`** has a few samples of delay at 96 kHz (measured at prepare and reported). Its
  half-band filters are not linear-phase, so "the dry sound, late" is only true of level, not
  sample for sample.

## The bass, reworked (25 November 2026)

Tom's first real test of the `bass9` effect (build 70, raw electric straight into the mixer):
he liked the tone but heard "clicky artifacts the whole time" and "static noise", less at a
slow Tracking speed. Every check passed, because every check used plain tones.

**Cause.** The first version split the guitar into 33 overlapping filters, halved each one's
phase, and kept neighbouring filters the same way up by flipping one in a single sample
whenever the two agreed for a moment. On a real guitar (stiff strings, harmonics beating,
hiss) that agreement comes and goes, so filters flipped at full level many times a second:
about 8 ticks a second on Tom's take, 100 a second on the made-up guitar with his compressor
and drive. Filters that heard two harmonics at once also came out as noise.

**What `BassMachine` does now** (the header has the full story):

- The bank runs at about 12 kHz (every 8th sample at 96 kHz; four running averages on the
  way in, a straight line and one more average on the way out), about 170 bands from 70 Hz
  to 4.4 kHz. It costs about 6% of a block, down from 11%.
- Each band works out where its tone sits (the smoothed turn of its arrow against its own
  centre) and how steady that is. A tone is spoken for only by the one or two bands nearest
  to it, with the filter's loss and lag at that pitch taken out, so the level is the same at
  every pitch. An unsteady band (two tones, or hiss) is turned down.
- A band that has to turn over is always the quieter of the pair and fades through zero in
  2 ms. Two octaves down turns a band by quarter turns the same way.
- Higher up the bands stop getting wider at 13 Hz either side, at every Tracking speed, so
  the harmonics of a low E, 82 Hz apart, stay in bands of their own.

Tracking speed trades quick low notes against clean chords, for notes below about 200 Hz: at
0 a low E takes about 50 ms to reach half level, at 0.6 (the default) about 22 ms.

**Checks** (`Bass9Checks.cpp`): a made-up guitar (`played`, `bassLine`: stiff strings, each
harmonic dying at its own rate and beating against a twin, pick click, hiss, hum) through the
bass at three Tracking speeds must leave next to nothing above 6 kHz (a clean octave down of
a guitar below 4.4 kHz has nothing there, so anything there is ticks), a single note must come
out as the note an octave down and little else, and a bend, a vibrato and a slide must not
tick. Borrow `played` for any other effect that follows pitch.

### Second pass the same day: warble, Tone, Drive and string noise

Tom on build 77: no ticks, "so much better", but "a bit of a weird warble" that eased at a
low Tracking speed, Tone and Drive "seem to not really do anything", and it still sounded
like "a normal octave pedal", without the string noise the real Bass9's tone knob brings in.

- **Warble.** The steadiness test compared each band's turn sample by sample with its
  smoothed turn. A strong harmonic a long way off upsets that far more than it upsets the
  band's level (the upset falls only in step with the distance, not its square), so the low
  harmonics of a note made the bands holding its upper harmonics look unsteady. From the 5th
  harmonic up they came out 6 to 20 dB down and wobbling by 3 to 9 dB. Now a lightly smoothed
  turn (`turnQuick`, a third of the band's ring time) is compared with the slow one, and the
  upper bands stay 13 Hz wide at every Tracking speed (they used to widen to 30 Hz). The same
  harmonics now come out within 3 dB and steady to under 1 dB. Check: "the harmonics of a
  dying low E come out at their own level and steady".
- **Tone.** Nothing above 1.2 kHz came out of the octave, so the tone knob had little to
  work on. The bank now reaches 4.4 kHz (2.2 kHz out), and a new setting `brightDb`
  ("String brightness", a lift above 1 kHz, default 9 dB) puts back the octave of top that
  halving a guitar takes away.
- **Drive and Compression** worked against a fixed level near full scale; Tom's raw electric
  peaks 28 dB below that, so the drive never bit and the compressor was only a 9 dB boost.
  Both now work against the playing's own recent peaks (a follower with a 2.5 s release), so
  they do the same at any input level. Compression no longer changes loudness, so **a preset
  saved on build 77 or earlier comes out about 8 dB quieter**: turn Level up. Checks: "Drive
  adds the same grit to a quiet guitar as to a hot one", "Compression evens out ...".
- **Pluck.** The octave takes 20 to 50 ms to ring up, so every note swelled in. A new setting
  `pluck` ("Pluck (string noise)", default 0.5) lets the guitar's own attack above 700 Hz
  through for the first few ms of each note, through the same pickup and tone model, and
  shuts as soon as the top of the guitar's sound settles. Check: "the pluck puts a click on
  the front of each note and nothing of the guitar after it".

New setting ids are added at the END of `BassSettings::ranges()` and of `setIndex`'s list:
the Effects tab hands values across by position. All of this was voiced by measurement on
Tom's take, not by ear and not against the real Bass9: a **Capture** of the real pedal (dry
on the chain's input socket, EFFECT OUT on the Reference socket, Tone fully down, at his
usual setting and fully up) is what would let the tone and string noise be matched properly.

**Still true:** two notes a fourth or closer apart on the low strings cannot be told apart
at a quick Tracking speed, and come out rough (the real Bass9 is a bass-line pedal too).
Nothing above 1.2 kHz comes out of the octave itself; if Tom finds it dull, raise the bank's
top (`highestBandHz`) before reaching for EQ. Tom's take of 25 Nov (raw electric, 96 kHz, in
the project files under `effects/bass-takes/`) is the real-guitar reference: it is far too
big for the repository.

## The delay throw

Tom's use (25 Nov 2026): a tempo-synced delay on a held switch that catches one phrase
("...after love, after love" in Believe) while the rest of the song stays clean.
`digitaldelay` has a choice setting `throw`: 0 Off (always on), 1 Ready (nothing goes in),
2 Catching. `EffectsRack::holdThrow` (pedal action 5, "delay throw while held") sets the
first effect in the chain that has a `throw` setting to 2 on the press and 1 on the release.
The way in opens in 5 ms and shuts in 15, and the output is never touched, so the repeats
ring out. Give another delay the same three-way `throw` setting and the switch works it too.
DECIDED by Tom (25 Nov 2026): the throw stays in the effects engine, in the vocal chain before
the looper, so it catches the live voice only and never the backing-vocal loops. Every other
vocal delay and reverb belongs in the mixer, after the looper.

## The test take

`EffectsRack::startTake / stopTake / playTake` (buttons on the Effects tab, or one pedal switch
through `takeStep`, pedal action 4): records the dry input of every chain that is on, for up to a minute, and
plays it back round and round in place of those chains' sockets, all in step. A chain that
was off while recording stays live. Silence at either end is trimmed and the loop's ends are
eased over 5 ms. Takes are saved as WAVs with one channel per chain (silent = not held) in
`Documents/GigRig/Test takes`. The audio thread reaches the take under a try-lock, like the
amp capture: the message thread only holds the lock for the moment it swaps takes.

This is the tool for listening to a whole set-up hands-free, and for a fair A/B against a real
pedal: an empty chain sends its part of the take out to the pedal, and the pedal's return is
another chain's Reference.

Each chain's share of the processor (`EffectsRack::chainLoad`: a running average and the
busiest block in the last 10 seconds) is on the status line and in the report. An effect that
is switched off still runs (so it can fade back in), so a full chain costs the same on or off.

## Presets and the rack's memory

- A chain preset is a small text file in `Documents/GigRig/Effect presets/<name>.txt`:
  `slot=<id>`, then `on=1`, then `<setting>=<value>` lines, then the next `slot=`.
  Unknown effects and settings are skipped, so old builds read new presets safely.
- The whole Effects tab is remembered in the app data folder as `effects-rack.txt`
  (`%APPDATA%\GigRig` on Windows, `~/.config/GigRig` on Linux). Deleting it starts afresh.
- Captures (dry, reference, effects: 3-channel 24-bit WAV + a .txt of the settings) go to
  `Documents/GigRig/Effect captures`.
- Starter presets (`resources/presets/*.txt`, listed in `src/Responses.cpp`) are copied into the
  presets folder at start only if no preset of that name exists, so Tom's edits are safe. The
  self-test loads each one and fails on an effect or setting that does not exist.

## The amp capture (for NAM models)

The Amp capture tab plays a test sound out of a return, through an amp pedal (Tom's UA
Dream '65) fed from a CQ output, and records what comes back on a socket. The WAV
(`Documents/GigRig/Amp captures`) has channel 1 = the sound as sent, channel 2 = what came
back, at the device rate (96 kHz on the CQ). The .txt beside it gives the name, levels and
the measured round trip. To train a NAM model: line the channels up with the three clicks
at the start (0.5, 1.5 and 2.5 s), then train on channel 1 -> channel 2, holding back the
last section (the repeat of the plucked notes, the final ~21 s) for validation.

The app's own test sound (`EffectsRack::makeTestSound`, about 2.5 minutes) is fixed-seed, so
the same rate always gives the same sound: clicks, sweeps at five levels, plucked notes and
chords at many levels, guitar-shaped noise, then a repeat of plucked notes. A WAV file
(such as NAM's standard input file) can be used instead; it is resampled to the device rate.

## Amp models (NAM)

The effect `amp` (`src/effects/amp/NamAmp.*`) plays a `.nam` file with the Neural Amp
Modeler core (MIT licence, fetched by CMake at tag v0.3.0, with Eigen at the commit NAM pins).
- The model runs at 48 kHz. At 96 kHz the effect takes the sound down to 48 kHz and back up
  around it with the half-band filters from `Oversampler.h` (10 samples of delay, reported).
- Its text setting `model` is a file name in `Documents/GigRig/Amp models` (or a full path).
  Loading happens on the message thread; a new model fades in over 20 ms.
- What the model puts out for silence is measured on load and taken off, so silence stays silent.
- The models trained from Tom's captures are built into the app (`resources/amp-models`, listed
  in `CMakeLists.txt` and in `src/AmpModels.cpp`) and copied into the models folder on start.
  To ship a new one: add the file in both places. The first in the list is what a new amp starts with.
- `namcore` is an OBJECT library on purpose: NAM registers its model types from static
  initialisers, which a static library would let the linker drop ("Factory not found for name").
- CPU: a standard WaveNet model is the heaviest effect so far (budget 45% of a block in the
  self-test, measured on the cloud build machine).

### Training an amp model

`tools/nam/train_from_capture.py` turns an Amp capture WAV into `model.nam` (see its header
for setting it up). It needs Python with `neural-amp-modeler==0.10.0`: that version writes
the classic format (0.5.4) the app's NAM core reads. A GPU makes it minutes; on 4 CPU cores a
standard model takes about 30 s per epoch, so 100 epochs is under an hour. Check the ESR it
reports on the held-back section: under 0.01 is very good, under 0.03 is usable.

The 23 November 2026 captures (Break Up, Clean, Driven) are in the project files under
`effects/amp-captures-2026-11-23`.

## Test main for the plain-C++ effect checks

The amp checks expect a model: call `gigrig::fx::NamAmp::setModelFolder` and
`setDefaultModel` first (with `resources/amp-models` copied under the names in
`src/AmpModels.cpp`), and link the NAM core and Eigen. The whole set takes about half a minute.

```cpp
#include "EffectChecks.h"
#include <cstdio>
int main()
{
    int fails = 0;
    gigrig::fx::runEffectChecks ([&] (bool ok, const std::string& what)
    {
        std::printf ("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
        fails += ok ? 0 : 1;
    });
    return fails != 0;
}
```
