# Setting up the rig computer (Linux)

This guide turns a computer into the rig's brain: it installs Linux, tunes it
for live audio, installs the Gig Rig app and makes the computer open the app by
itself when it is switched on. One script does nearly all of it.

There are two ways to use this guide:

- **Part A: try it on the Lenovo laptop, for free.** The laptop starts from a USB
  stick, so Windows is not touched. Optional: useful to prove the CQ12T and the
  app on Linux before money is spent.
- **Part B: set up the mini PC.** Tom bought a GMKtec M8 (Ryzen 5 PRO 6650H) on
  25 November 2026, so the steps are written for that one.

Parts C to G apply to both: interface settings, the acceptance tests, updates,
Claude on the machine, and troubleshooting.

**Doing the mini PC in one sitting?** Tom's tick-as-you-go page for that day, the
"Mini PC set-up day" page in the Claude project (https://claude.ai/artifact/QK1T9rBayzL7Q3ZL7Fsf78), has Parts B, C and D of this guide in the
order they are done, with nothing else. This file is the reference behind it: when one
changes, change the other.

**Contents**

- [What is decided and why](#what-is-decided-and-why)
- [Part A: try it on the laptop from a USB stick](#part-a-try-it-on-the-laptop-from-a-usb-stick)
- [Part B: set up the mini PC](#part-b-set-up-the-mini-pc)
- [Part C: audio interface settings](#part-c-audio-interface-settings)
- [Part D: the acceptance tests](#part-d-the-acceptance-tests)
- [Part E: updating, and going back to an older build](#part-e-updating-and-going-back-to-an-older-build)
- [Part F: Claude on the rig computer](#part-f-claude-on-the-rig-computer)
- [Part G: troubleshooting](#part-g-troubleshooting)
- [What the script changes (for Claude)](#what-the-script-changes-for-claude)

---

## What is decided and why

| Choice | Decision | Why |
| --- | --- | --- |
| Linux | **Ubuntu 24.04 LTS** desktop (the newest 24.04.x download, **not 26.04**) | The app's automatic Linux builds are made on 24.04, so they run on it unchanged. Supported with fixes until 2029. The most written-about Linux, so answers are easy to find. Ubuntu's front page now offers 26.04 first: the builds have never been run on it, and the set-up relies on the X11 desktop that 24.04 has. |
| Realtime | The normal Ubuntu kernel, started with `preempt=full threadirqs` | Gives most of a "realtime kernel" with nothing extra to install. A full realtime kernel (Ubuntu Pro, free for personal use) is a later option only if the tests ask for it. |
| How the app reaches the interface | Directly, through ALSA, with the desktop sound system (PipeWire) switched off | The same idea as ASIO on Windows: one program owns the interface, nothing in between. |
| Start-up | Logs in by itself and opens the app; no screen lock, no sleep, no automatic updates | A gig machine must come up the same way every time and never change by itself before a set. |
| Builds | The numbered builds from the GitHub Releases page; older ones kept for a one-command rollback | Matches the rule that the gig machine runs a fixed, tested build. |

---

## Part A: try it on the laptop from a USB stick

About 45 minutes. Nothing on the laptop's own drive changes. When you are done,
switch off, pull the stick out and Windows starts as normal.

### What you need

- A USB stick of **32 GB or more** (it gets wiped).
- The laptop, its charger, the CQ12T and its USB cable. The RC-600 too, if you
  want to test the pedal.
- Your **BitLocker recovery key**, just in case. Starting from a USB stick should
  not ask for it, but if Windows ever does ask on the way back, you need it. Find it at
  [aka.ms/myrecoverykey](https://aka.ms/myrecoverykey) (sign in with your Microsoft
  account) and write it down or keep it on your phone.

### A1. Make the USB stick (on Windows, 15 minutes)

1. Download **Ubuntu 24.04 LTS** desktop from [releases.ubuntu.com/24.04](https://releases.ubuntu.com/24.04/): click **64-bit PC (AMD64) desktop image**. It is one large file ending in `.iso` (about 6 GB). Ubuntu's main download page offers 26.04 first, which is **not** the one to use.
2. Download **Rufus** from [rufus.ie](https://rufus.ie) (the "Portable" version is fine; nothing to install).
3. Plug in the USB stick. Open Rufus.
4. **Device:** your USB stick. Check the size so you do not pick the wrong drive.
5. **Boot selection:** click **SELECT** and choose the Ubuntu `.iso` file.
6. **Persistent partition size:** drag the slider to about **10 GB**. This lets the stick keep what you install between restarts. (Without it, everything is forgotten each time the laptop restarts. That still works for one test session.)
7. **Partition scheme:** GPT. **Target system:** UEFI.
8. Click **START**. If it asks about "ISO image mode" or "DD image mode", pick **ISO image mode**. If it offers to download extra files, say **Yes**.
9. Wait until the bar says **READY**, then close Rufus.

### A2. Start the laptop from the stick (5 minutes)

1. Leave the stick in. In Windows, open **Settings**, then **System**, then **Recovery**.
2. Next to **Advanced startup**, click **Restart now**, and confirm.
3. On the blue screen, click **Use a device**, then pick the USB stick (it may be called "EFI USB Device" or the stick's brand name).
4. A black menu appears. Choose **Try or Install Ubuntu** (it is picked by itself after a few seconds).
5. Ubuntu starts. An install window may open: choose **Try Ubuntu**, then close that window. **Do not install**: that would change the laptop's drive.

If the blue screen has no "Use a device", restart and tap **F12** as the Lenovo logo shows, then pick the USB stick from the list.

### A3. Install the rig software (15 minutes)

1. Click the top-right corner of the screen and connect to Wi-Fi.
2. Open **Firefox** (the orange fox on the left) and open this guide (github.com/TomBoothMusic/gig-rig, then `docs`, then `linux-setup.md`) so you can copy from it.
3. Open a **Terminal**: press **Ctrl + Alt + T**.
4. Copy this line, paste it into the Terminal (**Ctrl + Shift + V**) and press Enter:

   ```
   wget -qO- https://raw.githubusercontent.com/TomBoothMusic/gig-rig/main/linux/bootstrap.sh | bash -s -- --test
   ```

5. The script asks **Carry on? [Y/n]**: press Enter. It runs for several minutes and prints a green **OK** for each step. Near the end it checks the build with its self-test, which takes a few minutes with nothing showing.
6. At the end it says to **log out and back in once**. Click the top-right corner, then the power symbol, then **Log Out**. On the log-in screen click **Ubuntu** (no password).

### A4. Test the app

1. Plug in the CQ12T by USB and switch it on. On the CQ, set **CONFIG**, **USB/SD/BT**, **USB/SD**: Sample Rate **96 kHz**, Stream Mode **Multitrack** (see [Part C](#part-c-audio-interface-settings)).
2. Open a Terminal and type `~/gig-rig-code/linux/rig-check.sh`, then Enter. Look for **FAIL** lines. Kernel option WARN lines are expected on the stick unless you did the optional step below.
3. Click the **Show Apps** grid (bottom left) and open **Gig Rig**.
4. Do the tests in [Part D](#part-d-the-acceptance-tests).
5. Click **Copy report** in the app, paste it into a message to Claude (Firefox on the stick works), and paste the rig-check output too.

When finished: switch off from the top-right menu, pull the stick out, and switch on again. Windows starts as normal.

**Optional, for the full effect:** the full rig also starts Linux with three
extra settings that the stick does not have. To try them: at the black menu in
step A2.4, press **e**, find the line starting with `linux`, move to the end of
it (after `---` if there is one, put them before it), type a space and
`preempt=full threadirqs usbcore.autosuspend=-1`, then press **F10**. This lasts
for that one start only.

---

## Part B: set up the mini PC

About an hour, plus downloads. Everything on the mini PC's drive is wiped.

### What you need

- The mini PC, its power supply, a screen with HDMI or DisplayPort, a USB keyboard and mouse, and internet (a network cable is simplest; Wi-Fi works if the PC has it).
- The Ubuntu USB stick from Part A (or make one the same way; persistence does not matter here).

### B1. BIOS settings (GMKtec M8, 10 minutes)

These names come from GMKtec's own guides for its mini PCs, not from this exact unit, so a
setting may sit under a slightly different name. If one is not there, skip it and tell Claude.

1. Switch on and tap **Esc** (or **Del**) over and over as the GMKtec logo shows, until the
   set-up screen opens. If neither works, tap **F7** for the boot menu and choose **Enter Setup**.
2. Change these, if they are there:

   | Where | Setting | Set to | Why |
   | --- | --- | --- | --- |
   | Advanced, or Chipset (look for a power or "AC" entry) | **AC Power Lost Policy** (also seen as "Restore on AC Power Loss" or "State After G3") | **Power On** | The rig starts the moment power reaches it: switch on at the wall and walk away |
   | Advanced | Wake on LAN | **Disabled** | |
   | Advanced, AMD CBS (if shown) | Global C-state Control | leave **on** for now | Turn off only if the glitch check finds trouble (Part G) |
   | Security | Secure Boot | leave as it is | Ubuntu works with it on |

3. Press **F10** to save and leave.

Before wiping it: the PC was sold as new with Windows on it. Start it once in Windows first to
see that it works at all (screen, USB sockets, Wi-Fi), because that is the easy moment to send
it back.

Another make of mini PC: the same ideas apply. The BIOS key is often F2, Del or F1, and the boot
menu key F12, F11 or F10. On a Dell OptiPlex Micro the key is **F2**, the start-on-power setting
is **Power Management, AC Recovery = Power On**, and **SATA Operation** must be **AHCI** or
Ubuntu may not see the drive.

### B2. Install Ubuntu (20 minutes)

1. Plug in the USB stick, switch on, and tap **F7** for the boot menu (F12 on a Dell). Pick the USB stick.
2. Choose **Try or Install Ubuntu**, then in the installer choose **Install Ubuntu**.
3. Answer the questions:
   - Language and keyboard: English (Australia).
   - Connect to the internet if asked.
   - **Interactive installation**, **Default selection** (not "Extended").
   - **Install third-party software**: tick it (graphics and Wi-Fi drivers).
   - **Erase disk and install Ubuntu**.
   - Your name: anything. **Computer's name:** `gigrig`. **Username:** `rig`. Choose a password and write it down.
   - Untick **Require my password to log in** (the script sets this too).
   - Time zone: Hobart.
4. When it says to, remove the stick and press Enter. The PC restarts into Ubuntu.
5. Skip through the welcome screens (no Ubuntu Pro, no sharing data).

### B3. Run the install script (20 minutes)

1. Open **Firefox** and open this guide (github.com/TomBoothMusic/gig-rig, then `docs`, then `linux-setup.md`) so you can copy from it.
2. Open a **Terminal** (**Ctrl + Alt + T**). Copy, paste (**Ctrl + Shift + V**) and press Enter:

   ```
   wget -qO- https://raw.githubusercontent.com/TomBoothMusic/gig-rig/main/linux/bootstrap.sh | bash
   ```

   The code is public (since late November 2026), so there is no GitHub sign-in. If the code is
   ever made private again, this line still works: it then shows a one-time code to type into a
   browser tab first.
3. Type your password when asked (nothing shows while you type; that is normal).
4. Press Enter at **Carry on?**. It runs for several minutes with a green **OK** per step. Near the
   end it checks the build with its self-test: a few minutes with nothing showing.
5. At the end it says **Restart the computer now**. Do that.
6. The PC restarts, logs in by itself, and the **Gig Rig** app opens on its own after a few seconds.

### B4. Bring your sounds across from the laptop (15 minutes)

A fresh install knows none of your presets, songs or mixer set-ups. They are ordinary files, and
the same files work on Windows and on Linux. Do this once the app has opened on the mini PC.

1. On the laptop, **close the app**. Plug in a USB stick.
2. Open **File Explorer** and go to **Documents**. Copy the whole **GigRig** folder onto the stick.
   (It holds Effect presets, Songs, Mixer, Amp models, Responses, Kick sounds and Test takes. If
   the **Recordings** folder inside it is large, leave that one out.)
3. Still on the laptop: click the address bar of File Explorer, type `%APPDATA%\GigRig` and press
   Enter. Copy every file there onto the stick, into a new folder called **settings**, **except
   these three**: `audio-settings.txt`, `loop-timing-2.txt` and `driver-delay.txt`. Those hold the
   laptop's own audio device and timing measurements, which are wrong for another computer. The
   ones that do come across are `effects-rack.txt` (your four chains and their sockets),
   `mixer.txt` (the mixer as you left it), `kick-pad.txt`, `tuner.txt`, `break-music.txt`,
   `expression-pedal.txt` and `remote.txt` (whichever of them exist).
4. On the mini PC, **close the Gig Rig app** (it would otherwise write its own settings over
   yours when it closes). Plug the stick in and open **Files**.
5. Copy the **GigRig** folder from the stick into **Documents**. If it asks, choose **Merge**.
6. In Files, press **Ctrl + L**, type `~/.config/GigRig` and press Enter. Copy the files
   from the stick's **settings** folder into it, replacing what is there.
7. Start the app again (the **Gig Rig** icon, or restart the PC). The Effects tab should show
   your chains, the Songs tab your songs with a `*`, and the Mixer tab your act and venue names.
8. On the **Latency and audio test** tab, click **Measure latency** once, so the looper has this
   computer's own timing.

Later changes go the same way in either direction. Copying the folders never changes the app
itself, so it cannot break a build.

### B5. Check it

1. Open a Terminal and run `~/gig-rig-code/linux/rig-check.sh`. Every line should say **PASS**, apart from the interface lines if nothing is plugged in yet.
2. Do the tests in [Part D](#part-d-the-acceptance-tests).
3. Install Claude on it ([Part F](#part-f-claude-on-the-rig-computer)), so problems can be looked at on the machine itself.

---

## Part C: audio interface settings

The app's **Latency and audio test** tab has three boxes in its top row:
**Audio device**, **Rate** and **Buffer**. Whatever you pick there is
remembered, so after every restart the app opens the interface the same way.

### Allen & Heath CQ12T (now)

1. On the CQ: **CONFIG**, then **USB/SD/BT**, then **USB/SD**: Sample Rate **96 kHz**, Stream Mode **Multitrack**, Source Point **Post Preamp**. The CQ sets its own rate here, so the app's Rate box shows only 96 kHz.
2. In the app: **Audio device** the CQ, **Buffer 64**. On Linux one interface is listed several times; choose the line that starts **ALSA** and ends **Direct hardware device without any conversions**. The first time, the app picks a CQ line by itself and opens it at whatever buffer Linux offers, so set **Buffer 64** by hand once; it is remembered after that.
3. There is no "safe mode" on Linux: that was a setting of the Windows driver.
4. The routing is the same as on Windows (see the README): returns 1 to 6 land on channel 9, channel 10, ST1 and the USB stereo channel.

Expected: all 16 inputs and 16 outputs show. If the CQ on Linux shows fewer, that is worth knowing and a finding to report (it is one of the open questions in the design doc).

### MOTU UltraLite mk5 (later)

1. Plug it in by USB and switch it on. It should appear as a class-compliant device; nothing to install.
2. In the app: **Audio device** the UltraLite, **Rate 48 kHz** first, **Buffer 64**.
3. Run the latency and glitch tests (Part D) at 48 kHz. Then try **96 kHz** with buffer 64, and 128 if 64 glitches. Keep the lowest setting that passes the 30-minute glitch check with no glitches.
4. Measure it within the shop's return window, so it can go back if it fails.

Note: the app's tests and looper were written around the CQ's channel layout.
They run on any interface, but some labels (like "the CQ's USB stereo channel")
will not match the MOTU until a later stage adds the MOTU layout.

---

## Part D: the acceptance tests

The rig computer passes when all of these pass. They use the app's own
**Measure latency** and **Glitch check** buttons, described step by step in the
README (Stage 1, parts A and B2). Click **Copy report** after each and paste it to Claude.

| # | Test | Pass when |
| --- | --- | --- |
| 1 | `linux/rig-check.sh` | No FAIL lines. On the mini PC, no WARN lines either. |
| 2 | App report, first lines under STAGE 1 | Says **Audio thread: realtime**, and shows the interface with all its channels (CQ: 16 in / 16 out). |
| 3 | Latency (README stage 1, part A, or **Measure latency** with no cable) | CQ at 96 kHz / 64: no worse than the 9.3 ms the laptop gets on Windows. MOTU: aim for under 5 ms. |
| 4 | Glitch check, 30 minutes, no playing needed | **Glitches: 0**. |
| 5 | Glitch check again, 30 minutes, RC-600 plugged in and a few switches pressed | **Glitches: 0**. |
| 6 | Play through it for 30 minutes (README stage 1, part B) | No clicks or gaps heard, dropout watch stays at 0. |
| 6b | **Load check:** mixer on, your act loaded, the heaviest song you have (amp model, delays, reverbs, a full looper layout), played for 10 minutes with loops running. Then **Copy report**. | In the report's **Dropout watch**: **Late blocks of audio: 0**, and **Busiest moment** under about 50% of the time available (Claude's rule of thumb, not a measured limit: under 50% leaves room for the effects still to come; 50 to 80% works but is worth a look; over 80% is too close). The Effects tab's **Processor** line says which chain is the heavy one. |
| 7 | Mini PC only: switch off at the wall, switch on, time it | The app is up and the CQ running with no keyboard or mouse touched. Note the time from power to sound (the design doc asks for start-up time to be checked early). Do it 5 times. |
| 8 | Mini PC only: close the app with its window's close button | It stays closed (a deliberate quit is not treated as a crash). After the next restart it opens again by itself. |
| 9 | Mini PC only: with the app open, in a Terminal type `pkill -KILL GigRig` (a pretend crash) | The app is back by itself within a few seconds, and the sound with it. |
| 10 | Mini PC only: the **Gig Rig: go back a build** icon, then **Gig Rig: update** | Each says which build it switched to, and the app's report shows that build number at the top after it is opened again. (Go back needs two builds on the computer: `~/gig-rig-code/linux/update.sh build-152` fetches an older one to practise with.) |

---

## Part E: updating, and going back to an older build

Update **between gigs, never on the day of one**, and run the glitch check after.

- **Update to the newest build, in one step:** press the Super key (the one with the Windows
  logo), type `Gig` and click **Gig Rig: update**. (Or in a Terminal:
  `~/gig-rig-code/linux/update.sh`.) It fetches the newest scripts and the newest build,
  checks the build with its built-in self-test, and only then switches to it. It asks for no
  password and touches nothing else on the computer. If the app is open it asks before closing
  it: say no and the new build is used the next time the app starts.
- **Install one particular build:** `~/gig-rig-code/linux/update.sh build-42`
- **Go back to the build before:** the **Gig Rig: go back a build** icon, or
  `~/gig-rig-code/linux/rollback.sh`, then restart the app. Run it with a build name
  (`rollback.sh build-41`) to pick one.
- **Check the computer's set-up:** the **Gig Rig: health check** icon, or `linux/rig-check.sh`.
- **Run the whole set-up again** (after an Ubuntu upgrade, or if the health check finds
  something): `~/gig-rig-code/linux/install.sh`. It is safe to repeat.
- **The phone as break music over Bluetooth** (optional, not proven on hardware yet):
  `~/gig-rig-code/linux/break-music.sh`. See `docs/break-music.md`, "The phone".
- **Update Ubuntu itself:** only now and then, between gigs, and run the glitch
  check afterwards: `sudo apt update && sudo apt full-upgrade`, then restart.
  If something breaks after an Ubuntu update, the start-up menu (shown for
  2 seconds) has **Advanced options** with the previous kernel.

Builds live in `~/gigrig/builds/`, one folder each, and are never deleted by
the scripts. `~/gigrig/current` points at the one in use.

---

## Part F: Claude on the rig computer

With Claude Code on the rig, Claude can look at the machine directly (read the
logs, run the check, try a fix) instead of you describing the problem.

1. Open a Terminal and paste:

   ```
   curl -fsSL https://claude.ai/install.sh | bash
   ```

2. Close the Terminal and open a new one. Type `cd ~/gig-rig-code` and Enter, then `claude` and Enter.
3. The first time, it shows a link to sign in to your Claude account. Sign in, then come back.
4. You can now type to Claude right there. Start with: "Read CLAUDE.md and docs/linux-setup.md, run linux/rig-check.sh and tell me if anything is wrong."
5. To let Claude in the Claude app (on your phone or laptop) work on the rig: in the Terminal, in `~/gig-rig-code`, run `claude remote-control`. Leave that window open. The rig then shows up as a computer Claude can use from the project.

The repository's `CLAUDE.md` file tells Claude how the rig is set up, where the
logs are, and the rules for working on it.

---

## Part G: troubleshooting

Run `~/gig-rig-code/linux/rig-check.sh --save` first: it saves its output in
`~/gigrig/logs/`, and most answers start there.

| What you see | What to do |
| --- | --- |
| The app says **"... would not open"** or the device is busy | Something else holds the interface. Run `rig-check.sh`: if PipeWire is running, run `install.sh` again and log out and in. Close any other audio program. |
| Report says **Audio thread: NOT realtime** | Run `install.sh`, then log out and back in (or restart). `rig-check.sh` should then show "Realtime priority allowed up to 95". |
| The CQ or MOTU is not in the Audio device list | Check `rig-check.sh` "Sound cards seen". Unplug and replug the USB cable, use a port on the PC itself (not a hub), and click **Scan again** on the Device check tab. |
| Only 48 kHz is offered for the CQ | Set the CQ's own Sample Rate to 96 kHz (Part C) and restart the app. |
| Glitches in the glitch check | One at a time, each with a 30-minute check: try buffer 128; unplug other USB gear; in the BIOS turn the C-state setting off (Part B1); use a different USB port. Paste each report to Claude. |
| Clicks only when the screen changes or a window moves | Report it to Claude: the graphics driver may need a change. |
| No sound in Firefox or YouTube | Expected: the desktop sound system is off so the app can own the interface. Run `install.sh --keep-pipewire` to turn it back on (for a test only), and run `install.sh` again to turn it off. |
| The app does not open after start-up | Click **Show Apps**, then **Gig Rig**, to open it by hand. Then look at today's log in `~/gigrig/logs/app-<date>.log` (or have Claude look). |
| The app keeps closing and reopening | It crashed. The launcher gives up after 5 quick crashes in a row. Go back to the previous build (`rollback.sh`) and send the log to Claude. |
| Screen stays black after an Ubuntu update | Restart, and in the start-up menu choose **Advanced options for Ubuntu**, then the second (older) kernel. Tell Claude. |
| The install seems stuck after "Installing the Gig Rig app" | It is checking the build with its self-test, which takes a few minutes and shows nothing. Give it ten minutes before worrying. |
| `install.sh` stops with **STOPPED: ...** | The line says what went wrong. Most often the internet dropped: run it again (it is safe to repeat). |
| GitHub sign-in fails | Run `gh auth login --web` on its own and follow it, then run `install.sh` again. |
| Forgot the password | Ubuntu's recovery mode can reset it; ask Claude for the steps for that moment. |

---

## What the script changes (for Claude)

`linux/install.sh` (safe to run again; it only changes what differs):

| What | Where | Mode |
| --- | --- | --- |
| Packages: the app's libraries (the same list CI builds with), alsa-utils, xvfb, xauth, gh, git, rtirq-init; with `--build` also cmake, ninja, build-essential | apt | both |
| Realtime rights: `@audio rtprio 95, memlock unlimited, nice -19`; user added to `audio` | `/etc/security/limits.d/95-gigrig-audio.conf` | both |
| `vm.swappiness=10` | `/etc/sysctl.d/90-gigrig.conf` | both |
| CPU governor `performance`, EPP `performance`, USB autosuspend off, all USB `power/control=on`, at every boot | `/usr/local/sbin/gigrig-tune`, `gigrig-tune.service`, `/etc/udev/rules.d/90-gigrig-usb-awake.rules` | both |
| `power-profiles-daemon` disabled | systemd | rig |
| Kernel options `preempt=full threadirqs usbcore.autosuspend=-1`, GRUB menu shown for 2 s | `/etc/default/grub.d/90-gigrig.cfg` + `update-grub` | rig |
| PipeWire and WirePlumber masked for the user (undo: `--keep-pipewire`) | `systemctl --user mask` | both |
| App: release tarball by plain download when the repository is public, otherwise via `gh release download` (or a local compile with `--build`), self-test under xvfb before switching | `~/gigrig/builds/<tag>/`, symlink `~/gigrig/current` | both |
| Launcher that restarts the app after a crash (gives up after 5 crashes within 30 s each; exit code 0 = deliberate quit) | `~/.local/bin/gigrig`, logs `~/gigrig/logs/app-<date>.log` | both |
| Menu entry | `~/.local/share/applications/gigrig.desktop` | both |
| Icons **Gig Rig: update**, **go back a build**, **health check**, and the small wrapper that keeps their window open | `~/.local/share/applications/gigrig-update.desktop`, `gigrig-rollback.desktop`, `gigrig-check.desktop`, `~/.local/bin/gigrig-run` | both |
| Autostart | `~/.config/autostart/gigrig.desktop` | rig |
| GDM automatic log-in and X11 (Wayland off, so screenshots work over Remote Control) | `/etc/gdm3/custom.conf` (backup `.before-gigrig`) | rig |
| No screen lock, blanking, sleep, notification banners or GNOME Software updates | gsettings | rig |
| No unattended upgrades or apt timers; snaps held | `/etc/apt/apt.conf.d/99gigrig-no-auto-updates`, systemd, `snap refresh --hold` | rig |

`linux/install.sh --app-only` (what `linux/update.sh` runs) does only the **App** row, with no
`sudo`. `linux/bootstrap.sh` installs git and curl (and gh if the code is private), clones the
code to `~/gig-rig-code` and runs `install.sh`.

`linux/break-music.sh` (optional, never run by `install.sh`; `--remove` undoes the services):

| What | Where |
| --- | --- |
| Packages `bluez`, `bluez-alsa-utils` | apt |
| `bluetooth.service` enabled; any packaged BlueALSA services disabled | systemd |
| The computer offered to phones as a speaker: `bluealsa -S -p a2dp-sink --a2dp-force-audio-cd` | `/etc/systemd/system/gigrig-bluealsa.service` |
| The phone's sound poured into the app's pipe (`bluealsa-cli open ... > pipe`), as a user service | `~/.local/bin/gigrig-phone-audio`, `~/.config/systemd/user/gigrig-phone-audio.service`, pipe `~/gigrig/break-music.pipe` |

PipeWire stays masked: BlueALSA talks to BlueZ directly, so the app keeps the interface to itself.

The app's own files on Linux are in `~/.config/GigRig/`: `audio-settings.txt`
(device, rate and buffer to open at start-up), `loop-timing-2.txt` (measured
loop timing) and `driver-delay.txt` (history of the driver's delay figures).

On Linux the app promotes its audio thread to `SCHED_FIFO` priority 70 on the
first audio callback (JUCE's ALSA thread is not realtime by itself), and the
STAGE 1 section of its report says whether that worked. rtirq ranks the USB and
sound interrupt threads above it.
