#!/usr/bin/env bash
# Gig Rig: one-command Linux set-up.
#
# Turns a fresh Ubuntu 24.04 LTS desktop into the rig computer: realtime audio
# tuning, the app itself, and (on the real rig) start-up straight into the app.
# Safe to run again at any time: it only changes what is not already right.
#
# Plain-language steps for Tom are in docs/linux-setup.md.
#
#   linux/install.sh                 full rig set-up (the mini PC)
#   linux/install.sh --test          laptop booted from a USB stick: tuning and app only,
#                                    nothing that changes how the computer starts
#   linux/install.sh --version build-42   install that numbered build (default: the latest)
#   linux/install.sh --build         compile this copy of the code instead of downloading
#                                    a build (for a test branch that has no release yet)
#   linux/install.sh --file GigRig-Linux.tar.gz   install a build already downloaded
#   linux/install.sh --keep-pipewire leave the desktop sound system on (see the guide)
#   linux/install.sh --yes           no questions (used by the automatic test on GitHub)
#
# Run it as the normal user, not with sudo. It asks for the password when it needs it.

set -euo pipefail

REPO="TomBoothMusic/gig-rig"
RIG_HOME="${GIGRIG_HOME:-$HOME/gigrig}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

MODE="rig"
VERSION=""
FROM_SOURCE=0
FROM_FILE=""
KEEP_PIPEWIRE=0
ASSUME_YES=0

while [ $# -gt 0 ]; do
    case "$1" in
        --test)          MODE="test" ;;
        --version)       VERSION="${2:?--version needs a build name, such as build-42}"; shift ;;
        --build)         FROM_SOURCE=1 ;;
        --file)          FROM_FILE="$(realpath "${2:?--file needs the path of a GigRig-Linux.tar.gz}")"; shift ;;
        --keep-pipewire) KEEP_PIPEWIRE=1 ;;
        --yes|-y)        ASSUME_YES=1 ;;
        -h|--help)       sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)"; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
say()  { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
ok()   { printf '   \033[32mOK\033[0m   %s\n' "$*"; }
note() { printf '   \033[33mNOTE\033[0m %s\n' "$*"; }
fail() { printf '\n\033[1;31mSTOPPED: %s\033[0m\n' "$*"; exit 1; }

# Writes a root-owned file only when its contents differ. Returns 0 if it changed.
put_file() {
    local path="$1" content="$2"
    if [ -f "$path" ] && [ "$(cat "$path")" = "$content" ]; then
        return 1
    fi
    sudo mkdir -p "$(dirname "$path")"
    printf '%s\n' "$content" | sudo tee "$path" > /dev/null
    return 0
}

REBOOT_NEEDED=0
RELOGIN_NEEDED=0

# ---------------------------------------------------------------------------
say "Checking this computer"

[ "$(id -u)" -ne 0 ] || fail "run this as your normal user, without sudo. It asks for the password itself."
command -v apt-get > /dev/null || fail "this script is for Ubuntu (or a close relative). See docs/linux-setup.md."

. /etc/os-release
ok "System: ${PRETTY_NAME:-unknown}"
case "${VERSION_ID:-}" in
    24.04|26.04) ;;
    *) note "Made for Ubuntu 24.04 LTS. This is ${PRETTY_NAME:-something else}; carrying on, but expect surprises." ;;
esac

if [ -d /run/casper ] || grep -qw boot=casper /proc/cmdline 2>/dev/null; then
    if [ "$MODE" = "rig" ]; then
        note "This looks like a USB stick (live session), so switching to --test mode."
        MODE="test"
    fi
fi
ok "Mode: $([ "$MODE" = rig ] && echo 'full rig set-up' || echo 'test (USB stick or trial, start-up left alone)')"

if [ "$ASSUME_YES" -eq 0 ]; then
    echo
    echo "   This installs packages, tunes the computer for live audio and installs the Gig Rig app."
    [ "$MODE" = rig ] && echo "   It also makes the computer log in and open the app by itself when switched on."
    read -r -p "   Carry on? [Y/n] " answer
    case "${answer:-y}" in [Yy]*) ;; *) echo "Nothing changed."; exit 0 ;; esac
fi

sudo -v || fail "the password was not accepted."

# ---------------------------------------------------------------------------
say "Installing packages (a few minutes the first time)"

