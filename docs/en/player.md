# PLAYER-FEATURES — LG P700 player functionality

> Public technical plan. **[verify]** marks estimates that require measurement;
> data paths are represented by placeholders.

> Compiled on 24.11.2026 from read-only facts collected from the phone: MPD 0.24.20,
> `/etc/mpd.conf`, `mpc`, `idle`, `readpicture`, `evtest`, `/proc`. No code was changed,
> MPD settings were not touched, and playback was not started during the audit.
> Where a conclusion is based on MPD capabilities rather than measurement, it is marked **[verify]**.

This document answers the question “what should the player do, and in what order?”.
Current hardware and system state: `PLAYER-STATUS.md`, `SYSTEM-STATUS.md`,
design: `notes/shell/DESIGN-SYSTEM.md` / `SCREENS.md`.

---

## 0. Summary

1. **The backend is already in place and fully featured.** MPD 0.24.20 supports everything
   the player needs: `idle`, filters and search, `readpicture`, playlists, a queue with
   positions, `random/repeat/single/consume/crossfade/replay_gain_mode`, and partitions.
   Only **stickers** (database disabled) and **playback speed** (no command at all) are missing.
   Therefore all the work is client-side.
2. **The main constraint is not features, but the 1 GHz / 400 MB / touch polled at ~200 ms budget.**
   Therefore the library is built once into a compact on-disk index, cover art is decoded
   outside the UI thread and cached, and screen updates use `idle` rather than polling.
3. **The player currently looks less capable than it is because of three specific issues:**
   embedded cover art is never displayed (`albumart` instead of `readpicture`), state is fetched
   with two requests once per second instead of `idle`, and the queue is reread in full.
   P0 fixes these and does more for the “feel of a player” than any new feature.

---

## 1. Current state and verified facts

### 1.1. Current player (`notes/lp7player.cpp`, ~1.5k lines)

- Screens: “Now”, “Queue”, “Info”, “Settings”, lock, power confirmation.
- MPD: one TCP socket, `status` + `currentsong` once per second, `playlistinfo`
  when opening the queue, `albumart` + reading `cover.jpg` from SD, `seekcur`,
  `setvol ±5`, `random`/`repeat` as two independent toggles.
- Cover art is decoded **synchronously on the UI thread** when the track changes.
- Settings: `<system-settings>` (`theme`, `btnlight`), writes are non-atomic.
- Keys: `event1` (Vol±, Home), `event5` (Vol±, Power), holding Vol ≥0.7 s — next/prev.

### 1.2. MPD on the phone — verified

| Parameter | Fact |
|---|---|
| Version | `mpd 0.24.20`, protocol `OK MPD 0.24.0` |
| Config | `music_directory /media/sd/Music`, `playlist_directory <mpd-data>/playlists`, `db_file tag_cache`, `state_file state`, `bind <loopback>:6600`, `auto_update yes`, `mixer_type software`, `hw:0,0` |
| Decoders | mad, vorbis, flac, opus, faad, wavpack, ffmpeg, pcm |
| Filters | libsamplerate, soxr |
| Playlist plugins | extm3u, m3u, pls, xspf, cue, embcue |
| Features | `inotify` (automatic updates work), `icu` (Cyrillic search), `sqlite` linked in |
| Tags | Artist, AlbumArtist, Album, Title, Genre, Date, Track, Disc, Composer, MusicBrainz* — all present |
| `idle` | works: event `changed: options` arrived **4 ms** after the change; client does not time out while idle |
| Latencies | `status` — 0.8 ms, `status`+`currentsong` — 5.5 ms, 20 pairs = 111 ms |
| `listallinfo` | 23 lines for 2 tracks in 20 ms (extrapolation for 5k tracks, see §6.4) |
| `readpicture` | embedded cover art: `size: 300007`, `type: image/jpeg`, returned in full in 141 ms with `binarylimit 1048576` (without it, in 8192 B chunks) |
| `albumart` | On the same files — `ACK No file exists`. This is **not** embedded cover art; it searches for `cover.*` in the directory |
| Stickers | `ACK sticker database is disabled` — no `sticker_file` in the config |
| Search | `search any "\u0433\u0440\u0430\u043D\u0436"` finds `Genre: \u0413\u0440\u0430\u043D\u0436` — case-insensitive, ICU |
| Filters | `find "(Title contains \"\u044E\u0440\u0430\")"` — **no match**; `=~ "\u044E\u0440\u0430"` — no match, `=~ "\u042E\u0440\u0430"` — match. `contains` and `=~` are case-sensitive |
| Playlists | directory empty, `listplaylists` → OK |
| State | `state` stores the queue, current index, modes, and `sw_volume`; **position within the track is not saved** |
| RAM | MPD RSS 11.4 MB (2 tracks), 3 threads; 114 MB free in the system, 215 MB in cache |
| Disk | root — 1.1 GB free, SD — 29.4 GB, SD read ≈ 20 MB/s |
| CPU | 1 GHz, governor `ondemand`, current frequency 1 GHz |

### 1.3. Hardware — verified

| Component | Fact |
|---|---|
| `event0` | touch, MT-B, X 0..480 / Y 0..800, coordinates already in screen orientation |
| `event1` `u0_keypad` | KEY_HOME 102, KEY_VOLUMEDOWN 114, KEY_VOLUMEUP 115 |
| `event5` `7k_handset` | KEY_END 107, VOL± 114/115, KEY_POWER 116, **KEY_MEDIA 226** |
| `event4` `h2w` | **KEY_MEDIA 226** (headset button) + SW_HEADPHONE_INSERT + SW_MICROPHONE_INSERT |
| Jack | `/sys/class/switch/h2w/state` = 1 (headset with microphone); `evtest` on event4 shows `SW_HEADPHONE_INSERT state 0` — kernel detection is partially broken, sysfs is authoritative (as `lgjack` does) |
| Volume | ALSA control `PCM Playback Volume` (-50..+18 dB) exists, but reads/writes return `Operation not permitted` → **no hardware volume, software only** |
| Output | `hw:0,0`, files tagged 44.1 kHz / 24 bit, MPD converts for the device |
| BT A2DP | does not work (see `BT-status.md`) — do not promise it in the UI |

### 1.4. Implications

