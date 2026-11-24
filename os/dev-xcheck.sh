#!/bin/sh
echo "XAUTHORITY=${XAUTHORITY:-<unset>}"
setpriv --reuid=10000 --regid=10000 --clear-groups env -u XAUTHORITY DISPLAY=:0 xset q >/dev/null 2>&1 \
    && echo USER_X_OK_NOAUTH || echo USER_X_DENIED_NOAUTH
setpriv --reuid=10000 --regid=10000 --clear-groups env DISPLAY=:0 xset q >/dev/null 2>&1 \
    && echo USER_X_OK || echo USER_X_DENIED
ls -l /tmp/.X11-unix/X0
ls -l /home/user/.Xauthority 2>&1
