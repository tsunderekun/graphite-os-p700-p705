#!/bin/sh
# Compile the named translation units (device, -O0) and relink.
#   sh build-files.sh src/app.cpp screens/player.cpp ...
set -e
cd "$(dirname "$0")"
for f in "$@"; do
    case "$f" in
        screens/*) o="build/scr_$(basename "$f" .cpp).o" ;;
        *)         o="build/$(basename "$f" .cpp).o" ;;
    esac
    echo "  CXX $f"
    g++ -O0 -std=c++17 -Iinclude -Iscreens -I/usr/include/freetype2 -c "$f" -o "$o"
done
echo "  LD  build/graphite-shell"
g++ -o build/graphite-shell build/*.o -lX11 -lXtst -lfreetype
ls -l build/graphite-shell
