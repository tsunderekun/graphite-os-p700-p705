# READER-V2 — reader as a graphite shell screen

> **Status, 26 Nov 2026:** Reader is implemented. EPUB 2/3, OPF/spine and TOCs
> from EPUB 3 nav or EPUB 2 NCX are supported, including nesting and `#fragment`
> targets, with headings/spine fallback. DRM/encryption, CSS and book images are
> unsupported. EPUB host tests: 8 PASS; EPUB 2 and 3 books were checked on the
> P705. The remaining design material below is not a promise that every proposed
> mode is implemented.

> Public project plan. Estimates and performance figures are targets pending
> confirmation on the device. This document contains no personal environment data.

> Project document. **No code has been changed; there are no commits.** Only
> read-only commands (`apk info`, `ls`, `df`, `free`) were run on the phone; nothing
> was installed or written. At the time of checking, the phone was in `player`
> mode; X was not started; it rebooted during the work (cause unknown). After
> boot, `/etc/lgtest-mode` = `shell`. Figures in §1.1 are a snapshot from before
> the reboot; this does not affect the conclusions.
>
> This document replaces the architecture part of `notes/READER-PLAN.md` (§3,
> separate `lgreader` process). The rest of that document — book model, layout,
> cache, themes — is reused with the adjustments below.
>
> Related documents: `notes/APPS-RESEARCH.md` (§4.6, §7.6, §9.6),
> `notes/shell/SCREENS.md`, `notes/shell/DESIGN-SYSTEM.md`,
> `os/include/graphite.h`, `os/src/app.cpp`, `os/src/gfx.cpp`,
> `os/screens/home.cpp`, `STATUS.md`.

---

## 0. Verdict

**The reader is a shell screen, not a separate application.** The shell already
exists: one process, one 480×800 window, its own `Canvas`/`Gfx`, FreeType,
themes, screen stack, key handling, and `apphost` for third-party GUIs. The
reader is added as a `ScreenView` with id `reader`; the registry already
contains `{"reader", make_reader}`, and `make_reader()` is currently a weak
stub. Neither `registry.cpp` nor `app.cpp` needs changes to make the screen
available.

- **v1:** TXT (UTF-8/CP1251 + KOI8-R/CP866/UTF-16 fallback) and FB2 (+`fb2.zip`).
- **v2:** EPUB 2/3. CSS is not executed; OPF/spine/NCX/nav + HTML SAX.
- **PDF:** not v1 and not linked into the shell. Separate `mupdf-x11` process
  via `apphost`; AGPL, 0.4–2 s/page, no reflow.
- **Parsers:** libxml2 SAX (push), libzip (or minizip as fallback), iconv only
  as a fallback for UTF-16/rare encodings; own CP1251 table.
- **Reader core:** deterministic layout with a fixed line grid, page map,
  anchor-based progress, and cooperative 8–12 ms slices. The parsers and
  “polished UI” are not the core — most bugs and all performance work will be here.
- **v1 estimate:** 10–13 days for one developer. S0 spike — 0.5–1 day.
- **Main risk:** not the format, but a long X/fb session and memory on 400 MB.
  Kernel #23 has already passed a 300-second stress test, but a 60-minute
  reader soak is mandatory; position is saved after every page.

One process is a deliberate choice. Isolating the parser in a separate process
improves resilience, but costs IPC, a second Canvas/font cache, key arbitration,
and startup latency. On 400 MB and one core, cooperative slices are more
valuable than a second process. If fuzzing reveals actual libxml2 crashes,
parsing can be moved to a short-lived helper that returns a ready `.book`; the
UI remains a shell screen either way.

---

## 1. Initial conditions and dependencies

### 1.1 Hardware and system

| Parameter | Fact |
|---|---|
| SoC | MSM7227A, 1×Cortex-A5 1 GHz, NEON/VFPv4, no GPU |
| RAM | Memory size depends on the model; free memory depends on load |
| Screen | 480×800, RGB565, 16-bit, panel rotated |
| X | Xorg on fbdev, `Rotate "UD"`, ShadowFB, `fbpan-ng` 10 Hz |
| Rootfs | ext4; free space depends on configuration |
| SD | Removable vfat partition, path `<sd-mount-point>` |
| Books | `/media/sd/Books` **does not exist**; directory is created on first scan |
| Mode | Depends on system configuration |
| Kernel | `3.4.0-CM #23-postmarketOS`; `msm_fb` patches, X stress passed for 300 s |

### 1.2 Packages relevant to the reader

Checked with `apk info -e`, `apk info -s`, `ls`:

| Package | On device | Size | License | Provides |
|---|---|---|---|---|
| `libxml2` 2.13.9-r3 | **yes** | 741 KiB | MIT | FB2/OPF/NCX/XHTML, SAX |
| `freetype` 2.14.3-r0 | **yes** | 449 KiB | FTL/GPLv2 | text, headings |
| `zlib` 1.4-r0 | **yes** | 65 KiB | Zlib | inflate for ZIP |
| `mupdf-libs` 1.28.5-r1 | **yes** | 4512 KiB | AGPL-3.0 | PDF (separate process only) |
| `font-dejavu` | **yes** | ~10 MiB | Bitstream Vera | Serif Regular/Bold/Italic |
| `libzip` 1.12.1-r0 | **no** (in repo) | 77 KiB | BSD-3 | EPUB, `fb2.zip` |
| `hyphen`/`libhyphen` | **no** (in repo) | 28 KiB | MPL-1.1/GPL-2.0 | hyphenation |
| `hyph_ru_RU.dic` | **no** | ~150–250 KB | verify when vendoring | Russian syllables |
| `mupdf-x11` | **no** (in repo) | 95 KiB | AGPL-3.0 | PDF window |
| `libxml2-dev`, `libzip-dev`, `hyphen-dev` | **no** | a few MiB | — | build only |

Important discrepancy with `APPS-RESEARCH.md`: it lists `libhyphen` as
installed, but `apk info -e libhyphen` and `ls /usr/include/hyphen.h` find
nothing. Treat `libhyphen` as a **new** package and the dictionary as a
vendored file. The device also lacks libxml2 headers (`/usr/include/libxml2`
does not exist), although the runtime library is installed.

### 1.3 Implications

- Runtime additions: `libzip` 77 KiB + `libhyphen` 28 KiB + dictionary ~0.2 MB
  = **≈0.3 MB**. Free space must be checked on the target device.
- Build dependencies: `libxml2-dev`, `libzip-dev`, `hyphen-dev` are needed on
  the host/WSL and on the phone when building there. They do not count toward
  runtime size.
- Shell linking: `-lxml2 -lzip -lhyphen` (and `-I/usr/include/libxml2`).
  `build.sh` currently links only `-lX11 -lXtst -lfreetype`, and
  `host-build.sh` does the same; both need extending.
- `mupdf-libs` is already installed, but `mupdf-x11` is not. PDF mode starts
  with `apk add mupdf-x11` (95 KiB), not by linking MuPDF.
- Linking MIT `libxml2` and BSD-3 `libzip` into the shell is allowed.
  `libhyphen` MPL/GPL linking is allowed without changes to the library source;
  the dictionary is a data file. MuPDF (AGPL) is **not** linked into the shell.
- `DejaVuSerif.ttf` 380 KB, `-Bold` 356 KB, `-Italic` 346 KB — load lazily:
  Regular immediately, Bold/Italic on first use.

---

## 2. Formats and parsers

### 2.1 Matrix

