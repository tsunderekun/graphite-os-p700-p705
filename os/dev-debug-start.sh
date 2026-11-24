#!/bin/sh
# Restart X + shell with key logging (development only).
set -e
export DISPLAY=:0
export HOME=/home/user
export XDG_CONFIG_HOME=/home/user/.config
export XDG_CACHE_HOME=/home/user/.cache
killall graphite-shell 2>/dev/null
killall fbpan-ng 2>/dev/null
if ! pidof Xorg >/dev/null; then rm -f /tmp/.X0-lock /tmp/.X11-unix/X0; fi
sleep 0.5
if [ ! -S /tmp/.X11-unix/X0 ]; then
    X :0 -nolisten tcp -ac vt1 -logfile /var/log/Xorg.0.log >/dev/null 2>&1 &
    i=0
    while [ ! -S /tmp/.X11-unix/X0 ] && [ $i -lt 20 ]; do sleep 1; i=$((i+1)); done
fi
xset s off -dpms 2>/dev/null
pidof fbpan-ng >/dev/null || /usr/local/bin/fbpan-ng 10 >/dev/null 2>&1 &
rm -f /tmp/keys.log
GRAPHITE_KEYDEBUG=1 nohup /home/user/os/build/graphite-shell >/tmp/gs.log 2>&1 &
sleep 2
echo "shell: $(pidof graphite-shell)"
