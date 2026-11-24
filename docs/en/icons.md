# ICONS-RESEARCH — a unified icon set for graphite

Date: 2026-11-24. Scope: `os/src/gfx.cpp`, function `icon()`, 39 names.
Nothing in `os/` was changed; the phone was not touched; there are no commits.

## 0. How this was tested

Our icons were not judged “by eye from screenshots”: a separate host dumper was built that links the **actual** `os/src/gfx.cpp` (`g++ -I os/include ... gfx.cpp`), calls `icon()` for all 39 names at 24 and 34 px on two backgrounds, and writes PPM. In other words, `ours-grid.png` is actual output from our function, not an approximation.

Packs were taken from npm packages (SVG 1:1, `unpkg.com`); rendered with `cairosvg` at the same sizes, without scaling. Metrics (ink coverage, optical width, center offset) were calculated from each glyph's alpha channel on the native 24 px grid.

The measured figures for our icons point to the root of the problem:

| | ours now | Phosphor | Lucide | Tabler | Fluent | Iconoir |
|---|---|---|---|---|---|---|
| ink coverage | **13.6 %** | 17.1 % | 22.0 % | 20.8 % | 16.6 % | 15.9 % |
| optical width (fraction of 24 px box) | **0.38–0.96** | 0.67–1.00 | 0.58–0.92 | 0.58–0.83 | 0.50–0.92 | 0.50–1.00 |

The 0.38–0.96 spread is what makes them look “uneven”: `terminal`, `play`, `next` occupy a quarter of the box; `globe`/`info` almost all of it. The packs have two to three times less spread; Tabler is the most consistent.

## 1. Verdict

**Primary pack — Phosphor (MIT), Regular weight; Light for 14–18 px.**
**Backup — Lucide (ISC).**
**Integration — SVG → polygons → C array + mini rasterizer (option B);**
the `icon()` signature remains unchanged.

Why Phosphor, rather than the “trendiest” option:

1. **WP/Metro philosophy**: monochrome, one stroke weight, a glyph is a single-color outline, with no gradients or inner fills, geometry with minimal rounding, optical size ~20/24 (2 px margins), app bar 48×48 px with a ~24–26 px glyph. Phosphor uses filled outlines on a **256** grid (so 16/256 = **1.5 px** at 24); the geometry is crisp, corner radius 8/256 ≈ 0.75 px, and inner areas remain empty. This is exactly “one stroke without fills”, expressed as an outline rather than `<path stroke>`.
2. **Complete coverage**: 39/39 of our glyphs, including 2 substitutions (`sd`, `lab`) and one shape refinement (`chevron` is thinner); the rest are exact name matches. Iconoir, whose visual character is closest, lacks `power` and sliders, but has a gear (which is confusing).
3. **Optical sizes**: 0.67–1.00 without outliers — glyphs do not “shrink” as ours do (0.38–0.96). Phosphor also fills more of the box, so optical trim (≈0.92) is required in the baker; otherwise icons will look larger than adjacent labels.
4. **Technically the most convenient for option B**: Phosphor does not use `stroke`, only `fill` — no need for a stroke-to-outline converter (a separate, and nastiest, part of the job: joins, caps, miter). Our 39 glyphs become a set of closed polygons filled with even-odd scanlines.
5. **MIT license** — it can be embedded in firmware; only the license text and copyright need to be included in the distribution.

Why not Iconoir as primary, although its visual character is closest: 22/24 without substitutions (no `power` or sliders), optical spread 0.50–1.00, no official TTF, and its `system-shut` means power-off, not “power” in our sense.

Why not Lucide as primary: a 2 px stroke at 24 becomes 1.33 px at our main sizes of 16–20 px and breaks on diagonals at 565; it is heavier than Metro. However, Lucide has a consistent, complete set, 24/24, and a ready-made TTF, so it is the backup.

Why not Fluent, although it is literally a descendant of Segoe: its ~1 px weight at 24 is too thin for 480×800 without subpixel antialiasing (16.6% ink), and it has no terminal icon (`code`). Also, its font is 2.9 MB.

