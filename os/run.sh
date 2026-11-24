#!/bin/sh
# Run the shell on the current DISPLAY. Intended to be started by the session
# script after X is up (no WM, the shell is override-redirect fullscreen).
set -e
if [ -z "$DISPLAY" ]; then
    echo "graphite: DISPLAY is empty" >&2
    exit 1
fi
export HOME=${HOME:-/home/user}
export XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-/home/user/.config}
export XDG_CACHE_HOME=${XDG_CACHE_HOME:-/home/user/.cache}
exec /home/user/os/build/graphite-shell
