#!/bin/sh
# Development session: minimal X on fbdev + graphite shell, no WM, no desktop.
# Stops the fb player first (it owns /dev/fb0). Use at the console, or via ssh
# with sudo. Leaves X running after the script exits so you can screenshot.
set -e

DISPLAY_NUM=${DISPLAY_NUM:-:0}
SHELL_BIN=${SHELL_BIN:-/home/user/os/build/graphite-shell}

[ -x "$SHELL_BIN" ] || { echo "no shell binary: $SHELL_BIN"; exit 1; }

pkill -f /usr/local/bin/lp7player 2>/dev/null || true
pkill -f '^X :0' 2>/dev/null || true
pkill -f fbpan 2>/dev/null || true
sleep 0.5

/usr/local/bin/fbset16 16 || exit 1
echo 200 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null
# NOTE: writing 0 to /sys/class/graphics/fb0/console spins on this kernel
# (fb console unbind blocks in R state), so we leave the console as is and
# simply cover it with the fullscreen override-redirect window.

X "$DISPLAY_NUM" -nolisten tcp -ac vt1 -logfile /var/log/Xorg.0.log >/dev/null 2>&1 &
i=0
while [ ! -S /tmp/.X11-unix/X0 ] && [ $i -lt 20 ]; do
    sleep 1; i=$((i+1))
done
[ -S /tmp/.X11-unix/X0 ] || { echo "X did not start"; exit 1; }

export DISPLAY=$DISPLAY_NUM
export HOME=/home/user
export XDG_CONFIG_HOME=/home/user/.config
export XDG_CACHE_HOME=/home/user/.cache
xset s off -dpms 2>/dev/null || true
/usr/local/bin/fbpan-ng 10 >/dev/null 2>&1 &
sleep 1

echo "starting shell on $DISPLAY"
nohup "$SHELL_BIN" >/tmp/graphite-shell.log 2>&1 &
sleep 2
ps w | grep -E 'graphite-shell' | grep -v grep || { echo "shell died:"; cat /tmp/graphite-shell.log; exit 1; }
echo "shell up"
