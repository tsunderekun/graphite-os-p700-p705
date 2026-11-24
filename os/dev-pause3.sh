#!/bin/sh
export DISPLAY=:0
mpc play >/dev/null 2>&1 || mpc play 1 >/dev/null 2>&1
sleep 1
echo "== before"
mpc status
xdotool key Home; sleep 0.5
xdotool mousemove 240 120 click 1; sleep 3
echo "== in player"
mpc status
xdotool mousemove 240 720 click 1; sleep 2
echo "== after pause tap"
mpc status
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-pause3.xwd
echo shot
