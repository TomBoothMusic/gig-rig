# The mixer

Built 26 November 2026. This is the first build of the rig's own mixer: the part that will
replace the CQ12T. It lives on the **Mixer** tab, the last tab in the app.

**It starts switched off, and switched off it changes nothing.** Effect chains and loops go
straight to their returns and the CQ12T does the mixing, exactly as in every build before.
The self-test proves that sample for sample. Tick **Mixer on** and the app does the mixing
instead; untick it and you are back where you were.

## What the sound goes through

```
a socket, after its effects chain (the same point the looper hears)
  + every loop that was recorded from that socket      (each loop comes back on its own channel)
  -> record point "after the looper", and the direct out for a sound crew
  -> the channel: gate, EQ, compressor, 1176-style insert
  -> fader, pan, mute -> main mix
  -> sends: monitor 1, monitor 2, and the four reverbs and delays
reverbs and delays -> low cut and high cut on the wet sound -> main mix (and the monitors, if sent)
main mix and each monitor -> EQ, notches, compressor, limiter, speaker delay, level -> a return
```

What follows from that order:

- **A loop is treated exactly like the live sound of the channel it came from.** The same
  fader, mute, EQ, monitor sends and reverb. A track that recorded the acoustic and the
  electric together returns each half to its own channel.
- **The mixer's reverbs and delays are after the looper,** so they are never recorded into a
  loop and never into the "after the looper" recordings.
- **A volume pedal in an effects chain is before all of it:** before the looper, before the
  mixer and before the direct outs to a sound crew.
- **The mixer adds no delay.** A click comes out on the very sample it went in (checked by the
  self-test), unless the speaker delay is set, which adds exactly what it is set to.

## The screen

One row of strips: eight **channels**, four **reverbs and delays**, three **outputs** (Main,
Monitor 1, Monitor 2). Each strip has a name, a fader, a meter and a button underneath.
The thin amber bar beside a meter shows how far the compressor (or limiter) is turning down.

**Press a strip's name** and everything about it appears underneath:

- **A channel:** its name, which socket it comes from, trim, pan, what it records, a direct
  out, its sends to each monitor and each reverb or delay, and four blocks of processing:
  **Gate**, **EQ**, **Compressor** and **1176 style**. Press a block's button to see its
  sliders; **This block on** switches it in and out. Double-click a slider for its starting point.
- **A reverb or delay:** which effect it is, a **low cut and high cut on its wet sound**
  (so an obvious reverb on a voice stays clean), how much goes into each monitor, and the
  effect's own sliders. The strip's fader is how loud it comes back.
- **An output:** which return it goes out on, mono or stereo, and its blocks: **EQ**,
  **Notches** (six narrow cuts for feedback and room booms), **Compressor**, **Limiter** and
  **Speaker delay**. Monitors also choose whether their sends are taken before or after the
  channel faders.

The channel EQ is fully parametric: a low cut, a low shelf, **four bells** (each with where,
how much and how wide), a high shelf and a high cut.

### How it starts

| Channel | Socket | Starts |
|---|---|---|
| Vocal | 6 | on |
| Acoustic | 7 | on |
| Stomp | 10 | off (no socket of its own yet); compressor ready |
| Electric | 8 | on |
| Bass | 9 | on |
| Duo vocal | 1 | off |
| Duo instrument | 2 | off |
| Vocal (in-ears) | 6 | off: a second, separately processed copy of the vocal that feeds only Tom's monitor |

