#!/bin/sh
export DISPLAY=:0
mpc play >/dev/null 2>&1
sleep 1
PID=$(pidof graphite-shell)
strace -f -p "$PID" -e trace=sendto,recvfrom,send,recv -s 200 -tt -o /tmp/mpd-trace.txt &
SPID=$!
sleep 0.5
xdotool key Home; sleep 0.4
xdotool mousemove 240 120 click 1; sleep 1.5
xdotool mousemove 240 720 click 1; sleep 2
kill $SPID 2>/dev/null
echo "== trace"
grep -a -E 'pause|toggle|status' /tmp/mpd-trace.txt | tail -30
echo "== state"
mpc status
