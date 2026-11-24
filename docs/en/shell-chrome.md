# Notifications and Task Switching

This document describes the plan for notifications, the quick settings panel, and the external application task switcher. P0/P1/P2 priorities and verification criteria are preserved from the plan.

## 0. Summary

1. **Notifications are 95% internal shell events** (MPD, Wi-Fi, battery, application launch/crash, QA). Therefore, the queue owner is the shell itself (`src/notify.cpp`), with no daemon. The P0 external input is a file queue in tmpfs; Unix socket and D-Bus are only needed when an actual client appears.
2. **The shade becomes a full-screen notification center**: 5 tiles, 2 sliders, a list of 432×76 cards (5 visible + “MORE N”), “CLEAR”, and a DND tile. MPD and Wi-Fi move from hard-coded `draw()` to ongoing notifications—the same appearance, but with deduplication, updates, and a unified list.
3. **A banner over another application is impossible in the current architecture**: the shell is one full-screen window *under* the application window. At P0, events are shown in the status-bar strip (not covered by the application)—icon + count + 5-second ticker. A full 480×64 heads-up is a separate override-redirect window with an empty input-shape (P1, `-lXext`).
4. **The task shade requires a multitasking apphost, not screenshots**: currently `apphost.cpp` holds exactly one `g_pid`/`g_win`, and Home calls `apphost_close()`—that is, it kills the application. First refactor to 3–4 tasks with `suspend/resume`, then UI.
5. **A 240×400 screenshot costs 187.5 KiB and is captured once**—when the window is still visible (before unmap). `XGetImage` cannot be taken from an already closed/covered window: contents are undefined. This addresses the objection in `METRO-RESEARCH.md` §4.7.
6. **Home no longer kills anything**: short Home = minimize (unmap), Back = close (WM_DELETE → TERM → KILL), long Home ≥650 ms = tasks. This is the key contract; without it, the switcher is pointless.

---

## 1. Current state (facts underlying the plan)

| Layer | Fact | Implication for the plan |
|---|---|---|
| Shell | one process, one 480×800 override-redirect window, `XPutImage`, no WM | shade/lock/tasks are shown after the application is unmapped (suspend), not over it; a banner over it requires a separate window |
| apphost | one `g_pid`, one `g_win`, `apphost_launch` rejects a second application | an array of tasks and partial API refactor are needed |
| Home | in `app.cpp`, with an active application: `apphost_close(); open("home")` | Home kills the application; this must be rewritten |
| Long Home | 700 ms → `push("shade")` | change to 650 ms → `push("tasks")` |
| Shade | panel 470, 4 tiles, 2 sliders, 2 cards (MPD/Wi-Fi) hard-coded | no new notifications; a list and model are needed |
| D-Bus | session bus starts lazily on the first `apphost_launch`, uid 10000, address in `/tmp/dbus-graphite.address` | no notification clients; a custom service is not worthwhile |
| Memory | 400 MB, ~182 MB used, `MemAvailable` critical threshold 40 MB | limit to 3–4 tasks, previews < 1 MB, no extra processes |
| Screen | 480×800, RGB565, 1 GHz, no GPU, no animations | no slides/fades; redraw on event |
| Keys | Back/Home/Menu/Search from event0, Home also from event1, Power from event5 | Home arrives twice; 120 ms deduplication already exists; one long-press timer |
| Lock screen | `lock.cpp`: tap and swipe up unlock; Home from lock screen also goes to home | separate security issue (PIN—P2), not changed in this plan but noted |

### 1.1. Three document contradictions and their resolution

1. `METRO-RESEARCH.md` §4.7: “live previews in the task switcher are impossible; a minimized window has no contents.” This is true only for a capture *after* unmap. We capture the frame **before** the window disappears (Home, shade, lock screen) and store 240×400 RGB565. The limitation remains: an application that does not redraw on Expose may show garbage after returning—such tasks get a placeholder card (icon + name), not a broken preview.
2. `APP-INTEGRATION-PLAN.md` §5.3: “there is no hardware Back on the P700.” In `os/README.md` and `STATUS.md`, Back works (`event0`, `KEY_BACK` 158). The plan follows the fact: Back exists; policy is below.
3. `APP-INTEGRATION-PLAN.md` §5.3: Home in an application is the launcher (it does not say “minimize”), and `apphost_close()` kills it. For the task switcher, we adopt an explicit rule: **Home = minimize, Back = close**.

---

## 2. Notifications

### 2.1. Model

One record is one notification. Two types:

- **alert** — an event (Wi-Fi loss, charge, application crash). Counted, shown as a banner/ticker, dismissed with a swipe/“CLEAR”.
- **ongoing** — live state (MPD track, current Wi-Fi connection). Always in the list, not counted, not dismissed, updated in place. This replaces the two currently hard-coded cards.

Fields (upper bounds; strings are UTF-8, limits in bytes):

| Field | Type | Limit | Meaning |
|---|---|---|---|
| `id` | uint32 | — | monotonic, survives reboot (`max+1`) |
| `app` | string | 16 | `mpd`, `wifi`, `battery`, `apphost`, `qa`, `storage` |
| `tag` | string | 24 | deduplication key within app; empty → hash(title) |
| `icon` | string | 16 | shell icon name (`music`, `wifi`, `bell`, …) |
| `title` | string | 96 | heading, 15 bold |
| `body` | string | 240 | body, 2 lines at 12 |
| `meta` | string | 32 | ongoing right column (`connected`, `1:23 / 3:45`) |
| `ts` | uint32 | — | epoch, local time |
| `pri` | uint8 | 0–2 | 0 low, 1 normal, 2 high |
| `flags` | uint16 | — | ongoing 1, transient 2, silent 4, bypass_dnd 8 |
| `seen` | uint8 | — | 0/1, unread counter state |
| `count` | uint8 | 1–9 | number collapsed by deduplication |
| `open` | string | 24 | shell screen id (`player`, `set-wifi`, …) |
| `ref` | string | 32 | task key/command for “Open again” (P1) |
| `nact` | uint8 | 0–2 | number of actions |
| `act[i]` | pair | id 12 + label 20 | actions (P1) |

Memory: 32 × ~500 B ≈ 16 KiB, including strings. Negligible against 400 MB.

### 2.2. Ring, deduplication, limits

- **Ring of 32 records**, oldest evicted. The shade shows 5 cards; the rest are under “MORE N” and scrolling. No more than 32 are kept, including on disk.
- **Deduplication by `(app, tag)`**: a repeat does not create a new record; it updates `title/body/ts`, increments `count` (up to 9), resets `seen=0`, and moves the record to the start of its group (after ongoing). For ongoing, `count` is always 1.
- **Identical text** without a tag within 5 seconds uses the same deduplication by `hash(app,title)`.
- **Rate limit**: no more than 1 alert per app per 2 s and 8 globally per 10 s; excess is collapsed into the last `count` or discarded (debug log). Ongoing and `pri=2` are not rate-limited.
- **Field limits** are truncated on receipt: title 96, body 240; body is no more than 2 lines at 12 px with “...”. Truncate at a UTF-8 boundary.
- **Grouping by app** — P2 (group heading 24 px, count). At P0, order: ongoing (MPD, Wi-Fi) first, then alerts by time.

### 2.3. Sources and rules