- MPD is the source of truth. The client does not store the queue, modes, or volume itself:
  it reads them from `status` and changes them with commands.
- A full library is **not requested from MPD on every screen**: one `listallinfo` pass →
  compact on-disk index → instant lists.
- Cover art comes from `readpicture` (embedded), then `cover.jpg`/`folder.jpg` from the file's
  directory; do not use `albumart`.
- User search uses only `search`/`searchcount`/`searchadd` (case-insensitive). `find` is for
  exact tag equality.
- Track position and “resume listening” are our responsibility: MPD does not remember it.

---

## 2. Client architecture

### 2.1. Two MPD sockets

```
control  — commands and short responses (1 s timeout, reconnect)
idle     — idle/noidle only, read with select(), reconnect independently
```

Why two: one connection cannot wait for `idle` and send commands at the same time. The code
currently has one `Mpd`, and `cmd()` breaks the connection on any error — this will become
visible after switching to idle.

Rules:
- `connect` → read greeting → `binarylimit 1048576` (otherwise cover art arrives in 8 KB chunks) → `idle player mixer options playlist database
  stored_playlist output update`.
- Event arrives → parse `changed: X` lines up to `OK` → update what is needed →
  **send `idle` again immediately**.
- Disconnect (`recv` ≤ 0) → close, backoff 1→2→5→10 s; on reconnect reset the queue cache
  and reread `status`, `currentsong`, and modes.
- While playing, position is extrapolated locally from `elapsed` + `CLOCK_MONOTONIC`
  and checked against `status` every 5 s (0.5 ms — cheap). The progress bar is
  redrawn once per second, without MPD requests.
- If `idle` is unsupported (ACK) — degrade to 1 s polling, without crashing.

### 2.2. Library index

One `listallinfo` pass (or per directory if the library is huge — see §6.4) → parse →
**text cache** `<player-data>/index.txt`:

```
#v1 <db_update> <songs> <albums> <artists>
A<TAB>album_id<TAB>Album<TAB>AlbumArtist<TAB>year<TAB>discs<TAB>tracks<TAB>duration
R<TAB>artist_id<TAB>Artist<TAB>albums<TAB>tracks
T<TAB>uri<TAB>title<TAB>artist_id<TAB>album_id<TAB>genre<TAB>year<TAB>track<TAB>disc<TAB>sec
```

- Album key — `(AlbumArtist || Artist, Album)`, for compilations
  (`AlbumArtist` = “Various Artists”) — `Album` only.
- `db_update` from `stats` is the cache version. If it matches, read the file (5k lines
  ≈ 600 KB, parsing ~30 ms). If not, rebuild in the background.
- Rebuild runs on a separate thread; UI shows “Updating library…”, while the list remains
  available from the old index.
- Empty tags: no `Album` → “Unknown album”, no `Artist` → “Unknown artist”, no `Title` →
  filename without extension and track number.
- The index is needed only for lists; playback and the queue work without it.

### 2.3. Cover art

```
1) readpicture(uri, 0)            — embedded (after binarylimit)
2) <dir>/cover.jpg|folder.jpg|front.jpg|albumart.jpg  — file next to track
3) placeholder ♪ / “NO COVER ART”
```

- Decoding runs **in a background thread**; UI first draws the placeholder and replaces it
  with cover art when ready. Synchronous `stbi_load` on a 300 KB JPEG blocks the interface
  for hundreds of milliseconds — that is what happens now.
- Thumbnail cache: `<player-data>/art/<hash>.565`, 96×96 RGB565 = 18 KB.
  500 albums ≈ 9 MB on rootfs (not on SD: the card is removed).
- Large 384×384 cover art is not cached on disk, but is held in RAM for the current album
  (≈290 KB RGB565) and decoded once per album, not once per track.
- Loading is lazy: visible rows only, one at a time, current item first.
  Bulk preloading 300 albums takes minutes of CPU at 1 GHz; do not do this.
- Cache limit — 30 MB, LRU eviction, “Clear” button in settings.

### 2.4. State files

| Path | Contents | Size |
|---|---|---|
| `<player-data>/index.txt` | library index | 0.5–1 MB per 5k tracks |
| `<player-data>/art/*.565` | thumbnails | up to 30 MB |
| `<player-data>/played.log` | listening history (uri, ts) | rotation at 2000 lines |
| `<player-data>/bookmarks` | uri → second (audiobooks) | a few KB |
| `<player-data>/lastpos` | uri + elapsed every 15 s | <1 KB |
| `<system-settings>` | theme, backlight, player preferences | <1 KB |

Settings and bookmarks must be written atomically (tmp + `rename`), otherwise power-off
will leave a truncated file.

---

## 3. Feature matrix

Complexity: **S** — up to 1 day, **M** — 2–3, **L** — 4–6, **XL** — 7+ (estimate for
this codebase: hand-drawn UI, no toolkit, one developer).

### A. Playback and transport

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| play/pause/next/prev | basics | `pause`, `play`, `next`, `previous` | 3 buttons 96 px | **P0** | done |
| seek | seek | `seekcur N` | 4 px bar, 14 px thumb, drag | **P0** | done |
| volume | basics | `setvol`, `status.volume` | hardware Vol± + `VOL N%` | **P0** | done |
| shuffle | random order | `random 0/1` | icon left, accent = on | **P0** | S |
| repeat off/all/one | repeat | `repeat 0/1` + `single 0/1` | icon right, 3-state cycle, “1” for “one” | **P0** | S |
| gapless | no gaps between tracks | automatic for same format | do not show, Info only | **P0** | 0 |
| resume on boot | “resume where I stopped” | `state_file` (queue+track) + our `lastpos` for seconds | “Resume position” setting | **P0** | S |
| crossfade | smooth transition | `crossfade 0/2/4/6` | settings, “Transition” section | P2 | M |
| ReplayGain | volume normalization | `replay_gain_mode off/track/album`, tags | settings, 3 segments | P2 | S |
| consume | remove played tracks | `consume 0/1` | settings only, off by default | P2 | S |
| sleep timer | fall asleep to music | our timers + `setvol`/`pause` | settings + lock-screen row | P1 | S |
| play/pause from launcher/shade | quick control | same commands | live tile, shade | P1 (shell) | S |

