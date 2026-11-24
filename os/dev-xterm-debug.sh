#!/bin/sh
# Restart the shell from build/ (no reboot) and run the xterm test with logs.
set -e
export DISPLAY=:0
sh /home/user/os/dev-unsupervise.sh >/dev/null 2>&1 || true
killall xterm 2>/dev/null || true
killall graphite-shell 2>/dev/null || true
sleep 0.6
rm -f /tmp/apphost.log
nohup /home/user/os/build/graphite-shell >/tmp/gs.log 2>&1 &
sleep 2
sh /home/user/os/dev-xterm-test.sh
echo "=== apphost.log ==="
cat /tmp/apphost.log 2>/dev/null
