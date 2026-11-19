# Gig Rig

Software for Tom's custom all-in-one gig rig: one box to replace the pedalboard,
the Boss RC-600 looper and the Allen & Heath CQ12T mixer. The design and the
staged plan live in the project's design doc; this repository holds the code.

The app is built in stages. Each stage adds to the same app, `GigRig`.

| Stage | What the app does | Status |
| --- | --- | --- |
| 0 | Device check: lists every audio and MIDI device, and says whether it can see the CQ mixer and the RC-600 | Built |
| 1 | Audio pass-through and a latency meter on the CQ | Next |
| 2 onward | RC-600 as the test pedal, looper, songs, effects, tablet app | Planned |

## Getting the app (no tools needed)

1. Open the [latest release](https://github.com/TomBoothMusic/gig-rig/releases/latest). You need to be signed in to GitHub, because the repository is private.
2. Under **Assets**, click **GigRig-Windows.exe** to download it.
3. Double-click the downloaded file. The first time, Windows shows a blue "Windows protected your PC" box, because the app is home-made and not signed. Click **More info**, then **Run anyway**.

Nothing is installed. The app is a single file; delete it to remove it.

## Using the stage 0 device check

1. Plug the CQ12T into the laptop by USB and switch it on. Do the same with the RC-600.
2. Open the app. It scans by itself; **Scan again** rescans after plugging something in.
3. The two boxes at the top answer the stage 0 questions: is the CQ there with all 16 inputs and 16 outputs, and is the RC-600 there as a MIDI device.
4. Click **Copy report** and paste the result to Claude.

The bottom panel shows any MIDI that arrives, live.

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
GigRig --self-test result.txt   # check the device-matching logic, no window
GigRig --report report.txt      # write the device report, no window
```

| Path | What it holds |
| --- | --- |
| `src/DeviceReport.*` | Scans audio and MIDI devices and judges whether the CQ and RC-600 are present |
| `src/MidiMonitor.*` | Listens to every MIDI input and describes what arrives |
| `src/MainComponent.*` | The stage 0 screen |
| `src/SelfTest.*` | Checks run by the automatic builds |
| `src/Main.cpp` | App start-up and the two windowless modes |

## Licence note

JUCE is used under its free AGPLv3 option, and the ASIO headers bundled with
JUCE are GPLv3. That is fine for a private, personal build. If the app is ever
given or sold to someone else, it must either be released with its source code
under the AGPLv3, or JUCE's paid licence is needed.
