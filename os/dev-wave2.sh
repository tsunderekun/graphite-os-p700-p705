#!/bin/sh
export DISPLAY=:0
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool mousemove 360 48 click 1; sleep 6
snap w-library