RUNTIME_PACKAGES=(
    # The app's own libraries (the -dev names stay the same across Ubuntu releases)
    libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev
    libx11-dev libxext-dev libxrender-dev libxrandr-dev libxinerama-dev
    libxcursor-dev libxcomposite-dev libxi-dev
    # Tools the guide, the check script and Claude use
    alsa-utils xvfb gh git curl ca-certificates pciutils usbutils
    # Realtime interrupt priorities for the sound and USB hardware
    rtirq-init
)
BUILD_PACKAGES=(build-essential cmake ninja-build pkg-config)

packages=("${RUNTIME_PACKAGES[@]}")
[ "$FROM_SOURCE" -eq 1 ] && packages+=("${BUILD_PACKAGES[@]}")

# gh and rtirq-init come from Ubuntu's "universe" section, which a USB stick may not have on.
if command -v add-apt-repository > /dev/null 2>&1; then
    sudo add-apt-repository -y universe > /dev/null 2>&1 || true
fi
sudo apt-get -o Acquire::Retries=5 update -qq
if ! sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends "${packages[@]}"; then
    note "Some packages would not install; trying again without rtirq-init."
    trimmed=()
    for p in "${packages[@]}"; do [ "$p" = rtirq-init ] || trimmed+=("$p"); done
    packages=("${trimmed[@]}")
    sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends "${packages[@]}" \
        || fail "packages would not install. Check the internet connection and run this again."
fi
ok "Packages installed"

# ---------------------------------------------------------------------------
say "Realtime audio rights"

# Lets members of the audio group run the audio thread at realtime priority and
# lock its memory, so nothing else on the computer can interrupt the sound.
if put_file /etc/security/limits.d/95-gigrig-audio.conf "# Gig Rig: realtime audio for the audio group (linux/install.sh)
@audio   -  rtprio     95
@audio   -  memlock    unlimited
@audio   -  nice       -19"; then
    RELOGIN_NEEDED=1
fi
ok "Audio group may use realtime priority 95 and locked memory"

if ! id -nG "$USER" | grep -qw audio; then
    sudo usermod -aG audio "$USER"
    RELOGIN_NEEDED=1
    ok "Added $USER to the audio group"
else
    ok "$USER is in the audio group"
fi

if put_file /etc/sysctl.d/90-gigrig.conf "# Gig Rig (linux/install.sh)
vm.swappiness = 10
fs.inotify.max_user_watches = 524288"; then
    sudo sysctl -q --system || true
fi
ok "Memory settings"

# ---------------------------------------------------------------------------
say "Steady CPU speed and no USB power saving"

# A small service run at every boot: CPU at full speed (no slow wake-ups between
# audio blocks), and USB devices never put to sleep (the interface and the RC-600).
put_file /usr/local/sbin/gigrig-tune '#!/bin/sh
# Gig Rig: run at boot by gigrig-tune.service (linux/install.sh)
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -w "$g" ] && echo performance > "$g"
done
for e in /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference; do
    [ -w "$e" ] && echo performance > "$e" 2>/dev/null
