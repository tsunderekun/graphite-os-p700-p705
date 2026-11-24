#!/bin/sh
export DISPLAY=:0
xdotool key Home; sleep 0.4
xdotool mousemove 220 120 click 1; sleep 2
echo "before: $(mpc status | head -2 | tr '\n' ' ')"
xdotool mousemove 240 720 click 1; sleep 2
echo "after:  $(mpc status | head -2 | tr '\n' ' ')"
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-pause.xwd
echo shot
