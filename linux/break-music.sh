#!/usr/bin/env bash
# Gig Rig: let a phone play the break music over Bluetooth (docs/break-music.md, "The phone").
#
# NOT PROVEN ON REAL HARDWARE YET. It was written without a rig to try it on, so treat the
# first run as a test, away from a gig, and paste what it prints to Claude if anything looks wrong.
# It is not part of linux/install.sh: nothing here happens unless this script is run.
#
#   linux/break-music.sh            set it up (safe to run again)
#   linux/break-music.sh --status   say what is running and whether a phone is connected
#   linux/break-music.sh --remove   take it all out again
#
# What it sets up:
#   - Bluetooth switched on, with the computer offering itself to phones as a speaker.
#     (BlueALSA does this. The desktop's own sound system, PipeWire, stays off, so the
#     app keeps the audio interface to itself.)
#   - A small service that hands the phone's sound to the app through the pipe the app reads,
#     ~/gigrig/break-music.pipe: 16-bit stereo at 44.1 kHz.
# In the app, choose "Phone (Bluetooth or Wi-Fi)" on the Break music tab and press Play.
#
# Everything it changes is listed at the end of docs/linux-setup.md.
# Run it as the normal user, not with sudo. It asks for the password when it needs it.

set -euo pipefail

RIG_HOME="${GIGRIG_HOME:-$HOME/gigrig}"
PIPE="$RIG_HOME/break-music.pipe"
DAEMON_UNIT=/etc/systemd/system/gigrig-bluealsa.service
USER_UNIT="$HOME/.config/systemd/user/gigrig-phone-audio.service"
FEEDER="$HOME/.local/bin/gigrig-phone-audio"