| Format | Version | Parser | Effort | Rationale |
|---|---|---|---|---|
| TXT | **v1** | own | 0.5–1 day | encodings, paragraphs, chapter heuristics |
| FB2 | **v1** | libxml2 SAX push | 2–3 days | 60–70% of Russian books; DOM for 1 MB would use 5–15 MB |
| `fb2.zip` | **v1** | libzip, find `*.fb2` inside | +0.5 day | common distribution as “one book in a zip” |
| EPUB 2/3 | **v2** | libzip + libxml2 HTML SAX | 3–5 days | OPF/spine/NCX/nav; CSS not executed |
| PDF | **v3, separate** | `mupdf-x11` via `apphost` | 1–2 days integration | AGPL, 0.4–2 s/page, 20–40 MB |
| MOBI/AZW3, DJVU, RTF, DOC | — | — | — | not in scope |

Shares are estimates; a corpus of 10 real books will be collected before S1 to
refine them.

### 2.2 TXT

- BOM → UTF-8/UTF-16LE/BE; otherwise strict UTF-8 validation on the first
  256 KB; otherwise score CP1251 / KOI8-R / CP866 by Cyrillic proportion and
  frequent bigrams (“th”, “he”, “in”, “er”, “an”, “re”, “on”, “at”, “en”, “nd”).
- CP1251 uses its own 128-entry table (fast, no iconv). KOI8-R/CP866 use
  similar tables. UTF-16 and rare encodings use iconv (musl, already on system).
- Normalization: CRLF/CR → LF, tabs → spaces, `\f` → blank line, invalid UTF-8
  → U+FFFD, remove control characters (except `\n`, `\t`).
- Paragraphs are separated by a blank line. Join hard wraps: if a line is
  60–90 characters and the next starts lowercase, join with a space.
- Chapters use heuristics: `CHAPTER|Part|Chapter`, short ALL-CAPS lines; if none
  are found, use one chapter named “Text”.
- Metadata: filename (without extension, `_` → space), author “—”. No cover →
  typographic placeholder (book icon, no small text).

### 2.3 FB2 + `fb2.zip`

- **libxml2 SAX, push mode:** `xmlCreatePushParserCtxt` + `xmlParseChunk` in
  32–64 KB chunks. This allows parsing in slices without building a DOM.
  Flags: `XML_PARSE_RECOVER`, `XML_PARSE_NONET`, `XML_PARSE_COMPACT`; do
  **not** enable `XML_PARSE_NOENT` (entity expansion).
- Encoding: libxml2 honors `encoding=` in the prolog; if there is no prolog
  and bytes are not UTF-8, detect CP1251 and transcode before parsing.
- Parse: `description/title-info` (title, author, annotation, coverpage,
  genre, date, sequence), `body` (main; skip `notes` in v1), `section`
  (nesting → chapters), `title`, `subtitle`, `p`, `empty-line`,
  `poem/stanza/v`, `epigraph`, `cite`, `text-author`, `image`, `table`.
- Inline: `emphasis` (italic), `strong` (bold), `strikethrough` (plain for now),
  `code`, `a` (footnote — plain text), `sup/sub` (plain).
- v1 tables are rendered as text, one row at a time, separated by ` · `, with
  no grid. v1 footnotes are text at the reference location; bottom sheet is v2.
  v1 illustrations are one line `[illustration]`, 11 px dim, no decoding; v2
  supports images.
- Cover: `<coverpage><image l:href="#id">` → `<binary id=...>` base64 →
  `stb_image` → 112×160 RGB565. Decode base64 only when building the cover
  cache, not on every startup. Source limit: 8 MB / 4096×4096.
- `fb2.zip`: detect ZIP by magic `PK\x03\x04`; find the largest `*.fb2`
  inside; read as a stream via `zip_fopen`, without extracting to disk.
  Limits: entry ≤32 MB, ≤10,000 entries, compression ratio ≤100:1.
  `libzip` is preferred (zip64, UTF-8 names, read-only); `minizip` from zlib
  contrib (zlib license) is a fallback only if the package is unavailable.
- Corrupt file: `XML_PARSE_RECOVER`; if no text is collected, use emergency
  extractor `<p>…</p>` + strip tags + basic entities; show “Could not parse”
  with an “Open as text” button.

### 2.4 EPUB (v2)

- `META-INF/container.xml` → OPF; `dc:title`, `dc:creator`, `dc:language`,
  manifest, spine, cover (`meta name="cover"` or `properties="cover-image"`).
- TOC: EPUB3 `nav` (XHTML) or EPUB2 `toc.ncx` (`navMap/navPoint`). In v2,
  navigation goes to the beginning of the spine document; exact `#frag`
  targets follow once anchors inside XHTML are implemented.
- Content: each spine document is parsed with libxml2 **HTML SAX**
  (`htmlCreatePushParserCtxt`, `HTML_PARSE_RECOVER | HTML_PARSE_NONET`),
  blocks: `p`, `h1–h6`, `blockquote`, `li`, `pre`, `hr`, `img`
  (placeholder). CSS is not executed: defaults only (italic/bold/headings/
  lists). `display:none` and footnotes may add extra text — an accepted limitation.
- ZIP safety: no extraction to disk, memory stream only; same limits as
  `fb2.zip`. `META-INF/encryption.xml` → “Book is protected (DRM)”.
- EPUB cache: v2 may cache the parsed book (`arena + Block[]`) in
  `books/<id>.bin` if measurements show that reparsing takes >0.5 s. In v1,
  only the page map is cached.

### 2.5 PDF — separate, not v1

Do not link PDF into `graphite-shell`: MuPDF is AGPL, and one link makes the
entire shell AGPL. Option: `apphost_launch("mupdf-x11 <file.pdf>")` — separate
process, its own 480×768 window below the status bar; Back closes it.

- `mupdf-libs` is already installed (4.5 MB); install `mupdf-x11` (95 KiB).
- 0.4–2 s per page, 20–40 MB RSS, no reflow/zoom; A4 at 480 px is hard to read.
  Scans have no OCR.
- In the shelf, PDF is shown with a `PDF` badge and “Open in mupdf” action.
  If `mupdf-x11` is missing, show the honest message “mupdf-x11 is not installed”.
- `pdftoppm` (poppler) is a fallback, but heavier and without text/search; do not use it.

### 2.6 Shared `Book` model

All formats are reduced to one stream of blocks; layout does not know the format.

```c
typedef struct {
    uint32_t off, len;        // offset/length in UTF-8 arena
    uint32_t first_run;       // index of first Run
    uint16_t nruns;
    uint16_t flags;           // P, H1..H3, CENTER, EPIGRAPH, VERSE, CITE,
                              // SUB, HR, IMG, PAGEBREAK
    uint8_t  level;           // chapter/list nesting
    int16_t  indent_px;       // extra indent (verse, quotes)
    uint32_t image_id;        // 0 = none (v2)
} Block;                      // 24 bytes

typedef struct {
    uint32_t off, len;        // within block text
    uint16_t flags;           // EMPH, STRONG, SUP, SUB, CODE
} Run;                        // 12 bytes

typedef struct {
    char     *arena;          // entire book text, UTF-8
    uint32_t  arena_len;
    Block    *blocks;
    Run      *runs;
    uint32_t  nblocks, nruns;
    uint32_t *char0;          // prefix sums of characters by block
    Chapter  *chapters;
    uint32_t  nchapters;
    Metadata  meta;           // title, author, series, lang, cover_id
    uint64_t  hash;           // book id
} Book;
```

