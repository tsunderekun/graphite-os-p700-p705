#!/bin/sh
# Build the graphite shell on the device (Alpine, g++ 14, libX11 + freetype).
#   ./build.sh            build everything -> build/graphite-shell
#   ./build.sh screens/home.cpp   syntax-check one translation unit only
set -e
cd "$(dirname "$0")"

CXX=${CXX:-g++}
OPT=${OPT:--O2}
FLAGS="$OPT -std=c++17 -Wall -Wextra -Iinclude -Iscreens -I/usr/include/freetype2 -I/usr/include/libpng16"

mkdir -p build

if [ $# -gt 0 ]; then
    for f in "$@"; do
        o="build/$(basename "$f" .cpp).o"
        echo "  CXX $f"
        $CXX $FLAGS -c "$f" -o "$o"
    done
    exit 0
fi

for f in src/*.cpp screens/*.cpp; do
    o="build/$(basename "$f" .cpp).o"
    case "$f" in
        *screens/*) o="build/scr_$(basename "$f" .cpp).o" ;;
    esac
    if [ -f "$o" ] && [ "$o" -nt "$f" ] && [ -z "$FORCE" ]; then
        continue
    fi
    echo "  CXX $f"
    $CXX $FLAGS -c "$f" -o "$o"
done

echo "  LD  build/graphite-shell"
$CXX -o build/graphite-shell build/*.o -lX11 -lXtst -lfreetype
echo "ok: $(pwd)/build/graphite-shell"
