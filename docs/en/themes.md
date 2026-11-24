# THEMES-PROPOSAL — themes, accents, and texture

> Proposal for the LG P700 graphite shell (480×800, RGB565, 1 GHz, no GPU).
> Design document and previews; project code and phone were not touched.
> Previews: `artifacts/themes/`. Generator: `artifacts/themes/tools/gen_themes.py`.
> Data for code: `artifacts/themes/palettes.json`, `palettes.h.txt`.

## 0. In brief

1. **Seven themes = three families × tone**: dark (graphite / ember / slate), light “paper” (warm / neutral), gray “book” (warm / neutral). All use the same set of 16 tokens, with no borders or gradients.
2. **Five accents in three registers** (`dark` for dark themes, `ink` for paper, `deep` for gray): turquoise (default), amber, blue, red, green. The minimum contrast across the matrix is 4.94:1.
3. **Texture is physically feasible only in some cases**: light and gray themes can use grain (1 RGB565 code = 3% brightness); on dark themes grain becomes dirt (1 code = 40%), so only laid lines are used there: 1 px lines at a pitch of 24. Everything is baked into a 256×256 tile at build time; there is no runtime noise.

---

## 1. Rules that do not change

Taken from `DESIGN.md`, `PRODUCT.md`, and `DESIGN-SYSTEM.md`, and not open to discussion:

- square corners; no shadows, gradients, glass, or animations;
- **one accent** per theme; the accent means action/state, not decoration;
- text ≥ 11 px (terminal 10), targets ≥ 44 px;
- 16 tokens per theme, instant switching;
- dense grid, space between blocks, 1 px lines.

Contrast thresholds used to check every pair (WCAG 2.1, calculated using the displayed 565 colors):

| Role | Token | Threshold | Purpose |
|---|---|---|---|
| Primary text | `fg` on `bg`/`surf` | ≥ 7.0 | 15–25 px, handheld reading |
| Secondary text | `dim` on `bg`/`surf` | ≥ 4.5 | 12 px, captions, metadata |
| Line numbers | `num` on `bg`/`sel` | ≥ 4.5 | 12 px, digits |
| Accent as text | `accent` on `bg`/`sel` | ≥ 4.5 | current track name, `DONE` |
| Text on fill | `onacc` on `accent` | ≥ 4.5 | play, live tile, `ENTER` |
| Cover placeholder | `note` on `art` | ≥ 2.5 | 150 px note, graphics only |
| Lines, track | `line`, `track` | 1.4–2.0 | decorative, not text |

---

## 2. Theme specifications

`L*` is background lightness: the higher the value, the lighter the theme. The range 6…95 is the point of the set: one shell for evening, daytime in the shade, and bright daylight.

| id | UI name | Family | Tone | bg | surf | fg | Default accent | Background L* | Use |
|---|---|---|---|---|---|---|---|---|---|
| `graphite` | Graphite | dark | neutral | `#141619` | `#1c2024` | `#e8ebee` | turquoise | 7 | evening, default, “instrument panel” |
| `ember` | Ember | dark | warm | `#101410` | `#211c19` | `#f7ebe6` | amber | 6 | night, warm lamplight |
| `slate` | Slate | dark | cool | `#191c21` | `#212429` | `#efefef` | turquoise | 10 | night, “metal,” cool accent |
| `paper` | Paper | light | warm | `#f7ebde` | `#fff7ef` | `#211c10` | turquoise | 94 | daytime, reading, cream paper |
| `paper-n` | Paper · neutral | light | neutral | `#efeff7` | `#fffbff` | `#101819` | turquoise | 95 | bright daylight, maximum contrast |
| `book` | Book | gray | warm | `#cec2b5` | `#d6cebd` | `#191810` | turquoise | 79 | daytime in shade, “book cloth” |
| `book-n` | Book · neutral | gray | neutral | `#c5c6c5` | `#ced2d6` | `#191c19` | turquoise | 80 | daytime in shade, neutral |

Why gray is not “dirty paper”: it is the color of book binding and page edges, L* 79–80. It provides a third brightness level between graphite and paper and does not glare in a dim room, where a white page is already hard on the eyes.