Inline styles use separate `Run` records, not a flag for the whole paragraph:
`<emphasis>` in one sentence must not italicize the entire paragraph. Fiction
has few Run records (0–2 per paragraph); memory use is a few kilobytes.

### 2.7 Encodings and UTF-8

- UTF-8 decoder with a fast path for 1–2 bytes (Latin/Cyrillic); 3–4 bytes use
  the general path; invalid sequences → U+FFFD, never crash.
- For width and line breaking, iterate by codepoint, but `off` in `Block` is
  byte-based — convenient for storage in the page map.
- Shaping is unnecessary: Cyrillic/Latin use one codepoint per glyph.
  Ligatures, Arabic, Indic, and vertical text are unsupported.
- Kerning is disabled by default: gains for Cyrillic are small, and FT calls
  per token are expensive.

### 2.8 Limits and parsing errors

| Item | Limit / behavior |
|---|---|
| Text size | 32 MB absolute; >5 MB — “large book” warning |
| ZIP entry | 32 MB, ≤10,000 entries, ratio ≤100:1 |
| Cover base64 | 8 MB source, 4096×4096 |
| Corrupt XML | recover → emergency `<p>` extractor → “Open as text” |
| Empty book | “The book contains no text” |
| DRM | “Book is protected (DRM)” |
| Book too large | “Book is too large (N MB)”, avoid OOM |
| File disappeared | “Book unavailable”, path, “Retry”, “Shelf” |

---

## 3. Shell architecture

### 3.1 Why not a separate process

| Criterion | In shell (chosen) | Separate `lgreader` |
|---|---|---|
| Shell today | exists; screen appears immediately | needs second binary, window, focus |
| Memory | one Canvas, one font cache | +3–8 MB, second X client |
| Launch from shelf | 0 ms for process | +50–100 ms |
| Vol/Home keys | `ScreenView::key` receives them first | requires XGrabKey/arbitration |
| Progress/resume | shared `reader.json`, no IPC | IPC or duplicated state |
| Window | native 480×800 | `apphost` resizes to 480×768 |
| Parser crash | takes down shell | isolated |
| Debugging | `GRAPHITE_SCREEN=reader` + `preview.sh` | separate launch |

Isolation is the only downside. Address it through discipline: SAX instead of
DOM, limits, no `NOENT`/network, fuzz corpus, no exceptions across boundaries.
If that is insufficient, move parsing to a helper process that writes a compact
`.book`; this is cheaper than a separate UI process.

### 3.2 Layers and files

```
os/
  screens/reader.cpp          thin ScreenView: make_reader(), forwarding
  include/reader.h            Book, LayoutCfg, PageMap, parse API (no X11)
  include/reader_view.h       ReaderView: modes, input, rendering
  src/reader/
    book.cpp                  arena, Block/Run, chapters, metadata, UTF-8
    parse_txt.cpp
    parse_fb2.cpp
    parse_epub.cpp            v2
    layout.cpp                TextEngine (FT Serif), break_line, justify
    paginate.cpp              Paginator, PageMap, page cache
    store.cpp                 reader.json, index.json, atomic write
    shelf.cpp                 scan, metadata, sorting, covers
    reader_view.cpp           shelf/read/toc/bookmarks/settings modes
  tools/reader_probe.cpp      headless golden tests and benchmark (no X)
```

`screens/reader.cpp`:

```cpp
struct ReaderScreen : ScreenView {
    ReaderView v;
    const char *id() const override { return "reader"; }
    void draw(Gfx &g) override { v.draw(g); }
    bool tap(int x, int y) override { return v.tap(x, y); }
    bool drag(int x, int y, int dx, int dy) override { return v.drag(x, y, dx, dy); }
    bool key(int ksym, bool down) override { return v.key(ksym, down); }
    void tick(unsigned now) override { v.tick(now); }
    bool animated() const override { return v.animated(); }
    bool busy() const override { return v.busy(); }
    bool work(unsigned now) override { return v.work(now); }
    void on_show() override { v.on_show(); }
};
ScreenView *make_reader() { return new ReaderScreen(); }
```

`ReaderView` knows nothing about X11 or `App`: it can later be embedded in
another host (fb mode, debug binary) without rewriting the UI. This is the
shared `reader_view`.

### 3.3 Modes and internal stack

One `reader` screen instead of five registry entries:

```cpp
enum Mode { SHELF, READ, TOC, BOOKMARKS, SETTINGS };
std::vector<Mode> mode_stack;   // root is SHELF
```

- `tap`/`key` act on the top mode.
- Back: `READ/TOC/BOOKMARKS/SETTINGS` → `SHELF` (intercepted in `key`, returns
  `true`); `SHELF` → `false`, and the core calls `back()` to “Launcher”.
- Home: do not change navigation — the core always goes to “Launcher” (shell
  convention); in `key()`, save position on Home and return `false`. Long Home
  opens the shade, as elsewhere.

Why not separate `read.cpp`/`toc.cpp`: that would require adding ids to
`registry.cpp` and lose shared state (book, map, anchor) between screens; one
`ReaderView` is simpler and does not touch other code.

### 3.4 Avoid blocking the UI at 1 GHz

Currently `app.cpp` calls `tick()` only every **1000 ms** (the README says
100 ms — trust the code). This is unsuitable for laying out 500 pages:
200 ms of work once a second causes stutter. One small core hook is required
(owner: orchestrator, not reader agent):

```cpp
// graphite.h, ScreenView
virtual bool busy() const { return false; }
virtual bool work(unsigned now) { (void)now; return false; }

// app.cpp, run loop
if (top() && top()->busy() && now - last_work >= 20) {
    if (top()->work(now)) dirty = true;
    last_work = now;
}
...
tv.tv_usec = (top() && top()->busy()) ? 20000 : 100000;
```

Rules:

- `work()` runs for 8–12 ms and checks `clock_gettime` inside loops;
  processes blocks/chunks, not the “entire book”.
- `busy()` is true only during parse/paginate/scan/cover decode; at idle it is
  `false`, `select` sleeps 100 ms, and consumption matches the rest of the shell.
- `draw()` and `tap()` never read the SD card, parse, or decode images; they
  only render ready state.
- Opening a book: first show an “Opening…” frame, then process slices; the
  first page appears after 1–3 slices, and the full map catches up with progress.
- Increment `generation++` when book/mode/settings change: old `work` sees the
  mismatch and exits without corrupting new state.
- Threads are unnecessary in v1: one core, no locks, deterministic. If
  measurements show response time >50 ms, revisit a worker thread with an
  immutable snapshot.

### 3.5 Book opening sequence

1. `stat`, id (FNV-1a 64 of size + first/last 64 KB), metadata from
   `index.json` (if present).
2. Open `O_RDONLY`, create `Book`, start parser in slices.
3. Check `pages/<id>.pg`: if `engine_version`, `model_version`, `book_hash`,
   `layout_hash` match, load the map.
4. Map exists → find page by anchor (binary search) → draw. If no map, lay out
   from the beginning to the anchor (or end), show progress in footer, then
   show the page; the rest catches up in the background.
5. Save progress (debounce); decode cover in `work` if needed.
6. On leaving `READ` for `SHELF`, free `arena/Block/Run`, retain metadata, map,
   and anchor; font cache may be retained.

### 3.6 Memory and ownership

- At any time, **one** book is open. Opening another frees the previous one
  (except its metadata in the index).
- `Book` uses `std::vector`/`std::string` with reserved capacity; no references
  to temporary parser buffers.