### B. Library

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| album list | main entry point | index (§2.2) | “Library” pivot, “Albums” tab, 64 px rows with 48 px thumbnail | **P0** | L |
| album screen + “Play” | play album | index + `find "(Album == …)"` | header, 160 px cover, 56 px tracks | **P0** | M |
| artists | navigation | index | “Artists” tab, 64 px rows, caption “N albums · M tracks” | **P0** | M |
| all songs | “play everything” | index | “Songs” tab, rows as in queue | **P0** | S |
| index + disk cache | speed | `listallinfo`, `stats.db_update` | “Updating…” in header | **P0** | L |
| cover art | recognition | `readpicture`, `cover.jpg` | 48 px in list, 384 in “Now” | **P0** | M |
| cover-art cache | speed | our files | invisible, counter in settings | **P0** | S |
| folders | bad tags, SD structure | `lsinfo ""`, `lsinfo dir` | “More” tab → “Folders” | P1 | S |
| search | find a track | `search any`, `searchcount` | separate screen + keyboard | P1 | M |
| quick letter navigation | long lists | our index | alphabet rail 6×5 | P1 | S |
| sorting | personal preference | our index | “A–Z / by year / by date added” | P1 | S |
| recently played | return to playback | our `played.log` | “More” tab → “Recent” | P1 | M |
| compilations / Various Artists | keep grouped | `AlbumArtist` | grouped according to §6.4 | P1 | S |
| multi-disc | track order | `Disc` tag | sort disc→track | P1 | S |
| genres | rare use case | `list Genre` | “More” tab → “Genres” | P2 | S |
| most played | “my favorites” | `played.log` | “More” tab | P2 | S |
| album grid (2 columns) | visual | — | view switch | P2 | M |

### C. Queue

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| view + tap to play | basics | `playlistinfo`, `play N` | 64 px rows, current in accent | **P0** | done |
| current by `Id`, not position | avoid random-order confusion | `status.songid`, `playid` | triangle beside row | **P0** | S |
| “Play next” | primary use case | `addid uri (song+1)` | track action sheet | **P0** | M |
| “Add to queue” | build up queue | `add uri` | track action sheet | **P0** | S |
| delete track | cleanup | `deleteid id` / `delete pos` | row action sheet | **P0** | S |
| clear | start over | `clear` | “⋯” in queue header | **P0** | S |
| persist across boots | keep queue | `state_file` (already present) | invisible | **P0** | 0 |
| queue of 100+ | avoid slowdown | `plchangesposid` + visible rows | smooth drag | **P0** | M |
| shuffle queue | one-time | `shuffle` | “⋯” | P1 | S |
| reorder | order | `moveid`, `move` | action sheet: “Up/Down” | P1 | M |
| save as playlist | carry over | `save NAME` | “⋯” + name entry | P1 | S |
| drag-reorder | familiar interaction | `moveid` | long press + drag | P2 | L |
| “Play everything after current” | album next | loop using `addid` | album action sheet | P1 | S |

### D. Playlists and favourites

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| list and play | basics | `listplaylists`, `load` | “More” → “Playlists” | P1 | S |
| create from queue | save selection | `save` | “⋯” in queue | P1 | S |
| rename/delete | organization | `rename`, `rm` | playlist action sheet | P1 | S |
| add track to playlist | curate | `playlistadd` | track action sheet → choose playlist | P1 | M |
| favourites | offline “like” | `Favorites` playlist (§6.6) | ♥ on “Now”, row in Info, “More” → “Favourites” | P1 | M |
| import m3u from SD | transfer from PC | `load`, m3u plugin | “More” → “Import” | P2 | S |
| smart playlists | “everything from the 90s” | our index | rules screen | P2 | L |

### E. Info

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| tags, format, bitrate, path | already present | `currentsong` | 42 px rows | **P0** | done |
| output format | “what is actually playing” | `status.audio` | OUT row | P1 | S |
| added / last played / plays | useful | `Added`, `played.log` | 3 rows | P1 | S |
| actions: ♥, add to playlist, play next | no need to find action sheet | commands | 3 rows 52 | P1 | S |
| track/album ReplayGain | diagnostics | tags | RG rows | P2 | S |
| file size, codec | diagnostics | `ls`/`Format` | rows | P2 | S |

### F. Settings

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| theme, backlight, lock, power | present | `<system-settings>` | as now | **P0** | done |
| playback | modes in one place | `random/repeat/crossfade/RG` | section with segments | P1 | S |
| library: update, cache, counters | maintenance | `update`, `stats`, our files | 3 rows | P1 | S |
| sleep timer | convenience | our timers | 15/30/45/60 segments | P1 | S |
| equalizer | audio | AUDPP (last project item, 25.11) | do not show | later | XL |
| diagnostics (RTT, idle, RAM, cache) | hardware debugging | our counters | hidden row | P1 | S |

### G. Hardware

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| short/long Vol± | present | `setvol`, `next/previous` | — | **P0** | done |
| Power/Home/lock | present | — | — | **P0** | done |
| headset button | avoid taking out phone | `event4` KEY_MEDIA 226 → `pause/play`, double `next`, triple `previous`, long `stop` | no UI (hint in settings) | P1 | S |
| duplicate KEY_MEDIA on event4/event5 | avoid double switching | 50 ms deduplication | — | P1 | S |
| pause when headphones are removed | avoid playing into silence | poll `/sys/class/switch/h2w/state` every 0.5 s | settings toggle, on by default | P1 | S |
| vibration on long press | tactile feedback | `/sys/class/timed_output/vibrator` | 30 ms | P2 | S |
| BT A2DP | — | blocked | do not show | — | — |
| headphone routing | works | `lgjack` | do not duplicate logic | — | — |

### H. Reliability

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| reconnect + backoff | MPD stops/starts | two sockets | “MPD unavailable · retrying” | **P0** | M |
| empty states | avoid confusion | — | texts §5.10 | **P0** | S |
| no SD | common failure | `stat("/media/sd")`, `mount` | “SD card not found” | **P0** | S |
| corrupt file/tag | library garbage | `status.error` | “File cannot be read” row | **P0** | S |
| MPD does not start without SD | avoid idle service | `/etc/local.d/mpd.start` exits if directory is absent | — (script change, not player code) | **P0** | S |
| atomic settings write | power-off | tmp+rename | — | P1 | S |
| player watchdog | freeze | — | restart ≤3 times | P1 | S |
| recovery after MPD is killed | — | clear caches, `status` | toast row | P1 | S |

