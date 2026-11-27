#!/usr/bin/env bash
# Gig Rig: update the app in one step.
#
# Fetches the newest scripts and the newest build, checks the build with its own self-test,
# and only then switches to it. The build that was running stays on the computer, so
# linux/rollback.sh (or the "Gig Rig: go back a build" icon) goes back in one step.
# It asks for no password and changes nothing else about the computer.
#
#   linux/update.sh                    the newest build
#   linux/update.sh build-131          that numbered build
#   linux/update.sh --restart          also close the app and open it again, without asking
#   linux/update.sh --no-restart       leave the running app alone, without asking
#
# Update between gigs, never on the day of one. The "Gig Rig: update" icon runs this.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RIG_HOME="${GIGRIG_HOME:-$HOME/gigrig}"

RESTART="ask"
TAG=""
while [ $# -gt 0 ]; do
    case "$1" in
        --restart)    RESTART="yes" ;;
        --no-restart) RESTART="no" ;;
        -h|--help)    sed -n '2,14p' "$0"; exit 0 ;;
        -*)           echo "Unknown option: $1 (try --help)"; exit 2 ;;
        *)            TAG="$1" ;;
    esac
    shift
done

before="$(basename "$(readlink "$RIG_HOME/current" 2>/dev/null || echo none)")"
echo "Gig Rig update. Running now: $before"

# The scripts and guides first, so a newer build is installed by the installer made for it.
if [ -d "$SOURCE_DIR/.git" ]; then
    if git -C "$SOURCE_DIR" pull --ff-only --quiet 2>/dev/null; then
        echo "   The scripts and guides are up to date."
    else
        echo "   NOTE: could not fetch newer scripts (no internet, or this copy was changed by hand). Carrying on with the ones here."
    fi
fi

args=(--app-only --yes)
[ -n "$TAG" ] && args+=(--version "$TAG")

# Run the installer afresh, in case the pull above changed it.
"$SCRIPT_DIR/install.sh" "${args[@]}"

after="$(basename "$(readlink "$RIG_HOME/current" 2>/dev/null || echo none)")"

if [ "$after" = "$before" ]; then
    echo
    echo "Nothing to do: $after is already the build in use."
    exit 0
fi

echo
echo "Switched from $before to $after."

# The app that is open is still the old build. Closing it stops the sound, so it is never done without a yes.
if pgrep -x GigRig > /dev/null 2>&1; then
    if [ "$RESTART" = "ask" ]; then
        if [ -t 0 ]; then
            read -r -p "Close the Gig Rig app and open the new build now? The sound stops for a few seconds. [y/N] " answer
            case "${answer:-n}" in [Yy]*) RESTART="yes" ;; *) RESTART="no" ;; esac
        else
            RESTART="no"
        fi
    fi

    if [ "$RESTART" = "yes" ]; then
        # After a plain quit the launcher stops; after anything else it opens the app again by
        # itself, on the new build. So: ask the app to close, wait, and open it only if nothing did.
        pkill -TERM -x GigRig 2>/dev/null || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -x GigRig > /dev/null 2>&1 || break; sleep 0.5; done
        sleep 3
        if pgrep -x GigRig > /dev/null 2>&1; then
            echo "The app has opened again on $after."
        elif [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
            nohup "$HOME/.local/bin/gigrig" > /dev/null 2>&1 &
            echo "The app is opening on $after."
        else
            echo "The app is closed. Open it from its icon (this window has no screen to open it on)."
        fi
    else
        echo "The app that is open is still $before. Quit it and open it again (or restart the computer) to use $after."
    fi
else
    echo "Open the Gig Rig app to use it."
fi

echo "To go back: $SCRIPT_DIR/rollback.sh"
