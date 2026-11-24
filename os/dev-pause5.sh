#!/bin/sh
export DISPLAY=:0
killall dillo 2>/dev/null || true
sleep 2
xdotool key Home; sleep 1
mpc play >/dev/null 2>&1 || mpc play 1 >/dev/null 2>&1
sleep 1
echo "== started"
mpc status
xdotool mousemove 240 120 click 1; sleep 3
echo "== player open"
mpc status
xdotool mousemove 240 720 click 1; sleep 2
echo "== after pause"
mpc status
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-pause5.xwd
echo shot
