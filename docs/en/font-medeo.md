# FONT-MEDEO — whether to replace DejaVu with Medeo

Status: **implemented in `os/`** (2026-11-24). Fonts are in `os/fonts/medeo/`, loading with fallback and baseline normalization are in `os/src/gfx.cpp`, and installation is in `os/install.sh`. The phone was not touched; there are no commits. Remaining: device QA (§11.4).

Short answer: **partly — yes, but not by “just dropping in a TTF.”** Of the four files, two (Display ExtraLight and Mono) are better than DejaVu immediately; body and Bold need two fixes: **tabular figures** and **baseline normalization**. Without them, Medeo breaks clocks/timers and the vertical rhythm of every screen.

---

## 1. What was downloaded and where

| Path | Contents |
|---|---|
| `artifacts/fonts/medeo/` | 11 static TTFs (Sans 200/300/400/500/700, Display EL/Light/Regular, Mono 200/400/700) + `OFL.txt` |
| `artifacts/fonts/medeo-map/` | DejaVu names ← Medeo (EL = **Display** ExtraLight) — for shell testing |
| `artifacts/fonts/medeo-map-el/` | same, but EL = **Sans** ExtraLight (comparison variant) |
| `artifacts/fonts/medeo-tnum/` | same files with the digit cmap rewritten to `tnum` alternates |
| `artifacts/fonts/medeo-map-tnum/` | DejaVu names ← patched Medeo (recommended set) |
| `artifacts/fonts/medeo-specimen.png` | specimen (2×, 2250×7628), + `specimen-layout.json` |

Downloaded from `github.com/DNFDEVS/medeo-font`, branch `main`, static TTFs (`fonts/MedeoSans/ttf/*`, `fonts/MedeoMono/ttf/*`). All files are valid TrueType (`00 01 00 00`), upem **1000**, with no `fvar` (not variable fonts).

---

## 2. Role → file → weight → size

| Shell role | Current (DejaVu) | Medeo | Weight | Size | DejaVu |
|---|---|---|---|---|---|
| body, rows 15–16, values | `DejaVuSans.ttf` | `MedeoSans-Regular.ttf` | 400 | **238 KB** | 739 KB |
| Bold (row headings, buttons) | `DejaVuSans-Bold.ttf` | `MedeoSans-Bold.ttf` | 700 | **240 KB** | 689 KB |
| headings 24–30, clock 76 | `DejaVuSans-ExtraLight.ttf` | `MedeoSansDisplay-ExtraLight.ttf` | 200 | **236 KB** | 347 KB |
| terminal, mono 12 | `DejaVuSansMono.ttf` | `MedeoMono-Regular.ttf` | 400 | **202 KB** | 333 KB |
| **4 files total** | | | | **0.92 MB** | **2.06 MB** |

57% saved (938 628 B versus 2 158 852 B). `MedeoSans-Light/Medium` (+475 KB) remain on disk; they are not needed until there is a fifth slot.

Measured metrics (fontTools):

| | Medeo Sans | DejaVu Sans | Consequence |
|---|---|---|---|
| upem | 1000 | 2048 | — |
| hhea ascender / em | **1.005** | 0.928 | baseline is 2 px lower at 16, 3 at 24, 5 at 30, 8 at 76 |
| asc+desc | 1.30 em | 1.16 em | line height 1.33–1.45 em versus 1.19–1.27 |
| cap height | 0.710 em | ~0.75 em | at 24 px: 17 versus 18; at 30: 21 versus 22 |
| x-height | 0.530 em | ~0.54 em | 11–16 px match pixel for pixel |
| digits | **proportional** (1 = 5.5 px at 15) | tabular (9.5) | clocks/counters jump |
| glyphs | 1276 | 6253 | fewer, but sufficient for us (see §5) |
| hinting | `fpgm`/`cvt`/`prep`, 569 glyphs, `gasp` **0x0F** | 1007 glyphs, `gasp` 0x02 up to 8 ppem | Medeo gridfit is enabled even at 11 px |
| kerning | GPOS only | kern + GPOS | irrelevant without HarfBuzz |

---

## 3. What Medeo offers

