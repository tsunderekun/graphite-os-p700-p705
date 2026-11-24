#!/bin/sh
# Restart from build/ and run the OSK test.
set -e
export DISPLAY=:0
sh /home/user/os/dev-unsupervise.sh >/dev/null 2>&1 || true
killall xterm 2>/dev/null || true
killall matchbox-keyboard 2>/dev/null || true
killall graphite-shell 2>/dev/null || true
sleep 0.6
nohup /home/user/os/build/graphite-shell >/tmp/gs.log 2>&1 &
sleep 2
sh /home/user/os/dev-osk-test.sh
