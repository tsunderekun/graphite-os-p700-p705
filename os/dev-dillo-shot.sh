#!/bin/sh
export DISPLAY=:0
WID=$(xwininfo -root -tree | awk '/"Dillo/{print $1; exit}')
[ -n "$WID" ] || { echo "no dillo"; exit 1; }
xwd -id "$WID" -silent > /tmp/w-dillo.xwd
echo "shot dillo $WID"