- Glyph cache is shared for the book, key `(face, px, cp, flags)`; clear it
  when font/size changes.
- In-memory cover cache — LRU 12 (≈420 KB); on disk — all covers, subject to a limit.
- Page map stores only `PageStart[]` + chapters; lines are not stored.
- Text limit is 32 MB; exceeding it is an error, not OOM.

### 3.7 Build and linking

- `build.sh`: add `src/reader/*.cpp` to the loop and
  `-I/usr/include/libxml2 -lxml2 -lzip -lhyphen` to LD.
- `host-build.sh`: same for WSL; otherwise `preview.sh reader` will not build.
- After editing `graphite.h` (new virtual methods), do a full rebuild, as
  `os/README.md` warns.
- Binary: +150–250 KB code; runtime libraries — 0.3 MB; fonts already exist.
- For headless tests, `reader_probe` links `src/reader/*` without X11.

---

## 4. Pagination

This is the reader core. The algorithm must be pure, integer-based, and
deterministic: the same `Book + LayoutCfg` produces the same `PageMap`.

### 4.1 Screen metrics and grid

| Parameter | Value |
|---|---|
| Screen | 480×800 |
| Status bar | 32 px (drawn by core) |
| Reading area | x=0..480, y=32..800 |
| Text margins | 16 / **24** / 32 px (setting) |
| Top whitespace | 24 px (text starts at y=56) |
| Text area | x=margin, y=56, h=704 (through 760) |
| Footer | 40 px (760..800), 1 px separator at 760 |
| Body | DejaVu Serif, 16/18/**20**/22/24 px |
| Line spacing | 1.30 / **1.45** / 1.60 |
| Paragraph indent | 1 em (20 px at 20 px) |
| Alignment | justified, except final line |
| Hyphenation | libhyphen + `hyph_ru_RU.dic`, enabled |
| Lines per page | `floor(704 / lh)` |

`lh` is an integer: `lh = (size * lh100 + 50) / 100`, where `lh100` is
130/145/160. No `double` in layout.

### 4.2 Page capacity (DejaVu Serif, margin 24)

Line width is 432 px; average Cyrillic width measured from DejaVu Serif metrics
(fontTools, no hinting; FreeType will be ±2%):

| Size | lh | Lines | Avg. chars/line | Chars/page |
|---|---|---|---|---|
| 16 | 23 | 30 | 47.8 | ~1430 |
| 18 | 26 | 27 | 42.5 | ~1150 |
| **20** | **29** | **24** | **38.2** | **~917** |
| 22 | 32 | 22 | 34.8 | ~766 |
| 24 | 35 | 20 | 31.9 | ~638 |

At 16 px margins (448), capacity is ~4% higher; at 32 px (416), ~4% lower.
A 1 MB UTF-8 book (≈500,000 Cyrillic characters) at 20 px is **≈545 pages**;
at 16 px ≈350, at 24 px ≈785. This is what “500+ pages” means.

### 4.3 Line breaking

- Tokens: word, space, nonbreaking space (U+00A0/U+202F), soft hyphen
  (U+00AD), nonbreaking hyphen U+2011 (do not break). Style is a token
  property from `Run`.
- Width uses cached advances: `adv[cp]` in integer pixels. Kerning is disabled.
- Greedy composition: add words while they fit. If a word does not fit:
  1. try libhyphen: syllable positions, longest prefix that fits with a hyphen,
     ≥2 letters on both sides, no more than two consecutive lines with hyphenation;
  2. otherwise move the entire word to the next line;
  3. if the word is wider than the line (URL), split by codepoint without a hyphen.
- Justification: `extra = width − natural`; distribute across spaces
  (`extra/gaps`, remainder +1 to the first gaps). If `extra/gap > 0.6 em`,
  leave a ragged edge to avoid “rivers”.
- Hanging punctuation: do not start a line with `,.;:!?»`, do not end with
  `«`; em dash `—` may start a line; allow 2–4 px into the margin.
- Final paragraph line is left-aligned, not stretched.
- Headings: H1 always starts a new page and takes 2 slots; H2/H3 take 1 slot
  and require at least 2 lines after (otherwise move to next page).
- Do not leave a single line of a new paragraph at the page bottom (widow/orphan ≥2).
- Verse (`v`): each line is a separate block, not justified, indent 24.
- Quotes/epigraphs: 24 indent on left and right, italic, not justified.
- Blank line (`empty-line`) — 1 slot; `hr` — 1 px line in a slot.
- v1 illustration — 1 slot, `[illustration]`, 11 px dim, centered.

Pseudocode:

```
break_line(text, maxw, indent, can_hyphen):
    natural = indent + widths(words, spaces)
    while natural > maxw and words > 1:
        w = last word
        if can_hyphen and len(w) >= 6:
            p = best_hyphen_prefix(w, maxw - width(before))
            if p: emit(before + p + "-"); return
        drop last word; natural = width(remaining)
    if natural > maxw:
        hard_break_by_chars(maxw)
    if justify and not last_line:
        distribute(maxw - natural)
```

### 4.4 Filling a page

Cursor is `(block, byte_off, line_in_block)`. A page is filled with slots:

```
fill_page(book, cfg, cursor, slots):
    n = 0
    while n < slots and cursor.block < book.nblocks:
        b = book.blocks[cursor.block]
        if b is H1 and n > 0: break
        if b is H1: n += 2                       # heading + blank slot
        if b is H and slots - n < 2: break       # do not leave heading at bottom
        if b is P and cursor.line == 0 and slots - n < 2: break  # widow
        while cursor.off < b.len and n < slots:
            line = break_line(arena + b.off + cursor.off,
                              cfg.width - (cursor.line ? 0 : indent(b)),
                              cfg.hyphen and cursor.line > 0)
            emit(line, y = cfg.top + n*lh)
            cursor.off += line.consumed
            cursor.line++
            n++
        if cursor.off >= b.len:
            cursor.block++; cursor.off = 0; cursor.line = 0
    return cursor
```

Grid: all lines have the same height `lh`; line coordinates are not stored.
Redraw a page from `PageStart` in 1–3 ms.

### 4.5 Page map and `layout_hash`

```c
typedef struct {
    uint32_t block;
    uint32_t off;      // byte offset of line start in block
    uint16_t line;     // line number within block
    uint16_t chapter;  // chapter index
} PageStart;           // 12 bytes
```

500 pages — 6 KB; 5000 — 60 KB; 20,000 — 240 KB. No need to store coordinates
for every line.

`layout_hash` = FNV-1a 64 of:

- `engine_version` (changes when layout/parser rules change);
- `model_version`;
- font identifier (size+mtime of file or hash of first/last 64 KB);
- face (serif/sans), px, `lh100`, margin, width, height;
- justify, hyphen, dictionary version;
- text normalization version (spaces/dashes/quotes).

`book_hash` is stored separately in the cache header. Changing any parameter
rebuilds the cache; progress remains because it is stored as an anchor, not a
page number.

Page cache `pages/<id>.pg`:

```
Header: magic "GRPG0001", engine_version, model_version,
        book_hash u64, layout_hash u64,
        n_pages u32, n_chapters u32, total_chars u32, crc32 u32
PageStart[n_pages]
ChapterRec[n_chapters]   // title_off/len, first_page, level
```

Write atomically (tmp + fsync + rename); a corrupt/mismatched file is deleted
and rebuilt. CRC32 is calculated over the arrays.

### 4.6 Anchor-based progress and resume