| Source | tag | pri | Type | When | Banner/ticker | Open |
|---|---|---|---|---|---|---|
| MPD | `music` | 0 | ongoing | track/state change | no | `player` |
| Wi-Fi | `link` | 1 | ongoing | updates SSID/IP every 10 s | no | `set-wifi` |
| Wi-Fi loss | `link-down` | 2 | alert | `operstate` leaves `up` | yes (unless DND) | `set-wifi` |
| Battery | `battery` | 1/2 | alert | 20% / 10% / 5%, once per discharge | yes | `set-power` |
| Launch | `app-<key>` | 1 | alert | window did not appear within 15 s | yes | `apps` |
| Crash | `app-<key>` | 1 | alert | `waitpid` returned a signal | yes | `home` + ref |
| LRU close | `app-<key>` | 0 | alert | insufficient memory | no | `home` |
| QA | `qa` | 1 | alert | file/socket | yes | `home` |
| Disk (P2) | `storage` | 2 | alert | free < 100 MB, once per boot | yes | `set-storage` |
| Time (P2) | `clock` | 0 | alert | NTP synchronized | no | `set-system` |

Production rules:

- **MPD**: update ongoing only when `file` or `state` changes, not on every tick. The shade reads `meta` (time) from the live MPD client, not the store—otherwise it would have to write to disk every second. Do not switch the shared `mpd_client()` (`queue_watch` untouched); `tick` no more than once per second; read status only.
- **Wi-Fi**: SSID/IP are read as they are now in `shade.cpp` (`iwgetid -r`, `getifaddrs`), but once every 10 s in `notify_tick`, not in `draw`. The card is always created, even without an IP (“Wi-Fi · no IP”).
- **Battery**: hysteresis: once a threshold fires, wait for `capacity > 25%` or `charging` to avoid repeating. The P700 driver reports `100 / Full` and has no `current_now` (see `STATUS.md`), so the check must tolerate no changes: without crossing a threshold, no notifications.
- **Apphost**: the launch timeout already exists (`kLaunchTimeout=15s`); replace `toast` with a notification so it remains after the overlay closes. Crash detection uses `WIFSIGNALED`, text “Application closed”, action “Open again” (P1).
- **QA**: the only external producer at P0. Format below.

### 2.4. Transport: what to choose on 400 MB

| Option | +RSS | Latency | Code | External clients | Survives shell restart | Verdict |
|---|---|---|---|---|---|---|
| API inside shell (`notify_post`) | 0 | 0 | ~0 | no | — | **P0, foundation** |
| File queue `/run/graphite/inbox` | 0 | ≤250 ms | ~60 lines | any script | yes, until reboot | **P0, external input** |
| Unix socket `/run/graphite/notify` in shell | ~0 | <10 ms | ~100 lines + fd in `select` | any | no | P1, if a client appears |
| Separate daemon | +1.5–3 MB | <10 ms | ~300 lines + init | yes | yes | ✖ |
| D-Bus `org.freedesktop.Notifications` in shell | +1–2 MB (libdbus) | <10 ms | ~350 lines | libnotify | no | P2 |
| Separate-process D-Bus bridge | +2 MB | <10 ms | ~250 lines | libnotify | yes | P2, if a client appears |
| `dunst` / `notification-daemon` | +5–20 MB | — | 0 | yes | yes | ✖ (own UI, GTK) |

**P0 decision**: the queue lives in the shell; external messages arrive as files in `/run/graphite/inbox/`. No daemon, socket, or D-Bus.

Why:

- The shell is the only consumer and the only one with a UI. If the shell is dead, there is nowhere to show a notification; no one can or needs to buffer it “for later” (the session goes to the framebuffer player and X is shut down).
- A daemon means another process, init script, protocol, restart handling, and +2 MB on a 400 MB machine for an event once an hour.
- D-Bus is not needed now: the device has neither `notify-send` nor applications that send notifications (dillo/xterm do not; pcmanfm/mousepad rarely do, and it is not critical). The bus also starts lazily under uid 10000, and the “who owns the name first” race is not in our favor if the client starts before the shell.

**File queue, not shared JSONL**: one message = one file, written via `tmp` + `rename` (atomic, no partially written lines on crash). The shell reads up to 4 files per tick and deletes them after parsing. Directory `/run/graphite/inbox` (1777, tmpfs), limit 64 files and 4 KiB per file; excess is removed by age. Name: `msg-<pid>-<n>.json`.

**Message format** — JSON Lines, flat object, same fields as the model (no nested arrays; actions are `act1id/act1label/...`). QA example:

```json
{"app":"qa","tag":"test","icon":"bell","title":"Check","body":"Notification from file","pri":1,"open":"home"}
```

Why JSON rather than `key=value` as in `/etc/lgtest-settings`: QA scripts and a future D-Bus bridge may write the file, and JSON Lines allows line-by-line recovery—a malformed line is discarded, the others are read. The parser is bounded (flat object, strings/numbers, escaping `\" \\ \n`), ~120 lines with no dependencies; do not bring in a full JSON engine.

**Security**: the shell is root and reads `/run` as user 10000. Open only with `O_NOFOLLOW`, check `fstat` (regular file, size ≤ 4096), name matching `msg-*.json`, then `unlink` by name after reading. No `..`, no symlinks, no `popen` on contents. `open` is restricted to an allowlist of screens (`player`, `set-wifi`, `set-power`, `set-storage`, `home`, `apps`); unknown → `home`. `ref` is **never executed as a command**: it is only a live task key or an id from the application registry; unknown ref = action unavailable. Otherwise, the world-writable inbox becomes root-exec.

**Unix socket (P1)**: `SOCK_STREAM`, `/run/graphite/notify.sock`, same JSON Lines + `\n`; fd added to `select()` in the main loop. Provides immediate delivery and an `id` response, but is only needed if a `graphite-notify` CLI or external daemon appears. Client: 50 lines of C, no libs.

**D-Bus (P2)**: if a real libnotify client appears, do not link libdbus into the shell; instead run a small bridge, `graphite-notifyd`, which owns the name on the session bus and writes the same files to the inbox. The shell remains without a D-Bus dependency; the contract does not change. The bus then needs to start in `session-shell` every time (+5 MB), otherwise the client may start before the name. Implement only when there is an actual client.

### 2.5. API (sketch, not code)

```cpp
// include/notify.h — new header; do not touch graphite.h.
enum { N_LOW = 0, N_NORMAL = 1, N_HIGH = 2 };
enum {
    NF_ONGOING    = 1,  // live state, not counted
    NF_TRANSIENT  = 2,  // do not store after display (P2)
    NF_SILENT     = 4,  // no banner/ticker
    NF_BYPASS_DND = 8   // pri=2 bypasses DND
};

struct NotifyAction { const char *id; const char *label; };

struct NotifySpec {
    const char *app;       // "mpd"
    const char *tag;       // "music"
    const char *icon;      // "music"
    const char *title;
    const char *body;
    const char *meta;      // ongoing right column
    int pri;               // N_*
    unsigned flags;        // NF_*
    const char *open;      // screen id
    const char *ref;       // task key / command (P1)
    int nact; NotifyAction act[2];
};

// Post; returns id. A repeat (app,tag) updates the existing record.
uint32_t notify_post(const NotifySpec &s);

// Targeted operations
bool notify_update(uint32_t id, const char *body, const char *meta);
void notify_dismiss(uint32_t id);
void notify_clear_alerts();          // keeps ongoing
void notify_mark_seen();             // shade opened / unlocked
int  notify_unread();                // for the icon
bool notify_dnd();
void notify_set_dnd(bool on);        // writes dnd=0|1 to /etc/lgtest-settings

// Read access for screens
// NotifyItem — view of §2.1 fields (id, app, tag, icon, title, body, meta,
// ts, pri, flags, seen, count, open, nact/act[]).
int  notify_count();
const NotifyItem *notify_at(int i);  // 0 = newest
uint32_t notify_latest_id();

// Service
void notify_tick(unsigned now);      // inbox, battery, Wi-Fi, MPD, persist
bool notify_draw_banner(Gfx &g);     // in-shell banner; false = none
void notify_draw_status(Gfx &g, int x0, int x1); // icon + count/ticker
void notify_hint(const char *text, unsigned ms);  // ticker without a record
bool notify_status_tap(int x, int y);             // tap in strip y<32
```

