#!/bin/sh
# Render a set of screens on the device (X already running) and leave the
# shell back on home. xwd files land in /tmp/shot-<id>.xwd for pull.py.
set -e
cd /home/user/os
export DISPLAY=${DISPLAY:-:0}

for s in home apps edit set-root set-display set-wifi set-sound set-power \
         set-keys set-storage set-apps set-access set-system set-lab \
         set-about set-launch player queue info files terminal notes \
         shade lock power menu; do
    killall graphite-shell 2>/dev/null || true
    sleep 0.4
    GRAPHITE_SCREEN=$s nohup ./build/graphite-shell >/tmp/gs-$s.log 2>&1 &
    sleep 3
    if pidof graphite-shell >/dev/null; then
        ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
        xwd -id "$ID" -silent > /tmp/shot-$s.xwd
        echo "shot $s ($ID)"
    else
        echo "FAILED $s: $(cat /tmp/gs-$s.log)"
    fi
done

killall graphite-shell 2>/dev/null || true
sleep 0.4
nohup ./build/graphite-shell >/tmp/graphite-shell.log 2>&1 &
sleep 1
echo "back on home: $(pidof graphite-shell)"