Progress is **not a page number**, but `(block, off)` + percentage by characters:

```
percent = (char0[block] + utf8_len(arena + block.off, off)) / total_chars
```

- `char0[]` — prefix sums of characters by block (4 B/block).
- After changing font/size/margins, the page number changes; the anchor does not.
- Go to page: `lower_bound` over `PageStart` (monotonic by `(block, off)`).
- If `model_version` (parser) changed, remap anchor by percentage: find block
  where `char0 >= percent * total_chars`, `off = 0`.
- Autosave: debounce 1.5–2 s after page turns, when opening HUD/TOC/settings,
  on Back/Home, and on book change. At most one page of progress is lost only
  on `kill -9`/hard power-off.
- Bookmark — same anchor + 80-character excerpt + date.

### 4.7 Large books and slices

- `Paginator::step(budget_us)` processes blocks until the 8–12 ms budget is
  reached; cursor and partial map live in the object.
- First page is calculated immediately after the first step (<20 ms); the full
  map catches up in `work()` with “Layout 42%” progress in the footer.
- While the map is incomplete: page number “12 / …”, percentage from anchor;
  TOC jumps to not-yet-laid-out chapters are queued (“Layout is still running”).
- Without a cache and with a saved anchor: layout from the start to the anchor,
  then show the page; the rest runs in the background. With a cache — immediate.
- Page map limit: 50,000 pages (600 KB) — protection against pathological
  files; beyond that show “Book is too large to lay out”.
- 32 MB text, ~100,000 blocks, 400 KB `char0`, 240 KB map — all within memory;
  the bottleneck is layout time, not the map.

### 4.8 Determinism and golden tests

`fill_page` is a pure function of `(Book, LayoutCfg, PageStart)`. Tests:

- repeated pass produces identical `PageStart` values;
- no line is wider than `width`;
- concatenating all lines reproduces source text, except for spaces/wrapping;
- pages neither overlap nor lose characters;
- anchor from the map lands on the same page as linear search;
- changing `layout_hash` rebuilds the map while preserving the anchor;
- 100 chapters, empty paragraphs, long word, URL, NBSP, 4-byte UTF-8.

### 4.9 Defer

| Item | When | Why |
|---|---|---|
| Inline illustrations | v2 | break the grid, JPEG 100–250 ms, +0.65 MB per image |
| Exact image layout/wrapping | v2+ | separate layout engine |
| Complex FB2 formatting (tables, footnotes, nested styles; no CSS in FB2) | v2 | requires grid/mono and anchors; currently rendered as text via ` · ` |
| Bottom-sheet footnotes | v2 | anchors and return navigation required |
| EPUB CSS, `display:none` | not planned | no CSS engine; honest defaults |
| Book search | v2 | separate arena scan and highlighting |
| PDF reflow/zoom | not planned | MuPDF is separate, no reflow |

---

## 5. UI

### 5.1 Principles and tokens

- Geometry — Metro: 480×800, status 32, margins 24, zones ≥44, text ≥11,
  1 px lines, square corners, one accent, no shadows/gradients/animations.
- Shelf, TOC, bookmarks, settings — **shell graphite** (`theme.*`), like
  the other screens.
- Book surface — its own reading theme (Day/Night/Paper); it applies
  only in area y=32..800. The status bar remains graphite: the core draws
  it after the screen and knows nothing about the book theme. This is a
  deliberate “device frame”; if it gets in the way in photos, later add
  `ScreenView::owns_status()` and draw the status bar in theme colors.
- Body — **DejaVu Serif** (deliberate exception: the book should look
  like a book). All UI — Medeo/DejaVu Sans.
- Page turns — instant frame change, no animation.

Reading themes:

| Theme | Background | Text | Dim | Line | Accent |
|---|---|---|---|---|---|
| Day | `#F7F4EC` | `#1B1D1F` | `#6B7075` | `#D8D0C0` | `#177E85` |
| Night | `#0E1012` | `#C3C8CF` | `#7C8591` | `#262B30` | `#3FC8D0` |
| Paper | `#EDE4D0` | `#2A2620` | `#6F6759` | `#CFC3AA` | `#1E7F86` |

Paper: 16×16 tile, ±1 LSB, pre-rendered 480×768 background (737 KB) +
`memcpy` per frame; do not generate noise every frame. Dithering — background
only, ordered 4×4, ±0.5 LSB; text — regular alpha blend. Night is not pure
white, to reduce halos.

### 5.2 Shelf

```
y=0..32    status (core)
y=32..96   header: “Library” 24 light (24,42)
            sub “12 books · 31.2 MB” 11 dim (24,72)
            right: 44×44 — sort (v1.1)
y=104..208 “Continue” card 432×104 (x=24)
            cover 56×76 (36,118)
            “CONTINUE” 11 accent +2 (108,116)
            title 17 bold, 2 lines (108,132)
            author 12 dim (108,176)
            progress 2 px (108..420, 194), “34%” 11 right
y=216..244 “ALL BOOKS” section
y=244..772 list, rows 88 (6 rows)
            cover 50×68 (24, y+10)
            title 16 (86, y+10), 1 line, “…”
            author 12 dim (86, y+34)
            progress 2 px (86..400, y+60), “34%” 11 num (456, y+56)
            full-width sep divider (y+87)
```

- 6 rows in visible area; drag 1:1, no inertia.
- Tap a row to open; long press (650 ms) — book card:
  title, author, format, size, path, “Open”, “Remove from shelf”,
  “Forget progress”. The file is not deleted.
- “Continue” card — last book; if its file is missing, show
  “Book unavailable” and “Remove”.
- No progress — card is hidden, list starts at 104 (7 rows).
- Empty: book icon 48 dim, “No books” 15, `/media/sd/Books` 12 dim,
  “Scan” button 200×44.
- No SD: “SD card unavailable” + “Retry”.
- Scan: 2 px accent line under header + “Scanning 12 of 84” 12 dim;
  scanning runs in `work()`, UI is not blocked.
- Cover: 112×160 RGB565 in cache, scaled down on screen; no cover —
  surf rectangle + book icon 22 dim (no small text).
- Cover grid 3×3 (card 136×230, cover 136×190, title 12,
  author 11, progress 2 px, scroll) — v1.1, after the list.

### 5.3 Reading

```
y=0..32    status (core, graphite)
y=32..800  reading theme surface
y=56..760  text: x=24, w=432, h=704; 20 px Serif / 29 px = 24 lines
y=760      sep 1 px
y=760..800 footer: “148 / 437” 11 (24, 770)
            “34%” 11 right (456, 770)
            progress 2 px (24..456, 794)
```

- Page number is always visible; while the map is not ready — “12 / …”.
- HUD on center tap (120<x<360): top panel 32..76 (44 px):
  back 44×44 (4..48), title 15 bold (60, 38), chapter 11 dim (60, 56),
  TOC 44×44 (384..428), bookmark 44×44 (430..474).
- Bottom panel 744..800 (56 px), 4 buttons of 120:
  “Contents”, “Bookmark”, “View” (Aa), “Theme”; icon 22 + label 11.
- Panels are drawn over text and do not reflow the page. A repeated
  center tap or page turn closes the HUD.
- Tap zones: x<120 — back; x>360 — forward; 120..360 — HUD.
- Swipe: |dx|≥40 and |dx|>1.5|dy| — turn page on release; one
  gesture — one transition. Start the swipe over text (x≥24), to avoid
  conflict with global edge-back (x<20).
