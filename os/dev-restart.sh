#!/bin/sh
# Restart the shell on the already-running X and take a snapshot.
set -e
DISPLAY=${DISPLAY:-:0}
export DISPLAY
killall graphite-shell 2>/dev/null || true
sleep 1
cd /home/user/os
nohup ./build/graphite-shell >/tmp/graphite-shell.log 2>&1 &
sleep 2
pidof graphite-shell >/dev/null || { echo "shell died:"; cat /tmp/graphite-shell.log; exit 1; }
xwd -root -silent > /tmp/shell.xwd
echo "ready: $(pidof graphite-shell), $(ls -l /tmp/shell.xwd | awk '{print $5}') bytes"