1. **A true display cut for headings.** `MedeoSansDisplay-ExtraLight` at 24–30 px is a separate optical size, not a stretched text font. In the 3× crop (`head-compare-3x.png`), DejaVu EL looks wide and “system-like”; Medeo Display is more composed, with wider counters. This is the most noticeable gain.
2. **Hinting at every size.** `gasp` = 0x0F versus DejaVu's 0x02 below 9 ppem. For 11 px at 217 ppi this is the right choice: Medeo does not leave small text to AA alone.
3. **Complete coverage of what the shell actually draws.** All 79 non-ASCII code points were extracted from `os/` sources (Cyrillic 96/96, `·«»×—–…←↑→↓−✓№₽°±`); **none are missing** from any of the 11 Medeo files. DejaVu Sans ExtraLight, by comparison, lacks `✓` (U+2713) — this does not currently cause a problem because `✓` is drawn in the regular face (`apps.cpp:166`, 11 px), but Medeo has the glyph at every weight.
4. **The mono grid remains intact.** `MedeoMono` has exactly 600/1000 em per cell: at 12 px, 7.20 → the same **7 px** as DejaVu (7.22). So the `kAdvance = 7` constant in `terminal.cpp` and the 65×36 grid remain valid, and box drawing is intact: 160/160 glyphs U+2500–259F, `─`/`═` exactly one cell wide, line joints continuous (crop `crop-mono-4x.png`).
5. **It has `tnum`.** GSUB contains 12 tabular alternates in Sans (`zero.tf` … `nine.tf`, advance 656/1000); the digits can be fixed natively, without drawing glyphs.
6. **File weight is 2.3 times lower** — 4 files instead of 2 MB.
7. The full 200/300/400/500/600/700 range and Display cut leave room to adjust typography later (there are 12 ss sets, but they are useless because the shell does not run shaping).

---

## 4. Where Medeo loses (with numbers)

**Proportional digits are the main problem.** At 15 px: `1` = 5.5 px, `0` = 10.2 px. Widths of time strings:

| String | DejaVu 15 px | Medeo 15 px | Δ |
|---|---|---|---|
| `00:00` | 43.2 | 44.9 | +4 % |
| `11:11` | 43.2 | **25.0** | **−42 %** |
| `22:35` | 43.2 | 42.2 | −2 % |
| `1:13` | 33.7 | 24.3 | −28 % |

That is, “11:11” is 1.8 times narrower than “00:00”. At 76 px, the lock-screen clock jumps ~20 px in width when the minute changes (visible in `crop-time-drift-graphite-4x.png` and the render comparison `font-medeo-lock.png` versus `font-medeotnum-lock.png`).

**The baseline shifts down.** Measurements from actual shell renders (not metrics):

| Element | DejaVu (ink top) | Medeo | Shift |
|---|---|---|---|
| clock 76 px, lock | 288 | 296 | **+8** |
| heading 30 px (`set-root`, “Settings”) | 43 | 48 | **+5** |
| heading 24 px (“Notes”, player pivot) | 43–45 | 45–48 | +2…3 |
| `ui_row` name 16 px | 129 | 131 | +2 |
| `ui_row` caption 12 px | 151 | 152 | +1 |
| notes body 15 px | 150 | 153 | +3 |
| status-bar clock 12 px | 11 | 12 | +1 |

There is no clipping (`ui_row` caption ends at y+45 in a row height of 56), but the heading crowds the caption: in `font-medeo-set-root.png`, “Settings” and “DEVICE · GRAPHITE” are tight together; in `font-medeo-set-display.png` they almost touch. With the baseline patch (§8.2), everything returns to position: notes heading 45..75 versus DejaVu's 45..76, `ui_row` 129..139 in both.

**Text is 10–15% narrower** — at the same point size it looks smaller and drier: 16 px `Power settings` 166 → 148 px, 24 px `Settings` 135 → 115, 30 px `Now` 112 → 100. At 24+ there is also −5% cap height.

**Narrower coverage:** Sans has **0/160** box-drawing glyphs (DejaVu Sans has them all), and 21/112 arrows (missing `⇔ ⇒ ⇐ ↺ ➜`), with less mathematics/symbol coverage. The current UI works around this, but a new screen that draws a frame in the regular face will show blanks.

