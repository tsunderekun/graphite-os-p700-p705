#!/bin/sh
# Gesture checks: pivot swipes, edge-back, shade open/close.
set -e
export DISPLAY=${DISPLAY:-:0}
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
swipe() { # x1 y1 x2 y2
    xdotool mousemove "$1" "$2" mousedown 1
    xdotool mousemove "$3" "$4"
    xdotool mouseup 1
    sleep 0.7
}

xdotool key Home; sleep 0.5
xdotool mousemove 220 120 click 1; sleep 0.8; snap g-player
swipe 340 55 150 55; snap g-queue
swipe 340 55 150 55; snap g-info
swipe 150 55 340 55; snap g-queue-back
swipe 150 55 340 55; snap g-player-back

xdotool key Home; sleep 0.5
xdotool mousemove 128 500 click 1; sleep 0.8; snap g-root
swipe 5 400 120 400; snap g-home-edge

swipe 20 5 20 200; snap g-shade
swipe 240 330 240 200; snap g-shade-closed
xdotool key Home; sleep 0.4
echo done