### I. Equalizer (last project item, not a release item)

| Feature | Purpose | MPD / data | UI | P | Complexity |
|---|---|---|---|---|---|
| AUDPP on/off + 5 bands | audio | `/dev/msm_pcm_ctl`, `AUDIO_ENABLE_AUDPP`, `AUDIO_SET_EQ` | separate screen from settings | P2 | XL |
| presets (FLAT/ROCK/VOCAL/BASS/TREBLE) | quick selection | coefficients in code | 5 segments 88×44 | P2 | M |
| reapply after format change | DSP resets | `idle player` → repeat ioctl | invisible | P2 | M |

---

## 4. First release scope (P0)

### 4.1. Included

1. **MPD layer**: two sockets, `idle` with re-send, backoff, `binarylimit`,
   operations by `Id`, incremental queue via `plchangesposid`.
2. **Cover art**: `readpicture` → adjacent file → placeholder; background decoding;
   96×96 disk cache; current album cached in RAM.
3. **Library index** + disk cache, invalidation via `stats.db_update`,
   background rebuild.
4. **Screens**: “Library” (Albums / Artists / Songs), “Album”, “Artist”; actions
   “Play”, “Add to queue”, “Play next”; long-press action sheet.
5. **Queue**: “Play next”, “Add to queue”, delete, clear, “⋯” menu,
   correct current item by `id`, 100+ tracks.
6. **Modes**: shuffle + repeat off/all/one with accurate indication.
7. **Resume**: verify `state_file`; own `lastpos` for seconds; setting.
8. **States and errors**: MPD unavailable, empty queue/library, no SD,
   corrupt file, no cover art.
9. **Startup**: wait for MPD up to 10 s, do not treat this as an error; do not try
   to start MPD from the UI.

Estimate: **~18–22 days** for one developer. To fit into two weeks,
cut “Artists” (−1.5 days), disk cover-art cache (−1), “Play next” (−1) —
but then the player is no longer “full-featured”.

### 4.2. Intentionally excluded

| Item | Reason |
|---|---|
| Search | Requires an on-screen keyboard; the framebuffer player has none, the shell does. P1 together with keyboard |
| Playlists and favourites | Requires name entry and a list; P1 immediately after P0 |
| Sleep timer | Does not affect “listen to music”; P1 |
| Headset button | P1, verified with one `evtest` |
| Crossfade | 1 GHz CPU, two decoders; stutter risk. P2 after measurements |
| ReplayGain | Few files have tags; P2 |
| Genres, most played, album grid, drag-reorder | P2 |
| Folders, “recent”, sorting, alphabet | P1, immediately after P0 — do not affect the basic use case |
| Equalizer | AUDPP not analyzed, noise in tests (`EQ-STATUS.md`) |
| Playback speed | **MPD does not support it** — no command in `commands` |
| Last.fm | No network or account; local `played.log` only |
| BT output | A2DP does not work |
| Album grid, drag-reorder | P2, after the list proves convenient |

### 4.3. Work order

```
1. MPD layer (idle, reconnect, ids)              3 days
2. Cover art (readpicture, background, cache)    3 days
3. Library index + cache                        5 days
4. Library: albums/artists/songs                 5 days
5. Queue: play next/delete/clear                 2 days
6. Modes + resume                                2 days
7. States/errors + acceptance                   2 days
```

Each step ends with a working screen — not “everything first, UI later”.

---

## 5. UI

Below is the target appearance after P0+P1. P0 includes the library (albums,
artists, songs), album screen, queue, action sheets, and states; search,
playlists, ♥, and playback settings arrive in P1.

### 5.1. Frame

Do not break Metro: status 40, pivot 96 (30 ExtraLight title on the left,
other pivots 12 regular on the right, active pivot is the large title), content
from 136, no shadows/gradients/animations, one accent.

Four pivots: **Now · Queue · Library · Info**. At 12 px widths:
“QUEUE” ≈ 52, “LIBRARY” ≈ 78, “INFO” ≈ 31, gaps 14 → 189 px on the right,
“Now” title ≈ 100 px on the left. Fits comfortably.

At the right edge of the pivot is “⋯” (24 px, 44×44 hit area) on “Queue”,
“Library”, and “Playlist” screens. There is none on “Now”.

Pivot tap targets are 44 high (y 40..96), not just the text.

### 5.2. “Library”

```
40      status
40-136  pivot: “Library” (large) + NOW/QUEUE/INFO + ⋯
136-184 tab bar: ALBUMS · ARTISTS · SONGS · MORE
184-800 list, 64 px rows (≈9.5 rows per screen)
```

- Tabs 12 caps, active accent + 2 px bottom line (y=182). Widths:
  ALBUMS 58, ARTISTS 92, SONGS 44, MORE 38; gaps 20; all within 20..312.
  On the right side of the bar — “A→Z” button (alphabet, P1) or “⌕” (search, P1).
- **Album**: 48 px thumbnail at (16, +8); title 15 bold x=76 baseline +26;
  caption 12 dim “Artist · 2023 · 12 tracks” +45; chevron on right.
- **Artist**: name 15 bold x=20; caption “3 albums · 34 tracks”.
- **Song**: number 12 num, title 15, artist 12 dim, time 12 on right —
  exactly like the queue, to avoid multiplying styles.
- No cover art — thumbnail `art` with 16 px `note` symbol.
- Scrolling — drag, 2 px scrollbar (as in queue). No inertia.
- “MORE”: 56 px rows — Playlists, Folders, Genres, Recently played,
  Favourites, Update library (last item with caption “126 tracks”).

### 5.3. “Album” / “Artist”

Separate screen with 96 px app header (back chevron + “ALBUM” caption,
11 caps + 30 light title, truncated with “…”) .

```
148  cover art 160×160 (20,148)
196  title 20 light (196,…), artist · year · 12 tracks 13 dim
224  [ PLAY ] 240×56 accent      [ ADD TO QUEUE ] text 13 accent
320  tracks, 56 px rows: number 12 num, title 15, time 12 on right
```

- “PLAY” = `clear` + `add` all tracks + `play 0` + switch to “Now”.
- Long press on “PLAY” (or “⋯”) → action sheet: “Shuffle and play”,
  “Play next”, “Add to queue”.
