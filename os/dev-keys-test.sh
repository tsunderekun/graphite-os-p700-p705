#!/bin/sh
# Drive the hardware keys through XTest (xdotool) and snapshot the result.
set -e
export DISPLAY=${DISPLAY:-:0}
SHOT=/tmp/key

snap() {
    ID=$(xwininfo -root -tree | awk '/graphite shell/{print $1; exit}')
    xwd -id "$ID" -silent > "/tmp/$1.xwd"
    echo "shot $1"
}

# 1. Menu key -> action sheet
xdotool key XF86MenuKB; sleep 0.8; snap key-menu
# 2. Back -> close the sheet
xdotool key XF86Back; sleep 0.6; snap key-back
# 3. Home -> home screen
xdotool key Home; sleep 0.6; snap key-home
# 4. Power short -> lock screen
xdotool key XF86PowerOff; sleep 0.6; snap key-lock
# 5. Power short again -> unlock
xdotool key XF86PowerOff; sleep 0.6; snap key-unlock
# 6. Volume up -> MPD volume + OSD
xdotool key XF86AudioRaiseVolume; sleep 0.5; snap key-vol
# 7. Power long -> shutdown dialog
xdotool keydown XF86PowerOff; sleep 1; xdotool keyup XF86PowerOff
sleep 0.6; snap key-power
# leave on home
xdotool key Escape; sleep 0.4
echo done