**Design direction.** `notes/METRO-RESEARCH.md` (p. 128) and `notes/DESIGN-PROMPT.md` (p. 56) record the opposite: “DejaVu Sans, no ‘designer’ fonts”, “do not use ExtraLight below 20 px”. Medeo is a geometric sans: this changes the shell's character, not just its metrics. On the plus side, the complaint that “EL breaks up on RGB565” is less applicable to Medeo — by ink mass Medeo EL is **heavier** than DejaVu EL (24 px: 384 versus 364, in a box 15% narrower).

---

## 5. Checks against the task requirements

**11 px readability — OK, but lighter by ink mass.** x-height and cap match DejaVu pixel for pixel (6 and 8 px); total ink on the same line is −10% (184 versus 205), with the same density inside letters (0.212 versus 0.216). In 3× crops (`sz-dejavu-3x.png` / `sz-fix-medeo-3x.png`), Medeo's uppercase labels “STATUS” are even denser and more legible, while lowercase letters are slightly narrower. Conclusion: 11 px is acceptable; moreover, Medeo at 12 px is as wide as DejaVu at 11 px (154 versus 149 px for “STATUS DEVICE”), so small labels could be increased by +1 if desired without stretching the layout — but that would require editing literals across dozens of screens and is outside the §8 plan.

**Cyrillic — no issues.** The forms corresponding to the Russian letters Y-short, yo, hard sign, yery, soft sign, and shcha are sound; diacritics do not stick to the line above, and descenders are even. All 96 lowercase/uppercase letters plus both yo forms are present.

**ExtraLight thickness at 24–30 — not too thin.** By ink mass Medeo EL is actually heavier than DejaVu EL (24 px: 384.6/384.7 versus 363.8; 30 px: 406.7 versus 361.3); it is simply composed in a box 15% narrower. Headings read confidently in the 2× specimen and renders; there is no “breakup” on 565. The difference between Display-EL and Sans-EL is only width/tracking (Display is 2–3% narrower, which helps at 24–30).

**Digit width — see §4.** There are three ways to address it, in order of increasing effort:
- `TF_MONO` (Medeo Mono) for 12 px counters — works immediately, but 76 px clock digits look like a terminal;
- keep DejaVu just for digits (an unwanted fifth slot);
- **`tnum` patch** (recommended, tested): remap cmap 0–9 to `.tf` glyphs. After patching, `00:00 = 11:11 = 22:35 = 43.3 px` at 15 px (DejaVu 43.2); at 76 px `22:35` = 205.7 versus 219.0. Clocks and timers stop jumping (`font-medeotnum-lock.png`, `font-medeotnum-player.png`).

**Line height.** Medeo is 1.33–1.45 em versus 1.19–1.27. It does not overflow fixed pitches anywhere: notes `LH = 24` with a 22-pixel box, terminal `kLineH = 18` with 17 (mono 12 px: asc 13 + desc 4), `ui_row` 56 px with 45. But the extra space is redistributed downward — the baseline patch fixes this.

**Clipping in `ui_row`** — none (see §4), but the row name sits 2 px lower and the gap between the name and caption baselines shrinks from 17 to 16 px. After the baseline patch, the row lines up exactly as with DejaVu (verified: ink 129..139 in both).

**Notes wrapping** is recalculated from `textw` (15 px), so Medeo fits **more** characters in the same 440 px (about +10%). This is a benefit; nothing needs to be changed. In the test, 4 lines remained 4 lines, with more text on each.

**Terminal mono grid** matches: at 12 px the cell is 7.20 → the same 7 px as DejaVu (7.22); box-drawing 160/160, joints `┌┬┐├┼┤└┴┘` are continuous, and `←↑→↓ ✓` are present. The only difference is that Medeo Mono is larger inside the cell than DejaVu (ink +12%, height 12→14 px), so the text looks denser — probably a plus at 12 px.

**File weight/size** — 0.92 MB versus 2.06 MB (−57%), see §2.

---

## 6. OFL 1.1 license — what is and is not allowed

`artifacts/fonts/medeo/OFL.txt` is the standard OFL 1.1, with copyrights for Medeo (2026), Geist (Vercel, 2024–2026), Inter (2016), ParaType (PT Sans/PT Serif), and Noto Serif Display. The font is derived from Geist/Inter, all under OFL; the aggregate remains OFL. The license is listed in `name` (IDs 13/14), and copyright is in ID 0.

