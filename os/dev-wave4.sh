#!/bin/sh
export DISPLAY=:0
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.5
xdotool mousemove 128 314 click 1; sleep 1
snap w-files
xdotool mousemove 128 156 mousedown 1; sleep 0.9; xdotool mouseup 1; sleep 0.6
snap w-files-sel
xdotool key XF86Back; sleep 0.4
xdotool key Home; sleep 0.4