- Short Vol−/Vol+ — page; ≥700 ms — chapter. In reading mode the reader
  intercepts Vol in `key()` and runs its own 700 ms timer (the core does
  not track long Vol); volume is available only from the shade. This must
  be recorded in the key ownership matrix.
- Back — shelf; Home — “Launcher” (core), position already saved.
- “Bookmark added/removed” toast occupies the footer for 1.5 s; text does
  not shift.
- End of book: “End of book” 13 dim centered + “To shelf” 44.

### 5.4 Contents

- Header 64: back, “Contents” 24, sub “42 chapters” 11.
- Rows 56 from 96 to 768 (12 rows): title 15, `fit` to 360,
  indent 16×min(level,2), page number 12 num right (456).
- Current chapter — `sel` + 3 px accent marker; tap — navigate to anchor.
- Before map is ready — “—” and “Layout is still running” toast (or queue).
- Empty: “No table of contents found” 13 dim + “To beginning” 44.

### 5.5 Bookmarks

- Header 64: back, “Bookmarks” 24, sub “5”.
- Rows 72 from 96: chapter 13 bold, excerpt 12 dim (1 line), page 11
  num right, “×” 44×44; 9 rows, then drag.
- Tap — navigate; “×” — delete immediately + toast; no confirmation.
- Empty: “No bookmarks” 13 dim, hint “The header icon adds a bookmark”
  12 dim.
- Bookmark is set at the first visible line; tapping again on the same
  page removes it. Limit 20 per book.

### 5.6 Reading settings

One screen, no scrolling (624 px of 680 available):

| Block | y | Elements |
|---|---|---|
| FONT | 96 | 2 segments 208×44: Serif / Sans |
| SIZE | 176 | 5 segments 80×44: 16/18/20/22/24 |
| LINE SPACING | 256 | 3×136×44: 1.30 / 1.45 / 1.60 |
| MARGINS | 336 | 3×136×44: 16 / 24 / 32 |
| HYPHENATION | 416 | row 56 + toggle 44×24, sub “Russian, syllabic” |
| JUSTIFICATION | 472 | row 56 + “full width” toggle |
| THEME | 536 | 3 rows 52: Day / Night / Paper + 24 swatch |
| Footer | 781 | “Changing the font recalculates pages” 11 dim |

- Segments: height 44, gap 8, active — accent/onacc.
- Changing font/size/lh/margins/hyphenation/justification: pause 300 ms →
  recalculate from current anchor, “Recalculating… 42%” overlay, old page
  remains visible. Theme — immediate, no recalculation.
- Settings are global (same for all books), stored in `reader.json`.
- Do not bring brightness here: it is already in the shade/`set-display`.

### 5.7 Keys and gestures

| Input | Action |
|---|---|
| Vol− / Vol+ short | back / forward one page |
| Vol− / Vol+ ≥0.7 s | previous / next chapter |
| Back | from reading/TOC/bookmarks/settings — to shelf; from shelf — “Launcher” |
| Home | “Launcher” (position saved) |
| Long Home | shade (core) |
| Tap x<120 / x>360 | page back / forward |
| Tap 120..360 | HUD |
| Swipe ←/→ over text | page |
| Swipe top to bottom (y<40) | shade (core) |

### 5.8 Errors and empty states

| Situation | Screen |
|---|---|
| No books | “No books” + `/media/sd/Books` + “Scan” |
| No SD | “SD card unavailable” + “Retry” |
| File disappeared | “Book unavailable”, path, “Retry”, “Shelf” |
| Invalid FB2/EPUB | “Could not parse”, “Open as text” (FB2), “Shelf” |
| DRM EPUB | “Book is protected (DRM)”, “Shelf” |
| Empty book | “The book contains no text”, “Shelf” |
| Too large | “Book is too large (N MB)” |
| Layout | “Layout 42%” in footer, current page available |
| `mupdf-x11` missing for PDF | “mupdf-x11 is not installed”, “Shelf” |

### 5.9 Shell integration

- `home.cpp`: add `{"reader","book","Books","12 books · 34%"}` to
  `kApps` (one line, owner — orchestrator) and optionally pin by default.
  Metadata comes from `store`: book count and last-book progress.
- `files.cpp`: for `.txt/.fb2/.fb2.zip/.epub` add “Open in reader” in
  properties/long tap: `app.open_path = path; app.open("reader");`.
  `ReaderView::on_show()` takes `open_path` if it has a supported
  extension, otherwise ignores it; `notes`/`files` already use this field
  — the reader must clear it after processing.
- Shelf scanning is self-contained: `/media/sd/Books`; files outside the
  directory are opened through `open_path`.
- PDF from files — “Open in mupdf” only (external process), not the
  reader screen.

---

## 6. Storage

### 6.1 Paths

```
/media/sd/Books/                         # books, O_RDONLY only
/media/sd/Books/Inbox/                   # optional
<user-home>/.config/graphite/reader.json # settings, progress, bookmarks
<user-home>/.cache/graphite/reader/
    index.json                           # catalog (rebuildable)
    covers/<id>.565                      # 112×160 RGB565
    pages/<id>.pg                        # page map
```

- The shell runs as root, but config and cache are under `<user-home>/...`
  (like `pins` in `apps.cpp`), not `/root`. If `/home/user` does not exist,
  fall back to `$HOME`. Do not hardcode the path in three places: use one
  `store_home()`.
- Cache is on ext4 rootfs, not vfat: atomic writes and the map do not
  depend on the removable SD card.
- `/media/sd/Books` is created by the “Create” button or on the first scan,
  if the SD is mounted rw; otherwise show the empty state, do not crash.
- Open all books with `O_RDONLY`; write nothing alongside a book.

### 6.2 `reader.json`

```json
{
  "v": 1,
  "settings": {
    "font": "serif", "size": 20, "lh": 145, "margin": 24,
    "hyphen": true, "justify": true, "theme": "paper",
    "shelf_view": "list"
  },
  "last_book": "9f3c1a02d4e5b607",
  "progress": {
    "9f3c1a02d4e5b607": {
      "block": 412, "off": 1834, "percent": 0.341,
      "page": 148, "pages": 437, "ts": 1790000000
    }
  },
  "bookmarks": {
    "9f3c1a02d4e5b607": [
      {"block": 120, "off": 44, "page": 41,
       "excerpt": "…he walked into the street and…", "ts": 1790000000}
    ]
  }
}
```

- Limits: latest 200 books in `progress`, 20 bookmarks per book and ≤200
  total (oldest evicted), file ≤128 KB.
- Write: `reader.json.tmp` → `fflush` → `fsync` → `rename` → `fsync`
  directory; `reader.json.bak` alongside. Invalid JSON → `.bak` → defaults.
- JSON — own minimal parser/serializer (or vendored public-domain
  `json.h`); no external dependencies, UTF-8 strings,
  escaping, numbers — integers/`double` only for `percent`.
- Debounce 1.5–2 s; write only on actual change.
- Progress is authoritative in `(block, off)`; `page/pages` are for
  display only and may be stale after changing the font.

### 6.3 `index.json`

```json
{
  "v": 1,
  "roots": ["/media/sd/Books"],
  "books": [
    {
      "id": "9f3c1a02d4e5b607",
      "path": "/media/sd/Books/<book-title>.fb2",
      "fmt": "fb2", "model": 1,
      "size": 1203456, "mtime": 1789000000,
      "title": "<book-title>",
      "author": "<author>",
      "series": "", "lang": "ru",
      "pages": 437, "cover": "9f3c1a02d4e5b607.565",
      "hidden": false, "last": 1790000000
    }
  ]
}
```

