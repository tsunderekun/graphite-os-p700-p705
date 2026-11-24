#!/bin/sh
# Start matchbox-keyboard as the current user and show windows/focus.
export DISPLAY=${DISPLAY:-:0}
nohup matchbox-keyboard -o portrait keyboard-us >/tmp/mbk.log 2>&1 < /dev/null &
sleep 1
xwininfo -root -tree | head -18
echo "focus: $(xdotool getwindowfocus 2>/dev/null)"
echo "matchbox log:"
cat /tmp/mbk.log
