#!/usr/bin/env bash
# Gig Rig: health check. Reads everything, changes nothing.
#
#   linux/rig-check.sh            print the check
#   linux/rig-check.sh --save     also save it to ~/gigrig/logs/rig-check-<date>.txt
#
# Each line says PASS, WARN or FAIL in plain words. Paste the whole output to
# Claude when something is wrong. docs/linux-setup.md explains each line.

set -uo pipefail

RIG_HOME="${GIGRIG_HOME:-$HOME/gigrig}"
SAVE=0
[ "${1:-}" = "--save" ] && SAVE=1

out=""
fails=0
warns=0
line() { out+="$1"$'\n'; printf '%s\n' "$1"; }
pass() { line "PASS  $*"; }
warn() { line "WARN  $*"; warns=$((warns + 1)); }
bad()  { line "FAIL  $*"; fails=$((fails + 1)); }

line "GIG RIG CHECK  $(date '+%F %T')  on $(hostname)"
line "---------------------------------------------------"

# ---- system --------------------------------------------------------------
. /etc/os-release 2>/dev/null || true
line "System:  ${PRETTY_NAME:-unknown}, kernel $(uname -r)"
line "CPU:     $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//'), $(nproc) threads"
line "Memory:  $(free -h | awk '/^Mem:/ {print $2 " total, " $7 " free"}')"
line "Disk:    $(df -h "$HOME" | awk 'NR==2 {print $4 " free of " $2}')"
if [ -d /run/casper ] || grep -qw boot=casper /proc/cmdline 2>/dev/null; then
    line "Running from a USB stick (live session)."
fi
line ""

# ---- realtime tuning -----------------------------------------------------
cmdline="$(cat /proc/cmdline)"
for opt in preempt=full threadirqs usbcore.autosuspend=-1; do
    if grep -qw -- "$opt" <<< "$cmdline"; then pass "Kernel started with $opt"
    else warn "Kernel started without $opt (the rig set-up adds it; restart after installing)"; fi
done

if id -nG | grep -qw audio; then pass "This session is in the audio group"
elif id -nG "$USER" | grep -qw audio; then warn "Added to the audio group, but this session started before that: log out and back in"
else bad "Not in the audio group: run linux/install.sh"; fi

rt="$(ulimit -r)"; ml="$(ulimit -l)"
if [ "$rt" = unlimited ] || [ "$rt" -ge 90 ] 2>/dev/null; then pass "Realtime priority allowed up to $rt"
else bad "Realtime priority allowed only up to $rt (needs 95): run linux/install.sh, then log out and back in"; fi
if [ "$ml" = unlimited ]; then pass "Memory locking unlimited"
else warn "Memory locking limited to $ml KB (should be unlimited after install and a fresh log-in)"; fi

govs="$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | xargs)"
if [ -z "$govs" ]; then warn "Could not read the CPU speed setting"
elif grep -qv performance <<< "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor)"; then warn "CPU speed setting: $govs (should be all performance: sudo systemctl restart gigrig-tune)"
else pass "CPU at full speed on every core"; fi

as="$(cat /sys/module/usbcore/parameters/autosuspend 2>/dev/null || echo '?')"
if [ "$as" = "-1" ]; then pass "USB power saving off"; else warn "USB power saving is on (autosuspend $as)"; fi

if systemctl is-enabled rtirq > /dev/null 2>&1 || [ -x /etc/init.d/rtirq ]; then pass "rtirq installed"
else warn "rtirq not installed (interrupt priorities left at default)"; fi

if pgrep -u "$USER" -x pipewire > /dev/null 2>&1 || pgrep -u "$USER" -x wireplumber > /dev/null 2>&1; then
    warn "PipeWire (desktop sound) is running and may hold the interface: run linux/install.sh, or log out and in"
else
    pass "PipeWire off: the app can have the interface to itself"
fi
line ""

# ---- the app -------------------------------------------------------------
if [ -x "$RIG_HOME/current/GigRig" ]; then
    pass "App installed: build $(readlink "$RIG_HOME/current" | sed 's#builds/##')"
    # shellcheck disable=SC2010 # build names are plain words
    others="$(ls -1 "$RIG_HOME/builds" 2>/dev/null | grep -vx "$(basename "$(readlink "$RIG_HOME/current")")" | xargs)"
    [ -n "$others" ] && line "      Other builds kept for rollback: $others"
else
    bad "App not installed: run linux/install.sh"
fi

if pid="$(pgrep -u "$USER" -x GigRig | head -1)" && [ -n "$pid" ]; then
    pass "App running (process $pid)"
    rtthreads="$(ps -L -o rtprio=,comm= -p "$pid" | awk '$1 != "-" && $1 > 0 {print $1}' | sort -rn | head -1)"
    if [ -n "$rtthreads" ]; then pass "App has a realtime audio thread (priority $rtthreads)"
    else warn "App running with no realtime thread (no audio open yet, or realtime refused: see the app's report)"; fi
else
    line "      App not running now."
fi

