#!/bin/sh
# OSK in xterm: open terminal -> xterm -> keyboard -> type -> Back.
set -e
export DISPLAY=:0
shell_id() { xwininfo -root -tree | awk '/graphite shell/{print $1; exit}'; }
xterm_id() { xwininfo -root -tree | awk '/"graphite"/{print $1; exit}'; }
osk_id() { xwininfo -root -tree | awk '/480x288\+0\+512/{print $1; exit}'; }
snap() { xwd -id "$1" -silent > "/tmp/$2.xwd"; echo "shot $2"; }

xdotool key Home; sleep 0.4
xdotool mousemove 352 314 click 1; sleep 0.7      # Терминал
xdotool mousemove 430 52 click 1; sleep 2.5       # XTERM
XPID=$(pidof xterm | awk '{print $1}')
[ -n "$XPID" ] || { echo "no xterm"; exit 1; }
echo "xterm=$XPID"
XW=$(xterm_id)
snap "$XW" osk-xterm

xdotool mousemove 320 16 click 1; sleep 1.2       # keyboard icon in status bar
OW=$(osk_id)
[ -n "$OW" ] || { echo "no osk window"; xwininfo -root -tree | head -14; exit 1; }
echo "osk=$OW"
snap "$OW" osk-keys

# EN, 'i', 'd', Enter
xdotool mousemove 120 756 click 1; sleep 0.35
xdotool mousemove 351 552 click 1; sleep 0.35
xdotool mousemove 136 620 click 1; sleep 0.35
xdotool mousemove 420 756 click 1; sleep 0.8
snap "$XW" osk-typed

xdotool key XF86Back; sleep 0.8                    # hide keyboard
snap "$(shell_id)" osk-closed
xdotool key XF86Back; sleep 2                      # close xterm
pidof xterm >/dev/null && echo "STILL RUNNING" || echo "xterm closed"
echo done
