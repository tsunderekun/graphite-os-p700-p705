# APPS-RESEARCH - built-in graphite shell applications: audit, market, implementation

> Date: 24.11.2026. The device was not changed: no code was edited, and nothing was run or installed on the phone. Packages and sizes are from
> documented project audits (`READER-PLAN.md`, `VIDEO-PLAN.md`,
> `PLAYER-FEATURES.md`) and the Alpine edge/armv7 index as of the document date.
>
> Related documents: `PLAYER-FEATURES.md` (player, 845 lines—not duplicated here,
> only reduced to P0), `READER-PLAN.md`, `VIDEO-PLAN.md`,
> `APP-INTEGRATION-PLAN.md`, `SETTINGS-SPEC.md`, `notes/shell/SCREENS.md`,
> `SHELL-TODO.md`, `METRO-RESEARCH.md`.

---

## 0. Brief conclusion

1. **All seven apps are currently “viewers,” not tools.**
   The player shows the current track, but does not have a library or covers; notes
    they can only append text; Files cannot copy or delete anything; the browser is
    a drawn page; there is no reader or video app at all.
    Users are frustrated not by the lack of features, but because each app lacks
    its final, essential step.
2. **Three low-cost changes deliver 80% of the product feel:** `idle` + covers in
    the player (S/M), write operations in Files (L), and cursor + notes list (L).
    These are not “new apps”; they complete three existing ones.
3. **Do not write engines.** Terminal: `xterm` (295 KiB, MIT); browser: `dillo`
    (322 KiB, GPL-3, with `dilloc` IPC); PDF: `mupdf-x11` as a separate process
    or `pdftoppm` (do not link AGPL into the shell). Build the shell, keyboard,
    layout, and integration ourselves.
4. **Implement the reader in the shell process, not as a separate `lgreader`.**
   `READER-PLAN.md` called for “a separate process” because there was no shell then.
   There is a shell: Canvas, FreeType, keyboard, themes, back-stack - everything is in place.
   A separate process is needed only for PDF (AGPL/fall) and video (fb0).
5. **Video is not a release feature.** The blocker is not the codec but the display panel:
    24–30 `FBIOPAN_DISPLAY` calls/s have already caused `lcd_off/lcd_on` and a reboot.
    Do not write a player until a 10-minute pan test passes. A hardware decoder (QDSP5)
    exists, but there is no access path to it; that is a separate 2–6 week project.
6. **Memory is the main constraint, not CPU.** There are 400 MB, of which Xorg uses
    ~20, the shell ~10, and MPD ~12. Any GTK browser uses 30–50 MB; `pcmanfm`
    uses 40–97 MB. Rule: at most one heavy external app; everything else should
    use our own Xlib screens.
7. **Resistive touch, ~200 ms polling, no multitouch.** No row swipes, pinches,
    or drag-reorder. Use only tap, drag-scroll, 650 ms long-press, large buttons,
    and bottom action sheets.
8. **P0 consists of 4–7 work packages, not a feature list.** The player has the
    most packages (MPD layer, covers, library, queue actions, resume, states); the
    terminal and browser need only one or two each. Everything else (search,
    playlists, EPUB, archives, bookmarks) is P1, after the basic workflow is no
    longer frustrating.

---

## 1. What exactly was checked

| Source | Findings used |
|---|---|
| `os/screens/player.cpp`, `queue.cpp`, `info.cpp`, `src/mpd.cpp` | actual behavior of the player and MPD client |
| `os/screens/terminal.cpp` | 5 commands, 65 columns, no PTY or scrolling |
| `os/screens/notes.cpp` | append-only editor, picker, autosave |
| `os/screens/files.cpp` | read-only browser, limit of 500 entries |
| `os/screens/browser.cpp` | fake offline page |
| `os/screens/stub.cpp`, `screens.h` | `reader` and `video` are stubs |
| `os/src/apphost.cpp`, `osk.cpp`, `app.cpp` | Stage 1: one external window 480×768, XTest keyboard 480×288 |
|`notes/*`| previously filmed facts about MPD, hardware, packages, X |
| Alpine pkgs (edge/armv7) | package sizes and licenses |
| Websearch | market expectations for 7 application classes |

The phone was **not queried in this session** (no access to the host shell tools).
Therefore, “on the phone” below means “observed in previous project audits”, not
“checked now”; everything else comes from code and package-index inspection.

---

## 2. Audit of the current state

### 2.1. Player (`player`, `queue`, `info`)

**What actually works**

- One TCP socket to MPD, polling `status` + `currentsong` every 1000 ms,
  backoff 1→2→5→10 s, connect timeout 300 ms, recv 500 ms.
- “Now”: 384×384 cover placeholder (48,100), title/artist/album,
  shuffle, repeat off/all/one (via `repeat` + `single`), `VOL N%`, seek bar
  (tap in zone 604..652), 3×96 px transport controls, seek recalculated locally.
- “Queue”: 56 px rows, current track by `songid` (fallback to `pos`), tap = `play`, “CLEAR”, drag-scroll, opens on the current track.
- “Info”: tags, format, path.
- Keys Vol±, play/pause/next/prev; software volume via MPD.
- The Live tile on Start is updated once per second, play/pause works.

**What is missing before it feels usable**

| # | Hole | Why does it hurt |
|---|---|---|
| 1 | No covers at all | An empty 384×384 square with a note makes the main screen look broken. `readpicture` works on the phone (300 KB in 141 ms); it simply is not called. |
| 2 | No library | You can't "play an album." The only entrance is a queue, and there is nothing to fill it with from the UI |
| 3 | No track/queue actions | Cannot “play next”, “add to queue”, delete, or save a playlist |
| 4 | Poll instead`idle`| The track change takes up to 1 s to reach the UI; at the same time`idle`on the phone responds in 4 ms |
| 5 | The entire queue is reread | `playlistinfo` on every version change; at 500 tracks this means noticeable traffic and parsing |
| 6 | No resume to the second | `state_file` remembers the queue/track, but not the position |
| 7 | Seek only by tap | The bar cannot be dragged; hitting it with a finger when it is only 4 px high is a lottery |
| 8 | No actionable empty states | “Queue is empty — play an album from the library”, but there is no library |

**Verdict:** The MPD foundation is sound, but the client uses only 20% of its
capabilities. This is the cheapest and most visible work in the project.

### 2.2. Terminal

**What actually works**

- Custom 65×25 screen, 12 px monospace, prompt `user@l7:~$`, 50-command
  history, 52-character input, 480×288 on-screen keyboard (Russian/English/123).
- Exactly 5 programs are allowed: `uname`, `free`, `df`, `ip`, `mpc`, plus
  `help` and `clear`. Launched via `fork`/`execvp` without a shell; 300 ms budget.
- The "XTERM" button in the header launches real xterm via`apphost`(480×768, from`user`, tested in Stage 1).

**What's missing**

- **This is not a terminal.** No PTY, no ANSI, no interactive programs, no
  pipes/redirects, no Ctrl/Alt/Tab/arrows.
-`squeeze()`collapses spaces and word wraps - output`df`,`ip`,`free`loses columns and reads worse than in reality.
- **No output scrolling**: only the last 25 lines are visible, old ones
  not available. There are no up/down buttons.
- No copy/paste, no search by output.
- 300 ms kills anything longer (any network call,`apk`,`top`).
- Entering 52 characters means you cannot type a long command.

**Verdict:** leave the built-in console as an emergency mode (5 commands),
but "Terminal" in the application list should open xterm.

### 2.3. Notes

**What actually works**

