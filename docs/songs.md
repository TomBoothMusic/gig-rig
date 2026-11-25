# Songs: how a song calls up its sounds

Built 25 Nov 2026 (the song-linking batch). This is the guide for Tom and for a future Claude.
The code is `src/SongBook.h/.cpp` (the logic, no screen), `src/SongsPanel.*` (the Songs tab) and
`src/SongSelfTest.cpp` (its checks).

## The idea in one minute

- A **preset** is a saved set of effects for one chain. Presets live in
  `Documents/GigRig/Effect presets`, one small text file each. They are made on the Effects tab
  with **Save as**.
- A **song** is an entry in the Song box. It has a looper layout (how the RC-600 memory was set
  up) and, new in this batch, a small text file in `Documents/GigRig/Songs` that says which
  presets each chain uses.
- A song **names** its presets. It does not copy them. Change a preset once and every song that
  names it changes with it.
- A song can also carry a few changes of its own on top ("the same clean sound, but more reverb").
- **Default sounds** (`Default sounds.txt` in the same folder) is what every song without lines of
  its own uses. This is Tom's "one default drive and boost for the generic banks".
- A song is an entry that many Songbook Pro songs can share. It loads when Songbook Pro sends its
  program number, or from the Song box.

## What happens when a song loads

1. The looper takes the song's layout and every loop is cleared, as on the RC-600.
2. For each of the four chains: if the song's file has lines for that chain, those are used. If
   not, the Default sounds' lines for that chain are used. If neither says anything, the chain is
   left exactly as it is.
3. The switches and the amp list work the same way: the song's own, else the default's, else as
   they are.
4. Delays and reverbs that were sounding ring out (see below).

