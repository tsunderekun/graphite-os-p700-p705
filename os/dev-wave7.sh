#!/bin/sh
export DISPLAY=:0
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.4
xdotool mousemove 352 500 click 1; sleep 1.2
snap w-notes
xdotool key Home; sleep 0.4
