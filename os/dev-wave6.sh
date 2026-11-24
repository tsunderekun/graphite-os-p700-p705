#!/bin/sh
export DISPLAY=:0
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
osk_snap() {
    OW=$(xwininfo -root -tree | awk '/480x288\+0\+512/{print $1; exit}')
    xwd -id "$OW" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.4
xdotool mousemove 352 314 click 1; sleep 0.8
xdotool mousemove 430 52 click 1; sleep 2.5
xdotool mousemove 320 16 click 1; sleep 1
osk_snap w-osk-fn
xdotool key XF86Back; sleep 0.6
xdotool key XF86Back; sleep 2
xdotool key Home; sleep 0.4
