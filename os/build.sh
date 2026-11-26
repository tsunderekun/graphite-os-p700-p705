#!/bin/sh
# Build the graphite shell on the device (Alpine, g++ 14, libX11 + freetype).
#   ./build.sh            build everything -> build/graphite-shell
#   ./build.sh screens/home.cpp   syntax-check one translation unit only
set -e
cd "$(dirname "$0")"

CXX=${CXX:-g++}
OPT=${OPT:--O2}
FLAGS="$OPT -std=c++17 -Wall -Wextra -Iinclude -Iscreens -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/libxml2"

mkdir -p build

# Incomplete header dependency tracking: if a public header is newer than the
# last linked shell, stale objects may have the old struct ABI. Rebuild them.
if [ -x build/graphite-shell ]; then
    for h in include/*.h; do
        if [ "$h" -nt build/graphite-shell ]; then
            echo "  CLEAN headers changed; rebuilding all objects"
            rm -f build/*.o
            break
        fi
    done
fi

if [ "${1:-}" = "lg-reboot" ]; then
    echo "  CXX tools/lg-reboot.cpp"
    $CXX $OPT -std=c++17 -Wall -Wextra tools/lg-reboot.cpp -o build/lg-reboot
    exit 0
fi

if [ "${1:-}" = "fbdmg" ]; then
    echo "  CC tools/fbdmg.c"
    ${CC:-gcc} $OPT -Wall -Wextra tools/fbdmg.c -lX11 -lXdamage -o build/fbdmg
    exit 0
fi

if [ "${1:-}" = "lg-usb-mode" ]; then
    echo "  CXX tools/lg-usb-mode.cpp"
    $CXX $OPT -std=c++17 -Wall -Wextra tools/lg-usb-mode.cpp -o build/lg-usb-mode
    exit 0
fi

if [ "${1:-}" = "reader_probe" ]; then
    echo "  CXX tools/reader_probe.cpp src/reader.cpp src/reader_fb2.cpp src/reader_epub.cpp"
    $CXX $OPT -std=c++17 -Wall -Wextra -Iinclude -I/usr/include/freetype2 -I/usr/include/libxml2 tools/reader_probe.cpp src/reader.cpp src/reader_fb2.cpp src/reader_epub.cpp -lfreetype -lxml2 -lzip -o build/reader_probe
    exit 0
fi

if [ "${1:-}" = "reader_epub_test" ]; then
    $CXX $OPT -std=c++17 -Wall -Wextra -Iinclude -I/usr/include/libxml2 tools/reader_epub_test.cpp src/reader.cpp src/reader_epub.cpp -lxml2 -lzip -o build/reader_epub_test
    build/reader_epub_test
    exit 0
fi

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
$CXX -o build/graphite-shell build/*.o -lX11 -lXtst -lXext -lfreetype -lxml2 -lzip
echo "  CXX tools/lg-reboot.cpp"
$CXX $OPT -std=c++17 -Wall -Wextra tools/lg-reboot.cpp -o build/lg-reboot
echo "  CXX tools/lg-usb-mode.cpp"
$CXX $OPT -std=c++17 -Wall -Wextra tools/lg-usb-mode.cpp -o build/lg-usb-mode
echo "  CC tools/fbdmg.c"
${CC:-gcc} $OPT -Wall -Wextra tools/fbdmg.c -lX11 -lXdamage -o build/fbdmg
echo "ok: $(pwd)/build/graphite-shell"