- One file at a time (default `~/.config/graphite/notes.txt`); `.txt`/`.md` picker
  with fixed roots (`/media/sd`, `/media/sd/Notes`, `/media/sd/Books`, `/home/user`, `~/.config/graphite`), “+ New”,
  "SAVE".
- Autosave 1.5 s after each character and on every DEL/Enter; header shows
  “modified HH:MM”; limits: 2 MB / 2000 lines / 1024 characters per line.
- Keyboard 288 px, Russian/English layouts, layer 123, SHIFT, DEL, SPACE, ENTER.
- Opening a file from "Files" via`app.open_path`.

**What's missing**

| # | Hole | Consequence |
|---|---|---|
| 1 | **No cursor: text is only appended to the end of the last line** | You can't correct a typo in the middle. This is a “add to end” notepad, not an editor |
| 2 | No arrows, Home/End, no highlight | You can't even go to the previous line |
| 3 | No list of notes | Picker shows file names, but it's a file manager, not "my notes" |
| 4 | No undo | Random DEL cannot be returned |
| 5 | Writes are non-atomic (`fopen("w")` + line by line) | Power loss during a write can truncate the file |
| 6 | No search or sorting | 20 notes can still be found by eye; 100 cannot |
| 7 | Markdown not rendering |`.md`looks like`.txt`|
| 8 | Editing only the “tail” (40 lines, 17 visible) | Long text cannot be scrolled up |

**Verdict:** The app is one step away from being useful, but that step is
the most important: cursor and insertion at any location.

### 2.4. Files

**What actually works**

- Directory by directory, 12 lines of 56 px, folders first, sorting
  by name without case, “..” as the first line, drag-scroll.
- Start from`/media/sd`→`/mnt/sd`→`/sdcard`→`/`.
-`.txt/.md/.log/.conf`open in Notes, the rest is toast
  "There is no application for this file type."

**What's missing**

- **No write operation**: no copying, moving, deleting,
  renaming, creating a folder. This is not a file manager, but a viewer.
- No multi-select, no long-press, no context menu.
- No sorting by size/date/type, no hidden files (dotfiles
  always shown), no search.
- No preview (pictures, text), no “open with”, no properties.
- No free space, no folder size, no bread crumbs.
- Hard limit of 500 records - silently trims large directories.
- The shell is running as root → you can log in`/proc`,`/sys`,`/etc`and (after
  the entry appears) break something.

**Verdict:** The biggest functional failure since the player.

### 2.5. Browser

**What exists**

- Drawn "Local Page" page with fake address bar,
  fake links and bottom bar. There is no network, it does not read files.
- The screen is **hidden** from “Applications” and from pinned tiles (correct:`SHELL-TODO.md`directly says “remove so as not to show the fake”).

**What's missing**

- Total. Or an honest launch`dillo`/`netsurf`, or absence of item
  "Browser" in the UI. There is no third option.

### 2.6. Reader

- `make_reader()` is a weak stub; there is no screen.
- Plan (`READER-PLAN.md`) is detailed: TXT+FB2 v1, EPUB v2, PDF v3,
  layout on a fixed grid, page map, anchor progress.
- One thing is outdated: “separate process”`lgreader`"—there is already a shell, and
  Canvas/Text/Keyboard/Themes are ready in it.

### 2.7. Video

- `make_video()` is a stub; there are no video files on the SD card.
- Plan (`VIDEO-PLAN.md`) gives an honest verdict: 480×272/320 H.264
  baseline/main is real, 480p and higher - no; hardware decoder not available;
  X is not suitable for video; blocker - panel and`FBIOPAN_DISPLAY`.

### 2.8. General:`apphost`+ OSK (Stage 1)

| Yes | No |
|---|---|
| One external window`XMoveResizeWindow`in 480×768 at y=32 | Mini WM:`ConfigureRequest`is not intercepted, dialogs/transient are not managed, the window may pop out |
|`fork`→`setgid/setuid(10000)`,`HOME`,`DISPLAY`,`XDG_RUNTIME_DIR`,`PATH`,`LANG`| No`DBUS_SESSION_BUS_ADDRESS`,`GTK_CSD=0`,`NO_AT_BRIDGE=1`,`XMODIFIERS`,`LC_ALL`→ GTK applications (pcmanfm, netsurf) may not open |
| XGrabKey on Back/Home/Power,`WM_DELETE`→ SIGTERM 0.8 s → SIGKILL 2.5 s | No task list, long-press Home, LRU-kill, “Running...” with a timeout of 15 s (there is toast) |
| OSK 480×288 in (0.512), XTest, Unicode-keysym on scratch keycode, RU/EN/123 | No terminal layer: Esc, Tab, Ctrl, Alt, arrows, Home/End/PgUp/PgDn,`\| ~ $ - _ /`|
| Keyboard icon in the status bar (x≈296) | Tapping on the input field does not open the keyboard (and cannot) |
| When the keyboard is open, the window shrinks to 480 height | Fixed-size/immersive applications do not have an overlay mode |

**Important for the document:** the actual Stage 1 model is “status bar 32 +
content 480x768" rather than "header 40 + 712 + doc 48" as in`APP-INTEGRATION-PLAN.md`. It's simpler and already works; no need to change, but
plans need to fix one of two things, otherwise the documentation is lying.

### 2.9. Audit result: the fate of each application

| Application | Fate | First step |
|---|---|---|
| Player | Keep its own screen and expand it | `idle` + `readpicture` + index |
| Terminal | external`xterm`, built-in console - emergency | launching xterm from the screen, OSK terminal layer |
| Notes | Keep its own screen; rewrite the editor | cursor and in-place editing |
| Files | Keep its own screen; add write operations | multi-select + copy/move/delete/rename |
| Browser | external`dillo`(NetSurf - option) | remove fake, launch dillo |
| Reader | New screen in the shell | layout spike on TXT |
| Video | separate fb process, late | pan test, not code |

---

## 3. What the market expects (and what to take from it)

### 3.1. MPD music player

An overview of MPD clients (rmpc, Sonata, Euphonica, Maximum MPD, myMPD) gives
a stable set of expectations:

- **Minimum:** play/pause/next/prev, seek, volume, queue, library
  by albums/artists/tracks, covers, search, playlists.
- **Expected:** “play next”, “queue”, rearrangement, save
  queues in the playlist, resume, random/repeat, cover in the list and on “Now”.
- **Advanced:** ReplayGain, crossfade, consume, dynamic playlists,
  scrobbling, lyrics, visualizer. We don't need (CPU/network/no services).
- **Separately:** MPD is a server, so the client does not store the state;
  our code already respects this.

Conclusion: P0 of the player is exactly “library + covers + queue + resume”,
not an equalizer.

### 3.2. Terminal

Mobile terminal expectations (Termux, TermRover, Mobile SSH, Termix):

- xterm-256color, PTY, scrollback 1,000–50,000 lines;
- **additional row of keys**: Esc, Tab, Ctrl, Alt, arrows, Home/End,
  PgUp/PgDn,`| ~ _ - /`,Backspace;
- sticky Ctrl (pressed Ctrl - pressed C → SIGINT);
- change PTY size when showing/hiding the keyboard;
- copy/paste, select long-press, tap on URL;
- the hardware keyboard works “as is”.

Conclusion: without PTY and without Ctrl/Esc/arrows, the “terminal” is useless. Your VT100 -
months;`xterm`gives it all out of the box.

### 3.3. Notes

The common denominator of file-based note apps (QuackNotes, Zettel Notes, Standard
Notes, FUTO Notes):

