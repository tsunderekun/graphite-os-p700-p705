#!/bin/sh
# Install the built shell and boot integration on the device (run as root).
set -e
cd "$(dirname "$0")"
[ -x build/graphite-shell ] || { echo "build first: OPT=-O0 sh build.sh"; exit 1; }

cp build/graphite-shell /usr/local/bin/graphite-shell
cp session-shell /usr/local/bin/session-shell
cp zz-mode.start /etc/local.d/zz-mode.start
mkdir -p /etc/X11/xorg.conf.d
cp 70-graphite-input.conf /etc/X11/xorg.conf.d/70-graphite-input.conf

# Medeo goes in its own directory: DejaVu stays where fontconfig/xterm expect it.
mkdir -p /usr/share/fonts/medeo
cp fonts/medeo/*.ttf fonts/medeo/OFL.txt fonts/medeo/README.md /usr/share/fonts/medeo/
chmod 644 /usr/share/fonts/medeo/*

chmod +x /usr/local/bin/graphite-shell /usr/local/bin/session-shell \
         /etc/local.d/zz-mode.start
sync
echo "installed: $(ls -l /usr/local/bin/graphite-shell | awk '{print $5}') bytes"
