#!/usr/bin/env bash
# Gig Rig: the whole set-up of a fresh Ubuntu computer from one line.
#
# On a fresh Ubuntu 24.04, open a Terminal (Ctrl + Alt + T), paste this and press Enter:
#
#   wget -qO- https://raw.githubusercontent.com/TomBoothMusic/gig-rig/main/linux/bootstrap.sh | bash
#
# (The code is public, so that line needs no sign-in. If it is ever private again, this
# script notices, signs in to GitHub with a one-time code, and carries on.)
#
# It installs git, fetches the code into ~/gig-rig-code (or brings a copy that is already
# there up to date) and runs linux/install.sh, which does everything else and says what it
# is doing. Anything after "bash -s --" is handed to install.sh, for example:
#
#   wget -qO- .../bootstrap.sh | bash -s -- --test
#
# Run it as the normal user, not with sudo. It asks for the password when it needs it.

set -euo pipefail

REPO="TomBoothMusic/gig-rig"
CODE="${GIGRIG_CODE:-$HOME/gig-rig-code}"

[ "$(id -u)" -ne 0 ] || { echo "Run this as your normal user, without sudo."; exit 1; }
command -v apt-get > /dev/null || { echo "This is for Ubuntu (or a close relative). See docs/linux-setup.md."; exit 1; }

echo "== Gig Rig: fetching what the installer needs"
if ! command -v git > /dev/null 2>&1 || ! command -v curl > /dev/null 2>&1; then
    sudo apt-get -o Acquire::Retries=5 update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y -qq git curl ca-certificates
fi

if [ -d "$CODE/.git" ]; then
    git -C "$CODE" pull --ff-only --quiet || echo "   NOTE: could not update $CODE; using it as it is."
elif ! git clone --quiet "https://github.com/$REPO.git" "$CODE" 2>/dev/null; then
    # The code is private: sign in with gh, then fetch it that way.
    echo "== The code is private, so GitHub needs a sign-in first"
    if ! command -v gh > /dev/null 2>&1; then
        sudo add-apt-repository -y universe > /dev/null 2>&1 || true
        sudo apt-get -o Acquire::Retries=5 update -qq
        sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y -qq gh
    fi
    gh auth status > /dev/null 2>&1 || gh auth login --hostname github.com --git-protocol https --web < /dev/tty
    gh repo clone "$REPO" "$CODE"
fi

echo "== Gig Rig: the code is in $CODE"

# The installer asks one question, so give it the keyboard even when this script arrived down a pipe.
if [ -t 0 ]; then
    exec "$CODE/linux/install.sh" "$@"
elif [ -r /dev/tty ]; then
    exec "$CODE/linux/install.sh" "$@" < /dev/tty
else
    exec "$CODE/linux/install.sh" --yes "$@"
fi
