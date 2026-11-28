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

Two things to know:

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

## Not built yet

- Virtual soundcheck (playing a recording back into the mixer).
- Copying recordings to a USB drive and clearing them from inside the app.
- Listening to the recording mix on headphones before recording it.
- The recorder holds 16 files at most (eight channels, four effects, two mixes).

## For developers

| File | What it is |
| --- | --- |
| `src/MixRecorder.*` | The files. `write` (audio thread) only queues; a background thread writes. |
| `src/Mixer.*` | The tap points (`RecordPoint`), `MixRecordSetup`, the recording mix bus (`RecOut`, only worked on while `rec.wants (recSlotMix)`), `recordTracks`, `recordRoom`, `recordWithoutMixing` (mixer off), `recordingReportText`. |
| `src/RecordingPanel.*` | The Recording tab. |
| `remote/screens.js` (`GR.screens.record`) | The Record screen. Orders `rec.start`, `rec.stop`, `rec.set`, and `mix.ch`, `mix.fx`, `mix.param` for the rest. |
| `src/MixerSelfTest.cpp`, `src/RemoteSelfTest.cpp` | The checks ("recording:", "recording mix:", "recorder:"). |

Saved words, never to be renamed: record points `none`, `raw`, `effects`, `looper`,
`processed`; channel keys `record`, `reclevel`, `recpan`; effect key `reclevel`; the sections
`[recording]` (`mainmix`, `mix`, `effects`, `level`) and `[recording processing]`. The file
name words `raw`, `after effects`, `after looper` are in recordings Tom already has.