- note = normal`.txt`/`.md`in the user's folder, without database and account;
- autosave (there may not be a “Save” button);
- list of notes with date/size/preview, search by name and content;
- markdown preview (often an option, not the default mode);
- folders/repositories, pinning, sorting;
- undo, selection, transfers.

Output: P0 - “open any file, place cursor, edit, autosave,
list." Markdown render - P1.

### 3.4. File manager

Even minimal Android managers (File Explorer, Flux, FileNo, Sally)
agree on one thing:

- copy / move / rename / delete, multi-select, delete confirmation;
- sorting (name/size/date/type), hidden files, search by name;
- “open with”, properties, free space;
- archives (zip/tar) - almost always, often “like a folder”;
- image/text preview, trash (on desktop).

On a vfat card, trash and permissions are unavailable, so deletion is immediate
and should honestly be described as “no trash”. P0: operations + multi-select + sorting.

### 3.5. Browser

| | NetSurf 3.11 | Dillo 3.3 |
|---|---|---|
| Engine | built-in (Hubbub/LibCSS/LibDOM) | built-in |
| HTML/CSS | HTML 4.01, CSS 2.1 + flex, part HTML5 | HTML 4.01 subset, CSS 2.1 subset |
| JS | Duktape, **off by default**, incomplete | **not at all** |
| Tabs | yes | yes |
| Bookmarks | hotlist |`bm.txt`, bm plugin |
| Downloads | loading window |`wget`via dpi |
| HTTPS | yes | yes (OpenSSL/mbedTLS) |
| Memory | 30–50 MB (GTK3) | 12–18 MB (FLTK) |
| armv7 package | 1.9 MiB / 4.8 MiB | 322 KiB / 628 KiB |
| IPC for shell | no | **`dilloc`** (open/url/title/reload/dump/quit) |

Conclusion: most of the modern web does not work without JS; the UI must state
that honestly. Dillo is cheaper, lighter, and controllable from the shell; NetSurf is an option for
sites that need CSS. Both are external processes; do not embed them.

Minimum: open URL, render HTML, back/forward, address bar,
file upload. Expected: tabs, bookmarks, history, page search.
Will not: JS applications, videos, complex CSS.

### 3.6. Reader

Expectations (Hetko, UZYTAK, Librera, Beresta, Aurora):

- FB2 + EPUB (+TXT),`fb2.zip`;
- shelf with covers and “continue reading”;
- TOC with nesting, progress in % and pages, resume;
- font size, leading, margins, alignment, hyphens;
- 3+ themes (day/night/paper), brightness;
- bookmarks with excerpts, book search, footnotes in a pop-up card.

Minimum for us: FB2+TXT, shelf, pagination, resume, TOC, font size,
3 themes. EPUB is the second stage. PDF - separate and late.

### 3.7. Video player

Checklists (Alpa Player, VLC Mobile Design, MX Player):

- containers/codecs, hardware decode;
- subtitles (SRT required, ASS - at least text), encoding;
- audio track selection, audio delay;
- seek ±10 s, resume, progress;
- gestures: tap - overlay, double tap - ±10 s, swipe - brightness/volume.

What we really have from this is: H.264 baseline/main 480×272/320, MPEG-4 ASP,
AAC/MP3, SRT, one audio track, resume, ±10 s. HEVC/VP9/AV1/1080p - no.
Hardware decoding - no.

---

## 4. Feature matrices

Designations: **P** - priority, **Next** - difficulty (S ≤ 1 day, M 2–3,
L 4–6, XL 7+ for one developer with manual rendering), “is” - already
works.

### 4.1. Player

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
|`idle`instead of a survey | <100ms reactivity, less traffic | **P0** | M | 2nd socket,`idle player mixer options playlist database`, resend, backoff | frozen idle → needed`ping`and timeout |
| Covers:`readpicture`→ file nearby → stub | the main screen is no longer blank | **P0** | M |`binarylimit 1048576`,`readpicture`,`cover.jpg/folder.jpg`,`stb_image`, cache 96×96/384×384 RGB565 | JPEG decode on the UI thread; only background + cache |
| Library index + cache | lists without requests to MPD | **P0** | L |`listallinfo`→`/var/lib/graphite/music.idx`, disability`stats.db_update`| 5 thousand tracks: time and RAM |
| Albums / Artists / Songs | entrance to music | **P0** | L | pivot “Library”, tabs, lines 64/56 | layout at 480 |
| Album screen: Listen, Queue, Next | basic scenario | **P0** | M |`clear`+`findadd`+`play 0`,`add`,`addid uri song+1`| intermediate empty queue → batch commands |
| Queue: long-press → play/next/delete/info | without this the queue is useless | **P0** | M |`deleteid`,`addid`, curtain 6 lines | no |
| Clear queue + “⋯” | hygiene | **P0** | S |`clear`, curtain | no |
| Resume with a second | "continue where stopped" | **P0** | S |`state_file`+ your`lastpos`,`seekcur`|`seekcur`immediately after`play`can give ACK |
| Seek drag | stripe 4 px, thumb large | **P0** | S | drag by zone 604..652 | conflict with pivot swipe (solved by zone) |
| Empty states and MPD/SD errors | don't scare | **P0** | S | texts from`PLAYER-FEATURES.md`§5.10 | no |
| Incremental queue | 500 tracks without brakes | **P0** | M |`plchangesposid`+ cache uri | version out of sync |
| Search (`search any`) | find track | P1 | M | search screen + keyboard, debounce 300 ms | ICU register - tested, working |
| Playlists and Favorites | selections | P1 | M |`save/load/playlistadd/rename/rm`, playlist`Favorites`| names/escaping |
| Folders, alphabet, recently played | crooked tags, long lists | P1 | S/M |`lsinfo`, index,`played.log`| Cyrillic alphabet order - sort by yourself |
| Sleep timer, pause on headphones, headset button | convenience | P1 | S | timer,`/sys/class/switch/h2w/state`,`KEY_MEDIA`event4 | duplicates event4/event5 |
| Crossfade / ReplayGain / consume | "like people" | P2 | S/M |`crossfade`,`replay_gain_mode`,`consume`| two decoders at 1 GHz |
| Album grid, drag-reorder | visually/habitually | P2 | M/L | — | touch without multitouch |
| Equalizer (AUDPP) | sound | don't | XL | — | owner's decision: last point of the project |

### 4.2. Terminal

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| "Terminal" = launch`xterm`| real shell, PTY, ANSI | **P0** | S |`apphost_launch("xterm -u8 -fa 'DejaVu Sans Mono' -fs 11 -bg '#101419' -fg '#efebef' -cr '#3fc8d0' -ls")`| map race (can be treated with the “Launch…” toast) |
| OSK terminal layer | You can't do without Ctrl/Esc/arrows | **P0** | M | second OSK page: Esc Tab Ctrl Alt ←↑↓→ Home End PgUp PgDn`\| ~ $ - _ /`; sticky Ctrl via XTest + US-keycode | Ctrl+C on the RU group - send US keycode |
| Output scroll | view history | **P0** | S | XTest button 4/5 (wheel), or Shift+PgUp/PgDn | xterm drag = highlight, not scroll |
| Built-in console - honest | emergency access without X | **P0** | S | rename to “Console (5 commands)”, remove`squeeze`, 80 columns with hscroll | no |
| Back: first - keyboard, second - exit | predictability | **P0** | S | already in`app.cpp`| no |
| Paste from clipboard | URL, long commands | P1 | M | your buffer + XTest type; X11 CLIPBOARD via`XConvertSelection`| no buffer manager |
| Copy by selection | "copy output" | P1 | L | XTest drag + read PRIMARY | resistive touch, precision |
| Tap on URL | open link | P2 | M | parsing the output`app.open("browser")`| no |
| Multiple sessions/tabs | — | don't | XL | tmux outside | memory |

