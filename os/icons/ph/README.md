# Phosphor Regular — sources for the baked icon set

39 glyphs from **Phosphor Icons 2.1.1**, weight `regular`, MIT.
Upstream: <https://github.com/phosphor-icons/core>, npm `@phosphor-icons/core`,
`assets/regular/<name>.svg` (single `fill` path on a 256×256 grid, no strokes).

The files here keep **our** icon ids so the mapping is visible at a glance; the
upstream glyph name is in `SOURCES.json` and in `os/tools/bake_icons.py`.

| our id | Phosphor | note |
|---|---|---|
| `sd`, `sim` | `sim-card` | Phosphor has no SD glyph; both ids share one glyph |
| `lab` | `flask` | Phosphor has no test tubes |
| `chevron` | `caret-right` | lighter than our hand-made chevron (see notes/ICONS-RESEARCH.md) |

Everything else is a 1:1 name match (`terminal` → `terminal-window`,
`note` → `file-text`, `grid` → `squares-four`). See the full table in
`os/tools/bake_icons.py` (`ICONS`).

## Regenerate

```sh
python os/tools/bake_icons.py --fetch   # refresh the SVGs + LICENSE from unpkg
python os/tools/bake_icons.py --analyze # bbox/point statistics
python os/tools/bake_icons.py           # rewrite os/src/icons_ph_data.inc
```

The baker flattens the `d="..."` beziers, centres each glyph by its bbox,
scales the whole set so the largest glyph spans `0.92 * 24 px`, simplifies the
contours and emits 1/16 px fixed point. Runtime code is in
`os/src/icons_ph.cpp`; the API is `bool icon_ph(Gfx&, const char*, int x,
int y, int size, uint16_t color)` in `os/include/icons_ph.h`.

## Licence

MIT, © 2023 Phosphor Icons — full text in `LICENSE`. A copy must ship with the
firmware; add one line to `THIRD-PARTY.md` when that file is created.
`bluetooth` is the only glyph that carries a brand mark (the Bluetooth rune).
