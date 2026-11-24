#!/bin/sh
export DISPLAY=:0
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.5
xdotool mousemove 220 120 click 1; sleep 4
snap w-player
