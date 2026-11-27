# The mixer

Built 26 November 2026. This is the rig's own mixer: the part that will replace the CQ12T.
It lives on the **Mixer** tab, the last tab in the app. The second build, the same day, added
what Tom asked for after reading the first: the studio-style blocks (warmth, an LA-2A style
compressor, a bus compressor, a tape machine), tap points for the monitors, stereo channels,
songs that set sends, the blend pedal's even-loudness travel and the toe switch.

**It starts switched off, and switched off it changes nothing.** Effect chains and loops go
straight to their returns and the CQ12T does the mixing, exactly as in every build before.
The self-test proves that sample for sample. Tick **Mixer on** and the app does the mixing
instead; untick it and you are back where you were.

## What the sound goes through

```
a socket, after its effects chain
  -> the channel: Trim, gate, source enhancer, warmth, EQ, compressor, 1176-style compressor, LA-2A style compressor
  -> out to the looper, which records the channel here, after all of its processing
  + every loop that was recorded from that socket      (each loop comes back on its own channel, at the same point)
  -> fader, pan, mute -> main mix
  (the direct out for a sound crew and the record point "after the looper" carry the channel
   after its Trim and before its processing, plus its loops)
  -> sends: the four reverbs and delays (after everything), and monitor 1 and monitor 2
     (each monitor chooses: before the processing, after the EQ, or after the compressors)
reverbs and delays -> low cut and high cut on the wet sound -> main mix (and the monitors, if sent)
main mix and each monitor -> EQ, notches, compressor, bus compressor, tape, limiter, speaker delay, level -> a return
```

What follows from that order:

- **A loop is processed once, as it was played** (Tom, 27 Nov 2026). The channel's Trim, gate,
  enhancer, warmth, EQ and compressors are in the loop when it is recorded, and the loop comes
  back after them. So layers never pile into a channel's compressor, and changing the Trim or
  the EQ later does not change a loop already down. From there a loop shares the live sound's
  fader, mute, monitor sends and reverb. A track that recorded the acoustic and the electric
  together returns each half to its own channel.
- **A loop made while the mixer was off** has no channel processing in it, and one made with
  it on has. Switch the mixer on before a song, not during one.
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
  out, its sends to each monitor and each reverb or delay, and seven blocks of processing:
  **Gate**, **Enhancer**, **Warmth**, **EQ**, **Compressor**, **1176 style** and **LA-2A style**. Press a
  block's button to see its sliders; **This block on** switches it in and out. Double-click a
  slider for its starting point. Under the sends are two switches: **Stereo with channel N**
  and **Belong to this song, not the act** (both explained below).
- **A reverb or delay:** which effect it is, a **low cut and high cut on its wet sound**
  (so an obvious reverb on a voice stays clean), an **EQ on what goes into it** (the sliders
  starting **Into it**: a low shelf, two bands and a high shelf), how much goes into each
  monitor, and the effect's own sliders. The strip's fader is how loud it comes back. The tick
  box **Belong to this song, not the venue** is explained below.
- **An output:** which return it goes out on, mono or stereo, and its blocks: **EQ**,
  **Notches** (six narrow cuts for feedback and room booms), **Compressor**, **Bus comp**,
  **Tape**, **Limiter** and **Speaker delay**. Monitors also choose whether their sends are
  taken before or after the channel faders, and **from where in each channel**.

The channel EQ is fully parametric: a low cut, a low shelf, **four bells** (each with where,
how much and how wide), a high shelf and a high cut.

### The studio-style blocks

All four start switched off, and all are in the Effects tab's list too (under **Studio**), so
they can go in an effects chain as well.

- **Enhancer** (every channel, after the gate): a source enhancer in the style of the Rupert
  Neve Portico 5045, the gentle alternative to a gate on a voice. While you sing the channel
  is untouched; between phrases it is turned down by **Depth** (a few dB), never shut, so
  spill and feedback in the gaps drop and no word is cut. Set **Threshold** just under your
  quietest singing. **Time** (A fastest to F slowest) is how slowly it closes, and it opens
  again within a few thousandths of a second. It listens to the voice's part of the sound,
  so a kick or a cymbal near the microphone opens it less than a voice does. **Listens to**
  chooses RMS (loudness, the usual) or Peak. The amber bar does not include it; its own
  turn-down shows in the report.
- **Warmth** (every channel, before the EQ): a touch of valve-desk colour. **Drive** adds the
  harmonics, **Colour** leans it dark or bright, **Level** trims, and **Mix** blends the
  untouched sound back in. The loudness stays about where it was as Drive goes up.