say()  { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
ok()   { printf '   \033[32mOK\033[0m   %s\n' "$*"; }
note() { printf '   \033[33mNOTE\033[0m %s\n' "$*"; }
fail() { printf '\n\033[1;31mSTOPPED: %s\033[0m\n' "$*"; exit 1; }

[ "$(id -u)" -ne 0 ] || fail "run this as your normal user, without sudo."

status() {
    say "Phone sound for the break music"
    if systemctl is-active --quiet bluetooth.service 2>/dev/null; then ok "Bluetooth is on"; else note "Bluetooth is off (bluetooth.service is not running)"; fi
    if systemctl is-active --quiet gigrig-bluealsa.service 2>/dev/null; then ok "The computer is offering itself to phones as a speaker"; else note "The speaker service (gigrig-bluealsa) is not running"; fi
    if systemctl --user is-active --quiet gigrig-phone-audio.service 2>/dev/null; then ok "The hand-over to the app is running"; else note "The hand-over service (gigrig-phone-audio) is not running"; fi
    if [ -p "$PIPE" ]; then ok "The pipe is there: $PIPE"; else note "The pipe is not there yet: $PIPE (the app makes it when Phone is played)"; fi
    if command -v bluealsa-cli > /dev/null 2>&1; then
        pcms="$(bluealsa-cli list-pcms 2>/dev/null | grep 'a2dpsnk/source' || true)"
        if [ -n "$pcms" ]; then ok "A phone is connected and can play: $pcms"; else note "No phone is connected for music right now"; fi
    fi
}

if [ "${1:-}" = "--status" ]; then status; exit 0; fi

if [ "${1:-}" = "--remove" ]; then
    say "Taking the phone sound set-up out"
    systemctl --user disable --now gigrig-phone-audio.service > /dev/null 2>&1 || true
    rm -f "$USER_UNIT" "$FEEDER"
    systemctl --user daemon-reload > /dev/null 2>&1 || true
    sudo systemctl disable --now gigrig-bluealsa.service > /dev/null 2>&1 || true
    sudo rm -f "$DAEMON_UNIT"
    sudo systemctl daemon-reload
    ok "Removed. The packages (bluez, bluez-alsa-utils) are left installed; they do nothing by themselves."
    exit 0
fi

[ -z "${1:-}" ] || fail "unknown option: $1 (see the top of this file)"
command -v apt-get > /dev/null || fail "this script is for Ubuntu (or a close relative)."

say "Installing the Bluetooth sound packages"
sudo add-apt-repository -y universe > /dev/null 2>&1 || true
sudo apt-get -o Acquire::Retries=5 update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends bluez bluez-alsa-utils \
    || fail "the packages would not install. Check the internet connection and run this again."
command -v bluealsa > /dev/null 2>&1 || fail "the bluealsa program is not where it was expected. Paste this to Claude."
ok "bluez and bluez-alsa-utils installed"

say "Bluetooth on, and the computer offered to phones as a speaker"
sudo systemctl enable --now bluetooth.service > /dev/null 2>&1 || note "bluetooth.service would not start. Is there a Bluetooth radio in this computer?"

# The packaged services (if the package brought any) would compete with ours for the same job.
for unit in bluealsa.service bluez-alsa.service bluealsa-aplay.service; do
    sudo systemctl disable --now "$unit" > /dev/null 2>&1 || true
done

# a2dp-sink: receive music from a phone. --a2dp-force-audio-cd: ask the phone for 44.1 kHz,
# which is what the app expects in the pipe.
printf '%s\n' "[Unit]
Description=Gig Rig: receive music from a phone over Bluetooth (BlueALSA)
After=bluetooth.service
Requires=bluetooth.service

[Service]
ExecStart=$(command -v bluealsa) -S -p a2dp-sink --a2dp-force-audio-cd
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target" | sudo tee "$DAEMON_UNIT" > /dev/null
sudo systemctl daemon-reload
sudo systemctl enable gigrig-bluealsa.service > /dev/null 2>&1 || true
sudo systemctl restart gigrig-bluealsa.service || note "The speaker service would not start. Run this with --status and paste the result to Claude."
ok "Speaker service: gigrig-bluealsa"

say "Handing the phone's sound to the app"
mkdir -p "$RIG_HOME" "$(dirname "$FEEDER")" "$(dirname "$USER_UNIT")"
[ -p "$PIPE" ] || { rm -f "$PIPE"; mkfifo -m 660 "$PIPE"; }

# The feeder: waits for a phone, then pours its sound into the pipe. Opening the pipe waits
# until the app is playing "Phone"; when the app stops, the pour ends and it waits again.
cat > "$FEEDER" <<EOF
#!/usr/bin/env bash
# Gig Rig (written by linux/break-music.sh): the phone's Bluetooth sound into the app's pipe.
pipe="$PIPE"
[ -p "\$pipe" ] || mkfifo -m 660 "\$pipe"
while true; do
    pcm="\$(bluealsa-cli list-pcms 2>/dev/null | grep -m1 'a2dpsnk/source' || true)"
    if [ -z "\$pcm" ]; then sleep 2; continue; fi
    bluealsa-cli open "\$pcm" > "\$pipe" 2>/dev/null || true
    sleep 1
done
EOF
chmod +x "$FEEDER"

printf '%s\n' "[Unit]
Description=Gig Rig: the phone's Bluetooth sound into the break music pipe

[Service]
ExecStart=$FEEDER
Restart=always
RestartSec=2

[Install]
WantedBy=default.target" > "$USER_UNIT"

if systemctl --user show-environment > /dev/null 2>&1; then
    systemctl --user daemon-reload
    systemctl --user enable gigrig-phone-audio.service > /dev/null 2>&1 || true
    systemctl --user restart gigrig-phone-audio.service || note "The hand-over service would not start. Run this with --status."
    ok "Hand-over service: gigrig-phone-audio (starts by itself at log-in)"
else
    mkdir -p "$HOME/.config/systemd/user/default.target.wants"
    ln -sf "$USER_UNIT" "$HOME/.config/systemd/user/default.target.wants/gigrig-phone-audio.service"
    ok "Hand-over service: gigrig-phone-audio (starts at the next log-in)"
fi

if ! id -nG "$USER" | grep -qw audio; then
    sudo usermod -aG audio "$USER"
    note "Added $USER to the audio group: log out and back in once before trying it."
fi

say "Done. To try it"
echo "   1. On this computer open Settings, then Bluetooth, and leave that window open."
echo "      (While it is open the computer can be found by phones. Its name is shown there.)"
echo "   2. On the phone open Bluetooth settings and tap this computer's name. Accept the pairing on both."
echo "   3. In the Gig Rig app, Break music tab: choose \"Phone (Bluetooth or Wi-Fi)\" and press Play."
echo "   4. Play something on the phone, with the phone's volume up full."
echo "   Check it any time with:  $0 --status"
echo