if [ -f "$HOME/.config/autostart/gigrig.desktop" ]; then pass "App opens by itself after log-in"
else line "      App does not open by itself (normal in test mode)"; fi

if grep -qs '^AutomaticLoginEnable=true' /etc/gdm3/custom.conf; then pass "Logs in by itself as $(grep -s '^AutomaticLogin=' /etc/gdm3/custom.conf | cut -d= -f2)"
else line "      Automatic log-in off (normal in test mode)"; fi

if [ -f /etc/apt/apt.conf.d/99gigrig-no-auto-updates ]; then pass "Automatic updates off"
else line "      Automatic updates still on (normal in test mode)"; fi

settings="$HOME/.config/GigRig/audio-settings.txt"
if [ -f "$settings" ]; then line "      App's remembered audio set-up: $(tr '\n' ' ' < "$settings")"; fi
line ""

# ---- devices -------------------------------------------------------------
line "Sound cards seen:"
cards="$(cat /proc/asound/cards 2>/dev/null)"
if [ -n "$cards" ]; then while IFS= read -r c; do line "      $c"; done <<< "$cards"; else line "      none"; fi

found=0
for name in CQ UltraLite MOTU Fireface RME Scarlett; do
    if grep -qi -- "$name" <<< "$cards"; then
        pass "Interface found: $(grep -i -m1 -- "$name" <<< "$cards" | sed 's/^ *//')"
        found=1
    fi
done
[ "$found" -eq 1 ] || warn "No CQ12T or MOTU seen (plug it in by USB, switch it on, run this again)"

for card in /proc/asound/card*/stream0; do
    [ -f "$card" ] || continue
    name="$(head -1 "$card")"
    line "      $name"
    while IFS= read -r l; do line "        $l"; done < <(grep -E 'Playback:|Capture:|Channels:|Rates:' "$card" | sed 's/^ *//' | uniq | head -12)
done

if command -v amidi > /dev/null 2>&1; then
    midi="$(amidi -l 2>/dev/null | tail -n +2)"
    if grep -qi 'rc-600\|rc600' <<< "$midi"; then pass "RC-600 found as a MIDI device"
    else line "      RC-600 not seen as a MIDI device (fine if it is not plugged in)"; fi
fi

usb="$(lsusb 2>/dev/null | grep -vi 'hub\|root' | sed 's/^Bus [0-9]* Device [0-9]*: ID //' | head -12)"
if [ -n "$usb" ]; then
    line "USB devices:"
    while IFS= read -r u; do line "      $u"; done <<< "$usb"
fi
line ""

# ---- Tom's own files, and the phone screen ---------------------------------
docs="$HOME/Documents/GigRig"
count() { find "$1" -maxdepth 1 -type f -name '*.txt' 2>/dev/null | wc -l; }
if [ -d "$docs" ]; then
    songs="$(count "$docs/Songs")"; presets="$(count "$docs/Effect presets")"; mixes="$(find "$docs/Mixer" -type f -name '*.txt' 2>/dev/null | wc -l)"
    line "Your files in Documents/GigRig: $songs songs, $presets effect presets, $mixes mixer set-ups"
    if [ "$songs" -eq 0 ] && [ "$presets" -eq 0 ]; then
        line "      None yet: bring them across from the laptop (docs/linux-setup.md, B4)."
    fi
else
    line "Your files: no Documents/GigRig folder yet (the app makes it the first time it opens)."
fi
chains="$HOME/.config/GigRig/effects-rack.txt"
if [ -f "$chains" ]; then line "      The app's remembered chains and sockets are here (effects-rack.txt)."
else line "      No remembered chains yet (effects-rack.txt comes across from the laptop, B4)."; fi

address="$(hostname -I 2>/dev/null | awk '{print $1}')"
remote="$HOME/.config/GigRig/remote.txt"
if [ -n "$address" ]; then line "Network: this computer's address is $address"
else line "Network: not connected (fine at a gig; the phone screen and updates need it)"; fi
if grep -qsE '^on *= *1' "$remote"; then
    port="$(grep -sE '^port *=' "$remote" | head -1 | cut -d= -f2 | tr -d ' ')"
    line "      Phone screen: switched on. On the phone, open http://${address:-<address>}:${port:-8080}/"
else
    line "      Phone screen: switched off (the Remote screen tab in the app switches it on)."
fi
line ""

# ---- Claude Code ---------------------------------------------------------
if command -v claude > /dev/null 2>&1 || [ -x "$HOME/.local/bin/claude" ]; then pass "Claude Code installed"
else line "      Claude Code not installed (optional: see the guide)"; fi

line ""
line "Result: $fails FAIL, $warns WARN"

if [ "$SAVE" -eq 1 ]; then
    mkdir -p "$RIG_HOME/logs"
    file="$RIG_HOME/logs/rig-check-$(date +%Y-%m-%d-%H%M).txt"
    printf '%s' "$out" > "$file"
    echo "Saved to $file"
fi

[ "$fails" -eq 0 ]