- **LA-2A style** (every channel, last): the slow, smooth studio leveller with two knobs,
  **Peak reduction** and **Gain**, and a Compress or Limit switch. Kind to a voice or a bass.
- **Bus comp** (main and both monitors): the desk-style compressor that glues a mix together.
  Threshold, ratio (2, 4 or 10), attack, release, make-up, a low cut on what it listens to (so
  the stomp does not pump the mix) and **Mix** to blend the untouched sound back in.
- **Tape** (main and both monitors): a tape machine across the mix. **Drive** pushes the tape,
  **Tape speed** chooses 7.5, 15 or 30 inches a second (slower is fatter and softer on top),
  **Level** trims.

The amber bar beside a strip's meter adds up every compressor on that strip.

### Setting levels: the two marks on a channel's meter

The compressors, the tape, the bus compressor and the limiter are all made for a healthy
level. Each channel's meter has a lighter band between two marks (-18 and -9 dB). Sing or
play your loudest and raise that channel's **Trim** until the peaks land inside the band,
then set the mix with the faders and the overall loudness with **Main**. If the limiter's
Ceiling has to go far down before it does anything, the channels are too quiet: raise the
Trims, not the Ceiling.

Trim comes first in the channel, before the looper hears it, so a loop is recorded at the
trimmed level and comes back as loud as the playing it was made from. Set the Trims before a
song: moving one later does not move loops already recorded.

### Where a monitor takes its sends from

Press **Monitor 1** or **Monitor 2**. **From where in each channel** has three choices:

- **Before its processing:** the live sound with no gate, EQ or compressor (the loops are as they were recorded).
- **After its gate and EQ:** the tone, without the squeeze. Good for a wedge, where a
  compressor brings feedback closer.
- **After its compressors:** everything, as the main mix hears it. This is how it starts.

It is separate from **Before the faders**, which decides whether the channel's fader and mute
change the monitor. The choice is saved in the monitor set-up.

### Stereo channels (keys, a backing track)

Press the left channel of the two, then tick **Stereo with channel N** (the next channel
along). From then on the pair is one stereo channel: the left one pans hard left, the right
one hard right, and every fader, mute, send, EQ and compressor setting made on either is made
on both. Each keeps its own name and its own socket. On a mono output both sides are in both
speakers, so stereo keys still work on a mono PA. Untick it and both go back to the middle.
The two compressors work each side on its own (they are not tied together), which is the one
thing a true stereo channel would do differently.

In-ears in stereo use four returns: the starter monitor set-up **Two in-ears (stereo)** puts
Monitor 1 on returns 1 and 2 and Monitor 2 on returns 3 and 4.

### Sends that belong to a song

A song can set how much of a channel goes to a reverb or a delay. It can also say which
reverb or delay one of the mixer's four effects is and how that is set (next section). It
changes nothing else in the mixer: **the venue keeps** each effect's EQ, cuts and levels, and
the main output.

1. Load the song. On the **Mixer** tab press the channel's name (say **Vocal**).
2. Tick **Belong to this song, not the act**. The four reverb and delay sends are now marked
   **(song)**.
3. Set them.
4. On the **Songs** tab press **Keep these sounds for this song**. The song's file gets a line for each,
   such as `send = Vocal / Vocal plate = -6.0`.

When another song loads, the sends go back to what the act says, unless that song has lines
of its own. The **Default sounds** file may have send lines too: they apply to every song
that does not name that channel and effect itself. By hand, a line reads
`send = channel / reverb or delay = level in dB` (or `off`), with the channel and the effect by
name or by number (`channel 1`, `fx 2`).

### A reverb or delay that belongs to a song

Tom's decision, 27 Nov 2026: the vocal's delay and reverb and the acoustic's room live in the
mixer, after the looper, and a song that has its own delay time or its own tape echo keeps it.

1. Load the song. On the **Mixer** tab press the effect's name (say **Vocal delay**).
2. Tick **Belong to this song, not the venue**. The **Effect** box now reads **Effect (this
   song's)**.
3. Choose the delay or reverb and set its sliders.
4. On the **Songs** tab press **Keep these sounds for this song**. The song's file gets a line
   such as `mixer effect = Vocal delay / tapedelay / time = 121.30 / feedback = 19.60 / ...`.

When another song loads, the venue's own effect comes back, unless that song has a line of its
own. The **Default sounds** file may have these lines too. By hand a line reads
`mixer effect = which one / the effect's id / setting = value / ...`, with the effect by name
or number (`fx 2`); a setting left out takes the effect's own starting value. The ids the
mixer has are `plate`, `room`, `spring`, `tapedelay`, `analogdelay` and `digitaldelay`.

**Save to this song** and **Save as default**, under the tick box, do steps 2 to 4 for that one
effect in one press: the effect as it is now is written into the loaded song's file (or the
Default sounds file) and nothing else in the file changes.