- Current track — accent triangle instead of number, as in queue.
- “Artist”: name, “All tracks” (first row), then albums in 64 px rows.

### 5.4. “Queue”

- Pivot: title “Queue”, caption “8 TRACKS · 34:12”, pivots, “⋯”.
- “⋯”: Clear, Shuffle, Save as playlist. Destructive actions have no dialog,
  but have an explanatory caption in the action sheet (“Clear queue”).
- Tap = play, long press (650 ms) = row action sheet:
  Play · Play next · Up · Down · Delete · Info.
- No row swipes: the resistive screen and drag scrolling cause false triggers.
  Reorder using action-sheet buttons (P1), drag is P2.
- Opens on the current track (already implemented), but only if current is off-screen.

### 5.5. Playlists and favourites

- “MORE” → “Playlists”: 64 px rows, name 15 bold, “12 tracks · 48:20” 12 dim;
  “Favourites” and “Recently played” pinned at the top.
- Long press: Play · Add to queue · Rename · Delete.
- “Add to playlist…” from the track action sheet: playlist list + “New
  playlist…” (name entry — keyboard, P1 together with search).
- ♥ on “Now”: **centered between shuffle and repeat** (240, 552), while `VOL N%`
  moves to the time row (it is already there in the old layout). This is the
  only change to the approved screen, and it is a purposeful one.
  Filled heart — accent, empty — `dim`, hit area 44×44.

### 5.6. Search (P1)

- 48 px row under pivot: “⌕ Artist, album, or track” (dim). Tap — search screen.
- Keyboard — same as in “Notes” (`m-notes`): 4 rows × 42 + 42,
  QWERTY/ENG, DEL/SPACE/ENTER, 240 px high at bottom.
- Query: ≥2 characters, 300 ms debounce, `search any` + `searchcount`.
- Results: “ALBUMS” (up to 3, 64 px rows) and “TRACKS” (up to 100, 56 px rows),
  above that — “Show all” (opens a scrollable list).
- Empty — “No results”; MPD error — “MPD unavailable”.

### 5.7. “Info”

Keep the structure, add:
- “OUTPUT” row — `status.audio` (44.1 kHz · 16 bit · stereo);
- “ADDED”, “PLAYED”, “TIMES” — from `Added` and `played.log`;
- “ACTIONS” section: “Add to favourites” (♥/♡), “Add to playlist…”, “Play next”;
- for long files (>20 min): “RESUME AT 12:34” instead of “From the beginning”.

### 5.8. “Settings”

Sections from top to bottom (56 px rows, 44 px segments):
1. PLAYBACK: “Shuffle” (toggle), “Repeat” (OFF/ALL/ONE),
   “Transition” (OFF/2/4/6 s), “ReplayGain” (OFF/TRACK/ALBUM),
   “Resume position” (toggle), “Pause when headphones are removed”.
2. SLEEP TIMER: OFF/15/30/45/60 + “12:34 remaining” on right.
3. LIBRARY: “Update database”, “Cover art: 9.2 MB · Clear”,
   “126 tracks · 12 albums · 34:12:00” (from `stats`).
4. ~~EQUALIZER~~ — do not show the row at all: EQ is at the very end of the project.
5. THEME, KEY BACKLIGHT, SYSTEM, STARTUP (as now).

Modes (`random/repeat/crossfade/RG`) are read from `status` and written with
MPD commands, not to a file: MPD saves them in `state` and restores them itself.

### 5.9. Action sheets

A new but native element: bottom panel 480×(40+56·N), `bg` background, `section`
top line, 62% dim overlay over the rest, appears instantly.
11 caps `dim` heading (e.g. truncated track title), 56 px rows,
optional 24 px icons. Tap outside closes. Maximum 6 rows (376 px).

### 5.10. Empty states and errors (text)

| Situation | What to show |
|---|---|
| Queue is empty | “Queue is empty” + accent link “Open library” |
| Library is empty | “No music in library” + “Update database”, caption `/media/sd/Music` |
| No SD | “SD card not found” + “Insert card and update” (transport dim) |
| MPD unavailable | “MPD unavailable” + “Retry in 3 s” (automatic), tap to retry now; progress and times hidden, cover art and text of last track remain, but dim |
| Database update in progress | “Updating library…” in pivot caption |
| File cannot be read | one 12 dim row under title: “File cannot be read” |
| No cover art | note + “NO COVER ART” (present) |
| No results | “No results” centered |
| Playlist is empty | “No tracks in playlist” |

---

## Translation notes

- `<loopback>` — network interface/address placeholder.
- `<mpd-data>` — MPD data directory placeholder.
- `<player-data>` — player data directory placeholder.
- `<system-settings>` — system settings file/path placeholder.
- No passwords or personal paths were present in the source. The Unicode escapes in the MPD search examples preserve the original Cyrillic query and tag values while keeping this translation ASCII-only.

## 6. Technical notes

### 6.1. Required MPD commands

| Task | Command |
|---|---|
| status | `status`, `currentsong` |
| queue | `playlistinfo [start:end]`, `plchangesposid <ver>` |
| play | `play <pos>`, `playid <id>`, `pause 0/1`, `next`, `previous`, `stop` |
| seek | `seekcur <sec>`, `seekid <id> <sec>` |
| add | `add <uri>`, `addid <uri> [pos]`, `findadd`, `searchadd` |
| remove/move | `delete <pos>`, `deleteid <id>`, `move`, `moveid`, `swap`, `shuffle` |
| clear | `clear` |
| modes | `random`, `repeat`, `single`, `consume`, `crossfade`, `replay_gain_mode` |
| volume | `setvol <0..100>`, `getvol` |
| library | `lsinfo`, `listallinfo`, `list <Tag>`, `find "(…)"`, `search`, `searchcount`, `stats`, `update`, `rescan` |
| cover art | `binarylimit <n>`, `readpicture <uri> <offset>` |
| playlists | `save`, `load`, `listplaylists`, `listplaylist`, `listplaylistinfo`, `playlistadd`, `playlistdelete`, `playlistmove`, `playlistclear`, `rename`, `rm` |
| events | `idle [player mixer options playlist database stored_playlist output update]`, `noidle`, `ping` |