### 4.3. Notes

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| Cursor: tap on text, arrows, Home/End | without this you cannot rule | **P0** | L |`(line, byte)`+ preferred column; hit-test by`g.textw(prefix)`| line breaks ↔ coordinates |
| Insert/delete at cursor position, Enter divides line | basic editor | **P0** | M | editing`vector<string>`| UTF-8 borders |
| List of notes | “my notes”, not files | **P0** | M | screen-mode: name, mtime, size, 2 preview lines | no |
| Atomic autosave | power is turned off | **P0** | S |`tmp`+`fsync`+`rename`+`fsync(dir)`, debounce 2 s | vfat on SD - only for /home |
| Scrolling text | long notes | **P0** | M | drag + transfer cache | 2 MB text |
| Undo (ring 20) | random DEL | **P0** | M | line/file snapshots | memory - limit |
| Rename/delete/create | hygiene | **P0** | S |`rename`,`unlink`+ confirmation | no cart |
| Search by name and text | find | P1 | M | line scan, 200 file limit | 1 GHz |
| Markdown preview |`.md`looks like`.md`| P1 | M | mini-render (H1–H3, bold/italic, lists, code) or “open in reader” | don't drag CommonMark |
| Pinning/sorting | order | P1 | S |`pins`-like file | no |
| Tags, backlinks, sync | — | don't | XL | — | no services |

### 4.4. Files

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| Multi-select (long-press → checkboxes) | without it, operations are meaningless | **P0** | M | header "SELECTED N", bottom bar, Back removes | long-press/scroll conflict |
| Copy/Move | main | **P0** | L | own cycle 64 KiB,`utimensat`, progress in`tick`, cancel | 20 MB/s SD, vfat |
| Delete (file/folder) | main | **P0** | M |`unlink`, recursive traversal without following symlink loops, confirmation | without a basket - write honestly |
| Rename | main | **P0** | S |`rename`, keyboard, extension separately | vfat case-insensitive |
| New folder | main | **P0** | S |`mkdir`| no |
| Sort by (name/size/date/type) + hidden | navigation | **P0** | S | field in settings | no |
| Long-press → action curtain | Metro pattern | **P0** | S | 6 lines | no |
| "Open with" | stop saying "no app" | **P0** | S | extension → notes/reader/image/video/player | MPD player is waiting for uri |
| Properties (size, date, rights) | understand what it is | **P0** | S |`stat`| no |
| Free space in header | landmark | **P0** | S |`statvfs`| no |
| Remove limit 500/page | large catalogs | **P0** | S |`readdir`portions or 2000 + “show more” | memory |
| Archives: Listing and Unpacking | zip/tar to SD | P1 | M |`libarchive`(read),`libzip`(write) | zip bombs, name encodings |
| Copy progress with cancellation | large files | P1 | M | job structure,`tick`| UI blocking |
| Recursive Search | find | P1 | M | bypass with limit, cancel | 1 GHz, SD |
| Image/Text Preview | quick look | P1 | S |`stb_image`+ scale, text 64 KB | decode in UI |
| Crumbs and roots (SD/Home) | navigation | P1 | S | — | no |
| Cart, batch rename, checksum, network | — | don't | L/XL | — | vfat/no services |

### 4.5. Browser

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| Remove fake page | honesty | **P0** | S | delete/overwrite`browser.cpp`| no |
| Launch`dillo`in content | real web | **P0** | S |`apphost_launch("dillo <url>")`| GTK/FLTK env, focus |
| Back = Alt+Left, reload, home | navigation | **P0** | S | XTest Alt+Left;`dilloc reload/open`| the application may not respond |
| OSK for address | enter URL | **P0** | M | existing OSK + terminal layer | focus |
| Honest signature "without JS" | expectations | **P0** | S | text on empty state | no |
| Address bar and header in shell | uniform style | P1 | M |`dilloc url/title`, header 40 + content 712 | need window resize |
| Bookmarks from`~/.dillo/bm.txt`| your links | P1 | M | plain text parser | format |
| NetSurf as a second engine | sites with CSS | P1 | S |`.desktop`+ choice in settings | 30–50 MB |
| Reader mode | read articles | P1 | M |`dilloc dump`→ reader | HTML quality |
| Adblock/domainrc, HTTPS exceptions | privacy | P2 | S |`dillo`config | no |
| Your own engine, JS, tabs in the shell | — | don't | XL | — | impossible/pointless |

### 4.6. Reader

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| Shelf + “Continue” | entrance | **P0** | M | scan`/media/sd/Books`,`index.json`, covers 112×160 / typographic | 200 books, decode |
| TXT | 90% simple texts | **P0** | M | encoding detection (BOM, UTF-8 strict, CP1251/KOI8/CP866), paragraphs | cp1251 |
| FB2 (+`fb2.zip`) | 60–70% Russian books | **P0** | L |`libxml2`SAX, metadata, TOC, base64 cover,`libzip`| broken files |
| Layout and pagination | readability | **P0** | XL | net`lh=round(size×1.45)`, greedy transfer, justification, page map | determinism |
| Progress anchor + resume | don't lose your place | **P0** | M |`(block, off)`+%,`pages/<id>.pg`, atomic record | change font |
| TOC | navigation | **P0** | M | FB2 chapters, TXT heuristics | nesting |
| Font size + 3 themes | comfort | **P0** | M | 16/18/20/22/24; day/night/paper | page recalculation |
| Scrolling: tap zones, swipe, Vol± | basic gesture | **P0** | S | x<120 back, x>360 forward, center - HUD | conflict with edge-back |
| Bookmarks with excerpt | return | P1 | M | same anchor + 80 characters | no |
| EPUB | 20–30% | P1 | L |`libzip`+`libxml2`HTML SAX, OPF/spine/NCX/nav | We do not execute CSS |
| Search by book | find | P1 | L | arena scan, lighting | 1 GHz |
| Hyphenations (libhyphen + ru) | quality | P1 | M | dictionary 150–250 KB attach | no dictionary in Alpine |
| Footnotes card | FB2 notes | P1 | M | bottom sheet | anchors |
| PDF | 5–10% | P2 | XL | separate process`mupdf-x11`or`pdftoppm`| AGPL, 0.4–2 pp |
| Pictures in text, TTS, sync | — | don't | L/XL | — | memory/network |

### 4.7. Video

| Feature | Why | P | Complexity | Implementation | Risk |
|---|---|---|---|---|---|
| **Stage 0: pan test 10 min** | you can't start without it | **P0** | S |`fbpan`30 Hz,`dmesg \| grep lcd_`| reboot |
| fb player, portrait, 180° in blit | X is not good for video | **P0** | L |`libav*`+ ALSA + fb, separate process | conflict with X for fb0 |
| H.264 baseline/main 480×272/320 | what pulls A5 | **P0** | L |`libavcodec`(already standing) | 70-100% core |
| MPEG-4 ASP (DivX/Xvid) | old files | **P0** | M |`libavcodec`| 640x480 - back to back |
| AAC/MP3, audio clock | synchronous | **P0** | L | ALSA`hw:0,0`,`snd_pcm_delay`| xrun |
| SRT (UTF-8/CP1251) | subtitles | **P0** | M | your parser + FreeType | encoding |
| Resume, ±10 s, progress | convenience | **P0** | S |`resume.json`| no |
| Selecting an audio track | MKV | P1 | M | restart audio from position | no |
| ASS/SSA text | more often SRT | P1 | S | cut`{...}`| styles get lost |
| Landscape | area | P2 | L | canvas 800×480 + tile rotation | +8–15% CPU |
| Hardware QDSP5 | battery | P2 | XL |`/dev/VIDEOTASK`+ ION | reboot DSP |
| HEVC/VP9/AV1/1080p, mpv, GL | — | don't | — | — | physically not |

