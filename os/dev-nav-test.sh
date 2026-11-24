#!/bin/sh
# Check the navigation stack: home -> settings -> wifi -> back -> back.
set -e
export DISPLAY=${DISPLAY:-:0}
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.5
xdotool mousemove 128 500 click 1; sleep 0.8; snap nav-root
xdotool mousemove 240 202 click 1; sleep 1.0; snap nav-wifi
xdotool key XF86Back; sleep 0.7; snap nav-back-root
xdotool key XF86Back; sleep 0.7; snap nav-back-home
echo done