Apphost and tasks sketch — §3.9.

Call sites:

- `App::run()` — `notify_tick(now)` before `render()` (the appearance updates even when an application is foregrounded). The tick sets `app.dirty` when a ticker/banner appears/expires; otherwise a 5-second message would remain until the next clock tick (10 s).
- `App::render()` — `notify_draw_banner(*g)` after the screen, before `ui_toast`.
- `ui_status()` — `notify_draw_status(g, 56, 288)`.
- `App::handle_event()` — `notify_status_tap()` for y < 32 with an active application (the status strip is not covered by the application).
- `shade.cpp` / `lock.cpp` — read list and `notify_mark_seen()`.

### 2.6. Storage, reboot, cleanup

| What | Where | Format | Size | Lifetime |
|---|---|---|---|---|
| Notification queue | `/var/lib/graphite/notifications.jsonl` | JSON Lines, ≤32 lines | ~20 KiB | survives reboot |
| External input | `/run/graphite/inbox/` | one JSON object per file | ≤64 × 4 KiB | tmpfs, cleared at shell startup |
| Boot id | `/run/graphite/boot_id` | string | 40 B | needed to distinguish reboot from shell restart |
| DND and flags | `/etc/lgtest-settings` | `dnd=0/1`, `notify_banner=1`, `notify_music=0` | ~50 B | survives reboot |
| Tasks and previews | RAM | — | ≤750 KiB | dies with the shell |

Writing:

- atomically: `notifications.jsonl.tmp` → `fsync` → `rename`;
- only on structural change (post/dismiss/clear/seen), not every tick or every MPD second;
- at startup: read, discard malformed lines, truncate to 32, delete records older than 7 days; if `boot_id` changed, mark all `seen=1` and discard ongoing `music`/`link` (producers recreate them; otherwise “playing” remains after reboot without MPD);
- `notifications.jsonl` is written by root, 0644; `/var/lib/graphite` is created in `install.sh` and at `session-shell` startup.

Cleanup:

- “CLEAR” in the shade — all alerts; ongoing remain;
- eviction — oldest alert when full;
- `notify_clear_alerts()` does not touch ongoing or reset DND;
- at most 32 records on disk, so no separate rotation is needed.

### 2.7. UI

#### 2.7.1. Shade — full-screen notification center

Panel 0…800, `bg` background + the same horizontal `surf2` lines (step 24, alpha 20) as now. The kernel draws the status bar 0…32 over it. Remove the 150/255 dimming over the whole screen: the full-screen panel already covers the content, and a 480×800 blend loop (384k pixels) is needless work.

```
y=32   ┌──────────────────────────────────────────────┐
       │              Wed, 25 November      [sliders] │  date 12 dim, icon 22
y=72   │ [Wi-Fi] [Sound] [Quiet] [Player] [Power]    │  5 × 80×80, gap 8
y=152  ├──────────────────────────────────────────────┤
y=166  │ VOLUME                                  60 % │  label 12, track 4 (y=196)
y=214  │ BRIGHTNESS                              70 % │  label 12, track 4 (y=244)
y=274  │ ▌NOTIFICATIONS · QUIET           CLEAR       │  section 12 caps; 11 tracked
y=312  │ ┌ card 432×76 ────────────────────────────┐ │
       │ │ [music] Track title            1:23/3:45 │ │
       │ │    artist · playing                     │ │
       │ ├─────────────────────────────────────────┤ │  gap 8
       │ │ ... 5 cards ...                         │ │
y=724  │ │              MORE 7                     │ │  44, 11 dim, tap = +5
y=768  ├──────────────────────────────────────────────┤
y=780  │                 ▬▬▬▬ (120×4)                 │  handle
y=800  └──────────────────────────────────────────────┘
```

Exact metrics:

- margins 24, content 432 (24…456);
- 5 tiles: `w=80, h=80, gap=8`, x = 24, 112, 200, 288, 376; y = 72…152; icon 24 centered (y+14), label 11 (y+56), entire tile is tap area; active — `accent` fill, `onacc` icon/text; inactive — `surf`;
- sliders: label 12 (y=166 / 214), value 12 on right, track 4 (y=196 / 244), knob 16; zones 44: 162…206 and 210…254 (no overlap, knob fully inside);
- section: `ui_section` style, y=274, `accent` marker 2×12, text 12 bold caps + tracking 2; after the marker in DND: “ · QUIET” 11 dim;
- “CLEAR”: 11 bold + tracking 1, right-aligned at 456, zone 372…456 × 268…312 (44); if there is nothing to clear — `dim` and ignore tap;
- list: 312…768; card 76, gap 8; 5 cards occupy 312…724; “MORE N” — 44 (724…768), tap scrolls by 5 cards; with cards containing actions (120 px), fewer fit—scroll the rest;
- list scroll: drag 1:1, no inertia, clamped; while scrolling, “MORE” is replaced with the remainder; tap coordinates account for `scroll`;
- handle 120×4 at (180, 780);
- close: swipe up >80 px in header/tiles (y<152) or on handle (y>770); vertical movement in list scrolls; on sliders it is ignored; Back/Home; tapping “empty” space no longer closes (panel is full-screen);
- no new animations.

Why 5 tiles, not 8: 8 tiles 100×72 (2 rows) use 152 px, leaving 300 px for notifications—fewer than 4 cards. 5 tiles in one row are 80×80 (zone ≥44) and leave 456 px for the list. DND is the fifth tile; brightness remains a slider, as now.

#### 2.7.2. Notification card

```
┌────────────────────────────────────────────────────┐ 76
│ ▌ [icon 20] Title 15 bold                  14:32   │
│              Body, line 1, 12 dim                  │
│              Body, line 2, 12 dim                  │
└────────────────────────────────────────────────────┘
```

- `surf` background; left strip 3 px: `accent` — unread alert, `dim` — ongoing, none — read;
- icon 20 at (40, y+14);
- title 15 bold at (68, y+9), `fit` to 368 (time on right);
- time 11 dim right-aligned at 456, y+11; for ongoing — `meta` (track time, “connected”);
- body 12 dim at (68, y+31) and (68, y+47), `fit` 372, maximum 2 lines;
- tap card (outside actions) — `seen=1` + `open`; if `open` is empty, only `seen=1`;
- swipe right >100 px — dismiss (P1), ignored for ongoing;
- press — `surf2` fill, no movement.

Card with actions (P1): height 120, `line2` divider at y+76, up to 2 buttons 44 high (216 px each, 1 px divider at x=240), text 12 bold `accent` centered. Actions are handled before card tap. The third action is not shown (P2 — “MORE”).

#### 2.7.3. Banner and ticker: important architectural limitation

The shell is one full-screen window **under** the window of another application. If a banner is drawn in `App::render()`, it is not visible while an application is active: the application covers y=32…800. The status bar (y=0…32) remains visible because the application window starts at y=32.

Therefore:

