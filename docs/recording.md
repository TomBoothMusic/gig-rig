# Recording

Built 28 Nov 2026. The rig records itself: every input as its own file, a stereo file of the
main mix, and a second stereo mix made only for the recording. It is the **Recording** tab in
the app and the **Record** screen on the phone or tablet (`docs/remote.md`); both work the same
recorder, and what one changes the other shows.

## What it can never do

- **It cannot change what anyone hears.** The recorder only copies sound that is already
  there. A check in the self-test runs two rigs side by side, one recording and one not, and
  every output must match to the sample.
- **It cannot make the sound stutter.** The sound part of the app only drops each block of
  samples into a queue in memory. A separate helper writes the queue to the drive. If the drive
  is slow, the helper falls behind, samples are left out of the *files*, and the screen and the
  report say how many. The sound carries on untouched.
- **It cannot fill the drive.** It will not start with less than 1.5 GB free, and a recording
  that is running stops by itself, cleanly, with 1 GB left. The screen says how many hours fit.

## Pressing Record

**Record** starts a new folder in `Documents\GigRig\Recordings` named by the date and time
(on the rig computer, `~/Documents/GigRig/Recordings`). Every file in it starts at the same
moment and is the same length, so they line up in any recording program. Files are WAV, 24-bit,
at 48 kHz even when the app runs at 96 kHz (Tom's decision, 20 Nov 2026). The folder also gets
`About this recording.txt`, which says what each file is. **Record** again stops.

Storage: one single (mono) file takes about 0.52 GB an hour and a stereo file twice that. Five
channels and both mixes come to about 4.7 GB an hour.

## Where each channel is recorded (its tap point)

Each mixer channel chooses one. In the file names they are the words in brackets.

| Choice | File name says | What is in the file |
| --- | --- | --- |
| Nothing | (no file) | |
| The plain socket | `raw` | The input itself, before any effect. Anything can be redone from it. |
| After its effects | `after effects` | After the guitar or vocal effects chain, before the looper. No loops. |
| After the looper | `after looper` | The live sound and its loops together, before the channel's own gate, EQ and compressors. This is what a channel starts on. |
| After the looper, with its EQ and compressors | `after channel processing` | The live sound and its loops together, after the channel's gate, EQ and compressors, before its fader. |

Three things to know:

- **Only a channel that is switched on is recorded.** On a solo act the two duo channels are
  off. To record the duo partner, load the **Duo** act, or switch the channels on: on the
  phone, **Record**, **Channels**, then tap the channel under **Switched off**; in the app,
  the **Mixer** tab, the channel, **In use**. A channel that is switched on joins the mix too.
  On the phone, the channel's own page has **Switch this channel off** to put it back.
- A loop is recorded into the looper after the channel's processing (Tom's decision, 27 Nov
  2026), so the loops inside an "after looper" file already carry the EQ and compressors they
  were played with. Only the live sound in that file is without them.
- The mixer's reverbs and delays come after all of these, so no channel file has reverb in it.
  To keep the reverbs, tick **Reverbs and delays**: each one is written as a file of its own
  (only its wet sound, at the level it has in the main mix), ready to sit under the dry channels.

## The mixes

| Tick box | File | What it is |
| --- | --- | --- |
| **Main mix** | `Main mix.wav`, stereo | Exactly what the PA was sent: the venue's EQ, limiter and level included. If the main output is set to mono, both sides are the same. |
| **Recording mix** | `Recording mix.wav`, stereo | The second mix, below. |
| **Reverbs and delays** | `Effect 1 Vocal plate.wav` and so on | Each mixer effect that is on, alone. |
| **A raw copy of every socket** (on to begin with) | `Vocal (raw for soundcheck).wav` and so on | Each socket in use, plain, before any effect. This is what a virtual soundcheck plays back. A channel already recorded raw needs no second file. |

Break music is never in any of them.

## The recording mix

A second balance of the same channels that only ever goes into a file. The room and the
in-ears never hear it. It is for a recording that sounds right on a phone or in the car, where
the live balance does not.

- **Balance:** each channel is this much louder or quieter than it is in the main mix. At 0 dB
  it is the same. It still follows the channel's fader and mute, so a song played quietly is
  quiet in the recording too. All the way down leaves the channel out. Each reverb and delay
  has the same control.
- **Pan:** where each channel sits, left to right, in the recording only. The recording mix is
  always stereo, even when the main output is mono. A stereo pair of channels sits hard left
  and right.
- **EQ, bus compressor, limiter:** the recording mix's own. The EQ is its own curve (low cut,
  low shelf, four bands, high shelf, high cut). The limiter starts switched on so a loud chorus
  cannot clip the file. The bus compressor starts off.
- **Level:** the whole recording mix into its file.

