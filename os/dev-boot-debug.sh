#!/bin/sh
echo "== ps =="
ps w | grep -E 'session|graphite|Xorg|fbpan|lp7' | grep -v grep
echo "== session log =="
tail -30 /var/log/graphite-session.log
echo "== xorg log =="
grep -i -E 'fb0|fbdev|screens|open|error' /var/log/Xorg.0.log | tail -25
echo "== local =="
rc-status default 2>&1 | tail -6
ls -l /etc/local.d/zz-mode.start
echo "== fb =="
ls -l /dev/fb0
cat /sys/class/graphics/fb0/virtual_size 2>&1