Escape all string arguments: `\` → `\\`, `"` → `\"`. Batch
operations use `command_list_begin` / `command_list_ok_begin` (one round trip).

### 6.2. `idle`

Subscriptions and responses:

| Event | Action |
|---|---|
| `player` | `status` + `currentsong`; on track change → cover art, `played.log`, `lastpos` |
| `mixer` | update `VOL N%` (hardware buttons) |
| `options` | shuffle/repeat/crossfade/RG/consume |
| `playlist` | queue changed → `plchangesposid` |
| `database` / `update` | rebuild index (debounce 2 s), clear “Updating…” |
| `stored_playlist` | reread playlists |
| `output` | `outputs` (one for now) |

- `idle` does not drop on `connection_timeout` — documented and verified.
- While `idle` is running, no commands can be sent on that socket, only `noidle`.
- There is no “one second elapsed” event: we calculate progress ourselves.
- If the connection hangs (no data and no events for > 30 s while status is playing) — `ping`; if there is no response, reconnect.

### 6.3. Queue

- Queue position changes due to `random`/`shuffle`/`move` → **always address by `id`** (`playid`, `deleteid`, `moveid`); position is only for rendering.
- `status.playlist` is the queue version. `plchangesposid <ver>` returns only changed `cpos`/`Id`; get their metadata with `playlistinfo pos:pos+1` or from the local cache if the uri is already known.
- “Play next” = `addid <uri> <song+1>`; for stopped, position = end.
- “Play all” = `clear` + `add` (or `findadd`) + `play 0`; preferably as a batch command so the UI does not see an intermediate empty queue.
- `max_playlist_length` defaults to 16 384 — for “queue the entire library,” check `playlistlength` and warn.

### 6.4. Library and large volumes

- `listallinfo` for 5,000 tracks ≈ 1.5–2.5 MB and 0.3–0.8 s (extrapolated from 23 lines/20 ms; **verify** against a real library).
- `max_output_buffer_size` limit = 8 MB: for ~25,000 tracks, a single response may not fit. In that case, walk the directories: `lsinfo ""` → for each directory `lsinfo "<dir>"`, combining the results.
- `search` is a linear scan of the in-memory DB: 5,000 tracks ≈ 10–30 ms (**verify**), 50,000 are already hundreds of ms, so debounce and limit it.
- `find` with `contains`/`=~` is **case-sensitive** (verified): unsuitable for UI search, use only for exact matches (`Album == "…"`, `Genre == "…"`).
- MPD memory: 11.4 MB RSS with 2 tracks (mostly code and libraries). Expected 15–25 MB at 5,000 (**verify**). At 10,000+, enable `metadata_to_use` (remove Composer, Mood, MusicBrainz*); this saves both RAM and traffic. **Do not change the config without agreement.**

### 6.5. Tags and grouping

- Album: `AlbumArtist` (fallback `Artist`) + `Album`. For compilations (`AlbumArtist` = Various Artists), use `Album` only.
- `Track` may be `3` or `3/12` → take the number before `/`.
- `Date` may be `2023` or `2023-12-19` → year = first 4 digits.
- `Disc` — sort by disc→track; if no tag, disc 1.
- MPD returns empty values as an empty string, not as a missing key: there is no need to distinguish “no tag” from “empty tag”; both → fallback.
- Encoding: MPD returns UTF-8 (verified with Cyrillic). If the file was cp1251 and the tag was not converted, we will see mojibake; do not fix automatically, display as is. In the UI, safely replace invalid sequences with “?”, without crashes.
- DejaVu font covers Cyrillic and Latin; CJK/emoji will be tofu — this is a font limitation, not a player limitation.

### 6.6. Playlists and favourites

- MPD playlists are `.m3u` files in `<mpd-data>/playlists`; the name = file name. `/` and control characters are prohibited; length is limited.
- **Stickers are unavailable**: `sticker database is disabled`; `mpd.conf` has no `sticker_file`. SQLite is linked into MPD, so it can be enabled with one line + restart, but that changes the config and adds a DB write for every like.
- Therefore, “Favourites” is a **regular playlist** with the ASCII name `Favorites` (in the UI — “Favourites”). Check “already in favourites” using the `listplaylist Favorites` cache (updated on the `stored_playlist` event).
- `playlistdelete` works by position, not uri → get the position from the cache.
- Only if ratings/“played N times” per track are needed later, enable `sticker_file` (this will be P2, with migration).

### 6.7. Modes and audio

| What | How | Detail |
|---|---|---|
| shuffle | `random 1` | the queue is **not** visually reordered; only the next selection changes |
| repeat off | `repeat 0`, `single 0` | |
| repeat all | `repeat 1`, `single 0` | |
| repeat one | `repeat 1`, `single 1` | `single oneshot` (0.24) — stop after the current track |
| gapless | automatic | only for the same format; with different formats, a ~100–300 ms gap |
| crossfade | `crossfade N` | same format only, two decoders → CPU. **Do not enable by default** |
| ReplayGain | `replay_gain_mode track/album/auto` | tags required; with `album`, per album; not applied without tags |
| volume | software (`mixer_type software`) | hardware `PCM Playback Volume` exists, but ioctl returns EPERM; below ~10%, loss of bit depth is audible — no need to warn, but the shade slider must not go down to 1–2% |
| normalization | `volume_normalization` in config only and 16-bit only | not used; ReplayGain is available |

### 6.8. Resume, bookmarks, sleep timer

- **Resume on boot**: `state_file` restores the queue, current track, modes, and volume, but **not the second within the track** (verified against the contents of `state`). Write our own `lastpos` (`uri`, `elapsed`) every 15 s during playback and on pause/stop; after startup, if the track matches and the setting is enabled, do `seekcur (elapsed-3)`. Verify that `seekcur` immediately after `play` does not return ACK.
- `state_file_interval` defaults to 120 s: a hard shutdown loses up to two minutes of queue changes. Reduce to 15–30 s — one line in the config.
- **Bookmarks (audiobooks)**: `<player-data>/bookmarks`, key = uri, value = second and time. For files >20 min, on open show “Continue from 12:34 / From the beginning”; auto-resume only when the setting is enabled, otherwise playback may jump to the middle accidentally.
- **Sleep timer**: deadline in memory; when it expires, gradually `setvol` in 5% steps every 100 ms down to 0, `pause`, restore volume, turn off the screen. While the timer is running, show the remaining time in settings; on the lock screen — “SLEEP 12 MIN” 11 dim.
- Playback speed: **not supported** (`commands` in MPD does not contain `speed`); there are no alternatives on this hardware. Do not promise it in the UI.