---

## 3. Tokens

One set for all themes; roles are the same as in `DESIGN-SYSTEM.md`, plus `onacc` (text color on an accent fill, present in the player's `Theme`).

| Token | Role | graphite | ember | slate | paper | paper-n | book | book-n |
|---|---|---|---|---|---|---|---|---|
| `bg` | screen background | `#141619` | `#101410` | `#191c21` | `#f7ebde` | `#efeff7` | `#cec2b5` | `#c5c6c5` |
| `bar` | dock, keyboard, bottom | `#0f1114` | `#100c08` | `#101419` | `#e6dfce` | `#e6e7e6` | `#bdb6a5` | `#b5babd` |
| `surf` | tile, card, page | `#1c2024` | `#211c19` | `#212429` | `#fff7ef` | `#fffbff` | `#d6cebd` | `#ced2d6` |
| `surf2` | pressed | `#39424a` | `#423929` | `#3a454a` | `#e6d7c5` | `#dedfde` | `#b5aa9c` | `#adaead` |
| `line` | 1 px border/line | `#3d444d` | `#4a4131` | `#3a414a` | `#ceba9c` | `#bdc2c5` | `#9c9284` | `#949a9c` |
| `line2` | row divider | `#22262b` | `#292419` | `#212831` | `#e6dbc5` | `#dee3e6` | `#b5ae9c` | `#adb2b5` |
| `art` | cover backing | `#0b0d0f` | `#080808` | `#101010` | `#e6dfce` | `#e6ebef` | `#bdb6a5` | `#b5babd` |
| `fg` | primary text | `#e8ebee` | `#f7ebe6` | `#efefef` | `#211c10` | `#101819` | `#191810` | `#191c19` |
| `dim` | secondary | `#8e969e` | `#9c9284` | `#949aa5` | `#63594a` | `#5a5d63` | `#4a4131` | `#42494a` |
| `accent` | action/state | `#3fc8d0` | `#e0a052` | `#3fc8d0` | `#0b7076` | `#0b7076` | `#085357` | `#085357` |
| `accent2` | pressed primary | `#2fb6c0` | `#c98b3f` | `#2fb6c0` | `#085c61` | `#085c61` | `#064246` | `#064246` |
| `track` | unfilled progress | `#454e57` | `#4a4131` | `#424d52` | `#cebea5` | `#c5c6c5` | `#ada294` | `#a5a6a5` |
| `sel` | current-row background | `#1a2225` | `#211c10` | `#192429` | `#efdfce` | `#e6ebef` | `#ded2c5` | `#d6d7d6` |
| `num` | line numbers | `#79838b` | `#8c8273` | `#7b868c` | `#6b6152` | `#63696b` | `#524d42` | `#4a5152` |
| `note` | cover placeholder | `#5a656a` | `#6b5d4a` | `#636973` | `#948a6b` | `#8c9694` | `#7b7163` | `#73757b` |
| `onacc` | text on accent | `#0f1114` | `#171008` | `#0f1114` | `#f7f2e7` | `#f7f2e7` | `#f2ece0` | `#f2ece0` |

Three things are apparent from the table:

- **`graphite` is left as is.** It is the accepted theme; the panel will show `#101419` instead of `#141619` (rounding in 565).
- **The other themes are chosen on the 565 grid**: their backgrounds, surfaces, text, and lines are exactly what appears on screen. The sole exception is the **three accent colors**: they are specified in sRGB and rounded by the panel to a code (`#0b7076` → `#087173`, `#3fc8d0` → `#3acad6`). Therefore `palettes.png` shows two chips for the accent, and contrasts in §4 are calculated using the displayed color.
- **Light themes invert the role of `surf`**: it is not a “recessed” tile but a raised one—the page is lighter than the background. Thus `surf/bg` = 1.11–1.13 in all themes: tiles are separated by spacing and texture, not by fill contrast.
- **`accent2` is always deeper than `accent`** (6–10% darker). In dark themes the pressed state gets darker and text contrast falls slightly (9.7 → 7.9 on graphite, still with a large margin); in light themes it rises (5.2 → 6.9 on paper), because the ink gets denser. The “deeper only” rule keeps the state predictable in all seven themes.

What is not in the table, and why: `sep`, `section`, and `line2` are one `line2` token with different lengths; a separate `section` is unnecessary, since a 1 px line is distinguished by length, not color.

---

## 4. Contrast

All values use the displayed colors (after 565). Calculating from “ideal” HEX values is pointless: they are not present on the phone.

| Theme | fg/bg | dim/bg | num/bg | accent/bg | onacc/accent | note/bg | line/bg |
|---|---|---|---|---|---|---|---|
| graphite | 15.7 | 6.1 | 4.8 | 9.3 | 9.7 | 3.1 | 1.9 |
| ember | 15.9 | 6.1 | 4.9 | 8.6 | 8.8 | 2.9 | 1.9 |
| slate | 14.9 | 6.0 | 4.6 | 8.6 | 9.7 | 3.1 | 1.7 |
| paper | 14.4 | 5.8 | 5.2 | 4.9 | 5.2 | 2.9 | 1.6 |
| paper-n | 15.7 | 5.8 | 4.9 | 5.1 | 5.2 | 2.7 | 1.6 |
| book | 10.2 | 5.7 | 4.8 | 5.2 | 8.0 | 2.7 | 1.8 |
| book-n | 10.0 | 5.4 | 4.7 | 5.3 | 8.0 | 2.7 | 1.7 |

What this means in practice:

- The `dim` margin (5.4–6.1) allows secondary text to be lowered by 1–2 steps later if it seems too loud. Below 4.5 is not acceptable.
- `accent` in light themes is **“ink,” not “light”**: 4.9–5.3 versus 8.6–9.3 in dark themes. This is deliberate: dark turquoise text on cream paper reads like print, while bright turquoise on paper gives 2.1 and turns to mud.
- `note` (2.7–3.1) is intentionally a quiet cover placeholder. Rule: **never set text in the `note` color**; it is for a large, 150 px note.
- `line` at 1.6–1.9 gives delicate hairlines. In light themes they must be darker than an ordinary “gray divider” (or they disappear); in dark themes they must be lighter. That is why `line` and `line2` are separate tokens.

---

## 5. Accents

Five options, three registers. The theme family selects the register, not the user: a dark background needs a glowing accent with dark text; a light one needs dark ink with light text. Each cell lists **fill / pressed / text color**.

| Accent | id | Dark (`dark`) | Paper (`ink`) | Gray (`deep`) | Min. contrast |
|---|---|---|---|---|---|
| Turquoise | `turquoise` | `#3fc8d0` / `#2fb6c0` / text `#0f1114` | `#0b7076` / `#085c61` / `#f7f2e7` | `#085357` / `#064246` / `#f2ece0` | 4.94 |
| Amber | `amber` | `#e0a052` / `#c98b3f` / `#171008` | `#8a5a12` / `#6f470c` / `#fbf4e6` | `#67430d` / `#52360a` / `#f2ece0` | 5.04 |
| Blue | `blue` | `#6aa8e0` / `#4f90c8` / `#0a1116` | `#1f5e96` / `#174a78` / `#f2f7fb` | `#194d7b` / `#143e62` / `#f2ece0` | 5.04 |
| Red | `red` | `#e4675c` / `#c9544a` / `#1a0b09` | `#a1362c` / `#832a21` / `#fbf1ee` | `#842c24` / `#6a231d` / `#f2ece0` | 5.05 |
| Green | `green` | `#79c47a` / `#61ad63` / `#0b160c` | `#2f6b33` / `#24532a` / `#f2f8f1` | `#255328` / `#1e4220` / `#f2ece0` | 5.28 |

“Min. contrast” is the lower of two measurements (`text on fill`, `accent on background`) across all seven themes, using displayed 565 colors. The complete matrix with rendering is `accents.png`.

### How accents pair with themes

| | graphite | ember | slate | paper | paper-n | book | book-n |
|---|---|---|---|---|---|---|---|
| Turquoise | ✓ native | ✓ contrasting | ✓ native | ✓ print | ✓ print | ✓ | ✓ |
| Amber | ✓ warm evening | ✓ native | ~ debatable (warm on cool) | ✓ sepia | ~ “office” | ✓ | ~ |
| Blue | ~ cool | ✗ debatable | ✓ native | ~ | ✓ native | ~ | ✓ |
| Red | ✗ semantics | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Green | ✓ | ~ | ~ | ✓ | ✓ | ✓ | ✓ |

✓ — use; ~ — possible, but not first choice; ✗ — do not ship.

### Accent rules

1. **One accent per theme.** A second color may only be `dim`/`fg`. Exception: yellow key illumination (hardware), which is not part of the palette.
2. **Accent means state, not decoration.** Accent fill is allowed for: play, the “now playing” live tile, an enabled toggle/quick tile, keyboard `ENTER`, the current-row marker, and the filled part of the progress bar. Not for: headings, tile icons, section backgrounds, or dividers.
3. **Do not ship red.** Red is reserved for “power/danger” semantics (the shutdown dialog). With a red accent, the play button reads as “record,” and “Power off” no longer differs from an ordinary action. If it is really needed, use terracotta and move it away from `#a1362c` toward `#a4552f`.
4. **Amber on warm paper is sepia.** Beautiful and bookish, but weaker as a state signal: its hue difference from the background is small. Turquoise is the default on paper; amber is the “second cover.”
5. **`onacc` is neither white nor black, but the warm/cool edge of the theme.** On dark themes it is `#0f1114` (almost the background); on light themes it is `#f7f2e7` (almost the paper). This keeps the fill part of the same world, not a “sticker.”
6. **Changing the accent changes only three tokens** (`accent`, `accent2`, `onacc`). Therefore it can happen instantly, without redrawing backgrounds and textures.

---

## 6. Texture

### 6.1 Why it is not “just 3% noise”

RGB565 has different grids per channel: R and B have 32 levels (step 8/255), G has 64 (step 4/255). One code step is **not 3% brightness**; it is 3% only on a light background:

| Background | R level | Code step | Perception of one step |
|---|---|---|---|
| Graphite `#141619` | 20 | 8 levels | **+40…56%** — no longer grain, but a pixel of another color |
| Book `#cec2b5` | 206 | 8 levels | 4% — borderline, reads as “roughness” |
| Paper `#f7ebde` | 247 | 8 levels | 3% — soft grain, visibility threshold ≈ 2% |

This leads to the conclusion that determines the entire construction: **dark themes cannot have grain**—any attempt produces either “dirt” or 50/50 dithering, which looks like a dirty screen at 16 bits. This is a consequence of the panel, not a matter of taste.

### 6.2 Two mechanisms

| Mechanism | Where | Amplitude | Pattern | Default |
|---|---|---|---|---|
| **Grain** (`grain`) | paper, both book themes | 1 565 code per channel; coverage 10 / 13 / 16% (R/G/B) | 1 px jaggies + occasional 2–7 px fibers | ON for light and gray |
| **Laid** (`laid`) | all themes | 1 code; dark +1, light −1 | 1 px line: pitch 24 (dark) / 32 (light) | OFF, optional |

Coverage is the share of pixels whose channel is lowered by one code. Exactly enough to change the average tone by ~1%, not 5%.

Grain creates “paper”: 10% of pixels one code darker is the roughness visible at arm’s length but not as “noise.” Fibers give it direction (paper has a grain), so the field does not look like uniform salt.

Laid is a second, “typographic” texture: on dark themes it is the only possible one (pitch 24, line one code lighter than the background, contrast 1.3:1—like graph paper on a graphite panel). On light themes it is almost invisible page ruling.

### 6.3 How to generate it

Script: `artifacts/themes/tools/gen_themes.py` (PIL + numpy, deterministic seed). Algorithm:

1. **Field**: 256×256 white noise → wrapped convolution → ~1–2 px jaggies. No sinusoids or 5 px blur: those create “blobs” and a repeating pattern visible on a flat background.
2. **Fibers**: 70·(size/128)² short 1 px strokes, 2–7 px long, angle N(0, 0.30)—along the paper’s “machine direction”; each stroke is drawn 9 times with offsets ±size to keep the tile seamless.
3. **Ranking**: sort the field and map it uniformly to [0,1]. This gives **exact coverage** (10% = 10%), regardless of the noise distribution. Without this, “3% amplitude” turns into 40% area.
4. **Quantize in code space**, not percentages: `code = code − (field > 1 − coverage)`, then back to 8-bit using the same formula as the driver. The tile is therefore already 565-exact and does not “drift.”
5. **Laid** uses separate tile rows (y = 8, 8+pitch, …), exactly one code.

Warm and neutral versions differ in channel thresholds: warm lowers G and B more often than R (the paper pores are warmer); neutral treats them equally.

Regenerate: `python artifacts/themes/tools/gen_themes.py` recalculates the previews, JSON, C table, and statistics.

### 6.4 How to store it

- **Mask**: 256×256, 1 bit per pixel = **8 KB** per variant (warm, neutral). Store as PNG (1-bit grayscale) in `/usr/share/graphite/tex/{warm,neutral}.png`, convert to raw/PGM at build time—libpng is not needed on the phone.
- **Surface tiles** are built in memory when the theme loads: mask + token color → 256×256×2 = 128 KB per surface. Three are needed: `bg`, `surf`, `art` (`bar`/`sel`/`line` are painted flat). Total **384 KB** for the active theme and ~0.2 million operations per switch.
- **Do not store** a PNG for every “theme × surface” pair: 7·3 = 21 files that must all be remembered and regenerated. Mask + color is a single source of truth.
- Previews and `palettes.json` are for people only; they are not installed on the phone.

### 6.5 How to apply it

**Xlib (shell).** The tile is an ordinary 16 bpp `Pixmap`; fill with `FillTiled`:

```c
Pixmap tile = XCreatePixmap(dpy, root, 256, 256, 16);
XImage *im = XCreateImage(dpy, vis, 16, ZPixmap, 0, (char *)px, 256, 256, 16, 0);
XPutImage(dpy, tile, gc, im, 0, 0, 0, 0, 256, 256);

static void fill_tex(Window w, GC gc, int x, int y, int ww, int hh, Pixmap p) {
    XSetFillStyle(dpy, gc, FillTiled);
    XSetTile(dpy, gc, p);
    XSetTSOrigin(dpy, gc, x, y);   /* pattern phase = screen coordinates */
    XFillRectangle(dpy, w, gc, x, y, ww, hh);
    XSetFillStyle(dpy, gc, FillSolid);
}
```

`XSetTSOrigin(x, y)` with the rectangle coordinates is key: the pattern continues across tile, dock, and background boundaries, so there are no seams. For the background window, do the same through `XSetWindowBackgroundPixmap` (the server tiles it automatically on expose). If tiled filling proves slow on fbdev, fallback: build one 480×800 background in a `Pixmap` when the theme changes and blit it with `XCopyArea`; use tiles only for tiles.

**Player (fbdev, no X).** The tile is 256×256 in RGB565; copy the rectangle row by row with wraparound using mask 255—two `memcpy` calls per row, no per-pixel math:

```c
for (int r = 0; r < h; r++) {
    const uint16_t *src = tile + ((y + r) & 255) * 256;
    int x0 = x & 255, n = w < 256 - x0 ? w : 256 - x0;
    memcpy(dst + r * stride, src + x0, n * 2);
    if (n < w) memcpy(dst + r * stride + n, src, (w - n) * 2);
}
```

The tile is rebuilt only when the theme/accent changes. The player’s once-per-second redraw (progress bar) does not use texture: progress and the thumb are always flat.

**Panel rotation** by 180° changes nothing: the texture is generated in screen orientation and rotates with the frame.

### 6.6 Where texture is and is not used

| Surface | Grain | Laid | Why |
|---|---|---|---|
| `bg` (launcher, reader, shade, lock) | yes | optional | large flat areas show the material |
| `surf` (tiles, cards, page) | yes | reader page only | a tile should read as a sheet/card |
| `art` (cover backing) | yes, weaker | no | cover placeholder is the same paper |
| `sel` (current row) | no | no | highlight is a state and must be flat |
| `bar` (dock, keyboard) | no | no | dense elements need clarity and speed |
| `accent` fill | **no** | no | accent is a state and must be flat |
| text, icons, numbers | **no** | no | readability |
| progress, thumb, `track` | **no** | no | 4 px bar; any noise makes progress look “dirty” |
| 1 px lines (`line`, `line2`) | **no** | no | texture breaks a line into dashes |
| terminal | **no** | no | mono 10/14; noise beneath glyphs is prohibited |
| dialog/dimming | **no** | no | overlay must be uniform |

### 6.7 Effect on 16-bit color and readability

Measured on generated tiles (`texture-stats.json`):

| Theme | R levels in tile | Share | Brightness difference | Average background shift |
|---|---|---|---|---|
| graphite · grain | 16 (one) | 100% | 0% | none (grain is not enabled) |
| graphite · laid | 16 / 25 | 96 / 4% | +42% on line | +0.3% |
| paper · grain | 247 / 239 | 90 / 10% | 4.1 percentage points | −1 level (`#f7ebde`→`#f6eadd`) |
| book-n · grain | 197 / 189 | 90 / 10% | 3.2 percentage points | −1 level |

Important points:

- **The average tone shifts by 1%, not 5%.** Therefore HEX values in the tables remain accurate, and `dim/bg` contrast changes by less than 0.1—the text thresholds are unaffected.
- **Text pixels are not textured at all.** Text is drawn after the fill, in a flat color; beneath it there can only be grain that changes the background by 1 code. This is not “noise beneath letters,” it is paper beneath letters.
- **Grain is not dithering.** Dithering simulates missing colors and preserves the average tone; the goal here is the reverse—to preserve tone and add material. Therefore ordered dither (50/50 checkerboard) is prohibited: on 565 it reads as a panel artifact.
- **There is no texture on dark themes**—the main conclusion of this section. The only exception is optional laid: a 42% change on a 1 px line looks like neat “graph paper,” not dirt.

### 6.8 What was deliberately rejected

- **Per-pixel runtime noise**—384 thousand pixels per frame on a 1 GHz CPU without a GPU, plus an unstable image between frames. Precompute only.
- **Transparency/alpha over color**—in Xlib this requires compositing or manual blending; at 16 bits it produces dirty intermediate codes.
- **Perforation/dot grid**—a regular grid of 1 px dots at 170 ppi creates moiré with the panel pixel grid and the font raster.
- **Texture on accent fills**—it harms `onacc` readability and the flatness of the state.
- **One texture layer over the entire screen**—the background under the terminal and shade must stay clean.

---

## 7. Previews

Everything is in `artifacts/themes/`, 480×800, **run through RGB565**—so it is shown faithfully, including text gradient steps.

| File | What to look at |
|---|---|
| `overview.png` | three themes in use: launcher and reader, reduced to 0.55 |
| `palettes.png` | all 16 tokens × 7 themes; sRGB on the left, 565 chip on the right |
| `accents.png` | “accent × theme” matrix with contrasts directly on the fills |
| `texture-sheet.png` | grain at 1:1 and ×3, tile seam, laid, codes and shares |
| `screens/start-graphite.png` · `start-graphite-laid.png` | launcher: flat and laid |
| `screens/start-paper.png` · `start-paper-tex.png` | launcher: flat and grain |
| `screens/start-book-n.png` · `start-book-n-tex.png` | launcher: flat and grain |
| `screens/reader-graphite.png` · `reader-graphite-laid.png` | reader: flat and laid |
| `screens/reader-paper.png` · `reader-paper-tex.png` | reader: flat and grain + laid |
| `screens/reader-book-n.png` · `reader-book-n-tex.png` | reader: flat and grain |
| `palettes.json` | tokens + `tokens565` + all contrasts + accent matrix |
| `palettes.h.txt` | ready-to-use lines for `THEMES[]` |

What to inspect first:

1. `overview.png`—do the three themes read as **one** product?
2. `screens/reader-*`—does 15/26 text hold up on gray and cream?
3. `texture-sheet.png`—does grain read as material at 1:1 and as “code jaggies,” not random noise, at ×3?

The previews are not product mockups: grid, icons, and text are approximate; screen purposes are described in `SCREENS.md`. “Reader” is not yet in `SCREENS.md`—it was drawn only to test paper themes with long text (15/26) and show where grain appears. If a reader is added to the product, its metrics must be documented separately.

---

## 8. Recommendation

### 8.1 What to ship: three themes

| Slot | id | Role |
|---|---|---|
| 1 | `graphite` | **default**, evening, entire shell and player |
| 2 | `paper` | daytime/reading, warm paper + grain |
| 3 | `book-n` | daytime in shade, gray book + grain |

Why these: they provide three distinct lightness levels (L* 7 / 94 / 80), three materials, and three different impressions—while taking only three settings rows that do not require deliberation. `paper-n` (L* 95) is too close to `paper` in role (only the temperature differs); `ember` and `slate` are “graphite with a different undertone,” and belong in the settings file, not UI.

If the owner says “paper is too yellow outdoors,” replace `book-n` with `paper-n`; that is one line.

The default accent is **turquoise** in every theme: that is the identity. Amber is the second most important (it already exists in the player as a separate “amber” theme); the others are optional. Do not ship red (see rule 3 in §5).

The default texture is `auto`: grain on `paper`/`book`, none on `graphite`, and laid off. Settings toggle: AUTO / ON / OFF.

### 8.2 How to switch

`/etc/lgtest-settings` remains the single source of truth for both the shell and the emergency fb player:

```
theme=graphite      # graphite | ember | slate | paper | paper-n | book | book-n
accent=turquoise    # turquoise | amber | blue | red | green
tex=auto            # auto | grain | laid | both | off
```

Read rules:

- missing key or unknown value → `theme=graphite`, `accent=<native to the theme>`, `tex=auto`; never crash or reset the file;
- **compatibility aliases**: `theme=amber → ember`, `theme=frost → slate`. Old values already exist in files and in the fb player; without aliases, a user will get graphite after an update and think the “theme got reset”;
- write `accent=` only after an explicit selection. If the key is absent, use the theme default accent (`ember` → amber);
- write atomically (temp + rename), because two processes read the file;
- switching is **instant**, with no reboot: 16 values, 3 tiles, and a full redraw (~30–50 ms). A reboot is needed only for `START (shell/no X)`.

Writers: the shell “Settings” screen and fb player settings. Both read the same value, so the theme does not “jump” on rollback.

### 8.3 Settings screen

The current three THEME rows cannot accommodate seven. Proposal (same 432 px, header 96, footer 11):

- **432×72 preview** at the top: background + `surf` tile + `accent` strip + “Aa 15”. Shows the result before applying it.
- **THEME**: groups `DARK` / `LIGHT` / `GRAY` (11 px label +2), rows **44 px** instead of 56: 20×20 swatch, 15 px name, 12 px selection marker. 7×44 + 2×24 = 356.
- **ACCENT**: five 44×44 squares spaced 84 apart (x = 20, 104, 188, 272, 356), 11 px caption under each. Selected item: 1 px `fg` border + `accent` marker.
- **TEXTURE**: three 136×44 segments (`AUTO` / `ON` / `OFF`), 11 `dim` hint: “dark themes do not have grain.”
- Total: 96 + 72 + 8 + 356 + 90 + 76 = 698 of 752—fits without scrolling.

Order THEME rows by lightness (graphite → ember → slate → book → book-n → paper → paper-n), so the eye sees a scale, not a list.

### 8.4 How this maps to code

**Player / shared layer.** Tokens are already in `Theme` (16 colors); adding themes is data, not code:

```c
static const Theme THEMES[] = {
/* ... lines from artifacts/themes/palettes.h.txt ... */
};
/* accent replaces three fields after the theme is selected */
struct Acc { const char *id; uint32_t base, press, on[3]; };
/* on[0] — for dark, on[1] — paper, on[2] — gray */
```

`apply_theme(theme_id, accent_id)` = copy 16 colors + replace 3. Everything else (rendering, layout) knows nothing about themes.

**Shell (Xlib).** Besides the palette, it needs a `Ui` with three `Pixmap` tiles and `fill_tex()` from 6.5; the window background uses `XSetWindowBackgroundPixmap`. Header and dock are override-redirect windows with their own `bg`, so the tile is shared and the phase is set with `XSetTSOrigin`. On theme change: rebuild tiles → `XClearWindow` → redraw. No timers or animations.

**Player (fbdev).** The same mask; build the tile in an RGB565 array and copy row by row (6.5). On the one-second tick, redraw only the progress bar using flat colors.

**External applications** (`st`, `pcmanfm`, `dillo`) will not receive the palette—this is a risk, see 9.5; for the terminal, `st` colors can be selected from tokens (`bg`/`fg`/`accent`) on launch.

---

## 9. Risks

| # | Risk | Manifestation | Mitigation |
|---|---|---|---|
| 9.1 | **Grain is impossible on dark themes** | if enabled, background looks “dirty,” dithering is visible in flat areas | do not enable; UI hint “dark themes do not have grain”; laid is optional |
| 9.2 | **Neutral light themes shift cool** | in `#eeeeee`, blue channel jumps 239→247 (+3%) | intentional: `paper-n` is “cool” paper; use `paper` if it is distracting |
| 9.3 | **Tile repetition** | “wallpaper” pattern 256×256 visible on a large flat area | 1-code amplitude, grain without low frequencies; if reported, second mask variant with phase offset on `surf` |
| 9.4 | **Tiled fill speed on fbdev** | `FillTiled` may be slow without acceleration | measure on phone; fallback is prebuilt 480×800 background + `XCopyArea` |
| 9.5 | **External applications fall outside the theme** | white `pcmanfm`/`dillo` window on graphite | minimum: `st` with theme colors; accept the rest as “foreign windows” |
| 9.6 | **Seven themes in settings** | choosing from seven takes longer than from three | ship three; keep the rest in the file only |
| 9.7 | **Old values `theme=amber/frost`** | theme “got reset” after update | aliases `amber→ember`, `frost→slate`; do not rewrite file on read |
| 9.8 | **Red accent** | play reads as recording; “Power off” loses meaning | do not ship; document the rejection |
| 9.9 | **Two writers for settings file** | shell and fb player overwrite one another | atomic write, read before changing, one field at a time |
| 9.10 | **Light theme at night** | at 100% brightness, paper glares | do not switch automatically; retain manual selection, graphite default |
| 9.11 | **Contrast after quantization** | mockup HEX ≠ screen | new themes use 565 grid; calculate contrast in 565 (as done here) |
| 9.12 | **Texture beneath small text** | 11 px `dim` on grain | grain does not affect text; 1% average shift; check on device with `tex=on` |

---

## 10. Three conclusions

1. **Seven themes are three families and three accent registers, not seven palettes.** One set of 16 tokens plus the “accent = state” rule gives the whole system; adding a theme means 16 values, not a new design. Therefore ship three in the UI (graphite / paper / book-n), while the other four live in `/etc/lgtest-settings` for those who need an undertone.

2. **Texture is limited by 16-bit color, not taste.** One 565 code is 3% on paper and 40–56% on graphite. Therefore grain is only for light and gray themes (10–16% coverage, 3–5 percentage-point difference, 1% average shift); dark themes get only laid lines of 1 px. Everything is precomputed into a 256×256 mask (8 KB) and baked into tiles at load time; not one random number is generated at runtime.

3. **The key to a “premium” feel is not texture, but disciplined contrast and color.** Every pair clears the thresholds with margin (`fg` 10–16, `dim` 5.4–6.1, accent 4.9–9.3, text on fill 5.2–9.7); light themes use the accent as ink, dark themes as light, and red is not shipped because its semantics are reserved for power. Texture adds 1% tone and 10% pixels—that is enough to keep the screen from feeling like “plastic,” and little enough not to impair reading.