**Allowed:**
- bundle TTFs in firmware/rootfs and distribute them with a product, including selling the device (OFL explicitly allows bundling/embedding, clauses 0 and 2);
- modify them (we need the `tnum` patch) and distribute the modification — but **only under OFL** (clause 5), retaining copyright and license;
- use the font to render anything: text, UI, images — those are not covered by the font license (last sentence of the preamble and clause 5);
- keep the name “Medeo”: the Reserved Font Names here are only `PT Sans`, `PT Serif`, `ParaType`; they do not apply to our changes.

**Not allowed:**
- sell the font by itself (clause 1);
- relicense it under GPL/MIT/“proprietary” terms (clause 5);
- use authors' or rights holders' names in product advertising (clause 4);
- distribute without the license text: clause 2 requires **every copy** to contain copyright and license in a form that is “easily viewed”.

**Practical implication:** the rootfs needs `/usr/share/doc/medeo/OFL.txt` (and it must be included with updates); the copyright and license are already present in the patched files' `name` tables — my patch does not touch them (verified: nameID 0/13/14 remain). UI attribution (“About phone” → “fonts”) is not required, but one line in `notes/README.md`/the about screen would not hurt.

---

## 7. Risks and mitigations

| Risk | Level | Details | Mitigation |
|---|---|---|---|
| Proportional digits | **high** | clocks, timers, `%`, and file sizes can jump up to 42% in width | `tnum` cmap patch (§8.3), tested: widths match DejaVu |
| Baseline 1.005 em | **high** | text shifts down 1–8 px, heading crowds caption | 3 lines in `gfx.cpp` (§8.2), tested on a copy of the tree |
| No box-drawing in Sans | medium | 0/160; a frame in the regular face will be blank | currently all frames use `TF_MONO`; do not draw tables in the regular font |
| Fewer glyphs (1276 vs 6253) | medium | lacks 91 of 112 arrows (`⇔ ⇒ ↺ ➜`), less math | check new screens; use DejaVu as a fifth file if needed |
| Design direction | medium | `METRO-RESEARCH`/`DESIGN-PROMPT` require “no designer fonts” | product decision; update the documents rather than changing the font silently |
| Body slightly lighter | low | Regular at 16 px looks drier than DejaVu Book | use `MedeoSans-Medium` (500) in the Regular slot if disliked |
| Font larger in mono cell | low | ink +12%, height 12→14 px at pitch 18 | measure on device; optionally use `MedeoMono-Light` |
| xterm/fontconfig | low | `-fa 'DejaVu Sans Mono'` | do not replace DejaVu files; install Medeo in its own directory |
| Hinting | **not a risk** | Medeo gridfit is enabled at all ppem (`gasp` 0x0F) | — |
| File weight | **not a risk** | 0.92 MB versus 2.06 MB | — |
| License | **not a risk** | OFL permits embedding and modification | include `OFL.txt` in rootfs, preserve copyright in `name` |

---

## 8. Implementation plan (4 paths + fallback)

### 8.1 `gfx.cpp` — four paths with per-slot fallback

`gfx_init()` currently hard-codes `dir + "/DejaVuSans*.ttf"`. Replace this with two tables and three attempts per slot (Medeo → DejaVu in the same directory → system DejaVu). Partial installation then degrades gracefully: `face_for()` already falls back from Bold/EL to Regular, and `gfx_init()` returns `!faces.empty()`.

```cpp
static const char *kMedeo[4] = {
    "MedeoSans-Regular.ttf", "MedeoSans-Bold.ttf",
    "MedeoSansDisplay-ExtraLight.ttf", "MedeoMono-Regular.ttf"};
static const char *kDejavu[4] = {
    "DejaVuSans.ttf", "DejaVuSans-Bold.ttf",
    "DejaVuSans-ExtraLight.ttf", "DejaVuSansMono.ttf"};

bool gfx_init(const char *dir) {
    if (const char *env = std::getenv("GRAPHITE_FONTS")) dir = env;
    // ...
    for (int i = 0; i < 4; i++) {
        const std::string cand[3] = {
            std::string(dir) + "/" + kMedeo[i],
            std::string(dir) + "/" + kDejavu[i],
            std::string("/usr/share/fonts/dejavu/") + kDejavu[i]};
        for (const std::string &p : cand)
            if (!ft_paths[i] && load_face(p, i)) break;   // load_face: FT_New_Face + Set_Pixel_Sizes(15)
    }
}
```