- **P0, in-shell banner** — only when an application is not foregrounded (home, settings, player): 432×64 at (24, 40), `surf` background, 3 px `accent` strip, icon 20 (40,56), title 13 bold (68,50), body 12 dim (68,68), “×” 44×44 (412…456 × 40…84), auto-hide after 5 s. Tap body — `open`. If `toast` is active (2.2 s), the banner waits; there are never two banners—only the last one + “+N”.
- **P0, ticker over application** — in the status bar: icon 14 + title 12 for 5 s in zone x=84…284. This is the only way to show an event over another window without a second window. Tap title — `open`; tap icon/count — shade.
- **P1, 480×64 heads-up** — separate override-redirect window (like OSK), with empty input-shape (`XShapeCombineRectangles(..., ShapeInput, ..., NULL, 0, ...)`) so taps pass through to the application. Requires `-lXext` and checking `XShapeQueryExtension`; if the extension is absent, use the ticker. This is the only correct heads-up in our architecture.

No banner is shown when the shade/tasks/lock screen is open: the card (or lock-screen row) is already visible there. With DND, banners and tickers are suppressed except for `pri=2 + NF_BYPASS_DND` (battery 5–10%, disk).

#### 2.7.4. Status bar: icon, count, ticker

- Clock 12 at (16,8)—as now; “14:35” ends around 48.
- If `unread>0` and the top screen is not shade/tasks/lock screen:
  - `bell` icon 14 at (64,9), `accent`; with DND — `dim` (or `bell_off`);
  - without ticker — count 11 at (82,10), `accent`;
  - with ticker — title 12 `fg` at (84,9), `fit` 200 (up to 284), 5 s.
- Tap zone: 56…300 × 0…32. Tap icon/count (56…100) — shade; title (100…284) — `open`, otherwise shade.
- If an application is foregrounded, both taps first call `apphost_suspend()`: icon — with `return_task` (closing the shade returns to the application), title — without return (open target screen). Otherwise the screen ends up under the application window.
- Keyboard icon remains at 296…314; no conflict.
- When the shade is open, icon and ticker are hidden (the list is already on screen).

Count is the number of unread alerts, maximum 32 (two digits fit). Ongoing are not counted.

#### 2.7.5. Lock screen

- Clock 76 (baseline 274), date 14 (372)—unchanged.
- If alerts exist: up to 3 rows of 40 px, 396…516: icon 14 (24, y+12), title 12 (46, y+11) `fit` 320, time 11 on right (456, y+12), `line2` divider at bottom; body is not drawn.
- With DND — one row “QUIET · N notifications” 11 dim at (24,400).
- Current track card: if rows exist — 540…604; otherwise as now, 420…484. Handle at 700 and hint at 726 do not move.
- Tap row — ticker/toast “Unlock” (P1); actions are unavailable on the lock screen.
- Lock screen does not show banners and does not reset `seen`; on unlock (`open("home")` from lock screen) — `notify_mark_seen()`.
- Known behavior: currently Home from lock screen goes to home (i.e. unlocks). PIN is a separate P2; within this plan, the lock screen at least stops being “leaky” for notifications. Recommendation: ignore Home/Back on lock screen (3 lines in `app.cpp`), leave only swipe.

#### 2.7.6. DND

- State: `dnd=1` in `/etc/lgtest-settings`; “Quiet” tile in the shade.
- Enabled: no banner or ticker; badge counts, but `bell` is dim; on lock screen—one row; ongoing continue to update.
- Exception: `pri=2` + `NF_BYPASS_DND` (battery ≤10%, disk ≤100 MB).
- P2: schedule `dnd_from/dnd_to`, auto-disable `dnd_until`, per-app DND (MPD/Wi-Fi separately).
- No sound: there is no system mixer or telephony; the only sound is MPD; DND is visual, not audio.

#### 2.7.7. States and transitions

```
new alert ──> unseen (badge+1, banner/ticker)
                  │  shade opened / lock screen unlocked
                  ▼
                seen (badge−1)
new ongoing ──> pinned at top, badge unchanged, no banner
DND ──> alert remains unseen, but no banner/ticker
“CLEAR” ──> all alerts deleted, ongoing remain
reboot ──> all seen, ongoing music/link discarded and recreated
```

While the shade is open, new cards are immediately marked `seen` (the user sees them), badge remains 0. If the list is scrolled, new items are inserted at the top; `scroll` is not reset.

### 2.8. Degradation and errors

| Situation | Behavior |
|---|---|
| No MPD | ongoing `music` is not created; shade shows empty state |
| No wlan0 | ongoing `link` with “adapter not found”, no IP |
| File in inbox is malformed / >4 KiB / symlink | delete, do not read, debug log |
| Queue full | delete oldest alert; do not touch ongoing |
| `/var/lib` disk unavailable | operate in RAM; at startup, empty list, debug log |
| DND enabled, `pri=2` arrived | show ticker (bypass), badge +1 |
| Shade open, 10 events arrived | list updates, badge 0, scroll does not jump |
| Lock screen, event arrived | row on lock screen, badge hidden, no banner |

---

Placeholders: none.

## 3. Task shade (long Home)

### 3.1. Task model

```cpp
enum TaskState { T_STARTING, T_FOREGROUND, T_BACKGROUND, T_CLOSING };

struct Task {                 // sketch
    char     key[32];         // command basename: xterm, dillo, ...
    char     name[96];        // _NET_WM_NAME or name from key
    char     icon[16];        // our icon
    pid_t    pid, pgid;
    Window   win;             // main toplevel
    TaskState state;
    int      back_mode;       // ALT_LEFT / ALT_UP / ESC / CLOSE
    unsigned last_active;     // ms
    unsigned started;         // ms
    unsigned expect_unmap;    // ms, 0 = not waiting
    int      rss_kb;          // every 5 s
    bool     heavy;           // RSS > 20 MB or known heavy
    bool     shot_valid;
    std::vector<uint16_t> shot; // 240×400 RGB565
};
```

Maximum **4 tasks**, including **2 heavy tasks** (see §3.7). Preview is 240×400,
3:5, matching the screen. `shot` is 192 000 B per task; 4 tasks = 750 KiB.

### 3.2. Lifecycle

```
                 MapNotify (pid/size/type filters)
STARTING ────────────────────────────────────────> FOREGROUND
   │ 15 s without a window → close + notify              │
   │                                                     │ Home / lock / tasks
   │                                                     ▼
   │                                                BACKGROUND (unmapped)
   │                                                     │ resume (tap card)
   │                                                     ▼
   │                                                FOREGROUND
   │                                                     │ Back / × / LRU
   ▼                                                     ▼
CLOSING ── WM_DELETE → 2 s → SIGTERM → 2.5 s → SIGKILL ──> removed
```

Key rules:

- **New task**: if a task with the same `key` is alive, do not create a second one;
  raise the existing task instead. Thus there is one xterm and one dillo; a second
  terminal is P2.
- **Launch while a task is active**: first `suspend()` the old task (capture,
  unmap), then the new one becomes foreground.
- **Home**: `suspend()` — capture a fresh image while the window is visible. Then
  unmap, `focus_shell`, `state=BACKGROUND`, `fg=-1`. The process remains alive,
  memory stays allocated, and the window does not draw or receive events (no Expose).
- **Long Home**: `suspend()`, `open("home")`, `push("tasks")`. If the application
  is already suspended, immediately `push("tasks")`.
- **Resume**: `XMapRaised(win)`, `XMoveResizeWindow(0,32,480,768)`,
  `XSetInputFocus(win)`, `state=FOREGROUND`, `last_active=now`,
  `app.open("home")` (the shell stack has home under the application).
- **Close**: `state=CLOSING`, `WM_DELETE_WINDOW`, after 2 s SIGTERM to the whole
  group, after 4.5 s SIGKILL. The card shows “closing…” and does not respond.
  `waitpid`/`DestroyNotify` remove the task; `apphost_tick` sets `app.dirty`,
  otherwise the tasks screen will not redraw after closing.
- **Crash**: `waitpid` with `WIFSIGNALED` → remove the record, alert
  “Application closed” (P1); if it was foreground, go to home.
