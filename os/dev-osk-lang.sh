#!/bin/sh
export DISPLAY=:0
OSK=$(xwininfo -root -tree | awk '/480x288\+0\+512/{print $1; exit}')
echo "osk=$OSK"
xdotool mousemove 120 756
xdotool getmouselocation --shell
xdotool click 1
sleep 0.6
xwd -id "$OSK" -silent > /tmp/osk-lang.xwd
echo "after tap:"
xdotool getmouselocation --shell