---

## 5. Internal vs external: terminal, browser, PDF

### 5.1. Terminal

| Criterion | Your VT100 |`xterm`|`st`|
|---|---|---|---|
| Readiness | 0 | full | complete, but without scrollback in stock |
| Volume | XL (months) | 0 | 0 |
| Package | — | 602 KiB | 53 KiB |
| RSS | — | ~12 MB | ~11 MB |
| PTY/ANSI/256 colors | write | yes | yes |
| Scroll | write | yes | no (needs patch) |
| Enter OSK | your sink | XTest | XTest |
| Style | ideal | customizable`-bg/-fg/-cr/-fa/-fs`,`-b 0 -bw 0`| configured |
| Risk | emulation bugs | race map, focus | same + no scrollback |

**Decision:** `xterm` is the default. Leave the built-in console as
“Console (5 commands)” for the case when the X application does not start.`st`we don’t take it: saving 550 KiB is not worth the loss of scrollback.

### 5.2. Browser

| Criterion | Your own engine |`dillo`|`netsurf`|
|---|---|---|---|
| Realism | no | yes | yes |
| Package | — | 322 + 194 KiB | 1.9 MiB + GTK dependencies |
| RSS | — | 12–18 MB | 30–50 MB |
| JS | — | no | limited, disabled |
| CSS | — | subset 2.1 | 2.1 + flex |
| Tabs/Bookmarks/Downloads | write | yes | yes |
| Shell control | — |`dilloc`| no |
| License | — | GPL-3 | GPL-2 |

**Solution:**`dillo`- the default engine and the only one in v1.`netsurf`— option “for sites with CSS”, launched by the same`apphost`.
Both are external processes, the shell does not link them (the GPL does not infect our code).
Honest signature in a blank state: “Sites without JavaScript. Many
modern pages will not open.”

### 5.3. PDF

| Criterion | Yours |`mupdf-x11`|`pdftoppm`(poppler) |
|---|---|---|---|
| Readiness | no | yes | yes |
| Package | — |`mupdf-x11`95 KiB+`mupdf-libs`4.5 MiB |`poppler-utils`+ libs (~2–5 MiB) |
| Speed ​​| — | 0.4–2 s/page | 0.5–2 s/page |
| Memory | — | 20–40 MB | 10–25 MB |
| License | — | **AGPL-3** | GPL (CLI) |
| Integration | — | separate window | render PPM →`stb_image`→ your screen |
| Text/search | — | yes (inside) | no |

**Solution:** PDF is not in v1. When needed:
- **do not link**`libmupdf` into `graphite-shell`- the whole shell will do this
  AGPL;
- option A:`apphost_launch("mupdf-x11 file.pdf")`- a separate process,
  AGPL does not infect. In Alpine it is a separate binary`/usr/bin/mupdf-x11`without OpenGL (`mupdf`/`mupdf-gl`not suitable for P700);
- option B:`pdftoppm -r 120 -f N -l N -png`→ picture on your screen,
  we get a single style, but without text/search.

---

## 6. Libraries and packages (Alpine edge/armv7, musl)

**Already installed (confirmed by project audits):**`freetype`(+dev),`zlib`(+dev),`libxml2`(without dev),`libexpat`,`libhyphen`(without ru dictionary),`sqlite-libs`(no headers),`mupdf-libs`4.5 MB,`libjpeg-turbo`,`libpng`,`libwebp`,`font-dejavu`,`libX11`/Xorg`ffmpeg-libs`9.0.2
(`libavcodec.so.63`~11 MB), GStreamer core+base (without`gst-libav`),`xterm`(Stage 1 runs it from`user`).

**Availability not confirmed; install with `apk add`:**`dillo`,`netsurf`,`mupdf-x11`,`libzip`,`libarchive`, dictionary`hyph_ru_RU`. For assembly
will be needed`libxml2-dev`,`libzip-dev`,`libarchive-dev`,`hyphen-dev`(megabyte units, host/build phase only).

> The "package/installed" size below is the package's own files without
> dependencies. For GTK applications, dependencies add 20–40 MB, for
> Xlib utilities - 1–3 MB.

| Package | Size/installed | License | Why | Verdict |
|---|---|---|---|---|
|`xterm`| 295 KiB / 602 KiB | MIT | terminal | **take** |
|`st`| 32 KiB / 53 KiB | MIT | alternative | no (no scrollback) |
|`dillo`+`dillo-common`| 322 + 194 KiB / 628 + 496 KiB | GPL-3.0 | browser; V`dillo-common`—`bookmarks.dpi`,`downloads.dpi`,`dpid`, configs | **take both** |
|`netsurf`| 1.9 MiB / 4.8 MiB | GPL-2.0 | browser with CSS | option P1 |
|`mupdf-x11`+`mupdf-libs`| 95 KiB + ~4.5 MiB | AGPL-3.0 | PDF | only as a separate process, P2 |
|`libxml2`| 419 KiB / 742 KiB | MIT | FB2/EPUB/XHTML | **take** |
|`libzip`| 49 KiB / 78 KiB | BSD-3 | EPUB,`fb2.zip`, zip | **take** (reader/archives) |
|`libarchive`| 284 KiB / 477 KiB | BSD-2/3 | tar/gz/7z/rar-read | P1 (archives in files) |
|`sqlite-libs`| 725 KiB / 1.02 MiB | public domain | index/search | not in v1 (JSON/text is enough) |
|`taglib`| 22 KiB / 63 KiB | LGPL/MPL | reading tags without MPD | **not needed** (tags provided by MPD) |
|`freetype`| 449 KiB | FTL/GPLv2 | text | already standing |
|`font-dejavu`| ~10 MiB | Bitstream Vera | fonts | already standing; load lazy |
|`zlib`| 44 KiB / 65 KiB | Zlib | gzip/PNG | already standing |
|`libhyphen`+`hyph_ru_RU`| 28 KiB + ~200 KiB | MPL/GPL | transfers | P1, dictionary attached |
|`libjpeg-turbo`| ~309 KiB | IJG/BSD | JPEG | you don't have to take:`stb_image`|
|`libpng`| ~129 KiB | libpng | PNG | you don't have to take:`stb_image`|
|`stb_image.h`| 0 (title) | public domain/MIT | JPEG/PNG/WebP | **take** (already used in`lp7player`) |
|`ffmpeg-libs`|`libavcodec.so.63`~11 MiB | GPL | video | already standing; for video mode only |

**Total new weight (without video/PDF):** ~2–3 MB packages (with dependencies`dillo`/`libxml2`) + ~0.3 MB shell binary. This is normal for rootfs
1.1 GB free.

### What to avoid

