#!/bin/sh
export DISPLAY=:0
xdotool key XF86Back; sleep 1.2
echo "after back1: $(pidof dillo)"
xdotool key XF86Back; sleep 2
echo "after back2: $(pidof dillo)"
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/w-after-dillo.xwd
echo shot
