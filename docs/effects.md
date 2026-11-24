# The effects: how they are built, and how to add one

Written 23 November 2026 with the first batch (drives, the Bass9 copy, the amp capture);
the compressor and graphic EQ came on 24 November 2026.
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
| One folder per effect | `src/effects/<id>/` |
| The engine side: chains on sockets, returns, A/B, level match, Capture, amp capture | `src/EffectsRack.*` |
| The Effects and Amp capture tabs | `src/EffectsPanel.*` |
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


## Presets and the rack's memory

- A chain preset is a small text file in `Documents/GigRig/Effect presets/<name>.txt`:
  `slot=<id>`, then `on=1`, then `<setting>=<value>` lines, then the next `slot=`.
  Unknown effects and settings are skipped, so old builds read new presets safely.
- The whole Effects tab is remembered in the app data folder as `effects-rack.txt`
  (`%APPDATA%\GigRig` on Windows, `~/.config/GigRig` on Linux). Deleting it starts afresh.
- Captures (dry, reference, effects: 3-channel 24-bit WAV + a .txt of the settings) go to
  `Documents/GigRig/Effect captures`.

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
`src/AmpModels.cpp`), and link the NAM core and Eigen.

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
