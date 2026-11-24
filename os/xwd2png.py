#!/usr/bin/env python3
"""xwd (depth 16/24/32, TrueColor) -> PNG, no ImageMagick needed."""
import struct
import sys

from PIL import Image


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    d = open(src, "rb").read()
    h = struct.unpack(">25I", d[:100])
    (header_size, version, pixmap_format, depth, width, height, xoffset,
     byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, bpp, bpl,
     visual_class, rmask, gmask, bmask, bits_per_rgb, cmap_entries, ncolors,
     win_w, win_h, win_x, win_y, bdr) = h
    off = header_size + ncolors * 12
    img = Image.new("RGB", (width, height))
    px = img.load()

    def chan(val, mask):
        if not mask:
            return 0
        shift = (mask & -mask).bit_length() - 1
        m = mask >> shift
        return int(round(((val & mask) >> shift) * 255 / m))

    fmt16 = "<H" if byte_order == 0 else ">H"
    fmt32 = "<I" if byte_order == 0 else ">I"
    for y in range(height):
        row = off + y * bpl
        for x in range(width):
            if bpp == 16:
                val = struct.unpack_from(fmt16, d, row + x * 2)[0]
            elif bpp == 32:
                val = struct.unpack_from(fmt32, d, row + x * 4)[0]
            else:
                val = d[row + x]
            px[x, y] = (chan(val, rmask), chan(val, gmask), chan(val, bmask))
    img.save(dst)
    print("saved", dst, img.size)


if __name__ == "__main__":
    main()