- **Any chrome (shade, lock, tasks, power)**: first `suspend()` the active task,
  then show the screen. The application is not left mapped under the overlay:
  lower CPU, no z-order or focus conflict. The shade and power dialog remember the
  task (`return_task`) and restore it when closed (Back/swipe/“Cancel”); lock and
  tasks return only when a card is selected, otherwise home.
- **Lock**: suspend, then lock; on unlock, home + tasks, with no auto-resume.
- **Never kill on Home.** The only kill paths are Back (explicit “close”), ×,
  LRU, exit/launch timeout.

### 3.3. Screenshots

| Question | Decision |
|---|---|
| When to capture | before **any** window leaves the screen: Home, shade, lock, tasks, power |
| Which window | `XGetImage(win, 0, 0, min(w,480), min(h,768), AllPlanes, ZPixmap)` — application window, not root: OSK/status/overlays are excluded |
| Why not later | contents of an unmapped/covered window are undefined (X11, no backing store) |
| Format | 240×400 RGB565, nearest 2:1 on X (480→240) and ~1.92 on Y (768→400) |
| Cost | ~737 KB read + downscale; estimate 30–80 ms at 1 GHz, measurement required; one-off “stutter” <100 ms on Home press is acceptable |
| RAM | 192 KB/task, 4 tasks = 750 KiB + temporary 737 KB XImage (freed immediately) |
| OSK | if the keyboard is visible, application window is 480×480: fit the 480×480 capture into 240×400 with `bg` margins (do not stretch) |
| Error | `XGetImage` fails/window is not viewable → `shot_valid=false`, placeholder card (48 icon + name), no “broken” preview |
| Cache | RAM only; tasks die when the shell restarts anyway, do not touch disk |
| Invalidation | new preview on every suspend; do not update in background |
| If capture is expensive | measurement >100 ms → capture in advance: 1 s after adopt and then no more than once every 30 s while the window is on screen; transitions use the ready frame |

Depth: on the device, 16-bit (RGB565 directly); on the host, Xvfb is also 16-bit;
code must handle 24/32 (as `App::render` already does) and ignore alpha. The X
error handler (`on_xerror`) must be installed before the first `XGetImage`, or
`BadMatch` will kill the process.

Verify with a GTK application (pcmanfm) that `XGetImage` on the window includes
child widgets. If not, capture the root region (0,32,480,768), but only when OSK
is hidden (otherwise the keyboard will appear in the preview); this is a fallback,
not the primary path.

### 3.4. Task switcher UI

Overlay `tasks`, `overlay()==true`; draws `ui_bg` (opaque), status bar at the top.
No need to dim home: the background is entirely its own, which is cheaper (480×800
cache copy vs 384k blend).

```
y=32   ┌──────────────────────────────────────────────────┐
       │ TASKS                                      2 OF 4 │ 24 light / 11 dim
y=88   ├──────────────────────────────────────────────────┤
y=96   │ ┌ 210×336 ────────┐   ┌ 210×336 ────────┐        │
       │ │ ┌ 180×300 ────┐ │   │ ┌ 180×300 ────┐ │        │
       │ │ │  preview    │×│   │ │  preview    │×│        │
       │ │ │             │ │   │ │             │ │        │
       │ │ └─────────────┘ │   │ └─────────────┘ │        │
       │ │ Terminal    bg  │   │ Browser      bg │        │
       │ └─────────────────┘   └─────────────────┘        │
y=444  │ ┌ 210×336 ────────┐   ┌ 210×336 ────────┐        │
       │ │ ...             │   │      ＋         │        │
       │ │                 │   │  APPLICATIONS   │        │
       │ └─────────────────┘   └─────────────────┘        │
y=780  └──────────────────────────────────────────────────┘
```

Metrics:

- header: “TASKS” 24 light at (24,44); right side “N OF 4” 11 dim at x=456,
  baseline 60;
- grid: x=24 and 246 (210 + 12), y=96 and 444 (336 + 12); second row ends at
  780 — exactly 20 px from the edge, as required;
- card 210×336, `surf` background; preview 180×300 at (x+15, y+8), `art` background;
  240×400 frame is reduced to 180×300 nearest when drawn — screen is static, this
  is not per-frame work;
  bottom row 28 px: name 14 bold (x+8, y+314), `fit` 130; status 11 dim at right
  (x+202, y+316): “active” / “in background” / “starting…” / “closing…”;
- active (most recent) — 1 px `accent` border, `accent` name; others — `line`
  border, `fg` name;
- “×”: 44×44 at (x+166, y+8), `bg` background (opaque square over the preview),
  `close` icon 16 at (x+180, y+22), `dim` color; on press — `surf2` + `accent`;
- tap card (outside ×) — resume; tap × — close;
- fourth cell when <4 tasks — “＋ APPLICATIONS”: `surf`, plus 34 `accent` centered
  (y+140), caption 12 dim (y+190); tap — close tasks (`back`) and `open("apps")`;
- 0 tasks: “No running applications” 12 dim centered at (240, 400) and
  “Open applications” 13 `accent` (240, 440) with a 44 hit area;
- no scrolling: 4 cards fit exactly; there can be no more than 4;
- no animations; press only changes fill/border;
- Back/Home/long Home — close tasks and go to home (application remains suspended).

Card order: most recently active first (top-left), then descending by
`last_active`. On resume the task moves to the beginning; on close the grid packs
tightly (no animation).

### 3.5. Back / Home / Power policy

**Back** (key `XF86Back`, event0):

| Context | Action |
|---|---|
| OSK keyboard visible | hide keyboard (already implemented) |
| Browser (dillo/netsurf) | Alt+Left; if the title did not change or a second Back occurs within 2.5 s — close |
| Terminal (xterm/st) | close (WM_DELETE → TERM → KILL); there is no “back” in a terminal |
| pcmanfm (P1) | Alt+Up; second Back — close |
| mousepad/mupdf/unknown | Esc; ticker “Again to close”; second Back within 2.5 s — close |
| Shade | close; if opened from an application, restore it (`return_task`) |
| Tasks / menu / power dialog | close; tasks → home, power → restore application if opened from it |
| Launcher/settings | pop stack; at root — home |
| Lock | ignore (recommended; currently unlocks) |

Back **never suspends**: it either navigates or closes.
Closing always uses `WM_DELETE`, then timers; `SIGKILL` only after 4.5 s.

**Home**:

| Context | Short (<650 ms) | Long (≥650 ms) |
|---|---|---|
| Application in foreground | suspend (capture + unmap), home | suspend + tasks |
| Application starting | cancel launch (kill), home | tasks (if there are others) |
| Shade | close shade → home (do not restore return_task) | tasks (application already captured) |
| Tasks | home | home |
| Launcher/screen | home | tasks |
| Lock | ignore | ignore |
| Power dialog | cancel | — |

**Return from shade/power.** `App` stores the task key `return_task`
(32 bytes, not an index — indices shift when a neighboring task closes):
when the shade or power dialog is opened while an application is foreground,
the core calls `apphost_suspend()`, copies `key` from `apphost_task_info()`
and only then calls `push("shade"/"power")`. Back/swipe/“Cancel” close the
screen, find the task with `apphost_find(return_task)` and call
`apphost_resume()`. Home, tapping a tile, card, or settings clear `return_task`
(the user has taken another path). Lock and tasks do not use `return_task`: the
former always goes to home, the latter to the selected card. If the task has
died, resume returns false — simply go home.

