# Gig Rig

Software for Tom's custom all-in-one gig rig: one box to replace the pedalboard,
the Boss RC-600 looper and the Allen & Heath CQ12T mixer. The design and the
staged plan live in the project's design doc; this repository holds the code.

The app is built in stages. Each stage adds to the same app, `GigRig`.

| Stage | What the app does | Status |
| --- | --- | --- |
| 0 | Device check: lists every audio and MIDI device, and says whether it can see the CQ mixer and the RC-600 | Built |
| 1 | Audio through the CQ: a round-trip latency meter, buffer-size choice, play-through with a dropout watch, and a test tone for each return | Built |
| 2 | The RC-600 as the pedal: every switch by name, and press, 2-second hold and double-press told apart | Built |
| 3 | The looper: six tracks that copy how the RC-600 loops, driven by the RC-600's switches, for a side-by-side test | Built, first version |
| 4 onward | Songs from the RC-600 backup, effects, tablet app | Planned |

The rig computer itself runs Linux. [docs/linux-setup.md](docs/linux-setup.md) sets one up with a
single script, including a free trial on the laptop from a USB stick.

## Getting the app (no tools needed)

1. Open the [latest release](https://github.com/TomBoothMusic/gig-rig/releases/latest). You need to be signed in to GitHub, because the repository is private.
2. Under **Assets**, click **GigRig-Windows.exe** to download it.
3. Double-click the downloaded file. The first time, Windows shows a blue "Windows protected your PC" box, because the app is home-made and not signed. Click **More info**, then **Run anyway**.

Nothing is installed. The app is a single file; delete it to remove it.

## Stage 1: the latency and audio test

The app opens on the **Latency and audio test** tab and starts the CQ by itself.
The second tab, **Device check**, is the stage 0 screen.

The rig sends eight signals back to the mixer, called **returns**. On the CQ12T
they arrive like this:

| Return | Arrives on the CQ at |
| --- | --- |
| 1 | Channel 9 |
| 2 | Channel 10 |
| 3 and 4 | ST1, left and right |
| 5 and 6 | The USB stereo channel, left and right |
| 7 and 8 | The Bluetooth stereo channel, left and right |

### Before you start

1. Close Reaper and any other audio program. The CQ's driver only lets one program use it at a time.
2. Switch the PA speakers off, or unplug them. The test plays short chirps through the main outputs.
3. On the CQ, go to **CONFIG**, then the **USB/SD** tab, and check: **Sample Rate** 48 kHz, **Stream Mode** Multitrack, **Source Point** Post Preamp.

### Part A: measure the latency (about 5 minutes)

1. Take an ordinary microphone cable. Plug one end into **MAIN OUT L** on the back of the CQ and the other end into the XLR socket of **input 10**. Any spare input from 1 to 10 works; the app finds out which one by itself.
2. On the CQ, for that input channel: make sure **48V** is off, turn its **Gain** right down, and pull its **fader right down** (use the fader, not mute). That stops the sound going round in a circle inside the mixer.
3. On the CQ, find the **USB** stereo channel (in the stereo inputs bank). Unmute it and set its fader to 0 dB. Set the main fader to 0 dB.
4. Open the app. The box near the top should say **READY** and "Running: 48 kHz".
5. Click **Measure every size, 8 to 256**. You will hear about 36 short chirps over 45 seconds or so. Leave everything alone until the box changes.
6. The box now shows the round trip at each buffer size. Click **Copy report** (top right) and paste the result to Claude.

If the box says **NOT HEARD**, the chirp is not reaching the main output: check step 3. If it says **NO CABLE**, the chirp went out but did not come back through the cable: check steps 1 and 2.

### Part B: play through it for 30 minutes

1. **Unplug the loopback cable first.** Leaving it in while play-through is on can howl.
2. Plug everything into its usual inputs (any of inputs 1 to 10). On the CQ, pull the faders for channels 1 to 10 right down, so the only sound you hear is the copy that has been through the computer. Use the faders, not mute.
3. In the app, choose the **Buffer** size to test (start with the smallest one that showed no dropouts in part A).
4. Set **Play-through** to **Inputs 1 to 10 mixed to the CQ's USB stereo channel**.
5. Switch the speakers back on, at a low level to begin with, and play for 30 minutes. Listen for clicks or gaps, and glance at the **Dropout watch** line now and then.
6. Click **Copy report** and paste it to Claude, with a note on anything you heard.

If the sound ever sticks at full level, the app switches play-through off by itself and says so.

### Part B2: the glitch check (leave it running, no playing needed)

Some glitches happen in the driver or the USB link, where the Dropout watch cannot see them. The glitch check catches those too. It sends a steady two-note tone out to the mixer, listens for it coming back, and counts every break in it.

1. No cable is needed. On the CQ, keep the **USB** stereo channel's fader up, and pull the faders for channels 1 to 10 right down.
2. Turn your speakers down or off if you do not want to listen to the tone. The check works either way.
3. In the app, click **Glitch check**. Within a couple of seconds the line beside it should read **Tone watched**, with a running time and **Glitches: 0**.
4. Leave it for 30 minutes and use the laptop as you normally would at a gig.
5. Click **Copy report** and paste it to Claude. The report lists how many glitches there were and when each one happened.
6. Click **Stop glitch check** when you are done. Play-through is silent while the check runs and comes back when you stop it.

If the line says the tone is not coming back cleanly, check the USB stereo channel is not muted and its fader is up.

### Part C: check each return arrives where it should (about 10 minutes)

This part changes where some CQ channels take their sound from, so **save your scene on the CQ first** and recall it afterwards.

1. On the CQ, go to **CONFIG**, then **INPUTS**. Set the **Input Source** of channels 9 and 10, ST1 and the Bluetooth channel to **USB**.
2. In the app, click the **Test tone on return** buttons **1** to **8**, one at a time. Each plays a steady tone on one return; click the button again to stop it.
3. For each button, look at the CQ's meters and note which channel shows the tone. It should match the table above.
4. Tell Claude which ones matched, then recall your saved scene.

## Stage 2: the RC-600 as the pedal

The **Pedal (RC-600)** tab shows the RC-600's nine switches, and CTL3 and CTL4, laid out as they are on the pedal.

### Part A: every switch (about 10 minutes)

1. Plug the RC-600 into the laptop by USB and switch it on. On the RC-600 choose memory **65** (MIDI CTRL).
2. Open the app and click the **Pedal (RC-600)** tab. The box at the top should say **CONNECTED**. If it says **NO RC-600**, go to the **Device check** tab, click **Scan again**, and come back.
3. For each switch in turn: press it once, press it twice quickly, then hold it down for 2 seconds. Under the switch's name the words **press**, **hold** and **double** fill in green as the app sees each one.
4. **Double-press: two presses within** starts on **one beat of the song**, so two presses a beat apart (say on beats 3 and 4) count as a double-press at any tempo. With no loop running it uses the chosen song's saved tempo. You can pick a fixed time instead. Note which setting feels right.
5. When all nine are filled in, click **Copy pedal results** and paste them to Claude. (CTL3 and CTL4 only fill in if footswitches are plugged into the RC-600's CTL 3/4 socket.)

You can also click a switch on the screen with the mouse. That does the same as a foot on the real one.

### Part B: glitch check with the RC-600 plugged in (30 minutes, no playing needed)

This is the same glitch check as stage 1, run again now that the RC-600 shares the laptop's USB.

1. Leave the RC-600 plugged in on memory 65. Set the CQ up as decided in stage 1: 96 kHz, buffer 64, safe mode on.
2. On the **Latency and audio test** tab click **Glitch check** and leave it for 30 minutes. Press a few RC-600 switches now and then.
3. Click **Copy report** (top right) and paste it to Claude. The report now also says how many switch messages the RC-600 sent while the check ran.
4. Click **Stop glitch check**.

## Stage 3: the looper

The **Looper** tab is a six-track looper that copies how the RC-600 behaves, so the two can be compared. The RC-600's switches drive it.

How it keeps time:

- The first loop you record sets the length of a bar, and so the tempo. For one of your own songs the saved tempo is the guide to how many bars that loop was: a 2-bar loop played near the saved tempo is counted as 2 bars at that tempo, not 1 bar at half of it. You never have to match the saved tempo exactly.
- Every later track is a whole number of bars. If you press mid-bar it waits for the next bar to start recording, and when you press to finish it ends on the nearest bar.
- A track with a preset length (as in some of your RC-600 memories) finishes by itself.
- Each recording is shifted by the round-trip delay measured in stage 1, so a loop plays back exactly where you played it.

### Setting up (5 minutes)

1. Plug in as you would for a gig, with the instruments going into the CQ. You hear yourself through the mixer as normal; the app only adds the loops.
2. On the CQ, bring up the **USB** stereo channel to 0 dB. The loops come back on that channel.
3. RC-600 on USB, memory **65**.
4. In the app, on the **Latency and audio test** tab: set **Play-through** to off, make sure the glitch check is stopped, and click **Measure latency** once (no cable needed; you will hear a few chirps). This gives the looper its timing. The app remembers the result for this buffer size and safe mode setting, so it only needs doing again if you change one of those. If it has not been done, the Looper tab says **TIMING NOT MEASURED** in amber. It does not matter if a microphone or guitar hears the chirps from a speaker: the app spots that (it arrives too late to be a cable) and uses the copy inside the mixer instead.

   The app also keeps a note of every delay figure the CQ's driver has given. If the driver's delay is well above what it was before at the same setting, the Looper tab says **DELAY HIGHER THAN BEFORE** in amber, and the report lists the figures with the date each was first seen. Loops still line up when that happens; restarting the computer is the first thing to try.
5. Click the **Looper** tab. The line under the song box should name the CQ and say **Timing correction ... from the latency test**. If it shows an amber message instead, it says what to fix.
6. Check the **socket** boxes on the right match where things are plugged in (vocal 6, acoustic and stomp 7, electric 8, bass 9).

### Part A: the basics, with the song set to "Standard layout"

Switches: 1 = track 1 record/play, 2 = track 1 stop, 3 and 4 = track 2, 5 and 6 = track 3, 7 = bank, 8 = undo/redo, 9 = all start/stop. Holding a record/play switch for 2 seconds undoes; holding a stop switch clears the track. Holding the record/play switch from the very press that started a new recording throws that take away.

1. Play a simple 2 or 4 bar guitar part. Press switch 1 on the first beat and again on the first beat as it comes round. Track 1 turns green and the loop plays. Listen to the join: is it clean, and is the loop in time with where you played it?
2. Play along with the loop for a minute. Does it stay in time?
3. Press switch 3 a little before a bar, play a bass line, and press switch 3 again near the end of a bar. Is track 2 in time with track 1?
4. Press switch 1 again to overdub on track 1, play something, press again to finish. Hold switch 1 for 2 seconds to undo it, and hold again to bring it back.
5. Press switch 9 to stop everything and again to start everything. Everything stops the moment you press, wherever in the bar that is, and the second press starts every loop from beat 1 straight away without waiting for a bar line.
6. Click **Copy looper results** and paste them to Claude, with a note on what you heard at each step.

### Part A2: one track, two sources kept apart

A track that listens to two input sockets records each into its own lane. The lanes share one length and the same switch presses, so they cannot drift apart, but each can come back on its own mixer channel.

1. On the CQ, set the ST1 channel's Input Source to USB and bring its fader up, as well as the USB stereo channel.
2. In the **Song** box choose **Two sources per track**. In **Loops play on** choose **two-source tracks split: first on 3+4, second on 5+6**. Track 1 now hears vocal and acoustic, track 2 acoustic and bass. Its tile says "(kept apart)".
3. Sing and play a verse together. Press switch 1 at the start and again at the end.
4. On the CQ, pull the ST1 fader down: the looped vocal goes and the looped guitar stays. Bring it back and pull the USB stereo channel down instead: the guitar goes and the vocal stays. Are the two in time with each other, exactly as you played them?
5. Overdub on track 1 (switch 1 again), then hold switch 1 to undo: the overdub should come off both.
6. Click **Copy looper results** and paste them to Claude with what you heard.

### Part B: side by side with the RC-600

1. In the **Song** box choose one of your songs (they are read from your RC-600 backup): Dirty Slap, CCR Acoustic, Wicked Game, Dreams, Throw Arms, Boiz, Dancing Moon or Songbird. The switches, each track's inputs and any preset lengths are then the same as in that RC-600 memory. The panel at the bottom left lists them.
2. Play the song's loops the way you do on the RC-600. Note anything that feels different: a switch that does the wrong thing, a loop that ends in the wrong place, timing that feels early or late.
3. Click **Copy looper results** and paste them to Claude with your notes.

If loops sound slightly early or late against your playing, move **Timing trim** a millisecond at a time and say which value sounded right.

Not copied yet: the RC-600's effects, rhythm, memory up/down, and tap tempo. A switch set to one of those shows as "Effect (later)" or similar and does nothing.

## The effects, first batch: drives in your pedalboard

New in this build: an **Effects** tab and an **Amp capture** tab. The Effects tab starts
switched off, so the looper works exactly as before until you tick **Effects on**.

The app's drives go into your pedalboard in place of the OD200, like any other pedal. The
rest of your board, your Dream '65 and everything after it stay exactly as they are, so you
compare one drive against the other and nothing else changes.

### Setting up (about 10 minutes)

1. Unplug the lead going **into** the OD200 (or coming out of it, if you want the app's drives
   after it) and plug that lead into **CQ socket 10** instead. This is the signal the app's
   drives work on.
2. Run a lead from a free **CQ aux output** (for example Aux 1) back into the pedalboard, where
   the lead you just moved came from (the OD200's input, or the next pedal after it).
3. On the CQ:
   - Channel 10: turn it right down in the main mix and in every aux, so you only hear what
     comes back through the pedalboard. The USB send stays post preamp, as it is now. Set the
     channel 10 gain so the meter is healthy but well clear of red when you dig in.
   - Channel **ST1**: Input Source = USB (as for the looper). Processing off. Send it to that
     aux only, not to the main mix. Start with the aux output turned **well down**: it is a
     line-level output going into a pedal input.
4. In the app, open the **Effects** tab. Tick **Effects on** and **This chain on**. Input should
   say Socket 10 and Out to should say Returns 3+4 (ST1).
5. Switch the OD200 off and play. You should hear the Tube Screamer copy through the rest of
   your board. With every drive in the app switched off you should hear your normal clean
   sound: if it is louder or quieter than with the app unplugged, set the aux output level
   until it matches.

To compare with the OD200 by feel: switch the app's drives off and the OD200 on, and back.
The app adds about 9 ms at this point in the chain (the mixer's USB round trip); say if you
can feel it.

### Part A: try the drives (as long as you like)

- The row of boxes is the chain, in order: Tube Screamer, Blues Driver, Klon and
  Marshall in a box. Tick **On** under each to switch it in or out.
- Click a box to see its knobs below. They work like the pedal's knobs while you play.
  Double-click a slider to put it back to its starting point.
- **Move left** and **Move right** change the order. **Add** puts in another effect: the new
  **Fuzz Face style** fuzz, the Clean boost, or the same drive twice. **Remove** takes one out.
- To use a drive as a boost: gain or drive down, level up.
- **Effects / Dry** flips between the chain and your plain guitar. **Level match** (play for
  8 seconds) makes the chain as loud as the dry sound, so the comparison is fair.
- When you like a sound, type a name under **Presets** (for example "Blues") and press
  **Save as**. **Load** brings it back.
- Optional: under **RC-600 switches for the effects**, choose CTL4 and an action (for example
  slot 2 on/off) to switch it with your foot. That switch stops working for the looper
  while the effects are on.
- If you want Claude to hear a sound, press **Capture**, play for up to 3 minutes, press it
  again, and send the WAV from Documents\GigRig\Effect captures.

### Part B: capture your Dream '65 settings (3 minutes each)

For this part the app plays a test sound into the Dream instead of the guitar. Move the
aux lead from step 2 into the **Dream's input**, and plug the **Dream's output** straight into
**CQ socket 8** in place of the pedalboard's lead (so the RC-600 and anything after the Dream
are left out). Then open the **Amp capture** tab and follow the six steps on screen, once for
each Dream setting you use (edge of breakup, and the sparkly clean one with the vibrato
channel). Put the leads back afterwards. Send Claude the WAV
files and the .txt files beside them from Documents\GigRig\Amp captures. Claude turns each
one into an amp model for the next build.

### Part C (optional): the Bass9 copy

The Bass9 copy built on 20 November is now one of the effects. To try it: pick **Chain 2**,
tick **This chain on**, set Input to the socket with the Bass9's DRY OUT, choose
**Bass9 PRECISION copy** and press **Add**. Set the reference to the socket with the real
Bass9 and use **Effects / Reference** to compare.

### What to send Claude

Press **Copy effects results** on the Effects tab and paste it, with what you liked and
didn't. Mention which drives, which order and which settings.

## The Device check tab

1. Plug the CQ12T into the laptop by USB and switch it on. Do the same with the RC-600.
2. The app scans by itself when it opens; **Scan again** rescans after plugging something in.
3. The two boxes at the top say whether the CQ is there with all 16 inputs and 16 outputs, and whether the RC-600 is there as a MIDI device.
4. The bottom panel shows any MIDI that arrives, live. With the RC-600's MIDI CTRL memory (slot 65) selected, each switch shows as CC 20 to 30.

**Copy report** copies the device list and the stage 1 results together.

## How the builds work

Every change to the code is built automatically by GitHub (the **Actions** tab) for
Windows and Linux. Each build also runs the app's self-test and a real device
scan, and the Linux build is installed on a fresh Ubuntu with the rig's set-up script. Changes that reach the `main` branch are published on the
[Releases page](https://github.com/TomBoothMusic/gig-rig/releases) as a new numbered build.
The build number shows in the app and at the top of every report.

## For developers

C++20, [JUCE](https://juce.com) 9 (fetched by CMake), CMake 3.22 or newer.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The app lands in `build/GigRig_artefacts/Release/`. On Linux, the packages
needed are listed in `.github/workflows/build.yml`.

```
GigRig --self-test result.txt   # run the built-in checks, no window
GigRig --report report.txt      # write the device report, no window
```

| Path | What it holds |
| --- | --- |
| `src/AudioEngine.*` | Opens the audio device, passes audio through, runs latency measurements and counts dropouts |
| `src/LatencyProbe.*` | Plays a chirp, records every input and finds how many samples later it came back |
| `src/GlitchWatch.*` | Sends a steady two-note tone, cancels it from what comes back and counts every break |
| `src/AudioPanel.*` | The stage 1 tab; remembers the audio device, rate and buffer for the next start |
| `src/Pedal.*` | The RC-600's switches: CC numbers to switch names, and press, hold and double-press |
| `src/PedalPanel.*` | The stage 2 tab, and the on-screen switch used by both new tabs |
| `src/Looper.*` | The looper core: tracks, bar timing, overdub, undo, and the RC-600's switch functions and linked actions |
| `src/TestSongs.cpp` | Song set-ups for the side-by-side test, taken from the RC-600 backup |
| `src/LooperPanel.*` | The stage 3 tab |
| `src/LooperSelfTest.cpp` | Checks for the pedal and the looper, with loop timing checked to the sample |
| `src/DeviceReport.*` | Scans audio and MIDI devices and judges whether the CQ and RC-600 are present |
| `src/MidiMonitor.*` | Listens to every MIDI input and describes what arrives |
| `src/DeviceCheckPanel.*` | The stage 0 tab |
| `src/MainComponent.*` | The window: title, Copy report and the tabs |
| `src/Style.*` | Colours and the small shared widgets |
| `src/SelfTest.*` | Checks run by the automatic builds, including a full latency measurement against a pretend cable |
| `src/Main.cpp` | App start-up and the two windowless modes |
| `linux/install.sh` | Sets up the Linux rig computer: realtime tuning, the app, start-up straight into it |
| `linux/rig-check.sh` | Health check of the rig computer; changes nothing |
| `linux/rollback.sh` | Switches the rig back to an earlier build |
| `docs/linux-setup.md` | Step-by-step guide to the rig computer, and the laptop USB-stick trial |
| `CLAUDE.md` | Notes for Claude: working rules, the rig computer, and how to add an effect |

## Licence note

JUCE is used under its free AGPLv3 option, and the ASIO headers bundled with
JUCE are GPLv3. That is fine for a private, personal build. If the app is ever
given or sold to someone else, it must either be released with its source code
under the AGPLv3, or JUCE's paid licence is needed.
