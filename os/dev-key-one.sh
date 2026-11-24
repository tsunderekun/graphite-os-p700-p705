#!/bin/sh
# Send one keysym, snapshot after a short delay.
# usage: dev-key-one.sh <keysym> <name>
set -e
export DISPLAY=${DISPLAY:-:0}
KS=${1:-Menu}
NAME=${2:-key}
xdotool key "$KS"
sleep 0.6
ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$ID" -silent > "/tmp/$NAME.xwd"
echo "sent $KS -> /tmp/$NAME.xwd"
