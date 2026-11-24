#!/bin/sh
# Read-only input probe: X keymap for the hardware keys and evdev capabilities.
export DISPLAY=${DISPLAY:-:0}
echo "== xmodmap (filtered) =="
xmodmap -pke 2>&1 | grep -i -E 'back|home|menu|power|XF86' | head -50
echo "== keypad devices =="
grep -i -A4 -B2 'keypad\|u0_keypad' /proc/bus/input/devices
echo "== evtest event1 =="
timeout 2 evtest /dev/input/event1 2>&1 | head -40