- `id` = FNV-1a 64 of file size + first 64 KB + last 64 KB.
  Renaming does not change the id; a copy of the book retains progress.
- At startup: load index, draw shelf, then `stat` all files in `work()`;
  new/changed — metadata and cover; missing — mark, do not delete progress.
- Scan: recurse to depth 4, no symlink loops, skip hidden directories,
  ≤5000 files, extensions `.txt/.fb2/.zip/.epub/.pdf`.
- Sort: by `last` (“Continue” first), then by title with casefold for
  Cyrillic/Latin; do not use `strcmp`.
- If id changed (file edited), migrate progress by path
  if title/size are close; otherwise start new progress.

### 6.4 Caches

| Cache | Size | Policy |
|---|---|---|
| Covers | 35,848 B × books | LRU 32 MB; decode visible ones, 1 per `work` |
| Page maps | 12 B × pages + 64 B | ≤8 MB and ≤300 files; oldest by mtime |
| Index | tens to hundreds of KB | rebuildable |
| Covers in RAM | 12 × 35 KB = 420 KB | LRU |
| Book in RAM | 3–5 MB per 1 MB of text | one, freed on exit |

Total disk cache ≤40 MB. Clean at startup and after scan; invalid file
is deleted and rebuilt. Cover format: 8-byte header
(`magic`, `w`, `h`, `format`) + 112×160×2 RGB565. `index.json` is written
atomically as well; invalid index — rebuild by scanning.

### 6.5 vfat and SD

- Mount with `utf8`/`iocharset`; check Cyrillic names on a real card
  (the `Books` directory does not exist yet).
- `errors=remount-ro`: on I/O error the card becomes read-only — show
  “SD unavailable”, do not hang.
- `mtime` on vfat has 2 s resolution; invalidate by `size+mtime`, and
  hash contents on change.
- Progress/bookmarks — rootfs only; removing the SD does not lose position.
- Do not delete or rename books from the reader; at most “Remove from
  shelf” (flag in index).

---

## 7. Performance

### 7.1 Budgets (ARMv7, 1 GHz; estimate, confirm in S0)

| Operation | Estimate | Target |
|---|---|---|
| Parse TXT 1 MB + CP1251 | 60–150 ms | ≤200 ms |
| Parse FB2 1 MB (SAX) | 100–250 ms | ≤400 ms |
| Parse EPUB 1 MB (v2) | 150–400 ms | ≤600 ms |
| Layout 500 pages | 1–2.5 s | ≤4 s |
| Layout 1000 pages | 2–5 s | ≤8 s |
| First TXT page | 0.1–0.25 s | ≤0.5 s |
| First FB2 page | 0.15–0.4 s | ≤1 s |
| Reopen (map cache) | 0.15–0.4 s | ≤0.5 s |
| Render page (24 lines) | 1–4 ms | ≤5 ms |
| `XPutImage` 480×800×16 | 15–40 ms | ≤60 ms |
| `fbpan` latency | 0–100 ms | ≤100 ms |
| **Page turn, total** | **30–150 ms** | **≤150 ms** |
| Shelf, 200 books (cached) | 0.2–0.6 s | ≤1 s |
| Cold shelf (metadata) | 2–5 s in `work()` | — |
| Decode cover 600×900 JPEG | 100–250 ms | ≤400 ms, 1 per tick |
| PDF page (v3) | 0.4–2 s | ≤2.5 s |

Why page turning is fast: pages are not rebuilt from all blocks, but
drawn from `PageStart` (24 lines); glyphs are cached; theme background —
`memcpy`; one `XPutImage` per frame.

### 7.2 Memory

| Item (1 MB book) | Size |
|---|---|
| UTF-8 arena | 1.0 MB |
| Blocks (6000×24) | 144 KB |
| Run (2000×12) | 24 KB |
| `char0` (6000×4) | 24 KB |
| Chapters/metadata | ~4 KB |
| Page map (545×12) | 6.5 KB |
| Glyph cache (~300) | 100–200 KB |
| DejaVu Serif Regular | ~0.4 MB (RSS on demand) |
| Theme surface (480×768×2) | 737 KB |
| Covers in RAM (12) | 420 KB |
| **Added to shell** | **~3–4 MB** |
| 5 MB book | ~10–15 MB |
| 32 MB book | ~40–60 MB (near limit, warn) |

Base shell (Xlib+FreeType+Canvas+backgrounds) already uses 15–25 MB;
`free` headroom 154 MB + 193 MB cache. Better not read while browser is open.

### 7.3 What and how to measure

1. `clock_gettime(CLOCK_MONOTONIC)` around `parse`, `Paginator::step`,
   `draw_page`, `XPutImage`; log only with `GRAPHITE_READER_DEBUG=1` to
   `/tmp/reader.log`.
2. `XPutImage` full frame 20 times: mean/maximum; optionally XShm —
   is the gain >10 ms.
3. Layout 500-page TXT/FB2; first page; reopen with and without cache.
4. `VmRSS` before/after opening, after 100 page turns, after closing
   book (check for arena leak/retention).
5. Cover decode time and one frame while scrolling the shelf.
6. Tap → frame (log timing): target ≤150 ms, ideally ≤100 ms.
7. Read for 30–60 minutes: RSS trend, `dmesg | grep lcd_`, `/var/log/Xorg.0.log`,
   heat/battery; compare with idle.
8. 3 themes: `xwd` → PNG screenshots, inspect visually (contrast, paper
   banding).
9. Change font in a 500-page book: anchor ±1 page.

### 7.4 Defer

- Illustrations in text, text wrapping around images, full-screen images.
- Complex FB2 (grid tables, nested footnotes, aligned verse).
- EPUB CSS, `display:none`, exact `#frag` anchors.
- Book search, TTS, synchronization, OPDS.
- PDF reflow/zoom/text selection.
- Page-turn animations (at 1 GHz and `fbpan` 10 Hz — harmful only).

---

## 8. Phases

| Phase | Days | Scope | DoD |
|---|---|---|---|
| **S0. Spike** | 0.5–1 | X+FreeType: `GRAPHITE_SCREEN=reader`, temporary screen with 20 KB Russian text, DejaVu Serif 20/29, 24 lines, 3 themes, 20 page turns, measurements | page turn ≤150 ms, added RSS ≤8 MB, screenshots readable, status/touch/rotation correct |
| **S1. Core** | 3–4 | `Book`, TXT, TextEngine, layout, `Paginator`, map, `layout_hash`, anchor, `work()` hook, golden tests | 1 MB TXT → 500+ pages deterministically; cache; reopen ≤0.5 s |
| **S2. Reading screen** | 2–3 | reading, HUD, footer, tap zones, swipes, Vol, themes, settings, TOC, bookmarks, autosave, errors | full cycle on phone; font change preserves anchor; 3 themes |
| **S3. Shelf** | 2 | scan `/media/sd/Books`, `index.json`, covers, “Continue”, empty/error states, book card | 200 books ≤1 s from cache; cold scan in background; resume |
| **S4. FB2** | 2–3 | SAX push, metadata, TOC, base64 cover, `fb2.zip`, recovery | 6 real FB2 files open, covers/TOC work, invalid file does not crash |
| **v1** | **10–13** | S0–S4 + acceptance | 10 books, criteria §9.4 |
| **v2. EPUB** | 3–5 | libzip, OPF/spine/NCX/nav, HTML SAX, DRM, cover, footnotes/search later | 4 EPUBs (2/3, no cover, malformed XHTML) |
| **PDF** | 1–2 | `apk add mupdf-x11`, PDF badge, `apphost_launch`, Back | PDF opens in separate window; shell is not AGPL |
| **S5. Integration** | 1–2 | “Books” tile on home, “Open in reader” in files, QA | launch from home/files, shade/lock, unified soak |

