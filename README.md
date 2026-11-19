# Gig Rig

Software for Tom's custom all-in-one gig rig: one box to replace the pedalboard,
the Boss RC-600 looper and the Allen & Heath CQ12T mixer. The design and the
staged plan live in the project's design doc; this repository holds the code.

The app is built in stages. Each stage adds to the same app, `GigRig`.

| Stage | What the app does | Status |
| --- | --- | --- |
| 0 | Device check: lists every audio and MIDI device, and says whether it can see the CQ mixer and the RC-600 | Built |
| 1 | Audio through the CQ: a round-trip latency meter, buffer-size choice, play-through with a dropout watch, and a test tone for each return | Built |
| 2 onward | RC-600 as the test pedal, looper, songs, effects, tablet app | Planned |

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

## The Device check tab

1. Plug the CQ12T into the laptop by USB and switch it on. Do the same with the RC-600.
2. The app scans by itself when it opens; **Scan again** rescans after plugging something in.
3. The two boxes at the top say whether the CQ is there with all 16 inputs and 16 outputs, and whether the RC-600 is there as a MIDI device.
4. The bottom panel shows any MIDI that arrives, live. With the RC-600's MIDI CTRL memory (slot 65) selected, each switch shows as CC 20 to 30.

**Copy report** copies the device list and the stage 1 results together.

## How the builds work

Every change to the code is built automatically by GitHub (the **Actions** tab) for
Windows and Linux. Each build also runs the app's self-test and a real device
scan. Changes that reach the `main` branch are published on the
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
| `src/AudioPanel.*` | The stage 1 tab |
| `src/DeviceReport.*` | Scans audio and MIDI devices and judges whether the CQ and RC-600 are present |
| `src/MidiMonitor.*` | Listens to every MIDI input and describes what arrives |
| `src/DeviceCheckPanel.*` | The stage 0 tab |
| `src/MainComponent.*` | The window: title, Copy report and the tabs |
| `src/Style.*` | Colours and the small shared widgets |
| `src/SelfTest.*` | Checks run by the automatic builds, including a full latency measurement against a pretend cable |
| `src/Main.cpp` | App start-up and the two windowless modes |

## Licence note

JUCE is used under its free AGPLv3 option, and the ASIO headers bundled with
JUCE are GPLv3. That is fine for a private, personal build. If the app is ever
given or sold to someone else, it must either be released with its source code
under the AGPLv3, or JUCE's paid licence is needed.
