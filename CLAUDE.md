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

Real audio and MIDI are proven only on Tom's hardware.

## The rig computer (Linux)

- Set up by `linux/install.sh`; everything it changes is listed at the end of
  `docs/linux-setup.md`. Health check: `linux/rig-check.sh` (changes nothing).
- Ubuntu 24.04 LTS, kernel options `preempt=full threadirqs usbcore.autosuspend=-1`,
  PipeWire masked so the app opens the interface directly through ALSA.
- Builds: `~/gigrig/builds/<tag>/GigRig`, the one in use at `~/gigrig/current`.
  Launcher `~/.local/bin/gigrig` (restarts after a crash). Logs: `~/gigrig/logs/`.
- App data: `~/.config/GigRig/` (`audio-settings.txt`, `loop-timing-2.txt`, `driver-delay.txt`, `kick-pad.txt`).
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
`Documents/GigRig/Kick sounds`: never commit one.
