#!/bin/sh
# Capture the shell window itself (root capture is black with the fbdev driver).
set -e
export DISPLAY=${DISPLAY:-:0}
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
[ -n "$ID" ] || { echo "no shell window"; xwininfo -root -tree | head; exit 1; }
xwd -id "$ID" -silent > /tmp/win.xwd
echo "captured $ID -> /tmp/win.xwd"