done
[ -w /sys/module/usbcore/parameters/autosuspend ] && echo -1 > /sys/module/usbcore/parameters/autosuspend
for c in /sys/bus/usb/devices/*/power/control; do
    [ -w "$c" ] && echo on > "$c"
done
exit 0' || true
sudo chmod 755 /usr/local/sbin/gigrig-tune

put_file /etc/systemd/system/gigrig-tune.service "[Unit]
Description=Gig Rig: CPU at full speed, USB power saving off
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/gigrig-tune
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target" || true

# Anything plugged in later (the interface, the RC-600) is kept awake too.
put_file /etc/udev/rules.d/90-gigrig-usb-awake.rules \
'# Gig Rig (linux/install.sh): never put USB devices to sleep
ACTION=="add", SUBSYSTEM=="usb", TEST=="power/control", ATTR{power/control}="on"' && sudo udevadm control --reload || true

# power-profiles-daemon would fight the CPU setting, so it is switched off on the rig.
if [ "$MODE" = rig ] && systemctl list-unit-files power-profiles-daemon.service > /dev/null 2>&1; then
    sudo systemctl disable --now power-profiles-daemon.service > /dev/null 2>&1 || true
fi

sudo systemctl daemon-reload
sudo systemctl enable gigrig-tune.service > /dev/null 2>&1 || true
sudo systemctl restart gigrig-tune.service || note "Could not apply the CPU and USB settings now; they apply at the next start."
ok "CPU at full speed, USB kept awake (now and at every start)"

if command -v rtirq > /dev/null 2>&1 || [ -x /etc/init.d/rtirq ]; then
    sudo systemctl enable rtirq > /dev/null 2>&1 || true
    ok "rtirq installed (works once the kernel runs with threadirqs)"
fi

# ---------------------------------------------------------------------------
say "Kernel start-up options"

# preempt=full  : the kernel can stop what it is doing at once for the audio thread
# threadirqs    : hardware interrupts become threads that rtirq can rank (USB and sound first)
# usbcore.autosuspend=-1 : USB never sleeps, from the very start
KERNEL_OPTS="preempt=full threadirqs usbcore.autosuspend=-1"

if [ "$MODE" = rig ]; then
    if [ -d /etc/default/grub.d ] || [ -f /etc/default/grub ]; then
        if put_file /etc/default/grub.d/90-gigrig.cfg "# Gig Rig (linux/install.sh): low-latency kernel options
GRUB_CMDLINE_LINUX_DEFAULT=\"\$GRUB_CMDLINE_LINUX_DEFAULT $KERNEL_OPTS\"
# Show the start-up menu for 2 seconds, so an older kernel can be picked if an update ever misbehaves
GRUB_TIMEOUT_STYLE=menu
GRUB_TIMEOUT=2"; then
            if command -v update-grub > /dev/null 2>&1; then
                sudo update-grub > /dev/null 2>&1 || note "update-grub reported a problem; see the guide's troubleshooting."
            fi
            REBOOT_NEEDED=1
        fi
        ok "Kernel options set: $KERNEL_OPTS"
    else
        note "No GRUB found, so the kernel options were not set. Add these by hand: $KERNEL_OPTS"
    fi
else
    if grep -qw threadirqs /proc/cmdline; then
        ok "Kernel already running with threadirqs"
    else
        note "Test mode leaves start-up alone. For the full effect on a USB stick, type these at the"
        note "start-up menu (press e, add to the line starting 'linux'): $KERNEL_OPTS"
    fi
fi

# ---------------------------------------------------------------------------
say "Desktop sound system"

# The app talks to the interface directly (ALSA), the way the Windows app uses ASIO.
# PipeWire, the desktop's sound system, would otherwise hold the interface and stop
# the app opening it, so it is switched off for this user.
PW_UNITS=(pipewire.socket pipewire.service pipewire-pulse.socket pipewire-pulse.service wireplumber.service)
if [ "$KEEP_PIPEWIRE" -eq 1 ]; then
    systemctl --user unmask "${PW_UNITS[@]}" > /dev/null 2>&1 || true
    note "PipeWire left on (--keep-pipewire). If the app cannot open the interface, run this again without it."
elif systemctl --user show-environment > /dev/null 2>&1; then
    systemctl --user stop "${PW_UNITS[@]}" > /dev/null 2>&1 || true
    systemctl --user mask "${PW_UNITS[@]}" > /dev/null 2>&1 || true
    ok "PipeWire off for $USER (web browser and system sounds go quiet; the app has the interface to itself)"
else
    # No user session (the automatic test on GitHub): mask by links instead.
    mkdir -p "$HOME/.config/systemd/user"
    for u in "${PW_UNITS[@]}"; do ln -sf /dev/null "$HOME/.config/systemd/user/$u"; done
    ok "PipeWire will stay off for $USER from the next log-in"
fi

# ---------------------------------------------------------------------------
say "Installing the Gig Rig app"

mkdir -p "$RIG_HOME/builds" "$RIG_HOME/logs"

if [ "$FROM_SOURCE" -eq 1 ]; then
    [ -f "$SOURCE_DIR/CMakeLists.txt" ] || fail "--build needs to be run from inside a copy of the code."
    label="src-$(git -C "$SOURCE_DIR" rev-parse --short HEAD 2>/dev/null || date +%Y%m%d-%H%M)"
    say "Compiling the app from $SOURCE_DIR (10 to 20 minutes the first time)"
    cmake -S "$SOURCE_DIR" -B "$RIG_HOME/compile" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGIGRIG_BUILD_LABEL="$label" > "$RIG_HOME/logs/compile.log" 2>&1 \
        || fail "setting up the compile failed. The details are in $RIG_HOME/logs/compile.log"
    cmake --build "$RIG_HOME/compile" --parallel >> "$RIG_HOME/logs/compile.log" 2>&1 \
        || fail "the compile failed. The details are in $RIG_HOME/logs/compile.log"
    mkdir -p "$RIG_HOME/builds/$label"
    cp "$RIG_HOME/compile/GigRig_artefacts/Release/GigRig" "$RIG_HOME/builds/$label/GigRig"
elif [ -n "$FROM_FILE" ]; then
    [ -f "$FROM_FILE" ] || fail "there is no file $FROM_FILE"
    label="${VERSION:-file-$(date +%Y%m%d-%H%M%S)}"
    mkdir -p "$RIG_HOME/builds/$label"
    tar -xzf "$FROM_FILE" -C "$RIG_HOME/builds/$label"
    ok "Unpacked $FROM_FILE as $label"
else
    if ! gh auth status > /dev/null 2>&1; then
        [ "$ASSUME_YES" -eq 0 ] || fail "not signed in to GitHub (set GH_TOKEN for an unattended run)."
        say "Signing in to GitHub (the code is private)"
        echo "   A one-time code appears below. Press Enter to open the browser, sign in to GitHub,"
        echo "   and type the code in. Then come back to this window."
        gh auth login --hostname github.com --git-protocol https --web \
            || fail "GitHub sign-in did not finish. Run this script again to retry."
    fi
    if [ -z "$VERSION" ]; then
        VERSION="$(gh release view --repo "$REPO" --json tagName -q .tagName)" \
            || fail "could not find the latest build on GitHub."
    fi
    label="$VERSION"
    if [ -x "$RIG_HOME/builds/$label/GigRig" ]; then
        ok "Build $label is already here"
    else
        tmp="$(mktemp -d)"
        gh release download "$label" --repo "$REPO" --pattern 'GigRig-Linux.tar.gz' --dir "$tmp" \
            || fail "could not download $label. Check the name on the Releases page."
        mkdir -p "$RIG_HOME/builds/$label"
        tar -xzf "$tmp/GigRig-Linux.tar.gz" -C "$RIG_HOME/builds/$label"
        rm -rf "$tmp"
        ok "Downloaded $label"
    fi
fi

app="$RIG_HOME/builds/$label/GigRig"
chmod +x "$app"

if ldd "$app" | grep -q 'not found'; then
    ldd "$app" | grep 'not found'
    fail "the app needs a library this computer does not have (listed above)."
fi

# Never switch to a build that fails its own checks.
selftest="$RIG_HOME/logs/self-test-$label.txt"
if xvfb-run -a "$app" --self-test "$selftest" > /dev/null 2>&1; then
    ok "Build $label passed its self-test"
else
    tail -20 "$selftest" 2>/dev/null || true
    fail "build $label failed its self-test, so it was not switched on. The rig still runs the build it had."
fi

previous="$(readlink "$RIG_HOME/current" 2>/dev/null || true)"
ln -sfn "builds/$label" "$RIG_HOME/current"
[ -n "$previous" ] && [ "$previous" != "builds/$label" ] && ok "Switched from ${previous#builds/} to $label (linux/rollback.sh goes back)"
ok "The rig now runs build $label"

# The launcher: restarts the app if it ever crashes, keeps a dated log.
mkdir -p "$HOME/.local/bin"
cat > "$HOME/.local/bin/gigrig" <<EOF
#!/usr/bin/env bash
# Gig Rig launcher (written by linux/install.sh). Restarts the app if it crashes;
# quitting it normally (exit code 0) stops it for good until the next start.
mkdir -p "$RIG_HOME/logs"
log="$RIG_HOME/logs/app-\$(date +%Y-%m-%d).log"
crashes=0
while true; do
    echo "\$(date '+%F %T') starting \$(readlink "$RIG_HOME/current")" >> "\$log"
    started=\$(date +%s)
    "$RIG_HOME/current/GigRig" "\$@" >> "\$log" 2>&1
    code=\$?
    echo "\$(date '+%F %T') app ended with code \$code" >> "\$log"
    [ "\$code" -eq 0 ] && exit 0
    if [ \$(( \$(date +%s) - started )) -lt 30 ]; then crashes=\$((crashes + 1)); else crashes=0; fi
    if [ "\$crashes" -ge 5 ]; then
        echo "\$(date '+%F %T') crashed 5 times in a row within 30 s; giving up" >> "\$log"
        exit 1
    fi
    sleep 1
done
EOF
chmod +x "$HOME/.local/bin/gigrig"
ok "Launcher: gigrig (logs in $RIG_HOME/logs)"

# A menu entry, so the app can be opened by clicking it.
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/gigrig.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Gig Rig
Comment=The rig's audio engine, looper and tests
Exec=$HOME/.local/bin/gigrig
Icon=audio-card
Terminal=false
Categories=AudioVideo;Audio;
EOF
ok "Menu entry: Gig Rig"

# ---------------------------------------------------------------------------
if [ "$MODE" = rig ]; then
    say "Start-up straight into the rig"

    mkdir -p "$HOME/.config/autostart"
    cp "$HOME/.local/share/applications/gigrig.desktop" "$HOME/.config/autostart/gigrig.desktop"
    echo "X-GNOME-Autostart-Delay=2" >> "$HOME/.config/autostart/gigrig.desktop"
    ok "The app opens by itself after log-in"

    if [ -f /etc/gdm3/custom.conf ]; then
        # Log in automatically, and use the X11 desktop rather than Wayland so that
        # Claude can take screenshots when troubleshooting over Remote Control.
        conf="$(sudo cat /etc/gdm3/custom.conf)"
        new="$(printf '%s\n' "$conf" | awk -v user="$USER" '
            BEGIN { done = 0 }
            /^\[daemon\]/ { print; print "AutomaticLoginEnable=true"; print "AutomaticLogin=" user; print "WaylandEnable=false"; done = 1; next }
            /^#? *AutomaticLoginEnable *=/ || /^#? *AutomaticLogin *=/ || /^#? *WaylandEnable *=/ { next }
            { print }
            END { if (!done) { print "[daemon]"; print "AutomaticLoginEnable=true"; print "AutomaticLogin=" user; print "WaylandEnable=false" } }')"
        if [ "$conf" != "$new" ]; then
            sudo cp /etc/gdm3/custom.conf /etc/gdm3/custom.conf.before-gigrig 2>/dev/null || true
            printf '%s\n' "$new" | sudo tee /etc/gdm3/custom.conf > /dev/null
            REBOOT_NEEDED=1
        fi
        ok "Logs in as $USER by itself, on the X11 desktop"
    else
        note "No GDM log-in screen found, so automatic log-in was not set. Set it in the system settings."
    fi

    # No screen lock, blanking or sleep in the middle of a set.
    if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] && command -v gsettings > /dev/null 2>&1; then
        gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
        gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null || true
        gsettings set org.gnome.desktop.screensaver idle-activation-enabled false 2>/dev/null || true
        gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type 'nothing' 2>/dev/null || true
        gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-battery-type 'nothing' 2>/dev/null || true
        gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
        gsettings set org.gnome.desktop.notifications show-banners false 2>/dev/null || true
        gsettings set org.gnome.software download-updates false 2>/dev/null || true
        gsettings set org.gnome.software allow-updates false 2>/dev/null || true
        ok "No screen lock, blanking, sleep or pop-ups"
    else
        note "No desktop session, so the screen-lock and sleep settings were skipped. Run this again from the desktop."
    fi

    # Nothing updates by itself: a gig machine changes only when Tom (or Claude) updates it.
    put_file /etc/apt/apt.conf.d/99gigrig-no-auto-updates '// Gig Rig (linux/install.sh): no automatic updates on the rig
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::Unattended-Upgrade "0";
APT::Periodic::AutocleanInterval "0";' || true
    sudo systemctl disable --now unattended-upgrades.service > /dev/null 2>&1 || true
    sudo systemctl disable --now apt-daily.timer apt-daily-upgrade.timer > /dev/null 2>&1 || true
    if command -v snap > /dev/null 2>&1; then
        sudo snap refresh --hold > /dev/null 2>&1 || true
    fi
    ok "Automatic updates off (update by hand between gigs: see the guide)"
fi

# ---------------------------------------------------------------------------
say "Done"

echo "   Installed build: $label"
echo "   Check the set-up any time with:  $SCRIPT_DIR/rig-check.sh"
if [ "$REBOOT_NEEDED" -eq 1 ]; then
    echo
    echo "   >>> Restart the computer now to finish. <<<"
    [ "$MODE" = rig ] && echo "   After the restart it logs in and opens the Gig Rig app by itself."
elif [ "$RELOGIN_NEEDED" -eq 1 ]; then
    echo
    echo "   >>> Log out and back in once (top-right menu, Log Out), so the realtime rights apply. <<<"
    [ "$MODE" = test ] && echo "   On a USB stick, click the user name on the log-in screen to get back in (no password)."
fi
echo