## 2. Candidates

| Pack | License | Formats | Icons | Stroke @24 | Our matches | Notes |
|---|---|---|---|---|---|---|
| **Phosphor** 2.1.1 | MIT | SVG; TTF only legacy v1.4.2 (1090 KB, 5131 glyphs, **no `usb`) | 1512 per weight (6 weights) | 1.5 (16/256) / 1.1 (12/256 Light) | **24/24** | primary; fill-only outlines, 256 grid |
| **Lucide** 1.79 | ISC | SVG + `font/lucide.ttf` 1000 KB (2146 glyphs; some names missing — `music`, `clock`, `sim-card`) | 2416 svg | 2.0, round caps/joins | **24/24** | backup; consistent geometry, but heavy at 16–20 px |
| Tabler 3.53 | MIT | SVG + webfont `tabler-icons.ttf` 1927 KB (5487 glyphs, 34/39 by name) | 5491 outline | 2.0, round joins | 24/24 | ideal geometry (0.58–0.83), but 2 px and the largest font |
| Fluent UI System Icons | MIT | SVG (`@fluentui/svg-icons`, 2481@24) + TTF Regular 2911 KB / Resizable 1624 KB | 2481 (24 px) | ~1.0–1.25, closed outlines | 23/24 (`terminal`→`code`) | true Segoe descendant, but too thin for 565 |
| Iconoir 7.12 | MIT | SVG only (no official TTF) | 1383 regular (+solid) | 1.5, round caps | 22/24 (`power`→`system-shut`, `sliders`→`settings`) | closest visual character; two required glyphs missing |
| Remix Icon 4.9 | Apache-2.0 | SVG 3231 (1689 base `-line`/`-fill`) + `remixicon.ttf` 599 KB (3154) | 1689 | 1.5–2 | 23/24 (no `headphones`) | good balance, but suffix names make mapping tedious |
| Material Symbols | Apache-2.0 | SVG + variable `woff2` 3931 KB (4425 cmap; FILL/GRAD/opsz/wght axes) | 4425 | adjustable (400 ≈ 2 px) | 24/24 | Google style, rounding/fills are not Metro; 3.9 MB font |
| Bootstrap Icons 1.13 | MIT | SVG 2078 + woff2 | 2078 | 1.5–2 on **16** grid | 23/24 (`skip-back`) | 16-grid and filled glyphs break the 24 rhythm |
| Feather 4.29 | MIT | SVG only | 287 | 2.0 | 23/24 (no `usb`) | attractive minimalism, but small set: `usb`, `sim`, `sd` absent |

About TTFs (important for option A): ready-made fonts exist for Lucide, Tabler, Remix, Fluent, and Material Symbols, but **the selected Phosphor has no current font** (v1.4.2 uses a different, old grid and lacks `usb`). Therefore, the font-based path either changes the pack or requires building a custom subset (fontforge, ~0.5 day).

## 3. What we take from WP philosophy

Rules for resolving disputed decisions later:

- **one color, one weight**: an icon is not “assembled” from parts of different thickness;
- **no fills** (except transport controls, where WP itself uses solid triangles and bars — our `play/pause` already do, so keep them);
- **square corners and minimal radii**: 0.75 px radius at 24 is the limit;
- **optical, not geometric size**: fit the glyph by its bbox, not its viewBox (otherwise `play` will be tiny and `globe` huge — our current problem);
- **thickness ≥ 1 px** at the minimum size, otherwise the stroke flickers on 565;
- **no shadows, gradients, or two-color glyphs**.

## 4. Mapping: our 39 → Phosphor

`*` marks a substitution (no 1:1 glyph); the reason is in parentheses.

