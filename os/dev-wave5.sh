#!/bin/sh
export DISPLAY=:0
xdotool key Home; sleep 0.5
xdotool mousemove 444 780 click 1; sleep 0.8
xdotool mousemove 240 284 click 1; sleep 6
echo "dillo pid: $(pidof dillo)"
xwininfo -root -tree | head -18