Chain routing (which input, which output, the chain's level) is not part of a song. It stays as
set on the Effects tab.

## A song file

`Documents/GigRig/Songs/Wicked Game.txt`:

```
program = 11
looper = Wicked Game
chain 1 = Comp & GE7 for Electric
chain 1 = Clean amp standard Verb & Modulations
set = chain 1 / spring / reverb = 3.5
set = chain 1 / tremolo / off
switch 9 = chain 1 / next amp
switch CTL3 = chain 3 / delay throw
amps = chain 1 / Dream 65 - Clean / Dream 65 - Break Up
```

Every line is optional. Lines starting with `#` are notes.

| Line | Meaning |
| --- | --- |
| `program = 11` | The program number that loads this entry (1 to 128, counted as the RC-600 counts its memories). For a built-in song the number is already known; a line here changes it. |
| `looper = Wicked Game` | Only for an entry that has no looper layout of its own: borrow this built-in song's layout. Without the line it uses the general set-up (memory 1, "Dirty Slap"). |
| `chain 1 = <preset name>` | Chain 1 is built from this preset. A second `chain 1 =` line stacks another preset on, exactly like **Add to chain** (it goes in front of the amp if the chain has one). One preset per line, because preset names may contain `+`. `chain 1 = (empty)` empties the chain. |
| `set = chain 1 / spring / reverb = 3.5` | The song's own change on top: chain, effect, setting, value. |
| `set = chain 1 / tremolo / off` | Switches one effect off (or `on`) for this song. |
| `switch 9 = chain 1 / next amp` | What a switch does in this song. Switches are `1` to `9`, `CTL3`, `CTL4` (the RC-600's, as on the Pedal tab). |
| `switches = none` | This song uses no switches for effects (overrides the default's). |
| `amps = chain 1 / A / B` | The amp models the "next amp" switch steps through, in order. Without it the switch steps through every installed amp model. |

Naming things:

- A chain is `chain 1` to `chain 4`, or the chain's own name from the Effects tab (`Electric`).
- An effect is its id (`spring`, `tapedelay`) or the name on screen (`Spring reverb`). A second
  one of the same kind is `eq #2`.
- A setting is its id or its name on screen. Ids are listed in `docs/effects.md`.
- What a switch does: `compare`, `chain on off`, `tap tempo`, `whammy`, `wah`, `delay throw`,
  `test take`, `next amp`, or `<effect> on off` (for example `chain 1 / tubescreamer on off`).

When a line cannot be used (a preset that is not in the folder, a setting no effect has), the
song still loads. The line is reported as `PROBLEM:` on the Songs tab and in the report, and a
chain whose preset is missing is left alone rather than emptied.

The app looks at the folder once a second. A file that is added or edited by hand (or dropped in
by Claude) is picked up without a restart, and if it belongs to the loaded song its sounds are
set again.

## The Songs tab

- **Song** box: the same list as on the Looper tab. A `*` marks a song with sounds of its own.
- **Try a program number** and **Load that program**: does what Songbook Pro's program change does.
- **Keep these sounds for this song**: writes the song's file from the chains as they stand. For
  each chain that is switched on it writes the presets the chain was built from and whatever was
  changed since. A chain that matches the default is left out, so it keeps following the default.
  If a chain was rearranged by hand so it no longer matches its presets, the chain is saved as a
  new preset named after the song and the song names that.
- **Keep these sounds as the default**: the same, into `Default sounds.txt`.
- **Forget this song's own sounds**: removes the sound lines. The program number stays.
- **Set this song's sounds again**: back to what the file says.
- **New entry**: a new song with the loaded song's looper layout and the sounds as they stand.
- **Switches**: six rows. What they show is saved by the **Keep** buttons.
- **Delays and reverbs ring out when the sound changes**: see below.

On the Effects tab, **Update its presets** writes the knobs turned since the chain was loaded
back into the presets the chain was built from, each preset getting its own effects. This is the
"edit once, every song follows" button. It only saves what was turned since loading, so a song's
own `set` changes never leak into a shared preset. It refuses if effects were added, removed or
moved (use **Save as** then).

## Program changes

Any MIDI input the app has open is listened to, on any channel. Program change `pp` on the wire
is program `pp + 1`, which is the RC-600 memory number Songbook Pro already calls up.

- A number that an entry carries loads that entry.
- A number no entry carries loads the general set-up: memory 1's looper layout with the default
  sounds. This is what Tom's shared memories do today.
- The number of the entry that is already loaded only sets its sounds afresh. The loops are left
  alone, so a second tap in Songbook Pro mid-song does not wipe a loop.

The report lists the last program changes heard and which entry each one loaded.

## Tails ring out

When a chain's effects change (a song loads, a preset loads), the old sound used to be cut in
10 ms. Now, if the old chain had a delay or reverb switched on, the signal going into that delay
or reverb is faded out in 10 ms while the new chain fades in, and the old delay or reverb keeps
running on silence until it has died away (quieter than -80 dB for 0.3 s), for at most 10 seconds.
The new sound is playable at once.

- Code: `fx::Chain::process` in `src/effects/Chain.cpp`. An effect says it has a tail with
  `hasTail()` in `Effect.h`. **A new delay or reverb must return true**, and
  `src/SongSelfTest.cpp` has a list of which effects must.
- Only the first tail effect in the chain and everything after it keep running. If that is in
  front of an amp model, the old amp runs too until the tail is over, so the processor works
  harder for those seconds.
- A change that only moves settings (same effects in the same order) swaps nothing at all: the
  settings glide and the tails carry on by themselves (`Chain::setModelGently`).
- If another change arrives while a tail rings, the tail is faded in 10 ms.
- The tick box on the Songs tab turns ringing out off (back to the 10 ms cut).
- Switching one effect off with its own on/off still stops its tail. That is separate.

## The next-amp switch

The switch action **next amp sound** steps the chain's amp model to the next one in the song's
`amps =` list, or through every installed model when there is no list. Only the model changes.
The amp's gain, tone and level sliders stay where they are. The report line `Amp switch:` says
which model it went to.

## The mixer comes back by itself

`src/ReconnectWatch.h`. If the audio device stops (the CQ's cable is pulled, the mixer is switched
off), the app notices within 1.5 seconds, closes it, and tries to open the same device with the
same settings again after 1 second and then every 3 seconds. The Latency and audio test tab shows
a red line while it waits, and the report says how many times the device was lost and came back.
Loops recorded before it went are cleared when it comes back (opening a device always starts the
looper clean); keeping them is a possible later improvement. A device scan (Device check tab) pauses the retries.

## Not built yet

- The song encoder and the harmony key by hold-switch (no encoder exists yet). A song sets the
  harmony's key with a `set` line.
- Songbook Pro's program changes have not reached the app from Tom's tablet yet (only the
  **Load that program** button and the self-test). The WIDI Jack needs a way into the laptop.
- An expression pedal per song.

## Tom's song list: the format to send

One block per song entry (an entry can serve many Songbook Pro songs). Anything left out uses
the default. Claude turns the list into the files in one go.

```
Wicked Game (program 11)
  electric: Comp & GE7 for Electric + Clean amp standard Verb & Modulations
  changes:  spring reverb 3.5, tremolo off
  acoustic: default
  vocal:    default, harmony on in key of B
  switches: 9 = next amp (Clean, Break Up), CTL3 = vocal delay throw
```

And once, at the top: `Default: electric = ..., acoustic = ..., vocal = ..., switches = ...`.
