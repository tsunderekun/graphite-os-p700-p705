#!/bin/sh
# Detach the running shell from session-shell so screenshot loops can restart
# the shell without the supervisor killing X and falling back to the player.
pid=$(pidof graphite-shell)
[ -n "$pid" ] || { echo "no graphite-shell"; exit 1; }
ppid=$(awk '{print $4}' /proc/$pid/stat)
if [ -n "$ppid" ] && [ "$ppid" != "1" ]; then
    kill -9 "$ppid" 2>/dev/null
    echo "killed supervisor $ppid, shell $pid keeps running"
else
    echo "no supervisor for $pid"
fi
