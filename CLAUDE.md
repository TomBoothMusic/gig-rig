# Notes for Claude working in this repository

This is the software for Tom's all-in-one gig rig: one box to replace his
pedalboard, Boss RC-600 looper and Allen & Heath CQ12T mixer. Tom is a working
musician and a fitter and machinist, not a programmer. He tests every build on
real gear and pastes the app's reports back.

The design lives in the project's design doc (a Claude Doc, linked from the
project's instructions). Read it before design decisions; the code follows it.

## Working rules

- **Plain language for Tom.** Anything he reads (README, guides, the app's own
  words, PR descriptions) uses full sentences and no jargon. Click-by-click steps.
- **The app reports, Tom pastes.** New features get a line in the app's report
  (the **Copy report** button) so results come back as text, not descriptions.
- **The self-test grows with the code.** `GigRig --self-test <file>` runs on
  every build in CI. Add checks for new logic, timing checked to the sample.
- **CI is the proof.** GitHub Actions builds Windows and Linux on every PR and
  runs the self-test, a device scan and the Linux install script. A build is
  ready only when CI is green. Merges to `main` publish a numbered release
  (`build-<n>`), which is how Tom (and the rig computer) get builds.
- **A working gig system is never put at risk.** The rig computer runs a pinned
  release. New work goes to a new release, is tested away from gigs, and
  `linux/rollback.sh` goes back in one command.
- **Licence:** the app is AGPLv3 (`LICENSE`), matching JUCE's free licence; see the README.
- **The repository may be public.** Never commit personal names (other than Tom),
  email addresses, phone numbers, home paths, passwords or tokens. Songs, presets
  and amp captures are fine.

## Building

C++20, JUCE 9 (fetched by CMake), CMake 3.22+. See the README "For developers".

In a cloud container without ALSA and X11 headers, a compile check still works:

```
git clone --depth 1 --branch 9.0.5 https://github.com/juce-framework/JUCE.git ../JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=$PWD/../JUCE \
  -DGIGRIG_ENABLE_JACK=OFF \
  -DCMAKE_CXX_FLAGS="-DJUCE_ALSA=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XINPUT=0"
cmake --build build --parallel
xvfb-run -a build/GigRig_artefacts/Release/GigRig --self-test result.txt
```

The whole self-test takes a few minutes. While working on one part,
`GIGRIG_SELFTEST_ONLY=break` (or `pedal`, `tuner`, `remote`, `mixer`, `recordings`, `songs`, `screens`) in front of the
last line runs only that part's checks. CI always runs all of them.

Real audio and MIDI are proven only on Tom's hardware.

## The rig computer (Linux)

- Set up by `linux/install.sh`; everything it changes is listed at the end of
  `docs/linux-setup.md`. Health check: `linux/rig-check.sh` (changes nothing).
- Ubuntu 24.04 LTS, kernel options `preempt=full threadirqs usbcore.autosuspend=-1`,
  PipeWire masked so the app opens the interface directly through ALSA.
- Builds: `~/gigrig/builds/<tag>/GigRig`, the one in use at `~/gigrig/current`.
  Launcher `~/.local/bin/gigrig` (restarts after a crash). Logs: `~/gigrig/logs/`.
- App data: `~/.config/GigRig/` (`audio-settings.txt`, `loop-timing-2.txt`, `driver-delay.txt`, `kick-pad.txt`, `effects-rack.txt`, `mixer.txt`, `remote.txt`, `break-music.txt`, `expression-pedal.txt`, `tuner.txt`, `switch-screens.txt`).
  Tom's presets, songs and mixer set-ups: `~/Documents/GigRig/`. Moving them from the laptop: `docs/linux-setup.md`, B4.
- Updates: `linux/update.sh` (the **Gig Rig: update** icon) fetches, self-tests and switches a build with no `sudo`; it never closes the running app without a yes. A fresh computer: `linux/bootstrap.sh`. The phone as break music: `linux/break-music.sh` (optional, unproven on hardware).
- The desktop runs X11, so `import -window root shot.png` (ImageMagick) or
  `gnome-screenshot -f shot.png` can show what is on screen.
- If you are running on the rig itself: never update packages, change kernel
  options or restart the app while Tom might be playing. Ask first. Changes go
  through the scripts in `linux/` so they can be repeated on a fresh install.

## Songs and their sounds

Built 25 Nov 2026 (the song-linking batch); **`docs/songs.md` is the guide. Keep it true.**
A song entry is a text file in `Documents/GigRig/Songs` that names effect presets per chain
(it does not copy them), plus its own changes, switches and amp list. `Default sounds.txt` is
what songs without lines of their own use. Program changes from Songbook Pro load entries.
Logic in `src/SongBook.*` (no screen, checked by `src/SongSelfTest.cpp`), screen in
`src/SongsPanel.*`. The words used in song files (chain, effect, setting and switch-action
names) are saved on Tom's disk: never rename them, only add.

