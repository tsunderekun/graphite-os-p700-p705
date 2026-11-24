#!/usr/bin/env python3
"""Build the hi-DPI Metro contact sheets from artifacts/shell/hidpi/.

    python shell/make_sheet_hidpi.py            # both sheets
    python shell/make_sheet_hidpi.py --2x        # 3072x8530, tiles 960x1600
    python shell/make_sheet_hidpi.py --1x        # 2112x5700, tiles 640x1067

Layout is scaled from the 2x spec (48 px margins/gaps, 22 px caps labels,
44 px ExtraLight title). Tiles are only ever downscaled with LANCZOS, so the
1x sheet is a clean 2/3 resample of the 2x renders.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
HIDPI = ROOT / "artifacts" / "shell" / "hidpi"
FONTS = ROOT / "shell" / "fonts"
OUT_DIR = ROOT / "artifacts" / "shell"

TILE_W, TILE_H = 960, 1600

ITEMS = [
    ("m-home", "ПУСК"),
    ("m-player", "СЕЙЧАС"),
    ("m-queue", "ОЧЕРЕДЬ"),
    ("m-info", "ИНФО"),
    ("m-settings", "НАСТРОЙКИ ПЛЕЕРА"),
    ("m-apps", "ПРИЛОЖЕНИЯ +/✓"),
    ("m-edit", "ПРАВКА ПЛИТОК"),
    ("m-shsettings", "НАСТРОЙКИ УСТРОЙСТВА"),
    ("m-terminal", "ТЕРМИНАЛ"),
    ("m-notes", "ЗАМЕТКИ"),
    ("m-files", "ФАЙЛЫ"),
    ("m-browser", "БРАУЗЕР"),
    ("m-shade", "ШТОРКА"),
    ("m-lock", "БЛОКИРОВКА"),
    ("m-power", "ВЫКЛЮЧЕНИЕ"),
]

BG = (12, 14, 16)
INK = (232, 235, 238)
DIM = (142, 149, 162)


def build(
    *,
    scale: float,
    columns: int,
    out: Path,
    margin: int | None = None,
    gap: int | None = None,
    label_size: int | None = None,
    title_size: int | None = None,
    title: str = "graphite shell — Metro, 15 экранов 480×800 (2×)",
) -> Path:
    tile_w, tile_h = round(TILE_W * scale), round(TILE_H * scale)
    pad = round(48 * scale) if margin is None else margin
    space = round(48 * scale) if gap is None else gap
    lsize = max(16, round(22 * scale)) if label_size is None else label_size
    tsize = max(30, round(44 * scale)) if title_size is None else title_size

    rows = (len(ITEMS) + columns - 1) // columns
    label_h = lsize + 12          # line + breathing room, drawn inside the gap
    header_h = tsize + 28         # title line + space before the first row
    width = pad * 2 + columns * tile_w + (columns - 1) * space
    height = pad * 2 + header_h + rows * tile_h + (rows - 1) * space + rows * label_h

    sheet = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(sheet)

    f_title = ImageFont.truetype(str(FONTS / "DejaVuSans-ExtraLight.ttf"), tsize)
    f_label = ImageFont.truetype(str(FONTS / "DejaVuSans.ttf"), lsize)

    draw.text((pad, pad), title, fill=INK, font=f_title)

    y = pad + header_h
    for i, (name, caption) in enumerate(ITEMS):
        col, row = i % columns, i // columns
        x = pad + col * (tile_w + space)
        row_y = y + row * (tile_h + label_h + space)

        src = HIDPI / f"{name}.png"
        if not src.exists():
            raise SystemExit(f"missing render: {src} (run shell/render_hidpi.py)")
        with Image.open(src) as im:
            tile = im.convert("RGB")
            if tile.size != (tile_w, tile_h):
                tile = tile.resize((tile_w, tile_h), Image.LANCZOS)
            sheet.paste(tile, (x, row_y))

        draw.text((x, row_y + tile_h + 6), caption, fill=DIM, font=f_label)

    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out, optimize=True)
    print(f"{out.name}: {sheet.width}x{sheet.height}, {out.stat().st_size / 1048576:.2f} MB, "
          f"tiles {tile_w}x{tile_h} x{len(ITEMS)}, {columns} cols")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--2x", dest="only2x", action="store_true", help="only the 2x sheet")
    ap.add_argument("--1x", dest="only1x", action="store_true", help="only the 2/3 sheet")
    ap.add_argument("--scale", type=float, default=None, help="custom tile scale (1.0 = 960 wide)")
    ap.add_argument("--columns", type=int, default=3)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--margin", type=int, default=None)
    ap.add_argument("--gap", type=int, default=None)
    ap.add_argument("--label-size", type=int, default=None)
    ap.add_argument("--title-size", type=int, default=None)
    args = ap.parse_args()

    custom = args.scale is not None or args.out is not None
    if custom:
        scale = args.scale if args.scale is not None else 1.0
        out = args.out or OUT_DIR / "metro-sheet-custom.png"
        build(
            scale=scale,
            columns=args.columns,
            out=out,
            margin=args.margin,
            gap=args.gap,
            label_size=args.label_size,
            title_size=args.title_size,
        )
        return 0

    if not args.only1x:
        build(
            scale=1.0,
            columns=args.columns,
            out=OUT_DIR / "metro-sheet@2x.png",
            margin=args.margin,
            gap=args.gap,
            label_size=args.label_size,
            title_size=args.title_size,
        )
    if not args.only2x:
        build(
            scale=2 / 3,
            columns=args.columns,
            out=OUT_DIR / "metro-sheet.png",
            margin=args.margin,
            gap=args.gap,
            label_size=args.label_size,
            title_size=args.title_size,
            title="graphite shell — Metro, 15 экранов 480×800",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