All of this can be moved while recording and is heard in the file from that moment. The
recording mix costs no processing at all unless it is being recorded.

It is saved with the **act** (it is a balance of that act's channels). An act saved before
this build has no recording mix in it and leaves the one in use alone.

## With the mixer off

The recorder still works, but there is less to record: each channel's socket, plain or after
its effects. The looper points fall back to "after effects", and there are no loops and no
mixes, because with the mixer off the app is not mixing. Switch the mixer on for those.

## In the report

The **RECORDING** part (inside the mixer's part) says whether it is recording, how long, where,
whether any samples were lost, the files Record would write now, the room left on the drive,
and each channel's record point and its place in the recording mix.

## Virtual soundcheck

Built 28 Nov 2026. A virtual soundcheck plays a recording's **raw** files back into the input
sockets they were recorded from. The rig cannot tell the difference between that and Tom
playing, so the effects, the looper and the mixer all work on it, and every change made is
heard as it would be live.

1. Open the **Recordings** tab (on the phone: **Record**, then **Recordings**).
2. Click a recording. Its line says how many sockets it can play, or "no raw files".
3. Click **Play as a soundcheck**. Click it again to stop.

What to know:

- Only raw files can be played, because anything recorded later in the chain already has
  effects in it. That is what the **raw copy** tick box on the Recording tab is for.
- While it plays, those sockets do **not** hear what is plugged into them. Every other socket
  stays live, so Tom can play along on something that was not recorded.
- **Round and round** (ticked to begin with) starts the recording again when it ends.
- Loops can be recorded from it, songs can be changed, the mixer can be worked on, and Record
  can be pressed, which mixes an old take again with today's settings.
- The files are 48 kHz and are brought back up to the rig's rate as they play.
- A drive too slow to keep up gives a moment of silence, counted in the report, never a wait.
- With nothing playing it changes no sample (a check).

## Loops, kicks and switches in a soundcheck

Built 28 Nov 2026 (Tom: "I'd love to be able to play the soundcheck back with loops but tweak
the processing according to the venue"). The loops themselves are **not** saved for this: a
saved loop already has the EQ and compressors of the night in it. Instead:

- **While a recording is made, every looper press is noted**, to the exact sample, in the
  recording's folder (`Looper presses.txt`), along with the song that was loaded and any song
  loaded later.
- **While that recording plays as a soundcheck, the same presses are made again** at the same
  samples. The looper builds every loop afresh out of the raw playback, through the effects
  and the mixer as they are set now. Change the venue EQ and the next time a loop is recorded
  it is recorded through the new setting, exactly as at the gig.
- **The rest of what the feet did is noted and done again too** (Tom, 28 Nov 2026: "I'd need
  all of those things as well"), in the same file:
  - **Kicks from the kick pad** (the SPD::ONE, the board's own pad on the controller, or the on-screen buttons), each with how hard it
    was hit, on the same block of sound it was played on. The kick is made afresh, so a change
    to the kick's sound or its channel is heard.
  - **Effect switch presses** (a switch the Effects tab or the song has taken from the looper:
    an effect or a chain on and off, A/B, a held whammy, wah, delay throw or harmony, tap
    tempo, the next amp sound, the toe switch), each within a hundredth of a second.
  - **Expression pedal moves**, looked at a hundred times a second.
  - **Where the switches and the pedal stood when the recording began.** The soundcheck puts
    them there before it plays (and again each time round), and puts them back where they
    were when it stops.

What to know:

- It is the **With its loops, kicks and switches** tick box next to Play (ticked to begin
  with). Unticked, the soundcheck plays and the looper, the kick, the switches and the pedal
  are left alone.
- A loop that is already going round keeps the sound it was recorded with, as it does live.
  With **Round and round** the loops are cleared each time the recording comes back to its
  start and are built again, so a change is in every loop one pass later.
- The song the recording was made with is loaded first, unless it is the one loaded already
  (then it is left alone, with whatever has been changed in it). A song change during the
  recording is made again at the same place, as a program change from Songbook Pro would be.
  If the song is no longer in the Songs list, the soundcheck plays without presses and says so.
- A recording that began with loops already going cannot have them built again, so it plays
  without looper presses and says so (its kicks and switches are still made again). Clear the
  looper before pressing Record for a soundcheck take.
- The loops the soundcheck built are cleared when it stops.
- A recording made before this build has no presses noted, and plays as before.
- A switch is only pressed again if it still does the same job as on the night. Change what a
  switch does in the song and the soundcheck leaves that switch alone.
- **Not made again:** kicks from a bare piezo pad on a socket (its knock is in that socket's
  raw file, so the soundcheck hears it and plays the kick by itself); the test take; and
  anything changed by hand on a screen during the recording (a slider, a preset). "The next
  amp sound" steps on from whichever amp is chosen now, so choose the amp the take began with.
- The best soundcheck take is short: one song, started with the looper clear. Tick **Keep** on it.

## The Recordings tab

Every recording on the rig's drive, newest first, with its length, size and whether it is kept,
copied to USB and playable as a soundcheck.

| Button | What it does |
| --- | --- |
| **Play as a soundcheck** | See above. |
| **Keep** | A kept recording is never removed by the Clear buttons, and Delete refuses it. Tom's plan (20 Nov 2026) is one soundcheck recording per act kept for good. |
| **Delete this recording** | Asks first. Gone for good. |
| **Copy this one to the drive** / **Copy all that are not copied yet** | Copies into a folder called `GigRig recordings` on the USB drive chosen, then reads every file back and compares it. Only then is the recording marked "copied to USB". |
| **Clear the copied ones** | Deletes every recording that is copied and checked, except kept ones. |
| **Clear all but the kept ones** | Deletes every recording that is not kept, copied or not. |

Copying is refused while recording or while a soundcheck plays, and it asks first, because a
USB drive at work made the sound click on the laptop (20 Nov 2026). **Only copy when nobody is
listening.** A recording that is being recorded or played cannot be deleted or cleared.

Two small files in a recording's folder are the memory of this: `Keep.txt` and
`Copied to USB.txt`. Deleting either by hand undoes it.

## Not built yet

- Listening to the recording mix on headphones before recording it (Tom, 28 Nov 2026: not needed).
- Pausing the sound outright during a USB copy (the earlier plan); this build refuses and warns instead.
- Choosing where in a recording a soundcheck starts.
- Making the kick pad's kicks, effect switch presses and expression pedal moves again in a soundcheck.
- The recorder holds 24 files at most.

## For developers

| File | What it is |
| --- | --- |
| `src/MixRecorder.*` | The files. `write` (audio thread) only queues; a background thread writes. |
| `src/Mixer.*` | The tap points (`RecordPoint`), `MixRecordSetup`, the recording mix bus (`RecOut`, only worked on while `rec.wants (recSlotMix)`), `recordTracks`, `recordRoom`, `recordWithoutMixing` (mixer off), `recordingReportText`. |
| `src/RecordingPanel.*` | The Recording tab. |
| `src/Soundcheck.*` | Virtual soundcheck. A reader thread fills a queue; `feed` (audio thread, first thing in `AudioEngine::processBlock`) swaps the sockets' inputs for it. |
| `src/RecordingLibrary.*` | The recordings on the drive: list, keep, delete, clear, copy to USB (its own thread). |
| `src/RecordingsPanel.*` | The Recordings tab. On the phone: the Record screen's Recordings pages, orders `check.*` and `recs.*`. |
| `src/LoopReplay.*` | Loops, kicks and switches in a soundcheck. `noteBlock` (audio thread) queues what the looper acted on and the kicks the pad started while recording, `noteSwitch` and `poll` (a hundred times a second) note effect switch presses and pedal moves, and the message thread writes `Looper presses.txt`; `pressesFor` hands the looper a soundcheck's presses with their samples (`Looper::setTimedPresses`) and the kick pad its kicks (`KickPad::hitNow`); `poll` presses the switches (`EffectsRack::takePedalNow`) and moves the pedal. |
| `src/RecordingsSelfTest.cpp` | Their checks ("soundcheck:", "recordings:"); `GIGRIG_SELFTEST_ONLY=recordings` runs them alone. |
| `remote/screens.js` (`GR.screens.record`) | The Record screen. Orders `rec.start`, `rec.stop`, `rec.set`, and `mix.ch`, `mix.fx`, `mix.param` for the rest. |
| `src/MixerSelfTest.cpp`, `src/RemoteSelfTest.cpp` | The checks ("recording:", "recording mix:", "recorder:"). |

Saved words, never to be renamed: record points `none`, `raw`, `effects`, `looper`,
`processed`; channel keys `record`, `reclevel`, `recpan`; effect key `reclevel`; the sections
`[recording]` (`mainmix`, `mix`, `effects`, `soundcheck`, `level`) and `[recording processing]`. The file
name words `raw`, `after effects`, `after looper` are in recordings Tom already has. So are the
`soundcheck: <file> = socket <n>` lines of `About this recording.txt`, and the marker files
`Keep.txt` and `Copied to USB.txt`. So is `Looper presses.txt` and its lines (`rate =`, `start: song = ..., loops = none`,
`<sample>: switch <n> down|down double|up|hold`, `<sample>: clear all`, `<sample>: song = <name>`,
`<sample>: kick <strength>`, `<sample>: effect switch <n> down|up`, `<sample>: pedal <position>`,
`switch at start: <n> chain <c> action <a> = on|off`, `pedal at start = <position>`).