**Power**: short — suspend the active task + lock; long — suspend + shutdown
dialog. The application is not killed. The long-press timer in `app.cpp` is
currently shared (700 ms) by Home and Power; to make Home fire at 650, either
lower the shared threshold to 650 (Power fires 50 ms earlier — not a problem),
or use two thresholds. A shared 650 is simpler.

**Menu**: (Menu key) — shell screens only; currently ignored while an application
is active, and remains so.

### 3.6. Back stack

- The shell screen stack does not change: `home → … → overlay`, Back pops.
- There is no shell stack for external applications: `back_mode` + last
  `_NET_WM_NAME`. If Back sent Alt+Left and the title is unchanged, assume no
  navigation occurred and the second Back closes.
- Switching tasks does not add anything to the back stack: `tasks` is an overlay;
  Back from it always goes home, not to the application.
- OSK is not a screen, but a separate window; Back first hides it (already implemented).

### 3.7. Memory and LRU

`MemAvailable` threshold (read `/proc/meminfo` every 5 s):

| MemAvailable | Maximum tasks | Heavy | Action |
|---|---|---|---|
| > 80 MB | 4 | 2 | nothing |
| 50–80 | 3 | 1 | when launching over the limit — close LRU background |
| 40–50 | 2 | 1 | over the limit — close LRU, with notification |
| 30–40 | 2 | 1 | close LRU background before launching a heavy task |
| 25–30 | 1 | 0 | launch lightweight tasks only, otherwise reject |
| < 25 | 0 | 0 | reject + “Low memory” |

Important: the device kernel is **3.4**, and the `MemAvailable` field appeared in
3.14. On the phone, check `grep MemAvailable /proc/meminfo`; if absent, use
`MemFree + Buffers + Cached` (without SReclaimable, with a 10–15% margin) and
interpret thresholds more conservatively. Existing `set_power.cpp` /
`set_apps.cpp` already parse `MemAvailable` — if the field is absent they silently
use the default; this is a separate small fix.

- “Heavy”: RSS > 20 MB or on the known list (pcmanfm, netsurf, mousepad, mupdf).
  xterm ~10 MB, dillo ~15 MB — lightweight.
- LRU — oldest `last_active` among `BACKGROUND`; do not touch foreground,
  `STARTING`, or `CLOSING`.
- Close due to memory: `WM_DELETE` → 1 s → SIGTERM (shorter than normal close:
  memory is more important); notification “Closed <name> — low memory” (P1, pri 0).
- Do not block the UI while waiting: send the signal and launch; if memory remains
  below 25 MB, reject.
- `oom_score_adj`: shell −900, foreground 0, background +300 (P1; requires
  `prctl`/`/proc/<pid>/oom_score_adj`, shell root).
- Never kill foreground and never kill based only on battery percentage.
- `swap` on SD exists (256 MB), but is slow — do not count it in the budget.

Why 4, not 6: 4 cards in a 2×2 grid fit exactly in 96…780 without scrolling;
5+ requires scrolling and increases application memory. 4 tasks × 192 KB previews
= 750 KiB, which is not a problem; the problem is the applications' own RSS.

### 3.8. X details that break naive implementations

1. **UnmapNotify from us and from the client cannot be distinguished.** Before
   `XUnmapWindow(win)`, set `expect_unmap=now`; ignore `UnmapNotify` for this
   window for 1 s in the handler. An unexpected unmap **does not kill** the task:
   move it to `BACKGROUND` (the client may have temporarily hidden the window);
   `waitpid`/`DestroyNotify` complete cleanup.
2. **Content loss on remap.** After `XMapRaised`, the server sends Expose (there is
   no backing store); the client must redraw. xterm and dillo do. For clients that
   do not, show a placeholder card and an honest status; do not show an old preview
   as “live”.
3. **Second task window** (dillo/pcmanfm dialog): do not make it a new task. If the
   task already has `win`, a second toplevel from its pid is auxiliary: raise it
   above `win`, do not change `win`. On suspend, also unmap auxiliary windows (P1;
   for P0, do not map them over another task, or a background dialog will appear
   above foreground).
4. **Focus.** Every transition ends with an explicit `XSetInputFocus`:
   suspend → shell, resume → task window, push chrome → shell.
   `RevertToPointerRoot` on focus unmap does not guarantee return.
5. **Grabs.** Keep `XGrabKey` for Back/Home/Power while there is at least one task;
   remove grabs when there are 0 tasks. Otherwise, after resume the application
   will intercept Home/Back. `AnyModifier` is already used.
6. **Window stack.** New client windows appear **above** the shell. Do not fight
   over z-order: before shade/lock/tasks, `unmap` the application and the shell
   will be on top automatically. This also fixes the current bug: the shade, when
   opened by swipe while dillo is active, is currently drawn under its window.
   There is no need to raise the shell with `XRaiseWindow`.
7. **Launch and adopt.** Extend `pid_is_ours` to all tasks; `try_adopt` assigns the
   window to the task whose pid/ancestor matches. `probe_windows` handles all
   `STARTING` tasks, every 500 ms, for up to 15 s.
8. **X errors.** One `XSetErrorHandler(on_xerror)` per process (not per launch),
   or `BadMatch` from `XGetImage` will terminate the shell.
9. **Close with dialog.** `WM_DELETE` → 2 s may kill an application showing “Save?”.
   For a background task this is an intentional force-close (there is nobody to
   show the dialog to anyway). For foreground: if a new toplevel arrives from the
   task during CLOSING, cancel timers, raise the dialog, let the user respond (P1).
   For P0, use 2/2.5 s timers and a warning in the documentation.

### 3.9. apphost API (sketch)

Keep existing names so `browser.cpp`, `terminal.cpp`, `app.cpp` do not need to be
rewritten in full; add new ones.

```cpp
// include/apphost.h
bool apphost_launch(const char *cmd);   // as now: new task or focus by key
bool apphost_active();                  // at least one task (for OSK icon)
bool apphost_foreground();              // mapped foreground exists (no shell overlay above it)
bool apphost_launching();               // foreground is STARTING
bool apphost_has_window();
const char *apphost_command();          // foreground command
bool apphost_is_browser();
bool apphost_back();                    // according to back_mode; true = consumed
void apphost_close();                   // close foreground (Back)
void apphost_close_task(int i);         // × in tasks
void apphost_tick(unsigned now);

// new
int  apphost_suspend();                 // capture + unmap + shell; task index or -1
bool apphost_resume(int i);             // map/raise/focus
int  apphost_task_count();
int  apphost_fg_index();                // -1 if no foreground
int  apphost_find(const char *key);     // -1 if no task with this key
struct TaskInfo {
    const char *key;      // xterm, dillo, ...
    const char *name;
    const char *icon;
    int state;            // T_*
    bool heavy;
    bool shot_valid;
    int  rss_kb;
    unsigned last_active;
};
bool apphost_task_info(int i, TaskInfo *out);
const uint16_t *apphost_task_shot(int i, int *w, int *h); // 240×400
void apphost_focus_app();
void apphost_set_content_height(int h);
bool apphost_on_map(unsigned long w);
void apphost_on_destroy(unsigned long w);
void apphost_note_key(int ksym, bool down);
```

`apphost_active()` semantics change: it used to mean “there is a foreground
application”; it now means “there are tasks”. All locations where “application
is currently on screen” matters (`app.cpp` key routing, `osk.cpp`, keyboard icon
in `ui.cpp`) must switch to `apphost_foreground()`. Do this in one commit,
otherwise keys and the icon will be routed incorrectly.

Task index in `apphost_task_info/resume/close_task`: 0 is most recent (card order),
then descending by `last_active`. Calls in `screens/browser.cpp` and
`screens/terminal.cpp` must also be updated: the button label must check
foreground/`apphost_find`, or the button will lie when dillo is suspended
(“Open” even though the task exists).

