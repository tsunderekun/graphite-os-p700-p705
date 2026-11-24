#!/bin/sh
# Open xterm, show OSK, switch to EN, snapshot the keyboard.
set -e
export DISPLAY=:0
osk_id() { xwininfo -root -tree | awk '/480x288\+0\+512/{print $1; exit}'; }
xterm_id() { xwininfo -root -tree | awk '/"graphite"/{print $1; exit}'; }
xdotool key Home; sleep 0.4
xdotool mousemove 352 314 click 1; sleep 0.6
xdotool mousemove 430 52 click 1; sleep 2
xdotool mousemove 320 16 click 1; sleep 1
xdotool mousemove 120 756 click 1; sleep 0.6
OW=$(osk_id)
xwd -id "$OW" -silent > /tmp/osk-en.xwd
echo "osk=$OW"