Also set the default in `include/graphite.h:68` to `"/usr/share/fonts/medeo"`, and change the message in `os/src/app.cpp:56` from “no DejaVu fonts” to “no fonts”.

**Important: do not remove DejaVu from the device.** `terminal.cpp`/`browser.cpp` launch `xterm -fa 'DejaVu Sans Mono'`, which resolves through fontconfig. If DejaVu files are replaced with Medeo under the same names, xterm will silently get a different font. Install Medeo in its own directory.

### 8.2 `gfx.cpp` — baseline normalization (3 lines, tested)

Currently `int asc = f->size->metrics.ascender >> 6;` — for Medeo this is 1.005 em. Use a threshold based on the font's ascender, not the pixel value (otherwise it catches DejaVu at 11 px):

```cpp
int asc = f->size->metrics.ascender >> 6;
float dr = f->units_per_EM ? (float)f->ascender / (float)f->units_per_EM : 0.f;
if (dr > 0.97f) asc = (int)(px * 0.928f + 0.5f);   // == DejaVu
```

Tested on a copy of the tree in `[temporary tree copy]` (`font-fix-*.png`): DejaVu renders byte-for-byte as before (bands 43..71 / 73..80 / 106..113 / 129..139); Medeo lands at the same y positions (129..139), and lock-screen clock ink is 290..345 versus 288..345 — baseline matches, with only cap-height differing.

When implemented, rounding was changed to `ceil`: DejaVu's scaled `ascender` is exactly `ceil(0.928 * px)` at all sizes 11–40 (verified with FreeType), whereas `+0.5` gives one pixel less at 11/12/24 px and moves the status bar and mono grid one pixel above the DejaVu lines. Details in §11.2.

### 8.3 Digits: `tnum` patch in font build

The script `artifacts/fonts/patch_tnum.py` already exists: it reads the GSUB `tnum` feature and substitutes `.tf` glyphs in cmap for U+0030–0039. There are 12 substitutions, advance 656 (Sans) / 626 (Display EL); the result was checked: all time strings have equal width. Run it when updating the font, placing the result in `/usr/share/fonts/medeo/`. The modification is an OFL derivative (§7): copyright is retained and the file remains OFL.

### 8.4 Point sizes

Do not change them. Tested: increasing the 24 px heading by +2 crowds the caption — in `set-display` (this is `ui_header`) at 26 px the descender in the Russian word for “Screen” falls onto the uppercase line “THEME · LIGHT” (`hdr2-h26-set-display-3x.png` versus `hdr2-fix24-set-display-3x.png`). `set-root` has its own 30 px title and caption at fixed y = 70 — the gap is already 1 px after the baseline patch, so it cannot be increased either. Leave body sizes 15/16 alone: Medeo would need 18 px to match DejaVu's width, and that will not fit in a 56-pixel row. `TF_LIGHT` already points to Display EL, so `style_of()` does not need to change.

If the “same optical size” is ever needed, this is not a point-size change; it requires moving the caption down and rebuilding dozens of screens.

---

## 9. Previews

Shell renders at 480×800 (`GRAPHITE_SCREEN=... GRAPHITE_FONTS=...`), display `:91`:

- DejaVu: `artifacts/host/font-dejavu-{home,set-root,set-display,lock,notes,terminal,player,shade,files,queue,library}.png`
- Medeo (EL = Display): `artifacts/host/font-medeo-*.png` (same screens)
- Medeo (EL = Sans ExtraLight): `artifacts/host/font-medeoel-*.png`
- Medeo with `tnum`: `artifacts/host/font-medeotnum-*.png`
- Medeo + tnum + baseline patch: `artifacts/host/font-fix-medeo-*.png`; controls: `font-fix-dejavu-{lock,set-root}.png`

Specimen and crops:

- `artifacts/fonts/medeo-specimen.png` — 11/12/15/16/24/30 px, sentence + Russian/English keyboard layouts + digits, graphite and paper themes, 1× raster ×2 nearest
- `artifacts/fonts/crop-11-12-graphite-4x.png`, `crop-15-16-graphite-4x.png`, `crop-24-30-graphite-4x.png`, `crop-el-24-30-graphite-4x.png`, `crop-nums-graphite-4x.png`, `crop-time-drift-graphite-4x.png`, `crop-clock-4x.png`, `crop-mono-4x.png`
- `artifacts/fonts/head-compare-3x.png` — three headings (DejaVu EL / Medeo Display EL / Medeo Sans EL) at 3×
- `artifacts/fonts/fix-medeo-head-3x.png` — same after baseline patch; `hdr-font-fix-medeo-set-root-3x.png` / `hdr-font-fix-dejavu-set-root-3x.png` — 30 px title at 3× (patched Medeo versus DejaVu); `hdr2-fix24-set-display-3x.png` versus `hdr2-h26-set-display-3x.png` — why the 24 px heading must not be increased to 26
- `artifacts/fonts/tz-{dejavu,medeo}-3x.png` — terminal at 3×; `artifacts/fonts/nz-{dejavu,medeo}-3x.png` — notes body at 3×; `artifacts/fonts/sz-{dejavu,fix-medeo}-3x.png` — ui_row at 3×

Scripts (in `artifacts/fonts/`, outside git): `fontprobe.py`, `cover.py`, `measure.py`, `weight.py`, `bbox.py`, `specimen.py`, `patch_tnum.py`, `render*.sh`, `pilot*.sh`.

Quick host check without rebuilding:

```sh
GRAPHITE_SCREEN=lock \
GRAPHITE_FONTS=./artifacts/fonts/medeo-map-tnum \
sh os/preview.sh my-lock :91
```

---

## 10. Three conclusions

1. **It is worth switching for headings and mono; body text only with patches.** `MedeoSansDisplay-ExtraLight` (24–30 and clock 76) and `MedeoMono` (terminal) are objectively better than DejaVu on this screen: a real display cut, hinting at all ppem, box-drawing 160/160, and half the file weight. Body and Bold are 10–15% narrower and look denser — a matter of taste, but without fixing digits and baseline they break clocks and screen rhythm.
2. **Two fixes are mandatory; both are small and tested.** The `tnum` patch (cmap 0–9 to `.tf`, time-string widths then match DejaVu within 0.2 px) and baseline normalization in `gfx.cpp` (3 lines using `ascender/upem > 0.97`). With these, Medeo lands on the same y positions as DejaVu, and no screen needs to be relaid out.
3. **Risks are manageable, except for one product decision.** Sans lacks box-drawing and 91 of 112 arrows, with 1276 glyphs versus 6253 — ample for the current UI, but new screens must be checked for tofu. The OFL license imposes no obstacle: TTFs may be included in firmware; `OFL.txt` must be alongside them and copyright preserved in patched files. The real risk is design direction: `notes/METRO-RESEARCH.md` and `DESIGN-PROMPT.md` specify “DejaVu, no designer fonts”, while Medeo changes the shell's character.

**First implementation step:** the `/usr/share/fonts/medeo` directory is built from `medeo-tnum`; `gfx.cpp` has a four-slot table with DejaVu fallback and baseline normalization (§11). Next is device QA: install on the phone, capture `set-root`, `lock`, `terminal`, `notes` in graphite and paper, and inspect on the live display in sunlight: (1) whether the time jumps, (2) whether the caption crowds the heading, (3) whether the terminal grid breaks, (4) whether 11 px labels are legible at 100% brightness. If the clock and headings are no worse on 565, roll out to all screens; if the body looks too small/light, put `MedeoSans-Medium` (500) in the Regular slot (a fifth file, +238 KB). Until this test is done, the device verdict is **partly**, not “yes”.

---

## 11. Implemented (2026-11-24)

### 11.1 What is in the repository

`os/fonts/medeo/` contains byte-for-byte copies of `artifacts/fonts/medeo-tnum/` (verified with `sha256sum`):

| File | Bytes | sha256 (prefix) |
|---|---|---|
| `MedeoSans-Regular.ttf` | 243 684 | `bc411b7cccea` |
| `MedeoSans-Bold.ttf` | 246 004 | `c3a9ff34c099` |
| `MedeoSansDisplay-ExtraLight.ttf` | 241 584 | `454798ed6558` |
| `MedeoMono-Regular.ttf` | 207 356 | `48c0920502b3` |

Beside them are `OFL.txt` (same as in `artifacts/fonts/medeo/`) and a short `README.md` (source, version 1.100, exactly what was patched, and how to rebuild the patch).

Check of tnum in the exact files committed to the repository: cmap U+0030–0039 → `zero.tf … nine.tf`, advance 656 (Sans R/B), 626 (Display EL); Mono cmap was unchanged because its digits are already 600. `name` IDs 0/13/14 remain.