---

## 4. Data and state

| Data | Where | Size | Survives reboot | Cleanup |
|---|---|---|---|---|
| Notifications | `/var/lib/graphite/notifications.jsonl` | ≤32 × 500 B ≈ 16 KB | yes | 7 days / 32 items / “CLEAR” |
| Inbox | `/run/graphite/inbox/` | ≤256 KB | no | at shell startup, after reading |
| Boot id | `/run/graphite/boot_id` | 40 B | no | at startup |
| DND/flags | `/etc/lgtest-settings` | <100 B | yes | manually/settings reset |
| Tasks | RAM apphost | ~1 KB + ≈750 KiB previews | no | when task/shell exits |
| Screen caches | RAM (as now) | covers, etc. | no | as now |

`/run` is tmpfs: 64 files × 4 KB = 256 KB, safe. Do not touch disk or wear out
the SD card. On shell restart without reboot, tasks die (shell children), previews
are unnecessary; notifications are loaded from disk, `seen` is preserved.

---

## 5. Stages

### P0-A. Notifications (2–3 days, can be done on host)

Files:

- new `include/notify.h`, new `src/notify.cpp` (ring, dedup, limits, JSONL,
  inbox, DND, ticker);
- `src/app.cpp` — `notify_tick`, `notify_draw_banner`, tap on status;
- `src/ui.cpp` — icon/count/ticker in `ui_status`;
- `src/gfx.cpp` — `bell`, `bell_off` icons;
- `screens/shade.cpp` — full list, 5 tiles, “CLEAR”, DND;
- `screens/lock.cpp` — 3 notification rows;
- `src/settings.cpp` — unchanged (keys through existing API);
- `install.sh`, `session-shell` — `mkdir -p /var/lib/graphite
  /run/graphite/inbox`.

Done when:

- 6 QA messages from the file are visible in the shade, badge is correct;
- MPD and Wi-Fi are ongoing, update, are not counted or dismissed;
- DND suppresses banner/ticker but does not lose events;
- after reboot, the list is intact, badge is 0, ongoing items are recreated;
- shell RSS increased by <1 MB, disk writes occur only on events;
- `preview.sh shade` with `GRAPHITE_NOTIFY_DEMO=6` shows 5 cards and “1 MORE”.

When another application is active, the shade opens after `apphost_suspend()`;
this is part of P0-B. Until then, test the shade on home/settings; the ticker over
an application works only as an indicator (tapping it while foreground is handled
in P0-B, otherwise the screen will open under the window).

### P0-B. Tasks (3–4 days, device only)

Files:

- `include/apphost.h`, `src/apphost.cpp` — task array, suspend/resume, captures,
  LRU, back_mode;
- new `screens/tasks.cpp` — 2×2 grid;
- `screens/screens.h`, `src/registry.cpp`, `screens/stub.cpp` — `tasks` id
  (core, orchestrator);
- `src/app.cpp` — long Home 650 → tasks, Home → suspend, key routing via
  `apphost_foreground()`, Back policy, suspend on lock/power;
- `screens/lock.cpp` — notification rows only (P0-A);
- `os/README.md`, `SHELL-TODO.md` — after verification.

Done when:

- xterm + dillo = 2 tasks; Home kills neither (pid is alive);
- long Home shows 2 cards with previews; tapping restores the application with
  redrawn content; × closes it;
- at `MemAvailable ~40 MB`, launching a third task follows the §3.7 table (LRU or
  rejection), with no OOM;
- 10-minute session: no hangs, focus is not lost, OSK still raises only for foreground;
- Back in dillo = Alt+Left, second = close; Back in xterm = close;
- shade opened by swipe while dillo is active shows dillo suspended and restores it
  on close;
- after lock, application is suspended and does not draw; unlock goes to home.

### P1. Actions, DND details, heads-up (3–5 days)

- actions in cards (2 × 44) and handling for `retry`/`open`/`dismiss`;
  `retry` only for allowlisted tasks/registry entries, not arbitrary commands;
- swipe-to-dismiss right >100 px;
- lock: tapping a row unlocks and opens the notification;
- ticker prompts (“Again to close”, “Esc sent”);
- heads-up 480×64 separate window + `XShape` (check `libXext`);
- crash/LRU notifications, `ref` → “Open again”;
- `set-notify` (DND, banners, music, test, clear) or replace the “DO NOT DISTURB”
  row in `set-sound`;
- `oom_score_adj`, “not responding” via `_NET_WM_PING` (if applications support it);
- close foreground task with dialog: cancel timers when a new window appears.

### P2. D-Bus, socket, grouping

- unix-socket + `graphite-notify` CLI, if an external client appears;
- `graphite-notifyd` D-Bus bridge (libdbus, writes to inbox), bus in `session-shell`;
- group by app, `×N` count, 7-day history;
- DND schedule, per-app DND;
- SIGSTOP background tasks after 5 minutes (CPU savings), if battery requires it —
  with caution around X clients;
- second terminal (multiple xterm instances) and “new/existing” choice.

Dependencies: P0-A and P0-B are independent by files (notify vs apphost);
P0-B requires the phone, P0-A is fully testable on host. P1 heads-up requires
`-lXext`; P2 D-Bus requires the `dbus`/`libdbus` package and a bus decision.

---

## 6. Test plan

### 6.1. Host (Xvfb 480×800×16, no phone)

Setup: `sh os/host-build.sh`, `sh os/preview.sh <screen>`.
Demo data is dev-env only, not for release:

- `GRAPHITE_NOTIFY_DEMO=6` — 6 fake notifications (2 ongoing, 4 alert,
  one has 2 actions);
- `GRAPHITE_TASKS_DEMO=3` — 3 tasks with placeholder previews;
- `GRAPHITE_RUN_DIR=<runtime-dir>/graphite` — to avoid accessing `/run` on host.

| # | Check | Expected |
|---|---|---|
| H1 | `preview.sh shade` with 6 notifications | 5 cards, “1 MORE”, bottom 724/768, handle 780 |
| H2 | `preview.sh tasks` with 3 tasks | 2×2 grid, 3 cards + “＋”, edges 24/456, rows 96/444 |
| H3 | `preview.sh lock` with 3 notifications | 3 rows 396…516, track 540…604 |
| H4 | DND + 6 notifications | no ticker/banner, accent tile, “SILENT” in section |
| H5 | 0 notifications | “No notifications” at 340, “CLEAR” dim |
| H6 | 0 tasks | empty state + “Open applications” |

Store screenshots in `artifacts/host/`, compare using `qadiff.py` with the
baseline; new screens require new reference captures.

### 6.2. Device (read-only until test starts; after that, with permission)

| # | Steps | Expected |
|---|---|---|
| D1 | Launch xterm from “Terminal”, Home | shell on home, xterm in `ps`, window unmapped |
| D2 | Long Home ≥650 ms | tasks, 1 card with xterm preview |
| D3 | Tap card | xterm restored, text intact (Expose), focus in xterm |
| D4 | Open dillo, Home, Long Home | 2 cards; dillo first |
| D5 | × on xterm | card “closing…”, pid disappears ≤5 s, dillo untouched |
| D6 | Back in dillo | Alt+Left (previous page), second Back — close |
| D7 | Launch 3rd task at `MemAvailable ~40` | per §3.7 table: LRU closed or rejection, no OOM |
| D8 | QA file in inbox | ticker 5 s, badge +1, card in shade |
| D9 | DND on → QA pri 1 | no ticker, badge +1, card present |
| D10 | DND on → battery pri 2 | ticker present (bypass) |
| D11 | Reboot | notifications intact, badge 0, ongoing MPD/Wi-Fi recreated |
| D12 | Lock with active xterm | xterm suspended, lock shows 3 rows; unlock → home |
| D13 | Swipe shade while dillo is active | dillo suspended, shade visible; close restores dillo (Expose) |
| D14 | 10 minutes: 2 tasks, 20 notifications, 5 locks | no D-states, shell RSS < +2 MB, idle CPU < +5% |