| Don't take | Why |
|---|---|
| GTK/Qt applications as embedded screens | 30–100 MB RSS, D-Bus, CSD, topics; only as external processes |
| WebKit/Chromium | not possible at 400 MB and 1 GHz |
|`cairo`,`pango`,`Xft`in hot way | extra layers; we have our own Canvas and FreeType |
| Compositor, XRender, GL | no GPU, extra risk and memory |
|`taglib`| MPD is already giving away tags |
| SQLite | a text index is enough for 5 thousand tracks; no headers |
|`file`/libmagic | Enough expansion table + 2–3 magic |
| Python/Node runtimes | memory and start |
|`mpv`/`ffplay`/GStreamer-wrappers | no GL/KMS, extra 30+ MB |
|`libass`| fribidi/harfbuzz/fontconfig; SRT is enough |
|`mupdf`/`mupdf-gl`| GL wrapper, useless on P700 (need`mupdf-x11`) |

---

## 7. How to implement on our stack

### 7.1. Player

**MPD layer (foundation; implement first).**

```
control socket — commands, 1 s timeout
idle socket    — only idle/noidle, select(), separate backoff
```
- On connect:`binarylimit 1048576`(otherwise covers are 8 KB),`idle player mixer options playlist database stored_playlist output update`.
- Event → parse`changed: X`→ update what you need → resend immediately`idle`. Break → backoff 1/2/5/10 s, queue cache reset.
- Position:`elapsed`+`CLOCK_MONOTONIC`, reconciliation`status`once every 5 s.
- All teams - by`id`(`playid/deleteid/moveid`), out of position.
- Batch operations -`command_list_begin/ok_begin`.

**Covers.**

1.`readpicture(uri, 0)`(after`binarylimit`);
2.`<dir>/cover.jpg|folder.jpg|front.jpg`;
3. “NO COVER” plug.

Decoding - in a background thread (`pthread`), the UI first draws the stub.
Cache:`/var/lib/graphite/art/<hash>.565`, 96×96 = 18 KB, 30 MB LRU limit.
Large 384x384 - in RAM for the current album (~290 KB RGB565), not on disk.
Decoder -`stb_image`(`stbi_load`+ custom resize in RGB565 with dithering).

**Index.**`listallinfo`once → text file:`T<TAB>uri<TAB>title<TAB>artist_id<TAB>album_id<TAB>genre<TAB>year<TAB>track<TAB>disc<TAB>sec`.
Album key -`(AlbumArtist || Artist, Album)`, for collections - only`Album`. Version -`stats.db_update`. Rebuild in the background, old index
available. 5 thousand tracks ≈ 600 KiB, parsing ~30 ms.

**Screens.** Pivot: Now · Queue · Library · Info (4 headings
placed at 12 px). Library: tabs Albums/Artists/Songs/More;
lines 64 with thumbnail 48. Album: cover 160, “LISTEN” 240×56, tracks 56.
Action curtain - 480×(40+56·N), N≤6.

**Resume.**`state_file`gives queue/track/modes; mine`/var/lib/graphite/lastpos`(`uri`,`elapsed`) write every 15 s and on
pause/stop. After the start -`seekcur(elapsed-3)`, if the track matches.

### 7.2. Terminal

-`terminal.cpp`→ launcher screen: draws “Terminal” + “Launch...”,
  in`on_show()`causes`apphost_launch(...)`, By`apphost_active()==false`shows "Terminal is closed" and the "Open again" button.
- xterm flags:`-u8 -ls -b 0 -bw 0 -sl 1000 -fa 'DejaVu Sans Mono'
  -fs 11 -bg '#101419' -fg '#efebef' -cr '#3fc8d0'`(without`-sb`:
  There is no need for a scroll bar, we scroll with the wheel).
- OSK: add a `TERM` page (switch `123` → `ABC` → `TERM`): `ESC TAB CTRL ALT ← ↑ ↓ → HOME END PGUP PGDN | ~ $ - _ /`.
- Ctrl - sticky:`XTestFakeKeyEvent(Ctrl_L, True)`, then the key,
  then`Ctrl_L False`. For`Ctrl+C`use latin keycode`c`(US), otherwise the RU group will not have 0x03.
- Scroll —`XTestFakeButtonEvent(4/5)`(wheel), not drag.
- Fix built-in console: remove`squeeze()`, 80 columns,
  horizontal scroll, ↑/↓ buttons, +10 commands (`ls`,`cat`,`mount`,`ps`,`uptime`,`mpc`,`df`,`free`,`ip`,`uname`), budget 800 ms.

### 7.3. Notes

Model:

```cpp
struct Doc {
    std::vector<std::string> lines;  // UTF-8, no newline
    int cur_line, cur_col;           // col is in code points
    int want_col;                    // for ↑/↓
    int scroll;                      // in pixels
    bool dirty;
};
```
- Render: for each line - hyphenation cache`vector<Span{off,len,x,w}>`,
  recalculation of only the changed string.
- Tap:`y`→ visual line → logical + offset;`x`→ codepoint
  (binary search by accumulated span width).
- Edit: insert/erase in`lines[cur_line]`, Enter - split, Backspace on
  beginning - join with the previous one.
- Undo: ring of 20 shots`(lines, cur_line, cur_col)`; for 2 MB this is
  expensive → snapshot of only the changed row + position.
- List of notes: separate screen mode (not new id), lines 64:
  name 16, "12.4 KB · 24 Nov" 11 dim, first line preview 12 dim.
- Autosave: debounce 2 s,`path.tmp`→`fsync`→`rename`→`fsync(dir)`.
- Leave Picker as “Browse...” for files outside the list.

### 7.4. Files

- Operations are our own, not`system("cp")`: progress and cancellation needed.
- Copy:`open/read/write`64 KiB,`fchmod`/`utimensat`by
  opportunities; on vfat rights are ignored.
- Removal:`unlink`/`rmdir`, recursion through`fdopendir`(do not follow
  symlink). Confirmation: “Delete 3 objects? There is no basket."
- Conflict: dialogue “Replace / Skip / Rename” + “apply to all”.
- Multi-select: long-press (650 ms, shift <14 px) →`sel`-mode,
  checkboxes 24 at x=16, header “SELECTED 2”, bottom bar 56 with actions.
- Sorting:`name/size/date/type`+`dirs_first`, store in`/etc/lgtest-settings`.
- Security: write only in`/home/user` and `/media/sd`; on`/etc`,`/usr`,`/proc`,`/sys`- read-only with the signature “system partition”.
- Limit: 2000 entries + “show more”, not the silent 500.

### 7.5. Browser

-`browser.cpp`→ launcher screen (like a terminal):`dillo <url>`through`apphost`; Back = Alt+Left(XTest); "Home" =`dilloc open about:blank`.
- Place`dillo`**together with`dillo-common`**: there`dpid`and plugins`bookmarks.dpi`/`downloads.dpi`; Without it there will be no bookmarks or downloads.
-`apphost_on_map()`takes the **first** viewable window with a matching pid and does not
  looks at the size. Dillo/netsurf has helper windows 1×1/10×10 - needed
  filter “≥200×200,`_NET_WM_WINDOW_TYPE_NORMAL`", otherwise you can grab
  helper instead of a page (the same applies to GTK applications).
- The address bar in the shell (P1) requires the dillo window to occupy
  480x712 below a 40 px header. Now`apphost`firmly places the window in`(0,32)`,
  so first you need to teach it to offset the content by 40 px
  (`apphost_set_content_height(712)`by itself does not provide this). Bye
  It's easier to leave the whole screen dillo.
- Bookmarks: read`~/.dillo/bm.txt`(plain text, sections`:sN:`), draw
  own lines 56; tap →`dilloc open <url>`.
- Downloads: dillo puts via wget in`~/Downloads`; show in "Files"
  and the toast “Loading has begun.”