**Back to default for this song**, beside them, is the way back: it takes the loaded song's own
line for that one effect out of its file, and nothing else, so the song follows the Default
sounds again (or the venue's own effect, where the Default sounds have none). Use it when a
reverb moves about too much from song to song. So the three buttons are: **Save to this song**
(this song gets its own), **Save as default** (every song without its own follows it) and
**Back to default for this song** (this song stops having its own).

Tom's split, 27 Nov 2026: **the song carries the effect's own settings** (which delay, its time,
how much reverb), and **the send level is the venue's choice**, set on the channel and saved
with the act. A song with no vocal delay has the mixer's delay with its **Mix** at 0, so it
stays dry whatever the send is. (`send =` lines in a song still work for a song that needs one.)

### Moving songs made before the mixer

**Move reverbs and delays to the mixer** on the Songs tab does it for every song file at once.
It first copies the whole songs folder to a folder beside it called **Songs backup** with the
date and time. Then, for each song that names its own presets for the vocal or the acoustic:

- the vocal's reverb that is on becomes that song's **Vocal plate** line, with the same settings;
- the vocal's delay that is on (not a delay throw) becomes that song's **Vocal delay** line;
- the acoustic's reverb that is on becomes that song's **Instrument room** line;
- the copies in the effects chains are switched off by `set` lines in the song. Presets are not changed.

A send that was off is opened at 0 dB, where the sound is what it was in the chain; save the
act to keep it. Chains with an amp or the bass in them are left alone, so electric delays stay
in their chain. Songs that follow the Default sounds follow it here too. Pressing it twice
changes nothing the second time. **Put the songs back** copies the newest backup back. The
report lists what moved, song by song, on lines starting **Moved to the mixer**.

Which mixer effect is which is worked out from names: a channel with "vocal" in its name goes
to the reverb and the delay with "vocal" in their names, any other channel to the reverb
without. If no mixer effect has "vocal" in its name, the places the mixer starts with are used:
effect 1 for the vocal's reverb, 2 for the vocal's delay, 3 for the instruments' room. A mixer
effect that is switched off is switched on (save the venue to keep it on). A chain meets the
channel whose **Comes from** socket is the chain's socket, or failing that the channel with the
chain's name. The line under **What happened** and the report say how each chain met the mixer,
and why when nothing moved. With the mixer off the moved effects are not heard.

An effect set the same way in an effects chain and in the mixer sounds the same: send 0 dB,
return 0 dB, and the effect's own **Mix** does the same job in both places.

### How it starts

| Channel | Socket | Starts |
|---|---|---|
| Vocal | 6 | on |
| Acoustic | 7 | on |
| Stomp | 10 | on; compressor on |
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

Changing songs never touches any of them. A song's own sends (above) sit on top of the act
and are not saved in it.

The app writes a few starters the first time (it never overwrites a file that is there):
acts **Solo** and **Duo (partner sings and plays)**, venues **Starting point (flat)** and
**Own PA (limiter catches peaks first)**, monitors **Two wedges (mono)** and **Two in-ears
(stereo)**, and the output preset **Flat**.

The whole mixer is also remembered as it was left (`mixer.txt` beside the app's other
settings), so after closing the app or a power cut it comes back as it was.

## The venue knob (ENC 5)

Four buttons stand in for the board's fifth knob until the board exists: **Turn left**,
**Turn right**, **Push** and **Hold**. The line beside them is what the board's main screen would show.

**Hold** hands the knob to the break music (turn = its level, push = fade it up or down) and a
second hold hands it back; it also comes back by itself when playing fades the music down. The
knob always rests on the venue list, and everything below is unchanged. See `docs/break-music.md`.

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
the amp up as it turns down. Its settings are **Pedal position** (heel 0, toe 100), **Quietest level**
(how quiet it gets backed right off; all the way down is silence. It is a limit, not the pedal: sliding it does nothing while Pedal position is at 100) and **Reversed**.

The blend: put a Volume pedal in the electric chain as it is, and one in the acoustic chain
with **Reversed** on. Both follow the one pedal, so toe down is all electric, heel down is
all acoustic, and between is a blend. Set **Travel** to **Blend (even loudness)** on both and
the two follow matching curves, so the overall loudness stays about the same all the way
through the travel (in the middle each is 3 dB down). Until a real pedal is wired in, moving
the **Pedal position** slider of either one moves both (`EffectsRack::moveExpressionPedal`, which is
where a real expression pedal will arrive).

### A real expression pedal (27 Nov 2026)

An expression pedal that arrives as MIDI now works the blend, and the wah or whammy under the
toe switch, exactly as the **Pedal position** sliders do. On the **Pedal (RC-600)** tab: plug
the pedal in (into the RC-600's expression socket, or any MIDI device), click **Learn pedal**,
rock the pedal fully heel to toe twice, and click **Done**. The line beside the button says
which controller it found and where the heel and toe are. **Swap heel and toe** turns it
round; **Forget pedal** forgets it. Foot switches are never taken for a pedal (a pedal sends
many different values, a switch two). What it learned is kept in `expression-pedal.txt` in the
app's settings folder. Code: `src/ExpressionPedal.*`, checks in `src/ExpressionSelfTest.cpp`.

### The toe switch

On the Effects tab, give a switch the action **toe switch** (Tom's choice for now: **CTL4**)
on the electric chain. One press does what the pedal's toe switch will do: the electric goes
to full, every other chain's volume pedal goes silent (the acoustic is muted), and the first
**wah** or **whammy** in the electric chain switches on and follows the pedal. Press again:
the wah or whammy goes off and the pedal is the blend again, from where it sits. In a song
file the words are `switch CTL4 = chain 2 / toe switch`.

## Not built yet

- The pedal's real toe switch (it needs the board; CTL4 stands in). The pedal itself works now, over MIDI.
- Partial recall (only a venue's room EQ, say), and backing presets up.
- Virtual soundcheck and the Recordings tab (copy to USB, delete).
- More than two monitor mixes (Tom: two is enough), spare sends for delay speakers, stereo
  channels whose compressors are tied together, a stereo main mix by default (mono for now).
- Looping a duo partner (Tom wants the option later): today a loop returns to the channel of
  the socket it was recorded from, so it already works for any socket a track records.
- Remote preamp gain: that needs the real interface.
- The tablet's simple everyday view. This tab is the workstation view.

## For developers

| File | What |
|---|---|
| `src/Mixer.h/.cpp` | The mixer core, presets, the venue knob's rules, the report. No screen. |
| `src/MixRecorder.h/.cpp` | Writes the WAV files off the audio thread. |
| `src/effects/mixer/MixerBlocks.*` | The mixer's own blocks: `mixeq`, `mixcomp`, `notches`, `limiter`, `linedelay`, `wetfilter`. They follow the Effect interface, so the standard effect checks run on them, but they are listed apart (`mixerEffectTypes()`) so the Effects tab does not offer them. |
| `src/effects/volume/` | The volume pedal effect. |
| `src/effects/la2a/`, `warmth/`, `tape/`, `buscomp/` | The studio-style blocks (ordinary effects, group "Studio"). |
| `src/MixerPanel.*` | The Mixer tab. |
| `src/MixerSelfTest.cpp`, `src/effects/mixer/MixerBlockChecks.cpp` | The checks. |

- `AudioEngine::processBlock` asks `Mixer::startBlock`. When the mixer is mixing,
  `Mixer::stripChannels` runs every channel's own processing first and hands the looper each
  socket as its channel sounds after that processing. The looper then
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
  `direct`, `link`, `lowcut`, `highcut`, `return`, `level`, `mono`, `prefader`, `tap`, `pair`),
  the record words (`none`, `raw`, `effects`, `looper`), the tap words (`pre`, `eq`, `all`),
  the `[song sends]` section of `mixer.txt`, and the block and setting ids.
- A channel's blocks run as two chains (`Chan::strip`, the tone blocks up to `stripSplit`, and
  `Chan::strip2`, the compressors) so a monitor can tap between them. The block order in the
  files is one list all the same.
- A song's sends are `Mixer::setSongSends` (from `resolveSends` in `src/SongBook.cpp`): an
  overlay read through `Mixer::sendDb`, never written into the act.
- Screenshots in the cloud: `GIGRIG_TAB=7` opens on the Mixer tab and `GIGRIG_MIXER_STRIP=12.3`
  chooses strip 12 (the main output) with its fourth block shown.