### 6.9. Cyrillic, sorting, search

- MPD determines the order of `list` (ICU/byte-based — Russian alphabetical order is not guaranteed: the yo character may come after ya). Therefore sort lists ourselves: casefold + map yo to e and short i to i for the sort key; original string for display.
- Search uses `search` (ICU, case-insensitive, verified). Escape the query; minimum 2 characters; `%`/`_` are not special characters (this is not SQL).
- No transliteration: ICU already finds the Russian word for “grunge” using its capitalized form.

### 6.10. Equalizer (deferred until the very end of the project)

> **Owner decision 25.11.2026: EQ is the last item in the project.** This section
> is retained as a reference; it is not included in the UI, settings, or P0/P1.

- Separate screen under “Settings” → “Equalizer”, not on “Now Playing”: no space.
- 5 bands (60/230/910/3600/14000), vertical tracks 4 px, sliders 14×14,
  values −12…+12 dB, labels 11 caps; ON/OFF and presets at the top
  (FLAT/ROCK/VOCAL/BASS/TREBLE) as 88×44 segments.
- State in `<system-settings>` (`eq=on|off`, `eq_preset`, `eq_bands`).
- **Key integration detail:** DSP belongs to the PCM session, which is owned by MPD.
  After a track/format change, EQ settings may reset — reapply ioctl on the `idle player` event (and every 5 s, as `lgjack` does for the route). Without this,
  the equalizer will “drop out” after a track.
- Until `EQ-STATUS.md` is closed, show a dim “Unavailable” row in settings, with no screen and no promises.

### 6.11. What MPD cannot do (and what we do not promise)

| Desired feature | Reality |
|---|---|
| speed 0.5–2× | no command |
| ratings/likes per track | stickers disabled (can be enabled via config, P2) |
| “smart” radio | unavailable |
| playlist cover art and descriptions | no playlist metadata |
| multiple queues in UI | partitions exist, but this is P2 and difficult for 400 MB |
| output to BT | A2DP does not work |

---

## 7. Scenarios

| # | Scenario | Expected result |
|---|---|---|
| 1 | First launch, queue empty | “Queue is empty” + “Open library”; library is built and shows albums |
| 2 | Tap an album → “LISTEN” | queue replaced, first track plays, “Now Playing” screen, cover art appears ≤0.6 s |
| 3 | Long press a track in the library | action shade: Play/Play next/Add to queue/Add to playlist/♥ |
| 4 | “Play next” while a track is playing | track is placed immediately after the current one; queue does not jump |
| 5 | Random enabled, track finishes | a random track plays, triangle in queue moves, queue is not resorted |
| 6 | Repeat “ONE” | track starts again, “REP 1” indicator |
| 7 | Headphones unplugged | pause (if setting is on), lgjack switched route; playback did not continue through speaker |
| 8 | Headset button pressed | play/pause; double — next; triple — previous; hold — stop |
| 9 | MPD killed (`kill`) | “MPD unavailable”, reconnects after 1–2 s, queue and track remain |
| 10 | MPD starts after the player | player reconnects by itself ≤2 s, without restart |
| 11 | SD removed during playback | MPD loses file → error status; “SD card not found” / “File cannot be read”, no log spam |
| 12 | File with a broken tag | “Unknown artist”, “No album”, file name as title |
| 13 | File without cover art | placeholder; no empty squares or hangs |
| 14 | 300 KB cover art | UI does not block, placeholder → cover art; reopening is instant (cache) |
| 15 | Queue of 500 tracks | scrolling without stutter, opens at current track, memory does not grow while scrolling |
| 16 | Library of 5,000 tracks | first launch — “Updating…” in background, UI responsive; second launch < 1.5 s |
| 17 | Reboot during playback | queue, track, and second restored; playing (or paused — according to setting) |
| 18 | 15-minute sleep timer | gradual fade, pause, screen turns off, volume restored |
| 19 | 3-hour audiobook, exit | bookmark saved; on return — “Continue from 1:42:10” |
| 20 | Cyrillic in tags and file name | displayed correctly; search for lowercase “kino” in Cyrillic finds capitalized “Kino” in Cyrillic |
| 21 | Playlist created, renamed, deleted | list updates without restart (`stored_playlist` event) |
| 22 | Favourites: ♥ / tap again | added/removed; state persists across launches |
| 23 | Album playing while browsing the library | no “hang” longer than 100 ms |

---

## 8. Test plan

### 8.1. Data

A test library is needed (currently there are 2 files on the SD card — not enough):

- 2,000–5,000 tracks (short MP3s can be generated from one template),
  structure `Artist/Album/NN Title.mp3`, 200+ albums;
- 10 cover images: 300 KB embedded, `cover.jpg` 640×640, `folder.jpg` 2000×2000,
  PNG, broken JPEG, missing;
- tags: full set, Title only, empty, `Track 03/12`, `Disc 2`,
  compilation (AlbumArtist=Various Artists), year `2023-12-19`;
- names: Cyrillic, yo character, emoji, 120 characters, spaces and quotes in the name
  (MPD escaping test), `#`, `&`;
- failure cases: 0 bytes, truncated MP3, file without permissions, 3-hour file.

### 8.2. Cases

