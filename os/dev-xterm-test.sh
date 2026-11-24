#!/bin/sh
# Stage 1 check: terminal -> XTERM (as user), type id, Back returns to shell.
set -e
export DISPLAY=${DISPLAY:-:0}

shell_id() { xwininfo -root -tree | awk '/graphite shell/{print $1; exit}'; }
xterm_id() { xwininfo -root -tree | awk '/"graphite"/{print $1; exit}'; }
snap() { # id name
    xwd -id "$1" -silent > "/tmp/$2.xwd"
    echo "shot $2"
}

xdotool key Home; sleep 0.5
xdotool mousemove 352 314 click 1; sleep 0.8   # Терминал на Пуске
xdotool mousemove 430 52 click 1; sleep 3      # XTERM в шапке
echo "xterm pid: $(pidof xterm)"
PID=$(pidof xterm | awk '{print $1}')
[ -n "$PID" ] || { echo "xterm did not start"; exit 1; }
grep -E '^Uid:' "/proc/$PID/status"
XW=$(xterm_id)
echo "xterm window: $XW"
snap "$XW" xt-start
xdotool type --delay 80 'id'
xdotool key Return
sleep 1
snap "$XW" xt-id
xdotool key XF86Back
sleep 2
pidof xterm >/dev/null && echo "STILL RUNNING" || echo "xterm closed"
snap "$(shell_id)" xt-back
echo done
