#!/bin/sh
export DISPLAY=:0
# close any external app / keyboard first
xdotool key XF86Back; sleep 0.7
xdotool key XF86Back; sleep 2
pidof dillo && echo "dillo still running" || echo "no dillo"
xdotool key Home; sleep 0.7
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
xwd -id "$ID" -silent > /tmp/w-pause4.xwd
echo shot