v1 = S0–S4. Do not start EPUB until TXT+FB2 is stable: layout and cache
must be proven on a simple format.

---

## 9. Test plan

### 9.1 Corpus of 10 real books

| # | Book | What it tests |
|---|---|---|
| 1 | FB2 UTF-8, cover, nested chapters | metadata, TOC, cover |
| 2 | FB2 CP1251, no cover | encoding, placeholder |
| 3 | FB2 with verse, epigraphs, footnotes | blocks/inline, notes skipped |
| 4 | `fb2.zip` (one book) | libzip, entry selection |
| 5 | TXT UTF-8, chapters | chapter heuristic |
| 6 | TXT CP1251, CRLF | detection/normalization |
| 7 | TXT KOI8-R or UTF-16 | fallback encodings |
| 8 | Large FB2 ≥2 MB / 1000+ pages | slices, memory, progress |
| 9 | Invalid/truncated FB2 | recovery, “Open as text” |
| 10 | EPUB2 + EPUB3 (v2), one with DRM | OPF/spine/NCX/nav, DRM screen |

Additionally: empty file, 32 MB text, URL word longer than a line, NBSP,
dashes/quotes, 4-byte UTF-8, file without permissions, SD removed.

### 9.2 Unit/headless

- Encodings: BOM, strict UTF-8, CP1251/KOI8/CP866, UTF-16, garbage,
  invalid UTF-8, empty file.
- Hyphenation: 30 Russian words with known syllables; <2 letters
  prohibited; no more than 2 in a row; dictionary missing → hyphenation
  off, do not guess.
- Lines: long word, URL, dashes, quotes, NBSP, mixed
  Latin/Cyrillic, 4-byte UTF-8.
- Pagination: determinism, width ≤ margins, concatenation = text, anchor ↔ page,
  empty paragraphs, 100 chapters, heading at bottom of page.
- Cache: invalid file, changed `layout_hash`, `engine_version`, hash mismatch.
- JSON: interrupted write, invalid file, restore from `.bak`, limits.
- `fb2.zip`/EPUB: zip bomb, 10,001 entries, entry >32 MB, path traversal
  (do not write to disk), DRM.

### 9.3 On phone

- Open 10 books: time to first page, full layout, RSS.
- 100 page turns: mean/maximum, RSS leak, no D-state.
- 60 minutes reading: X stability, heat, battery, `dmesg`.
- Tap zones, swipes, Vol±, long Vol, Back/Home, lock/unlock, shade.
- Remove SD while reading: error, position saved.
- Nearly fill rootfs: cache does not grow without limit.
- 3 themes: photos in sunlight and darkness; paper without banding.
- Change font in a 500-page book: anchor ±1 page.
- `kill -9` and reboot: position within ≤1 page.

### 9.4 v1 acceptance

- [ ] FB2 1 MB: first page ≤1 s, full layout ≤4 s.
- [ ] Reopen ≤0.5 s.
- [ ] Page turn ≤150 ms (subjectively “instant”).
- [ ] 500 pages: page numbers correct, TOC goes to chapter.
- [ ] Position survives `kill -9`/reboot (≤1 page).
- [ ] Added RSS ≤5 MB per 1 MB book; no leak after 100 page turns.
- [ ] Shelf with 200 books ≤1 s from cache; cold scan does not block UI.
- [ ] Invalid files, empty book, no SD — no crashes.
- [ ] Metro check: 44 px, 11 px, 1 px lines, one accent, no animations.
- [ ] Vol turns pages while reading, volume only from shade.

---

## 10. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| X/fb hangs during a long session | medium | blocker | #23 + 300 s passed; 60-min soak; autosave each page; `session-shell` fallback in player |
| libxml2/our parser crashes inside shell process | medium | shell crash | SAX+limits, no NOENT/NONET, fuzz corpus; parsing helper process if needed |
| Memory with large book | medium | OOM | 32 MB limit, one book, no images, LRU, free arena |
| Layout blocks UI | high | annoyance | `work()` 8–12 ms, 20 ms select, progress, cancel by generation |
| Bad layout (hyphenation/justification) | medium | quality | golden tests, determinism, hyphenation toggle |
| No hyphenation dictionary/package | fact | quality | vendor `hyph_ru_RU.dic`; without it hyphenation off, do not guess |
| libzip/libhyphen not installed | fact | build/runtime | `apk add libzip` + hyphenation package (`hyphen`/`libhyphen`); dev packages for build only |
| EPUB without CSS looks different | high | expectations | honest defaults, tests, do not promise CSS |
| PDF: AGPL/CPU/memory | high | product | `mupdf-x11` separately only, not v1, do not link |
| Vol conflict with player | medium | duplicate actions | reader intercepts Vol while reading; volume from shade |
| Cover decode on UI | high | stutter | 1 decode per `work`, cache, placeholder |
| vfat: names/mtime/read-only | medium | access | UTF-8, `size+mtime`, errors without crash |
| 16-bit and text quality | medium | eye strain | 3 themes, size/line spacing, photo test |
| Font missing glyph | low | “square” | DejaVu Sans fallback, U+FFFD, log |
| Single process = no isolation | accepted | shell | parser discipline; helper as escape hatch |

---

## 11. Three conclusions

1. **A separate `lgreader` is no longer needed.** The shell already
   provides a window, Canvas, FreeType, themes, screen stack and keys. The
   reader is `reader.cpp` + `ReaderView`, while the engine (`Book`, parsers,
   layout, cache) does not depend on X11 and can be embedded in any other
   host. This means less code, memory and latency than a second process.
2. **v1 = TXT + FB2(+zip), EPUB second, PDF separate.** TXT and FB2 cover
   the Russian-language library; EPUB is needed, but must not delay the
   core; PDF (AGPL, 0.4–2 s/page, 20–40 MB) — external `mupdf-x11` only,
   not linked and not v1.
3. **Speed and quality come from our layout, not the format.** Fixed
   line grid, integer metrics, deterministic pagination, page map,
   `layout_hash` and anchor-based progress provide fast startup, correct
   numbering and position retention when changing fonts at the same time.
   Cooperative 8–12 ms slices are mandatory, otherwise 1 GHz will turn
   layout into stutter.

## What to do first

**S0 spike (0.5–1 day), not parsers.** Build a temporary `reader` screen
(`GRAPHITE_SCREEN=reader` / `preview.sh reader`) with 20 KB of Russian text,
DejaVu Serif 20 px / 29 px, 24 lines, 3 themes, 20 page turns. Measure
`XPutImage`, `fbpan` latency, page rendering, RSS and AA quality; check the
status bar, tap zones and that rotation is not applied twice.

If page turning is ≤150 ms, go straight to S1: `busy()/work()` hook in the
core, TXT, layout, `Paginator`, map and golden tests. FB2 only after TXT
proves the layout. In parallel, collect 10 real books (§9.1). EPUB,
PDF and images no earlier than v1.

## Placeholders

- `<user-home>` — personal home directory, replaced with a placeholder.
- Book title and author in the index example replaced with `<book-title>` and `<author>`.