## The mixer

Built 26 Nov 2026 (two builds the same day); **`docs/mixer.md` is the guide. Keep it true.** The mixer
is inside the engine (`src/Mixer.*`, no screen, checked by `src/MixerSelfTest.cpp`), its own
blocks are in `src/effects/mixer/`, its screen is `src/MixerPanel.*`. It is **off by default
and off must stay sample-for-sample what the app did before it existed** (the first mixer
check in the self-test). Each loop is recorded after its channel's own processing (Trim, gate, EQ, compressors) and returns to that channel after it, so layers never pile into a compressor;
the mixer's reverbs and delays are after the looper; the mixer adds no delay. Presets (act,
venue, monitors, output) are text files in `Documents/GigRig/Mixer`: their section names,
keys and block ids are saved on Tom's disk, so never rename them, only add. A song may set a
channel's sends to the mixer's reverbs and delays (`send =` lines, an overlay on the act) and
which reverb or delay each of the mixer's four effects is and its settings (`mixer effect =`
lines, an overlay on the venue; Tom's decision, 27 Nov 2026), and nothing else in the mixer:
**a song never changes the venue itself** (each effect's EQ, cuts and levels, the main output).
Tom's split (27 Nov 2026): the song carries a reverb's or delay's own settings, the send level is
the venue's choice. `moveToMixer` in `src/SongBook.cpp` (the Songs tab button) moves old songs over.

## Recording

Built 28 Nov 2026; **`docs/recording.md` is the guide. Keep it true.** `src/MixRecorder.*` writes
the files (the audio thread only queues, a background thread writes; a slow drive loses samples
from the files, never from the sound). The Mixer feeds it: each channel at its own tap point
(`RecordPoint`), the main mix, each mixer effect's wet sound, and the **recording mix**, a second
stereo balance (per-channel level against the main mix and its own pan, then its own EQ, bus
compressor and limiter) that is only ever written to a file and is only worked on while it is
recorded. **A rig that is recording must send every return exactly what a rig that is not
recording sends** (a check); every file of a recording must be the same length. With the mixer
off `Mixer::recordWithoutMixing` still records the sockets. Screen: `src/RecordingPanel.*`
(the Recording tab) and the remote page's Record screen (`rec.*` orders). Record points, the
`reclevel`/`recpan` keys and the `[recording]` sections are saved in act files on Tom's disk:
never rename, only add.

Since 28 Nov 2026 (second round, Tom's ask): **virtual soundcheck** (`src/Soundcheck.*`) plays a
recording's raw files back into the sockets they came from, first thing in
`AudioEngine::processBlock`, so effects, looper and mixer work on it as on Tom playing; **not
playing, it must change no sample** (a check). Only raw files can be played, which is why a
recording keeps a raw copy of every socket (`MixRecordSetup::checkCopy`, on by default) and its
`About this recording.txt` has `soundcheck: <file> = socket <n>` lines. `src/RecordingLibrary.*`
lists, keeps, deletes, clears and copies recordings to USB (each file read back and compared);
it never touches a kept recording or one in use, and refuses to copy while recording or
soundchecking (a USB drive at work can make the sound click). Screen: `src/RecordingsPanel.*`
(the Recordings tab) and the Record screen's Recordings pages (`check.*`, `recs.*` orders).
Checks: `src/RecordingsSelfTest.cpp`. The `soundcheck:` lines and the marker files `Keep.txt` and
`Copied to USB.txt` are on Tom's disk: never rename them.

**Loops in a soundcheck** (`src/LoopReplay.*`, 28 Nov 2026, Tom's ask): loops are never saved for
it. While recording, every looper press is noted to the sample in the recording's
`Looper presses.txt` (on Tom's disk: never rename it or reword its lines), with the songs loaded;
a soundcheck makes the same presses again at the same samples (`Looper::setTimedPresses`), so the
loops are built afresh through the effects and mixer as they are now. **A press handed to the
looper with its sample must do exactly what a foot at that sample does** (a check), and with no
soundcheck playing the looper's own path is untouched. The same file notes kicks from the pad
(made again on the audio thread, `KickPad::hitNow`), effect switch presses and expression pedal
moves (made again on the message thread, a hundred times a second, `EffectsRack::takePedalNow`),
and where the switches and pedal stood at the start; the soundcheck puts them there and puts
them back when it stops.

## The remote screen (the phone and tablet control app)

Built 27 Nov 2026; **`docs/remote.md` is the guide. Keep it true.** It is a web page (`remote/`,
plain JavaScript, no libraries, built into the app) served by a small server inside the app
(`src/RemoteServer.*`), which is **off by default and off must open no port**. `src/RemoteApi.*`
turns the rig into JSON and JSON back into actions with no network in it; `src/LooperLayout.*`
writes a song's own looper layout as `looper ...` lines in its song file (the words are saved on
Tom's disk: never rename them, only add) for the arrangement builder. All checked by
`src/RemoteSelfTest.cpp`. The page has a demo (`remote/demo.js`, a pretend rig) whose data
`remote/demo-data.js` is made by `tools/remote_demo.py` from a running app: never edit it by
hand, and capture it again when `RemoteApi::state` gains a part (the self-test says when); on a
machine without Tom's songs use `tools/remote_demo.py top-up` instead, which adds only what is
missing. The state must never hold a path from the computer's disk (the demo data is public).
Every new order goes in four places: `RemoteApi::command`, `remote/demo.js`, the table in
`docs/remote.md` and `src/RemoteSelfTest.cpp`.
The demo's looper is a stand-in, never evidence about timing. Nothing in the remote screen may
run on the audio thread, and the looper engine was not changed for it.

## Break music, the tuner and the expression pedal

Built 27 Nov 2026 in one batch. **`docs/break-music.md` and `docs/tuner.md` are the guides; the
expression pedal is in `docs/mixer.md`. Keep them true.**

- **Break music** (`src/BreakMusic.*`, tab `src/BreakMusicPanel.*`, checks
  `src/BreakMusicSelfTest.cpp`): a stereo channel that uses no interface input and plays Tom's own
  mixes from `Documents/GigRig/Break music` (never commit one), or on Linux a phone's sound from a
  pipe. Tom's two rules: **it fades down by itself when he starts playing, and it only ever comes
  back up by hand.** Nothing plays until Play is pressed. With the mixer on it joins the main
  output only, after the main mix is recorded, so it is in no loop, recording or monitor; with
  the mixer off it goes to a pair of returns. A background thread reads the file; the audio
  thread only empties a queue. `VenueKnob` (same files) is ENC 5: it rests on PA/venue, a hold
  hands it to the break music, and it hands itself back when playing fades the music.
- **The tuner** (`src/Tuner.*`, full-screen strobe `src/StrobeTuner.*`, header strip `src/TunerStrip.*`,
  checks `src/TunerSelfTest.cpp`): listens to one socket before any effect; the note is worked out on
  the message thread. Since 28 Nov 2026 (Tom's ask) the tuner fills the screen with a purple strobe,
  and with the mixer on **only the guitar channels go silent** (known by their names, or ticked by
  hand; they leave the main mix and the mixer's reverbs while voices, the stomp, playing loops and
  tails carry on; the monitors keep them unless that is unticked, Tom's 19 Nov decision that the
  in-ears stay live); "the whole main mix" is still a choice. The strobe's picture and its cents both come from `Tuner::phaseAgainst` (the
  sound against the note's exact frequency): the checks hold it to a tenth of a cent, keep them.
  **Mixer off, it must change no sample** (a check).
- **The expression pedal** (`src/ExpressionPedal.*`, checks `src/ExpressionSelfTest.cpp`): a MIDI
  controller found by Learn on the Pedal tab; every move goes to
  `EffectsRack::moveExpressionPedal`, the same door the on-screen Pedal sliders use.
- All three have their part in the report, their memory file in the app's settings folder (keys
  are saved on Tom's disk: never rename, only add) and their controls on the remote screen's
  Tools screen (`break.*`, `knob`, `tuner.*` orders).

## Adding an effect

Tom wants to keep adding effects for years, mostly after his heavy-use
subscription ends, without ever risking a gig. The first batch (four drives, a fuzz, a
clean boost, the Bass9 copy) was built on 23 Nov 2026, the second (amp models from
Tom's Dream '65 captures, a noise gate) the same night, the third (1176-style compressor,
GE-7-style graphic EQ) and the fourth (delays, spring reverb, tremolo, vibrato, chorus, tap
tempo) on 24 Nov, a Whammy-style pitch pedal on 25 Nov, and the fifth (plate and room reverbs,
flanger, Uni-Vibe, channel EQ, acoustic body, response files, a first vocal harmony, the test
take) and a Cry Baby style wah on 25 Nov; **`docs/effects.md` has
the exact steps, file names and rules. Keep it true.** In short:

1. One effect = one folder `src/effects/<id>/` (plain C++, no JUCE) following
   `src/effects/Effect.h`. It must not reach into other effects or the engine.
2. One line in `src/effects/Registry.cpp`, one line in `CMakeLists.txt`.
3. The standard checks in `src/effects/EffectChecks.cpp` then run on it in CI
   automatically; add its own character checks if it has one worth guarding.
4. Effect and setting ids are saved in presets and songs: never rename them.
5. Sound tweaks happen with live sliders and presets in the app, not by
   rebuilding (design doc: "no rebuild for tweaks").
6. Neural Amp Modeler captures and, possibly, open-source LV2 plug-ins are the
   no-code way to add sounds. Prefer them when they fit. The Amp capture tab
   records an amp pedal for a NAM model (see `docs/effects.md`).

**Checks every effect must pass (in the self-test, so CI enforces them)**

- No crash and no NaN or infinity on silence, full-scale noise, a sine sweep,
  and a sudden jump of every parameter between its extremes.
- No silence where there should be sound, and no sound out of silence (unless
  the effect is meant to make sound, such as a tail).
- No clicks when it is switched on and off or when parameters move.
- Within its CPU budget: time `process` at 96 kHz and 64 samples and fail above
  the budget (start with 5% of one block per effect and adjust when the whole
  chain is known).
- Reports its latency truthfully (an impulse through it lands where it says).
- An effect that follows pitch or splits the sound into bands is also checked on a played
  instrument, not only on test tones: the first Bass9 copy passed every tone check and ticked
  on Tom's guitar (`docs/effects.md`, "The bass, reworked").

**Getting it to Tom**

1. Open a PR. CI must be green.
2. Tom tries it on the laptop (Windows build) or on the rig with
   `linux/install.sh --version <new build>` between gigs, A/B against the pedal
   it copies where there is one.
3. If anything is wrong at any point: `linux/rollback.sh`.
4. Keep the design doc's effects list and the funny pedal names (in project
   memory) in step.

## The kick pad test

Built 26 Nov 2026 as a **test** of an undecided idea (design doc, Effects, "Built-in kick pad"):
the SPD::ONE Kick over USB plays a kick made inside the app, by how hard it was hit.
**`docs/kick-pad.md` is the guide. Keep it true.** Logic in `src/KickPad.*` (no screen, checked by
`src/KickPadSelfTest.cpp`), screen in `src/KickPadPanel.*`. Kick recordings are Tom's own files in
`Documents/GigRig/Kick sounds`: never commit one. Since 27 Nov 2026 the same tab can also listen
to an input socket for a bare piezo pad (the cheap bench test of the pad that could replace the
SPD::ONE): `KickPad::listen` on the audio thread, off unless a socket is chosen.

## The controller's bench test program

Built 28 Nov 2026 for Tom's first parts order (a Teensy 4.1, three of the twenty 1.54 inch
screens, piezo discs). `firmware/bench-test/` is a PlatformIO project, separate from the app;
CI's "Controller test program" job builds it into `GigRig-Controller-Test.hex` on the Releases
page and can never hold up an app release. **`docs/bench-test.md` is the guide. Keep it true**,
and keep its pin tables in step with Tom's own step-by-step page (the "Gig Rig bench tests"
artifact in the project). CI proves it compiles and nothing more: it has not run on hardware
until Tom says so. It is a test program, not the final board's controller program.

## The footswitch screens

Built 28 Nov 2026 (Tom's ideas of that day, all **undecided until his bench and sun test**).
**`docs/switch-screens.md` is the guide. Keep it true.** One "tile" per switch (background colour =
state, words, lane pictures, a pedal drawing). `firmware/shared/SwitchTile.*` holds the tile, its
drawing, the sample pages and the MIDI system exclusive messages: plain C++14 with no Arduino and
no JUCE, compiled into **both** the app and the controller's program, so the app's Switch screens
tab shows dot for dot what the Teensy draws and the app's self-test is the drawing's proof.
`firmware/shared/TileArt.h` (letters and lane pictures) is made by `tools/make_tile_art.py`: never
edit it by hand. The app (`src/SwitchScreens.*`, tab `src/SwitchScreensPanel.*`, checks
`src/SwitchScreensSelfTest.cpp`) builds the tiles from the rig's state on the message thread and
sends them; the controller (`firmware/bench-test/src/Screens.cpp`) only draws, and shows sample
pages by itself when no computer is talking. **Colours, words and rules change in the app only;
keep it that way, so a look never needs a new hex file.** Sending is **off by default and off
must open no MIDI output** (a check). The numbers of the message commands, item kinds, text sizes,
pictures and pedal kinds are shared by two programs updated separately, and the keys in
`switch-screens.txt` are on Tom's disk: never renumber or rename, only add. The app still knows
the RC-600's 11 switches, not the board's 17; the switch assignment screen comes with the enclosure.
`firmware/shared/` is also used by the kick pad's messages: keep the two apart.
