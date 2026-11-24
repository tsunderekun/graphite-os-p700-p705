#!/bin/sh
# Tap-navigate through the booted shell and snapshot key screens.
set -e
export DISPLAY=${DISPLAY:-:0}

snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}

xdotool key Home; sleep 0.6; snap t-home
xdotool mousemove 128 500 click 1; sleep 0.8; snap t-root
xdotool mousemove 240 662 click 1; sleep 0.8; snap t-access
xdotool key XF86Back; sleep 0.6
xdotool mousemove 240 202 click 1; sleep 0.8; snap t-wifi
xdotool key XF86Back; sleep 0.4
xdotool key Home; sleep 0.5
echo done