| ours | Phosphor | note |
|---|---|---|
| music | `music-notes` | |
| folder | `folder` | |
| terminal | `terminal-window` | exact match for our “window + >_” |
| globe | `globe` | |
| sliders | `sliders` | three vertical tracks, as in ours |
| note | `file-text` | ours is a page with lines; `notepad` is also available |
| grid | `squares-four` | |
| wifi | `wifi-high` | |
| lock | `lock` | |
| power | `power` | |
| battery | `battery-high` | optionally `battery-medium` |
| sd | `sim-card` `*` | no SD glyph; this is closest to our card with contacts |
| play | `play` | solid triangle, as now |
| pause | `pause` | |
| next | `skip-forward` | |
| prev | `skip-back` | |
| shuffle | `shuffle` | |
| repeat | `repeat` | |
| search | `magnifying-glass` | |
| plus | `plus` | |
| check | `check` | |
| close | `x` | |
| sun | `sun` | |
| volume | `speaker-high` | |
| keyboard | `keyboard` | |
| bluetooth | `bluetooth` | |
| sim | `sim-card` | same as `sd` — see note below |
| info | `info` | |
| back | `arrow-left` | |
| chevron | `caret-right` `*` | Phosphor chevron is thinner than ours (2 px); at 16 px either keep our own or use `caret-right-bold` |
| usb | `usb` | trident, 1:1 |
| clock | `clock` | |
| airplane | `airplane` | |
| storage | `hard-drives` | two drives, as in ours |
| shield | `shield` | |
| lab | `flask` `*` | ours has three test tubes in a rack; a flask is the only close glyph |
| trash | `trash` | |
| restart | `arrow-clockwise` | |
| headphones | `headphones` | |

Mapping notes:

- `sd` and `sim` map to the **same** glyph (`sim-card`). In code, `sd` is not actually used (only declared); neither is `sim`. I suggest keeping just `sim` and removing `sd` in the next cleanup — no screens need editing.
- The shell draws `chevron` at 2 px and uses it at 16 px for “expand”; `caret-right` would be almost hairline-thin there. Alternatives: keep our chevron as the only hand-drawn glyph (it is geometrically trivial) or use `caret-right` and increase its weight to 2 px in the bake step.
- `terminal` is an exact match, not a substitution.

## 5. Integration options

### A. Icon TTF through FreeType

FreeType is already linked and working: `text()` calls `FT_Load_Char(FT_LOAD_RENDER)` and `cv.blend()` with grayscale, so we already have 8-bit antialiasing.

- **+** ~40 lines of code (name→codepoint mapping + `FT_Load_Char` in `icon()`); any size without recalculating data; familiar mechanism.
- **−** needs a font with 39/39 coverage and a current version: Phosphor has none, `lucide.ttf` is incomplete, `tabler.ttf` has 34/39 by name. Either change packs or build a subset from SVG (fontforge/SVG→TTF, ~0.5 day) — so option A still entails option B's contour flattening, just with different packaging.
- **Weight**: full font 0.6–2.9 MB; estimated subset of 39 glyphs, 30–60 KB.
- **Quality on 565**: good, but hinting needs investigation: with `FT_LOAD_DEFAULT`, thin strokes collapse unevenly at 16 px; icons need `FT_LOAD_NO_HINTING` (then strokes are even, but slightly blurrier).
- **Complexity: S** (if TTF is ready) / **M** (if building one).

### B. SVG paths → compact C array + mini rasterizer  ← recommended

Offline: SVG → flatten (M/L/H/V/C/Q/A/Z) → polygons in integer coordinates on a 256 grid → `os/src/icons_data.cpp`. On-device: bbox centering, scaling, scanline fill (even-odd) with 2×2 antialiasing through `blend()`.

- **+** artwork from the selected pack without compromise; Phosphor is fill-only, so no stroke→outline conversion; works at 14 and 140 px (we have album-art placeholders at 100/120/140 px); ~15–20 KB of data; no new dependencies; fully deterministic behavior.
- **−** requires writing flatten + fill (~120–150 lines) and debugging curves at large sizes; more code than “call FT”.
- **Weight**: 39 glyphs × ~250 points × 2 bytes ≈ **17 KB** + ~4 KB code.
- **Quality on 565**: best of the three; antialiasing comes from our own `Canvas::blend` (5 bits per channel — edges will be slightly “stepped” on smooth arcs, but this is not visible at 24 px).
- **Complexity: M** (~4–6 h for the rasterizer + 3 h for the baker).