Reverbs and delays: **Vocal plate**, **Vocal delay** (follows the song's tempo), **Instrument
room**, and a spare tape echo (off). Every send starts at off, so nothing has reverb until
you raise a send.

Outputs on the CQ12T: **Main** on returns 5 and 6 (the USB stereo channel), **Monitor 1**
on return 3 and **Monitor 2** on return 4 (ST1 left and right). All mono to start with.

## Presets

Four kinds, loaded independently, as in the design. They are small text files in
`Documents\GigRig\Mixer`, one folder per kind.

| Kind | Holds | Loading it leaves alone |
|---|---|---|
| **Act** | Every channel: what is plugged in where, names, levels, EQ, gate, compressors, sends, record points | the venue and the monitors |
| **Venue / PA** | The main output's whole chain and level, and the four reverbs and delays | the act and the monitors |
| **Monitors** | Both monitor outputs: where they go, mono or stereo, processing | the act and the venue |
| **Output** | One output's processing, loadable into any output | everything else, and that output's level and sockets |

To save: type a name in the box and press **Save**. A star beside **Act**, **Venue / PA** or
**Monitors** means it has changed since it was loaded or saved; pressing **Save** with the
same name puts tonight's changes back into the preset in one step.

Changing songs never touches any of them.

The app writes a few starters the first time (it never overwrites a file that is there):
acts **Solo** and **Duo (partner sings and plays)**, venues **Starting point (flat)** and
**Own PA (limiter catches peaks first)**, monitors **Two wedges (mono)** and **Two in-ears
(stereo)**, and the output preset **Flat**.

The whole mixer is also remembered as it was left (`mixer.txt` beside the app's other
settings), so after closing the app or a power cut it comes back as it was.

## The venue knob (ENC 5)

Three buttons stand in for the board's fifth knob until the board exists: **Turn left**,
**Turn right** and **Push**. The line beside them is what the board's main screen would show.

- **Turning only browses.** It shows a venue's name; nothing about the sound changes. After
  5 seconds without a push it gives up.
- **Push loads** the venue shown. The new main chain fades in beside the old one over about
  a second: no click, no step.
- **Never louder in a jump.** If the venue was saved quieter, the main fades down to it. If it
  was saved louder, the level stays where it is and the screen says by how much it differs.
- **Unsaved changes:** if tonight's changes to the venue in use are not saved, the first push
  only warns; a second push loads over them.

The **Load** button beside **Venue / PA** loads the same safe way.

## Recording

**Record** starts a new folder in `Documents\GigRig\Recordings` named by date and time, with
one mono WAV for each channel that is on and one stereo WAV of the main mix. Each channel
chooses what it records: nothing, the plain socket, after its effects, or **after the looper**
(live and loops together, before the channel's EQ and the mixer's reverb). Files are 24-bit at
48 kHz even when the app runs at 96 kHz. **Record** again stops.

## Safety

- Off by default, and off means untouched (see the top).
- **Feedback guard.** If the main mix sits far over full level for a quarter of a second,
  which is a howl round the CQ and not music, the mixer silences itself and says so. **Reset**
  brings it back once the cause is found.
- The **limiter** on every output is on from the start, at -1 dB.
- Warnings appear in amber under the venue knob: a channel with no socket, two channels that
  put the same socket in the main mix twice, outputs that share a return, an effects chain the
  mixer cannot hear, a loop with no channel.

## The volume pedal, and the acoustic and electric blend

The volume pedal is an effect (**Volume pedal** in the Effects tab's list, id `volume`), so
it can sit anywhere in a chain: between the drives and the amp, as on Tom's board, it cleans
the amp up as it turns down. Its settings are **Pedal** (heel 0, toe 100), **Heel level**
(how quiet the heel is; all the way down is silence) and **Reversed**.

The blend: put a Volume pedal in the electric chain as it is, and one in the acoustic chain
with **Reversed** on. Both follow the one pedal, so toe down is all electric, heel down is
all acoustic, and between is a blend. Until a real pedal is wired in, moving the **Pedal**
slider of either one moves both (`EffectsRack::moveVolumePedals`, which is where a real
expression pedal will arrive).

## Not built yet

- The toe switch: electric to full, acoustic muted, and the song's wah or whammy engaged.
- A song nudging the venue's delay time or reverb length.
- Partial recall (only a venue's room EQ, say), and backing presets up.
- Virtual soundcheck and the Recordings tab (copy to USB, delete).
- More than two monitor mixes, spare sends for delay speakers, linked stereo channels (keys).
- Remote preamp gain: that needs the real interface.
- The tablet's simple everyday view. This tab is the workstation view.

## For developers

| File | What |
|---|---|
| `src/Mixer.h/.cpp` | The mixer core, presets, the venue knob's rules, the report. No screen. |
| `src/MixRecorder.h/.cpp` | Writes the WAV files off the audio thread. |
| `src/effects/mixer/MixerBlocks.*` | The mixer's own blocks: `mixeq`, `mixcomp`, `notches`, `limiter`, `linedelay`, `wetfilter`. They follow the Effect interface, so the standard effect checks run on them, but they are listed apart (`mixerEffectTypes()`) so the Effects tab does not offer them. |
| `src/effects/volume/` | The volume pedal effect. |
| `src/MixerPanel.*` | The Mixer tab. |
| `src/MixerSelfTest.cpp`, `src/effects/mixer/MixerBlockChecks.cpp` | The checks. |

- `AudioEngine::processBlock` asks `Mixer::startBlock`. When the mixer is mixing, the looper
  adds each lane to the bus of the socket it was recorded from (`Looper::setLaneBuses`)
  instead of to a return, and `Mixer::process` and `Mixer::addOutputs` replace the rack's own
  outputs. Play-through from the first tab stays quiet while the mixer is on.
- A reverb or delay on a send gives only its wet sound: every one of those effects passes the
  dry sound at exactly unity, so the mixer subtracts what it sent in. The self-test checks
  this for each effect a mixer effect may hold (`mixerFxChoices()`); check it for any new one.
- A channel's and an output's blocks are fixed (`defaultStripModel`, `defaultOutputModel`).
  A preset file can only change their settings, never what a channel is made of (`conform`),
  so older or hand-edited files stay safe. To add a block, add it to the model; older files
  then get it at its starting point.
- **The words in preset files are on Tom's disk: never rename them, only add.** That is the
  section names (`[channel 1]`, `[fx 1 effect]`, `[output main processing]` and so on), the
  keys (`name`, `on`, `socket`, `trim`, `fader`, `mute`, `pan`, `monitor1`, `fx1`, `record`,
  `direct`, `lowcut`, `highcut`, `return`, `level`, `mono`, `prefader`, `pair`), the record
  words (`none`, `raw`, `effects`, `looper`), and the block and setting ids.
- Screenshots in the cloud: `GIGRIG_TAB=7` opens on the Mixer tab and `GIGRIG_MIXER_STRIP=12.3`
  chooses strip 12 (the main output) with its fourth block shown.
