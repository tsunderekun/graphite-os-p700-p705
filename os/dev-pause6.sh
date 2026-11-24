#!/bin/sh
export DISPLAY=:0
xdotool key Home; sleep 0.5
xdotool mousemove 240 120 click 1; sleep 2
echo "== player"
mpc status
xdotool mousemove 240 720 click 1; sleep 2
echo "== after pause"
mpc status
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-pause6.xwd
echo shot