### C. Pre-rendered masks (14…38 px) in firmware

- **+** simplest rendering: bitmap arrays + `paste`, no math; fast.
- **−** fixed set of sizes: 39 icons × 11 sizes = 429 masks; alpha8 ≈ 80–120 KB (1 bpp — 10–15 KB, but jagged); 100/120/140 px placeholders must remain code-rendered; a new size requires rebuilding tables; only baked-in antialiasing.
- **Weight**: 10–120 KB depending on 1/8-bit depth.
- **Quality on 565**: acceptable at baked sizes, unusable at others.
- **Complexity: S/M**.

**Recommendation: B.** A is only for an experiment or if we decide to switch to Lucide/Tabler and build a subset; C might serve as a one-evening intermediate step to see the new look on the phone, but it does not cover 140 px.

## 6. Implementation plan

| # | Task | Files | Time |
|---|---|---|---|
| 0 | Vendor 39 Phosphor Regular SVGs + MIT text + `THIRD-PARTY.md` | `os/assets/icons/pho/*.svg`, `os/assets/icons/LICENSE`, `THIRD-PARTY.md` | 0.5 h |
| 1 | Baker: `d=` parser, flattening, optical centering in a 24 box, C-array output + `enum IconId` | `tools/icons/bake.py` → `os/src/icons_data.cpp`, `os/include/icons.h` | 3 h |
| 2 | Rasterizer: `icon_mask()` + scanline fill + 2×2 AA; `icon()` remains `(Gfx&, const char*, int, int, int, uint16_t)` — do not touch screens | `os/src/gfx.cpp` (remove `Pen` and 180 lines of `if` chain, −180/+40) | 4–6 h |
| 3 | Host checks: `settings`, `now`, `home`, `queue`, `lock` via `sh os/preview.sh`; size grid 11–30 px | `artifacts/host/*.png` | 2 h |
| 4 | Tune weight/centering: minimum 1 px at 16, uniform optical trim, `chevron` | `icons_data.cpp`, Light weight ≤18 px if needed | 2 h |
| 5 | Device QA (orchestrator): 565, before/after, speed at 140 px, legibility | phone | 1 h |

Total ~1.5 agent-days + half an hour on device.

Key point: **the `icon()` signature does not change**, and all 39 names remain (including declared but unused `sd`, `sim`, `shield`) — no screen or `registry.cpp` edits are needed. Only the implementation changes, with `icons_data.cpp` and `bake.py` added.

How to check (minimum that catches 90% of regressions):

1. `compare.png` mode: 6 icons × 24/34 px × graphite/paper, 1:1, no scaling — thickness and stroke “holes” are visible by eye;
2. size ladder 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 34 px: at 11–14 no elements should disappear (`wifi` arcs, `lock` shackle);
3. paper theme: thin glyphs “fatten” on a light background — compare ink coverage after rendering (tolerance ±10% from the current 13.6%);
4. 140 px album-art placeholder: no curve jaggies (flattening errors are visible there).

## 7. Risks

