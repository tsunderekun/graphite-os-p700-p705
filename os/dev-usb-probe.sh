#!/bin/sh
echo "== init.d =="
ls /etc/init.d/ | grep -i -E 'usb|udhcp|net'
echo "== conf.d =="
cat /etc/conf.d/unudhcpd 2>&1
echo "== local.d =="
ls /etc/local.d/
echo "== rndis refs =="
grep -R -l 'rndis\|android_usb' /etc /usr/share 2>/dev/null | head -10
echo "== lsmod usb =="
lsmod 2>/dev/null | grep -i usb | head
echo "== gadget attrs =="
for d in /sys/class/android_usb/android0/f_*; do
    echo "$(basename "$d")"
done
echo "== mass storage =="
ls /sys/class/android_usb/android0/f_mass_storage/lun 2>&1
echo "== charge only =="
ls /sys/class/android_usb/android0/f_charge_only 2>&1
