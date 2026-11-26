#!/bin/sh
# Build the shell for the host (WSL/Ubuntu) so screens can be previewed without
# touching the phone. Serialized with flock: several agents may build at once.
set -e
cd "$(dirname "$0")"
mkdir -p build ../artifacts/host

exec 9>/tmp/graphite-build.lock
flock 9

CXX=${CXX:-g++}
$CXX -O1 -std=c++17 -Wall -Wextra -Iinclude -Iscreens \
    -I/usr/include/freetype2 -I/usr/include/libxml2 -o build/graphite-host \
    src/*.cpp screens/*.cpp -lX11 -lXtst -lXext -lfreetype -lxml2 -lzip
echo "ok: build/graphite-host"
