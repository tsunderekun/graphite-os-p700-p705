#!/bin/sh
# Open the shade and close it with a vertical swipe over a slider row.
set -e
export DISPLAY=${DISPLAY:-:0}
snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}
xdotool key Home; sleep 0.5
# open: pull down from the top edge
xdotool mousemove 20 5 mousedown 1 mousemove 20 200 mouseup 1; sleep 0.7; snap sh-open
# close: vertical swipe starting on the brightness slider (y=330)
xdotool mousemove 240 330 mousedown 1 mousemove 240 190 mouseup 1; sleep 0.7; snap sh-closed
echo done