`os/install.sh`: `mkdir -p /usr/share/fonts/medeo`, `cp fonts/medeo/*.ttf fonts/medeo/OFL.txt fonts/medeo/README.md`, `chmod 644`.

### 11.2 Code

Only two locations in `os/src/gfx.cpp`:

1. `gfx_init()`: two name tables (`kMedeo`/`kDejavu`) and three attempts per slot. Without `GRAPHITE_FONTS`, directories are `/usr/share/fonts/medeo`, then `/usr/share/fonts/dejavu`; an explicit directory (`GRAPHITE_FONTS` or argument) takes precedence, but system DejaVu remains the final fallback, so DejaVu can be forced on the device. DejaVu files are not replaced — `xterm -fa 'DejaVu Sans Mono'` must not silently change fonts.
2. `Gfx::text()`: if `f->ascender / units_per_EM > 0.97`, ascent is taken as `ceil(px * 0.928f)`. For DejaVu (0.928) the branch is not taken — output remains byte-for-byte unchanged; for Medeo (1.005) the baseline aligns exactly with a DejaVu line.

`ceil`, not `+0.5` from §8.2: DejaVu's scaled `ascender` equals `ceil(0.928 * px)` for every px 11–40 (verified with a FreeType probe), whereas rounding to nearest at 11/12/24 px gives one pixel less — the status bar, captions, and mono grid would shift up one pixel relative to DejaVu. With `ceil`, in host renders the status-bar bands `11..20`, `set-root` heading `43..80`, and `ui_row` lines `129..139` match DejaVu line for line.

`ui.cpp`, `set_display.cpp`, screens, and themes were not touched.

### 11.3 Host verification

- `sh os/host-build.sh` — clean (`-Wall -Wextra`, no warnings).
- DejaVu is byte-for-byte identical: a control build was compiled from the same sources with `gfx.cpp` from `HEAD`; `notes`, `set-root`, `lock` — `diff bbox = None`. In `terminal`, only the live line differs (`free -m`/uptime — the same build differs from itself); the font is unrelated.
- 480×800 previews on `:91`, `GRAPHITE_FONTS=./os/fonts/medeo`: `artifacts/host/medeo-{home,set-root,lock,notes,terminal,player}.png`; controls: `artifacts/host/dejavu-*.png` (same screens, same binary).
- Bands (ink, y): status bar `11..20` in both; `set-root` heading and caption `43..80` in both; `ui_row` name/caption `129..139` / `141..157(158)`; notes body 15 px — line bottoms match (`164, 188, 212`), tops are 1 px lower due to shorter cap height; terminal — first lines `45..67` and `69..76` match, then ±1–2 px due to the larger mono ink box.
- Clock: time-string widths at 15 px — `00:00 = 11:11 = 22:35 = 44 px` (DejaVu 45); on the lock screen, Display EL cut at 76 px — `00:00 = 11:11 = 22:35 = 207 px` (DejaVu 218). Thus the clock block is 5% narrower, but does not change minute to minute. Ink widths of digits still differ (`1` is narrower); there is no shift because centering uses `textw` and all ten digits have the same advance.
- Terminal: `kAdvance = 7`, Medeo Mono 600/1000 × 12 px = 7.2 → same 7 px; the 65×36 grid and `TTY1 · 65×36` label are unchanged.
- No clipping or collisions are visible: `set-root` caption is separated, notes keyboard is in place, player pivots do not collide, and home tiles and “Add tile” remain within their original bounds.

### 11.4 Remaining work

- **Device QA** (the only open item): run `install.sh`, check the 4 items under “First implementation step” and body weight on the live display.
- Run `xterm`/`dillo` after installation: they must still use DejaVu from `/usr/share/fonts/dejavu` (Medeo is installed separately).
- Make the product decision: keep Medeo in `METRO-RESEARCH`/`DESIGN-PROMPT` (which specify “DejaVu, no designer fonts”) or not.
- If body text looks too small, replace Regular with `MedeoSans-Medium` (500) and recheck screens whose layout depends on `textw` widths.
- Cosmetic change outside the allowed list: `os/src/app.cpp:56` still prints “no DejaVu fonts”, although Medeo is tried first — a one-line fix for the next integration.
