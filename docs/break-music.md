# Break music

Built 27 Nov 2026. The music between sets, played by the rig itself, so no phone or laptop
has to be plugged into the PA. This is the guide; the design doc section is "Intermission /
break music". **Keep this page true when the code changes.**

## What it does, in short

- It is a stereo channel of its own. It uses **none of the interface's inputs**.
- It plays **your own mixes**: any WAV, FLAC, AIFF, OGG or MP3 file in
  `Documents/GigRig/Break music`. A mix plays round and round until you stop it.
- **It fades down by itself when you start playing** (a foot switch press, by default).
- **It only ever comes back up by hand.** Nothing brings it back by itself, ever.
- While it is faded down it keeps playing silently, so at the next break it is somewhere new.
- It is **never in a loop, a recording or a monitor**.
- **Nothing plays until somebody presses Play.** An app that has just started is silent.
- Each mix remembers where it was stopped, even after the app is closed.

## Trying it (10 minutes)

1. Open the app and start the audio on the first tab as usual.
2. Click the **Break music** tab (the last one).
3. Click **Open the break music folder**. Copy one or two long music files into the folder
   that opens. Close that window.
4. Back in the app, the files are in the **Play** list by name. Choose one and click **Play**.
   It fades in over four seconds.
5. Move **Level** until it suits the room. The level is remembered.
6. Press any foot switch on the RC-600. The music fades down over two seconds and the tab
   says it was faded by playing.
7. Wait as long as you like. It stays down.
8. Click **Bring the music up**. It comes back over four seconds, further on in the mix.
9. Click **Stop**. Click **Play** again: it carries on from where it stopped. **Play from the
   start** begins the mix again.

## Where the sound comes out

| The mixer is | The break music comes out of |
| --- | --- |
| **On** | The **main output only**. It joins after the main mix has been recorded, so it is in no recording, no loop and no monitor. It follows the main output's level and mute. It does **not** pass through the venue's EQ or limiter (see the open questions). |
| **Off** | A pair of returns you choose on the tab: **With the mixer off it plays on**. The usual choice is returns 5 and 6, the CQ's USB stereo channel. Bring that channel up on the CQ. |

## What counts as "started playing"

Three tick boxes on the tab, under **Fade down by itself when**:

| Tick box | Starts ticked | What it means |
| --- | --- | --- |
| **a footswitch is pressed** | Yes | Any press of any RC-600 switch, CTL3 or CTL4. |
| **a song loads** | No | A song loaded by hand or by a program change from Songbook Pro. |
| **an input is louder than** | No | Sound on any input socket above the level on the slider beside it (start with -30 dB). Leave this off if a microphone is open in a noisy room, or the crowd will fade your music. |

**Fade down takes** and **Fade up takes** set how long each fade lasts.

## The knob (ENC 5)

On the finished board the fifth knob does this. Until then the four buttons marked **Knob
(ENC 5)** on the Break music tab, the same four on the Mixer tab, and the ones on the phone's
Tools screen stand in for it.

- The knob **rests on PA / venue**: turning browses venue presets, a push loads one, exactly
  as before.
- **Hold** hands the knob to the break music. Now a turn is the level (one dB a click; turning
  it up also brings faded-down music back) and a push fades the music up or down. A push while
  nothing is playing starts the mix that played last.
- **Hold** again hands it back to PA / venue. It also goes back by itself when your playing
  fades the music down, so mid-set the knob is always the venue knob.

## The phone (Bluetooth), on the rig computer only

On the Linux rig computer the **Play** list has one more entry: **Phone (Bluetooth or
Wi-Fi)**. It plays whatever a phone sends, with the same fades and the same rules.

**This part is not proven on real hardware yet.** It was written without a rig to try it on.
Try it at home first, never for the first time at a gig.

1. On the rig computer open a Terminal and run `~/gig-rig-code/linux/break-music.sh`. Type the
   password when asked. It ends with four steps; they are the same as steps 2 to 5 here.
2. Open **Settings**, then **Bluetooth**, and leave that window open. While it is open, phones
   can find the computer.
3. On the phone, open Bluetooth settings and tap the computer's name. Accept on both.
4. In the app, **Break music** tab, choose **Phone (Bluetooth or Wi-Fi)** and click **Play**.
5. Play something on the phone, with the phone's own volume up full.

`linux/break-music.sh --status` says what is running and whether a phone is connected.
`linux/break-music.sh --remove` takes it all out again. What it changes is listed at the end
of `docs/linux-setup.md`.

Wi-Fi streaming is not set up by the script. AirPlay is the usual way and Tom's phone is not an
iPhone, so it waits for a decision (see the open questions).

## What the report says

**Copy report** has a **Break music** part: the folder and how many mixes are in it, what is
playing and where it is up to, the fade times and which kinds of start are ticked, how many
times playing has faded the music down, how many times the music ran dry (it should be 0),
which returns it uses with the mixer off, and on Linux whether the phone's pipe is there.

## Open questions for Tom

- Should the break music pass through the venue's EQ and limiter? Now it does not: it joins
  after them, so that the main recording stays free of it.
- Which kinds of "started playing" should be ticked by default? Now only the foot switch.
- Bluetooth only, or Wi-Fi as well, and from which phone?

## For developers

| File | What it is |
| --- | --- |
| `src/BreakMusic.h/.cpp` | The player (`BreakMusic`) and the knob (`VenueKnob`). No screen. |
| `src/BreakMusicPanel.*` | The Break music tab. |
| `src/BreakMusicSelfTest.cpp` | The checks (`GIGRIG_SELFTEST_ONLY=break` runs only these). |
| `linux/break-music.sh` | The Bluetooth receiver on the rig computer. |

- **Threads.** A background thread reads the file (or the phone's pipe), brings it to the
  interface's sample rate (four-point interpolation) and fills a two-second queue. The audio
  thread only empties the queue and fades: no file, no lock, no memory allocation.
- **Where it joins.** `AudioEngine::processBlock` calls `BreakMusic::render`. Mixer on:
  `Mixer::setBreakFeed`, added to output 0 after `rec.write` of the main mix. Mixer off: added
  to the chosen pair of returns. **Mixer off with nothing playing must stay sample for sample
  what the app did before** (the first mixer check).
- **The phone.** A named pipe, `~/gigrig/break-music.pipe`: raw 16-bit little-endian stereo at
  44.1 kHz and nothing else. The app nudges its reading speed by up to half a percent to follow
  the phone's clock.
- **Memory.** `~/.config/GigRig/break-music.txt` (`%APPDATA%\GigRig` on Windows): the level,
  the settings, the last mix and each mix's place. Its keys are saved on Tom's disk: never
  rename them, only add.
- **The remote screen** has the same controls (`break.*` and `knob` orders, `docs/remote.md`).
