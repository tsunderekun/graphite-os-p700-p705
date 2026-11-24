#!/bin/sh
# Render the current shell in a throwaway Xvfb display and save a PNG.
#
#   sh os/preview.sh home            -> artifacts/host/home.png  (display :99)
#   sh os/preview.sh home :93        -> second agent's own display
#
# Run inside WSL:  wsl -e bash -lc "cd /mnt/c/path/to/repo && sh os/preview.sh home"
set -e
cd "$(dirname "$0")"
NAME=${1:-screen}
DISP=${2:-:99}
N=${DISP#:}
OUT=../artifacts/host
mkdir -p "$OUT"

sh host-build.sh

if [ ! -S "/tmp/.X11-unix/X$N" ]; then
    Xvfb "$DISP" -screen 0 480x800x16 >/tmp/xvfb-$N.log 2>&1 &
    sleep 1
fi

GRAPHITE_FONTS=${GRAPHITE_FONTS:-../shell/fonts} \
GRAPHITE_SCREEN=${GRAPHITE_SCREEN:-home} \
DISPLAY=$DISP ./build/graphite-host >/tmp/graphite-$N.log 2>&1 &
PID=$!
sleep 1.5

DISPLAY=$DISP xwd -root -silent > "$OUT/$NAME.xwd"
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true

python3 xwd2png.py "$OUT/$NAME.xwd" "$OUT/$NAME.png"
echo "$OUT/$NAME.png"
