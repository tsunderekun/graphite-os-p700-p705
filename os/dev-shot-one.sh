#!/bin/sh
# usage: dev-shot-one.sh <screen-id> <out-name>
set -e
export DISPLAY=${DISPLAY:-:0}
ID=${1:-home}
NAME=${2:-one}
killall graphite-shell 2>/dev/null
sleep 0.4
cd /home/user/os
GRAPHITE_SCREEN="$ID" nohup ./build/graphite-shell >/tmp/gs-$NAME.log 2>&1 &
sleep 2
WID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
xwd -id "$WID" -silent > "/tmp/$NAME.xwd"
echo "shot $ID -> /tmp/$NAME.xwd"