### 6.3. Measurements

- Read-only check before test: `grep -E 'MemTotal|MemFree|MemAvailable|Buffers|Cached' /proc/meminfo`
  (whether `MemAvailable` exists on 3.4) and `apk info -e libxext` (for P1 heads-up).
- `XGetImage` + downscale: log with `GRAPHITE_APPHOST_DEBUG=1`,
  `<runtime-dir>/apphost.log`, marker `shot ms=…`; target <100 ms.
- Memory: `MemAvailable` before/after 2–3 tasks; `ps -o rss` for shell,
  Xorg, applications; previews — 192 KB × N (verify in code/log).
- Disk: `ls -l /var/lib/graphite/notifications.jsonl`; writes only on events,
  no more than once/s.
- Rendering: `clock_gettime` around `shade::draw` (debug) — target <30 ms.

---

## 7. Risks

| # | Risk | Prob. | Impact | Mitigation |
|---|---|---|---|---|
| 1 | `XGetImage` on a covered/suspended window returns garbage | high | broken previews | capture only while window is visible, before unmap; check `map_state` |
| 2 | Application does not redraw after remap | medium | blank window | test xterm/dillo; placeholder card; do not show old image as live |
| 3 | `UnmapNotify` mistaken for close | high | “closes by itself” | `expect_unmap` + 1 s; unexpected unmap = background |
| 4 | Two windows of one task (dialog) appear over another | medium | confusion | auxiliary window list; unmap on suspend (P1) |
| 5 | `WM_DELETE` kills application with unsaved dialog | medium | data loss | 2 s grace; cancel timer on new window (P1); force-close for background |
| 6 | Banner not visible over application | high | “no notifications” | P0 ticker in status bar; P1 separate window with input-shape |
| 7 | Shade under application window | high | shade unusable in dillo | mandatory `suspend` before chrome; current bug — fix in P0-B |
| 8 | Notification spam (MPD/Wi-Fi) | medium | annoyance, disk writes | ongoing, dedup, rate limit, MPD only on track change |
| 9 | Inbox: symlink/junk/overflow | low | root reads another user's file | `O_NOFOLLOW`, regular, 4 KB, 64 files, unlink |
| 10 | D-Bus client appears unexpectedly | low | “notify-send does not work” | P2 bridge; document honestly for now |
| 11 | Focus goes to the wrong place (OSK/shell/application) | medium | input does not work | explicit `XSetInputFocus` on every transition; test D3/D4 |
| 12 | LRU kills the wrong task / OOM | medium | application loss | do not touch foreground; thresholds; log; `oom_score_adj` (P1) |
| 13 | 4 tasks on 400 MB | medium | OOM | cap at 4, 2 heavy, thresholds, reject below 25 MB |
| 14 | Long Home fires together with short Home | low | accidental tasks | 650 ms + `key_long_fired` + 120 ms dedup (already present) |
| 15 | Full-screen shade: redraw cost | low | lag | 16k blend instead of 384k — cheaper; measure <30 ms |
| 16 | Home unlocks lock | present | security | P1: ignore Home/Back on lock; PIN is P2 |
| 17 | Application keeps ticking in background | medium | battery | no SIGSTOP in P0; measure CPU; P2 freeze |
| 18 | Clock/time jumps (RTC) | low | “14:32” is wrong | ts = epoch; for 1970/future — “—:—” |

---

## 8. Three conclusions and what to do first

**Conclusion 1. Notifications are an internal shell subsystem, not a service.**
95% of events originate in the shell itself; a daemon and D-Bus add a process,
RSS, and lifecycle for the sake of a QA script. P0: `notify.cpp` + file queue;
socket and D-Bus only if an external client actually appears, via a bridge, not
in the shell.

**Conclusion 2. The task switcher is constrained not by screenshots, but by apphost.**
Preview costs 192 KB and is captured once; the complexity lies in multitasking,
`unmap/remap`, focus, and the fact that Home currently kills the application.
First refactor apphost and the Home/Back contract, then build the card grid.

**Conclusion 3. Home must stop meaning “close”.**
Short Home — suspend, Back — close, long Home — tasks. Without this separation,
the switcher is pointless: the user cannot tell “suspended” from “killed”, and
400 MB of memory does not forgive unnecessary launches.

**What to do first.** One step, without UI: **refactor `apphost` for 2–3 tasks
with `suspend/resume` and `capture-before-cover`, and switch Home to `suspend`** —
test on the phone with xterm and dillo (D1–D4). This is the only part with unknown
X behavior (unmap/remap, `XGetImage`, focus), and it blocks the entire task shade.
Notifications (P0-A) can proceed in parallel with a second agent: they do not touch
`apphost` and can be tested on the host.

---

## Appendix A. Files, icons, strings

**New files**

- `include/notify.h`, `src/notify.cpp` — model, ring, inbox, DND;
- `screens/tasks.cpp` — task grid (`make_tasks`).

**Core changes (orchestrator)**

- `src/app.cpp` — long Home 650 → tasks, Home → suspend, `return_task` for
  shade/power, key routing, `notify_tick/draw`, tap on status;
- `src/ui.cpp` — `notify_draw_status`;
- `src/gfx.cpp` — `bell`, `bell_off` icons;
- `src/registry.cpp`, `screens/screens.h`, `screens/stub.cpp` — `tasks` id;
- `include/apphost.h`, `src/apphost.cpp` — multitasking, captures,
  suspend/resume, LRU;
- `install.sh`, `session-shell` — directories `/var/lib/graphite`,
  `/run/graphite/inbox`.

**Screen changes**

- `screens/shade.cpp` — new notification center;
- `screens/lock.cpp` — notification rows (suspend is called by the core);
- `screens/set_sound.cpp` (P1) — remove “no notification system”;
- `screens/set_root.cpp` + new `set_notify.cpp` (P1) — “Notifications” section.

**Icons**: `bell` (dome + tongue + base, 1.6 stroke), `bell_off`
(same + diagonal) — no new raster assets; `close`, `plus`, `grid`, `music`,
`wifi`, `terminal`, `globe`, `folder`, `note` already exist.

**Strings**: “No notifications”, “CLEAR”, “SILENT”, “Do not disturb”,
“TASKS”, “N OF 4”, “active”, “in background”, “starting…”, “closing…”,
“No running applications”, “Open applications”, “Application did not open”,
“Low memory”, “Unlock”.

## Appendix B. Open questions

1. **Notification sound** — no: there is no system mixer or telephony, and MPD
   is the only sound; DND is visual. If needed, use a separate short WAV through
   ALSA, outside this plan.
2. **Screen turns off/DPMS** — the shell has no real sleep; lock is a screen.
   Notifications on lock are visible only while the screen is on.
3. **Full-screen shade vs 470 panel** — full screen selected: 5 cards instead of 2,
   less blend work. If the owner wants “a shade with home visible behind it”,
   restore the 470 panel and keep 2 cards + “MORE”.
4. **One xterm or several** — P0: one per `key`; a second is P2.
5. **MPD/Wi-Fi as ongoing vs separate “Now” section** — ongoing selected: one list,
   dedup, unified tap. If visually mixed with alerts, add P2 group header.
6. **PIN/lock** — outside scope; but Home/Back unlocking lock should be blocked
   with a separate small fix (P1).