- NetSurf:`.desktop`-record + engine selection in settings; in the package there is
  both binaries (`netsurf`,`netsurf-gtk3`), and also`hotlist.ui`,`downloads.ui`,`globalhistory.ui`And`adblock.css`.

### 7.6. Reader

- Where to live: **in a shell**, with a separate screen. Parser and layout - in`src/reader/`(or`libgraphite`) to be reused later.
- Parsing: TXT - yours; FB2 -`libxml2`SAX (not DOM); EPUB -`libzip`+`libxml2`HTML SAX.`fb2.zip`—`libzip`, search`*.fb2`.
- Model: arena UTF-8+`Block[]`+`Run[]`(italics/bold within paragraph).
- Layout: fixed row grid (all`lh`are equal), the title occupies
  2 slots, H1 - from new page, hanging punctuation, justification with refusal
  at`extra/gap > 0.6 em`.
- Page map:`PageStart{block, off, line, chapter}`— 12 bytes/page.
  Cache`~/.cache/graphite/reader/pages/<id>.pg`.
- Progress is an anchor`(block, off)`+ percentage by characters, not page number.
- Cover cache 112×160 RGB565; without cover - typographical.
- Keys: Vol± - scrolling (volume only from the curtain). This is necessary
  record in the key mastery matrix.
- Memory: 3–20 MB; text limit 32 MB; pagination with slices 10–20 ms.

### 7.7. Video

- Only a separate fb process, not an X window:`libav*`+ ALSA + fb, rotate
  180° in blit,`FBIOPAN_DISPLAY`with a dirty rectangle.
- Before start: stop MPD (`stop`), otherwise ALSA busy; after leaving
  resume automatically.
-`oom_score_adj=-1000`,`wake_lock`, return highlight/blank on exit.
- Blocker: 10-minute pan test. Not yet passed - in the UI there is only “Video”
  unavailable” with a reason, without promises.

---

## 8. UX for 480x800 and finger

### 8.1. Strict rules

| Rule | Number | Why |
|---|---|---|
| Minimum Goal | 44px | less - a miss on a resistive screen |
| Basic buttons | 56–72 px | 44 px = 5.2 mm, not enough for the main thing |
| List string | 56 or 64 px | 12–13 lines per screen |
| Text | ≥11 px, body 15–16, header 24–30 Light | Metro |
| Long-press | 650ms, undo on offset >14px | already in`app.cpp`|
| Scroll | drag 1:1, no inertia | poll 200 ms, inertia “leaves” |
| Animations | no | 1 GHz + fbpan 10 Hz |
| Accent | one per screen | Metro |
| Swipe by line | **banned** | false positives when scrolling |
| Multitouch | no | iron |

### 8.2. Gestures by application

| Application | Tap | Long-press | Drag | Swipe |
|---|---|---|---|---|
| Player | play/pause, seek-strip, pivots | — (P1: track curtain) | seek (P0), vertical on cover - no | by pivot - page change |
| Queue | play | curtain: play/next/above/below/delete/info | scroll | by pivot |
| Terminal | keys, scroll buttons | — | scroll | — |
| Notes | place cursor | highlight word (P2) | scroll text | — |
| Files | open/login | multi-select entry | scroll | — |
| Browser | given to the engine | — | given to the engine | — |
| Reader | x<120 back, x>360 forward, center - HUD | — | — | ←/→ page |
| Video | overlay | — | seek bottom | down - exit |

Global shell gestures: swipe from the left edge (x<20, >60 px) - back;
swipe from above (y<40, >60 px) – curtain. They conflict with the reader and
browser - in these screens edge-back must be disabled or the threshold raised.

### 8.3. Screens and states

Each application must have:

1. **Empty:** one line 15 dim + one action accent 44 px
   (“Open Library”, “Scan”, “Create Note”).
2. **Loading:** text 12 dim, without spinners (“Library update…”,
   “Marking 42%”, “Launch...”).