1. **2 px vs 1.5 px.** Phosphor provides 1.5 px at 24; at 16 px this is 1.0 px — borderline. If the stroke breaks after baking, the rule is: `size < 20 → Light`, or guarantee a minimum 1 px thickness in the rasterizer (dilate the mask by 1 px). Lucide as backup provides 2 px if we decide to go “heavier”.
2. **Hinting.** Option B has none (a plus). Option A requires `FT_LOAD_NO_HINTING`; otherwise stems jump by 1/2 px at 14–16 px.
3. **Optical sizes.** Phosphor draws glyphs “to the edge” (`wifi`/`globe` = 1.00 box). If positioned by viewBox, `wifi` will hit the margin and `lock` will float. Centering by alpha-bbox and a uniform optical trim (for example, 0.92 of the box) are mandatory — do this in the baker, not in screens.
4. **Missing glyphs.** `sd` (no SD glyph), `lab` (flask instead of test tubes), `chevron` (thinner). Decision: remove `sd` (unused), accept `lab`, keep our own `chevron` or thicken it in the bake step. Check that no screen draws `sd` before removing it.
5. **565 and no AA in current code.** Our icons are currently whole rectangles, so jaggies are less noticeable. After switching to AA, gray edge pixels will appear; on `theme.note`/`theme.dim` (low contrast) this may look dirty. Check the paper theme and `dim` color.
6. **Licenses.** MIT/ISC/Apache-2.0 all permit embedding, but MIT/ISC require copyright and license text in the distribution. Add `os/assets/icons/LICENSE` (full Phosphor text) and a line in `THIRD-PARTY.md`. Brand logos (Bluetooth is a trademark in the glyph itself) are usually not challenged by lawyers, but it is worth knowing: `bluetooth` is our only “branded” glyph.
7. **Performance.** 140 px × 39 glyphs with 2×2 supersampling means ~4× the pixel area per glyph; at most 3 large icons (placeholders) appear on one screen, so the real cost is < 5 ms. If using 3×3, measure on device (our ARMv7 has no NEON optimizations).
8. **Name compatibility.** A typo in the baker's name mapping makes `icon()` return nothing — the icon simply disappears without a build error. Generate the `enum` + name array and add a host test that “all 39 names produce a non-empty mask”.

## 8. Previews

All sheets are in `artifacts/icons/`:

| File | Contents |
|---|---|
| `compare.png` | main sheet: ours vs Phosphor/Lucide/Fluent/Tabler, **1:1, 24 and 34 px**, graphite and paper, no scaling |
| `favorites.png` | 6 rows (Iconoir, Phosphor R/L, Lucide, Fluent, Tabler), 24 icons, 40 px |
| `fav-iconoir.png` | Iconoir, MIT, 1.5 px, 48 px (=24 @2×), labels “our name + glyph name” |
| `fav-phosphor.png` | Phosphor, both weights (Regular and Light) on one sheet |
| `fav-lucide.png` | Lucide, ISC, 2 px |
| `fav-fluent.png` | Fluent Regular, ~1 px |
| `fav-tabler.png` | Tabler, 2 px |
| `pho-set.png` | selected pack in full: all 39 of our names → Phosphor glyphs |
| `ours-grid.png` | our current 39, 24/34 px, graphite/paper (raw material for comparison) |

`*` on the sheets marks substitutions: Iconoir `sliders→settings`, `power→system-shut`; Fluent `terminal→code`; Phosphor `sd→sim-card`, `lab→flask`, `chevron→caret-right`.

## 9. Conclusions

1. **The problem is not style but spread**: our glyphs occupy 0.38–0.96 of the box at varying thicknesses (1.0–2.2). Any of the five packs fixes this in one move, so pack choice is about taste and coverage, not “rescue”.
2. **Phosphor Regular is the best compromise**: full coverage 39/39 (2 substitutions), MIT, 1.5 px “Metro-like” weight, consistent optical sizes, and, technically important, **fill-only outlines** — the only pack that needs no stroke-to-outline converter. Lucide is the backup if we want 2 px and a ready-made font.
3. **The font path (A) is not free**: Phosphor has no current TTF; Lucide's font is incomplete — “just hooking up a TTF” will not work. It is cheaper and better to bake 39 glyphs into polygons (~17 KB) and fill them with our own scanline routine: this covers both 16 px and 140 px placeholders.

**First step:** stage 1 — baker `tools/icons/bake.py` + `icons_data.cpp` for 39 Phosphor glyphs (this can be completed and checked entirely on the host, without touching the phone), followed immediately by a `compare.png`-style check at 24/34 px on both themes. Only after that should `icon()` in `gfx.cpp` be touched.
