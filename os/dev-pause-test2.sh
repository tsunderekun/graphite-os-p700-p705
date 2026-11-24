#!/bin/sh
export DISPLAY=:0
mpc play >/dev/null 2>&1 || mpc play 1 >/dev/null 2>&1
sleep 1
echo "start: $(mpc status | grep -E '\\[' || true)"
xdotool key Home; sleep 0.4
xdotool mousemove 220 120 click 1; sleep 2
xdotool mousemove 240 720 click 1; sleep 2
echo "after tap: $(mpc status | grep -E '\\[' || true)"
xdotool mousemove 240 720 click 1; sleep 2
echo "after tap2: $(mpc status | grep -E '\\[' || true)"
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-pause2.xwd
echo shot
