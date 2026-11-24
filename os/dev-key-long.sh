#!/bin/sh
# Long-press Power via XTest, then snapshot.
set -e
export DISPLAY=${DISPLAY:-:0}
xdotool key Home
sleep 0.5
xdotool keydown XF86PowerOff
sleep 1.5
xdotool keyup XF86PowerOff
sleep 0.8
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > /tmp/k-power2.xwd
echo "captured $ID"
