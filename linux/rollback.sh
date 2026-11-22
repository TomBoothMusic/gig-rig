#!/usr/bin/env bash
# Gig Rig: go back to (or forward to) a build already on this computer.
#
#   linux/rollback.sh             list the builds here, and go back to the one before
#   linux/rollback.sh build-41    switch to build-41
#
# Nothing is downloaded and nothing is deleted. Restart the app afterwards
# (or the computer) to use the build switched to.

set -euo pipefail

RIG_HOME="${GIGRIG_HOME:-$HOME/gigrig}"
cd "$RIG_HOME/builds" 2>/dev/null || { echo "No builds installed yet: run linux/install.sh first."; exit 1; }

current="$(basename "$(readlink "$RIG_HOME/current" 2>/dev/null || echo none)")"
mapfile -t builds < <(ls -1tr)    # oldest first, by when they were installed

echo "Builds on this computer (oldest first):"
for b in "${builds[@]}"; do
    if [ "$b" = "$current" ]; then echo "  * $b   <- running now"; else echo "    $b"; fi
done

target="${1:-}"
if [ -z "$target" ]; then
    for i in "${!builds[@]}"; do
        if [ "${builds[$i]}" = "$current" ] && [ "$i" -gt 0 ]; then target="${builds[$((i - 1))]}"; fi
    done
    [ -n "$target" ] || { echo; echo "There is no earlier build to go back to."; exit 1; }
    echo
    read -r -p "Go back from $current to $target? [Y/n] " answer
    case "${answer:-y}" in [Yy]*) ;; *) echo "Nothing changed."; exit 0 ;; esac
fi

[ -x "$RIG_HOME/builds/$target/GigRig" ] || { echo "There is no build called $target here."; exit 1; }

ln -sfn "builds/$target" "$RIG_HOME/current"
echo
echo "The rig now runs $target. Quit the app and open it again (or restart the computer)."