| # | Check | How | Criterion |
|---|---|---|---|
| T1 | MPD responds | `mpc status` | < 50 ms |
| T2 | idle | `printf 'idle player\n' \| nc <placeholder> 6600`, change track with `mpc next` | `changed: player` < 100 ms |
| T3 | cover art from tag | `printf 'readpicture "<uri>" 0\n' \| nc <placeholder> 6600` and player | embedded art is shown (currently — no) |
| T4 | cover art from file | album with `cover.jpg` | shown after `readpicture` ACK |
| T5 | cover art cache | open album twice | second time < 50 ms, no SD read |
| T6 | Cyrillic search | lowercase “grunge” in Cyrillic, uppercase “GRUNGE” in Cyrillic, “yura” in Cyrillic | found, < 300 ms for 5,000 |
| T7 | escaping | file with `"` in name | command is not split; track plays |
| T8 | queue of 500 | `mpc add` ×500 | scrolling, opening, memory |
| T9 | play next | “Play next” on 10th track | placed at current+1 position |
| T10 | delete current | “Delete” on the playing track | next track plays; queue does not break |
| T11 | repeat one | 3 states using button | `status`: 0/0 → 1/0 → 1/1 |
| T12 | random | enable and let track finish | random selection; queue unchanged |
| T13 | resume | reboot during playback | queue+track+second (±5 s) |
| T14 | MPD down | `kill $(pidof mpd)` | error screen; reconnect after startup |
| T15 | SD out | `umount /media/sd` (by agreement) | “SD card not found”, no crashes |
| T16 | broken file | add to queue | “File cannot be read”; playback continues |
| T17 | headset | `evtest /dev/input/event4` + press | KEY_MEDIA 226; double/triple press is not duplicated |
| T18 | jack | unplug/plug | pause, route via `lgjack`, no duplicate logic |
| T19 | timer | 1-minute test | fade, pause, volume restored |
| T20 | bookmark | long file, exit/return | continue prompt |
| T21 | favourites | ♥, restart | persisted; playlist count correct |
| T22 | large index | 5,000 tracks | cold start < 4 s in background, warm < 1.5 s |
| T23 | long soak | playing for 30 min + browsing | RSS ±1 MB, no D-states, no hangs |
| T24 | power | hard shutdown | settings and bookmarks are not corrupted (atomic write) |

### 8.3. Budgets

| Metric | Target |
|---|---|
| First frame after startup | < 1.0 s |
| Tap → highlight | < 50 ms |
| Tap → action (play/next) | < 150 ms |
| idle event → redrawn screen | < 150 ms |
| Cover art from cache / first decode | < 50 ms / < 600 ms (in background) |
| Search on 5,000 tracks | < 300 ms |
| Index: cold / warm | < 4 s (background) / < 1.5 s |
| CPU: idle / playing / crossfade | < 3% / < 20% / < 40% |
| RSS: player / MPD at 5,000 tracks | < 20 MB / < 40 MB |
| Free RAM after 30 min | does not fall below 60 MB |

---

## 9. Risks

1. **1 GHz and JPEG.** Decoding 300 KB cover art on the UI thread guarantees
   stutter. Fix with background processing and cache; if it is still slow in the
   background, pre-decode the next track in advance.
2. **Memory with a large library.** MPD + index + cover art cache fit in 400 MB
   up to ~10,000 tracks; beyond that — `metadata_to_use`, cache limit,
   disable preloading.
3. **Parallel move to X.** Logic (MPD, index, cover art, settings)
   must live in a separate layer, otherwise it will be written twice — for fb and for the shell.
   This is the main architectural risk of the project, not the features.
4. **Touch.** Resistive screen, polling ~200 ms, no multitouch: long press
   versus scrolling, no swipes on rows, zones ≥44 px.
5. **SD and vfat.** No permissions/owners, case-insensitivity, long names,
   Unicode NFC/NFD — compare uris bytewise; do not normalize.
6. **MPD without SD.** `mpd.start` exits silently if `/media/sd/Music` is missing →
   “MPD unavailable” out of nowhere. Retry/supervisor and clear text are needed.
7. **Power.** Non-atomic settings write and `state_file_interval` of 120 s.
8. **Promises.** Do not show BT, GPS, speed, Last.fm, EQ in the UI until they
   work.

---

## 10. Three main conclusions

1. **Everything needed from the backend is already available.** MPD 0.24.20 on the phone supports
   `idle` (4 ms), case-insensitive Cyrillic search, `readpicture`,
   playlists, queue by id, `random/repeat/single/crossfade/replay_gain`.
   The only real gaps are stickers (disabled in config), speed
   (not in MPD), and BT. Therefore, the “player” is client-side work, and its visible
   effect depends on three things: index, cover art, and responsiveness.
2. **The library is an index, not a set of queries.** At 1 GHz, MPD cannot be
   queried on every screen and cover art cannot be decoded on the UI thread. One pass
   of `listallinfo` → file cache, lazy 96×96 thumbnails on disk, background
   rebuild on `db_update`. This also addresses “thousands of tracks”.
3. **The player currently falls short not in features, but in feel.** Embedded
   cover art is not visible at all (`albumart` instead of `readpicture`), the UI updates
   by polling once per second, and the queue is reread in full. Fixing these three
   things + honest empty states will deliver more than search or the equalizer.

---

## 11. What to do first

1. **Measurements on a real library (0.5 day).** Load 2,000–5,000 tracks,
   measure: `listallinfo` (response time/size), MPD RSS, `stbi` cover art
   decode on the phone, `status` while playing. Without these numbers, P0 estimates are
   guesses.
2. **MPD layer (3 days).** Two sockets, `idle` + resubmission, backoff,
   `binarylimit`, operations by `id`, `plchangesposid`. This is the foundation; changing
   it later is expensive.
3. **Cover art (3 days).** `readpicture` → file → placeholder; decode in background; cache
   96×96; current album cache. Test with 300 KB cover art from the test set.
4. **Index + “Library” screen (8–10 days).** Albums, artists, tracks,
   “Listen”, “Add to queue”, “Play next”, action shade.
5. **Queue and modes (4 days).** Delete, clear, “⋯”, repeat one, current
   by id, 100+ tracks.
6. **Acceptance (2 days).** Scenarios §7, budgets §8.3, 30-minute soak.

Search, playlists, favourites, sleep timer, and headset button — immediately after P0,
without diluting the first release. The equalizer is the **very last item in the project**
(owner decision 25.11), after the shell, reader, video, and stability.

## Placeholder list

- `<placeholder>` in the `nc` commands replaces the IP address `127.0.0.1`.

## Translation notes

- No passwords or personal paths occur in this fragment. The only replaced sensitive value is the loopback IP address used in the test commands.
- Commands, option names, filenames, dimensions, timings, version numbers, thresholds, and technical identifiers are retained from the source. Translated explanatory text is kept alongside those values so the implementation constraints and verification criteria remain explicit.
- The source's section numbering and table structure are preserved; command syntax and placeholders within commands remain in code formatting.