3. **Error:** what happened + what to do (“MPD unavailable” + “Retry”
   in 3 s", tap - now).
4. **No media:** “SD card not found” + “Insert card.”
5. **No rights/system partition:** “Read only” (files).
6. **Tripped:** "..." and, where important, a second line/toast with the full meaning.

### 8.4. Texts (short, without “soon”)

| Situation | Text |
|---|---|
| Queue empty | “Queue empty” · “Open library” |
| There is no library | “There is no music in the library” · “Update database” ·`/media/sd/Music`|
| No cover | "NO COVER" |
| MPD not available | "MPD unavailable" · "Retry in 3 sec" |
| Browser without JS | “Sites without JavaScript. Many pages will not open" |
| The file does not open | “There is no application for this file type” · “Open with...” |
| Removal | “Delete 3 objects?” · “No trash” |
| Copy | "Copy 12.4 MB of 340 MB" · "Cancel" |
| Note not saved | "Failed to save" · path |
| Broken book | “Could not parse” · “Open as text” |
| Video | "Video unavailable: Screen test failed" |

### 8.5. What not to do in UX

- Swipe by line (delete/archive) - curtain/button only.
- Pinch, turn, “long tap + drag” as the only way.
- Animations of appearance, “rubber band”, parallax.
- Modal dialogues for each action: toast + undo is better.
- Icons without signatures in the lower bar (11 px signature).
- Horizontal scrolling, except in obvious places (terminal, wide tables).

---

## 9. Roadmap

### 9.1. Immediately after Stage 1 (1–1.5 weeks)

These are not “features”, but removal of blockers for all applications:

1. **Application registry.** Scan`.desktop`(user/local/system),`Exec`,`TryExec`,`NoDisplay`,`Terminal`, PNG/fallback icons. Now at
   “Applications” 5 hardcoded lines.
2. **Environment for GTK.** One`dbus-daemon --session`,`XDG_RUNTIME_DIR`0700,`GTK_CSD=0`,`NO_AT_BRIDGE=1`,`XMODIFIERS=@im=none`,`LC_ALL=C.UTF-8`. Without this`pcmanfm`/`netsurf`may not open.
3. **Back/Home Policy** for external applications: Back = keyboard →
   Alt+Left/Esc → close; Home = collapse; don't kill by Back.
4. **The “Launching…” screen** with a timeout of 15 s and “Cancel”.
5. **Memory:** monitor`MemAvailable`once every 5 s, maximum 2 heavy ones,`oom_score_adj`(shell −900, active 0, background +300).
6. **OSK: terminal layer** (needed by the terminal, browser and search).

### 9.2. Wave A — player P0 (2.5–3 weeks)

`idle` (2–3 days) → covers (3 days) → index (5 days) → library
(5 days) → queue and actions (2 days) → resume and states (2 days).

Criterion: “play an album in 3 taps, see its cover, and resume at the same second after pausing.”

### 9.3. Wave B – P0 files (1.5–2 weeks)

Multi-select → operations (copy/move/delete/rename) →
sort/hidden → “open with” → properties/free space →
remove the 500 limit.

Criteria: “copy 20 photos from SD to /home/user/Pictures, see
progress, cancel, delete confirmation folder.”

### 9.4. Wave C - P0 Notes (1-1.5 weeks)

Cursor and edit in position (5–6 days) → list of notes (3 days) →
atomic autosave + undo (2 days) → scrolling (1–2 days).

Criteria: “open an old note, correct the word in the middle, exit,
return - the text is in place."

### 9.5. Wave D - browser (3–5 days)

Remove fake → launch dillo → Back/reload/home → OSK for address →
honest empty state. Address bar in shell, bookmarks and NetSurf -
P1, after.

### 9.6. Wave E - reader v1 (2–2.5 weeks)

Spike layout (1 day) → TXT + pagination + page map (5 days) →
reading screen + themes + font (3 days) → shelf + covers + resume (3 days) →
FB2+`fb2.zip`(4 days) → acceptance of 10 real books (1 day).

Criteria: “FB2 1 MB opens in 1 s, scrolls ≤150 ms, position
is undergoing a relaunch, TOC leads into the chapter.”

### 9.7. P1 (after P0, 3–4 weeks, can be parallelized)

Player: search, playlists, favorites, folders, recently, sleep timer, button
headsets. Notes: search, markdown preview, pinning. Files: archives,
progress/cancel, recursive search, image previews, crumbs. Browser:
address bar, bookmarks, NetSurf. Reader: EPUB, bookmarks, search,
hyphenations, footnotes. Terminal: paste, copy.

### 9.8. P2 and "later"

- PDF (`mupdf-x11`separately or`pdftoppm`) — 3–5 days, only when
  real request.
- Video - after the pan test; 2–3 weeks, separate fb mode.
- Crossfade, ReplayGain, consume, album grid, video landscape,
  hardware decoder.
- Equalizer is the very last point of the project (owner’s decision).

### 9.9. Don't do it at all

| What | Why |
|---|---|
| Your own browser engine / JS | impossible and unnecessary |
| Your own VT100 / PTY emulator | months, xterm is already there |
| Your video decoder or the path to QDSP5 in v1 | separate project, risk of reboot |
| Rich text / WYSIWYG in notes | breaks the file-based approach |
| Synchronization, accounts, cloud | no network of trust and services |
| Scrobbling, lyrics, TTS, OPDS | no network/engine, wrong device class |
| Trash and permissions on vfat | the filesystem does not provide them |
| Multitasking, Android style | 400 MB, maximum 4 tasks |

---

## 10. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| 1 | Memory: GTK browser + X + shell | high | OOM | max 2 heavy, LRU-kill, dillo by default |
| 2 | X/`msm_fb` hangs during a long session | medium | blocker | 10-minute soak, watchdog, fallback to fb player |
| 3 | Panel at 24–30 pans/s (video) | medium | reboot | stage 0 before code; kernel patch if necessary |
| 4 | Resistive touch: long-press vs scroll | high | UX | 650 ms/14 px, no line swipes |
| 5 | vfat: no rights/cart, case-insensitive | fact | operations | write only to permitted roots, honest texts |
| 6 | Power loss during a write | medium | data loss | tmp + rename + fsync, debounce |
| 7 | `listallinfo` for 5,000–25,000 tracks | medium | memory/time | index + `metadata_to_use`, traverse by directory |
| 8 | AGPL`mupdf`when linking in shell | high | legal | only a separate process |
| 9 | XTest/Unicode and applications with XIM | average | input |`XMODIFIERS=@im=none`, US-keycode for Ctrl |
| 10 |`dillo`without JS: “the Internet is not working” | fact | expectations | honest text, NetSurf as an option |
| 11 | Root shell and writing to system paths | average | brick | whitelist roots, read-only on`/etc`|
| 12 | JPEG/PDF decoding on the UI thread | high | stutter | background thread/cache/slices |
| 13 | Caches on rootfs (1.1 GB) | low | disk space | limits 30/32 MB, clear in settings |
| 14 | Reader: broken FB2/EPUB | high | "does not open" | recovery + emergency`<p>`-extractor |
| 15 | Volume keys in reader vs player | medium | duplicate actions | key-ownership matrix: Volume pages in the reader |
| 16 | `apphost` takes the first window by pid (1×1 helper) | high | app appears not to open | filter by window size/type; wait for `MapNotify` on the correct window |

---

## 11. Three main conclusions

1. **The problem is not the number of features, but unfinished basic workflows.**
   The player cannot play an album, Notes cannot edit the middle of a document,
   and Files cannot copy. Until these three things work, every new feature
   (search, playlists, EPUB) only widens the gap between what is promised and what works.
2. **Build only the shell and integration ourselves.** `xterm`, `dillo`, and
   `mupdf-x11` handle terminal, browser, and PDF better and more cheaply than
   custom implementations. Their style and focus are compromises to address with
   colors, fonts, the shell header, and keyboard—not by rewriting the engines.
3. **The 400 MB budget dictates the architecture more than 1 GHz.** Our Xlib
   screens (player, files, notes, reader) use 10–20 MB; any GTK app uses
   30–100 MB. Build an in-house screen wherever practical; keep external only
   what requires an irreplaceable engine.

## What to do first

1. **Finish Stage 1** (`.desktop` registry, D-Bus/GTK environment, Back policy,
   “Launching…” screen, OSK terminal layer, memory management). 1–1.5 weeks.
   Without this, every subsequent app will run into the same issues.
2. **Player P0**:`idle`→ covers → index → ​​library. 2.5–3 weeks.
   This is the most noticeable increase in the “feeling of the product” per unit of work.
3. **P0 files**: multi-select + copy/move/delete/rename. 1.5–2 weeks.
   Transforms the device from a “dialer with a player” into a tool.
4. **P0 Notes**: cursor, list, atomic save. 1–1.5 weeks.
5. **Browser** - 3-5 days to launch dillo, without its own UI.
6. **Reader v1** - 2–2.5 weeks, after stabilization of the previous three.
7. **Video and PDF** - not before the pan test has been passed and the real one has appeared
   request; and never by linking AGPL to the shell.

---

## Sources

**Project code and documents:**`os/screens/player.cpp`,`queue.cpp`,`info.cpp`,`terminal.cpp`,`notes.cpp`,`files.cpp`,`browser.cpp`,`os/src/mpd.cpp`,`apphost.cpp`,`osk.cpp`,`app.cpp`,`notes/PLAYER-FEATURES.md`,`READER-PLAN.md`,`VIDEO-PLAN.md`,`APP-INTEGRATION-PLAN.md`,`SETTINGS-SPEC.md`,`shell/SCREENS.md`,`SHELL-TODO.md`,`METRO-RESEARCH.md`.

**Market:**
- MPD clients: <https://www.musicpd.org/clients/>, rmpc, Sonata, Euphonica,
  Maximum MPD.
- Terminals: Termux manual <https://github.com/termux/termux-tools>,
  TermRover <https://termrover.sh/blog/best-mobile-terminal/>, Mobile SSH
  <https://mobile-ssh.github.io/docs/terminal/>.
- Notes: QuackNotes <https://quacknotes.app/>, Zettel Notes
  <https://www.zettelnotes.com/>, Standard Notes (XDA, 2025).
- Files: Android File Explorer <https://fileexplorer.org/>, Flux, FileNo,
  Sally.
- Browsers: NetSurf <https://www.netsurf-browser.org/>, Dillo manual
  <https://dillo-browser.org/user_help.html>, Dillo 3.3.0 release notes.
- Readers: Hetko <https://hetko.eu/apps/reader>, UZYTAK, Librera,
  Beresta, Aurora Reader.
- Video: Alpa Player checklist, VLC Mobile Design
  <https://wiki.videolan.org/Mobile_Design_Player/>, MX Player features.

**Alpine packages (edge/armv7):** xterm 412-r0, st 0.9.3, dillo 3.3.0-r3,
netsurf 3.11-r1, mupdf-x11 1.28.5-r1, libxml2 2.13.9-r3, libzip 1.12.1-r0,
libarchive 3.8.11-r0, sqlite-libs 3.54.1-r0, taglib 2.2.1-r0 —
<https://pkgs.alpinelinux.org/packages?arch=armv7>.
