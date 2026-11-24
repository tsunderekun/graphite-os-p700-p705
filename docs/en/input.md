# INPUT-PLAN — unified text input in the graphite shell

> Public change plan. This describes the target architecture and verification
> criteria; it does not claim that these changes have already been implemented.

> Date: 24.11.2026. Plan only: no code was changed and the phone was not touched (read-only).
> Based on reading `os/src/osk.cpp`, `os/screens/notes.cpp`, `os/screens/terminal.cpp`,
> `os/screens/files.cpp`, `os/src/app.cpp`, `os/src/apphost.cpp`,
> `notes/APP-INTEGRATION-PLAN.md`, `notes/SHELL-TODO.md`,
> `notes/shell/DESIGN-SYSTEM.md`, `os/build.sh`, `os/preview.sh`.

## 0. Summary

- The shell currently has **three different on-screen keyboards** (OSK for external X applications,
  notes, terminal) and **three different physical keyboard implementations**. There is no shared code;
  notes does not accept physical letters at all.
- There is no input in Files: `open_rename`/`open_mkdir` display “Keyboard input”, but
  it is handled only in `key()` (`files.cpp:1808`), so a hardware keyboard is required.
- The proposal splits input into four layers: **layout** (data and geometry only),
  **view** (draws and detects taps), **sink** (buffer or XTest), and
  **host** (screen canvas or separate X window). One table serves all screens;
  profiles only change the set of rows and dimensions.
- **An in-canvas keyboard** (as in notes/terminal) covers 90% of scenarios:
  it needs neither focus nor XTest and has no races. A separate OR window remains only for
  external applications—otherwise our canvas is not visible at that time.
- For external applications, P0 is not new keys but fixing the mechanics:
  pre-mapping keycodes instead of `XChangeKeyboardMapping` for every character, restoring
  focus without `XRaiseWindow` on every key, sticky Shift, and choosing Ctrl chords
  based on WM_CLASS.
- P0: module + inline keyboard in Files (rename/new folder) + migrate
  notes and terminal to the shared view. P1: selection/copy/paste, X-selection.
  P2: PIN (numeric layout), bookmarks, search. Do not implement predictive input or XIM.

---

## 1. Current state (fact-based inventory)

### 1.1. Three keyboards

| | OSK (`src/osk.cpp`) | notes (`screens/notes.cpp:1249`) | terminal (`screens/terminal.cpp:458`) |
|---|---|---|---|
| Surface | separate `override_redirect` X window, 480×288 at (0,512) | drawn in screen canvas | drawn in screen canvas |
| Focus | does not take it (OR + no `XSetInputFocus`) | not applicable | not applicable |
| Rows | 3×3 + bottom: `y=8/76/144/212`, `RH=64`, `pad=8`, `gap=4` (`osk.cpp:24-26`) | nav + 3 letter rows + bottom: `y=520/576/632/688/744`, `RH=52`, `pad=8`, `gap=4` (`notes.cpp:47-50`) | 4 rows: `pad=6`, `gap=3`, `RH=66` (`terminal.cpp:460`) |
| Layers | letters / 123 / terminal (`layer=0,1,2`) | letters / 123 | letters / 123 |
| RU letters | `\u0439\u0446\u0443\u043A\u0435\u043D\u0433\u0448\u0449\u0437\u0445`, `\u0444\u044B\u0432\u0430\u043F\u0440\u043E\u043B\u0434\u0436\u044D`, `\u044F\u0447\u0441\u043C\u0438\u0442\u044C\u0431\u044E.` | `…\u0431\u044E\u044A` | `…\u0431\u044E\u044A` |
| Sink | XTest | note body, cursor `(line,col)`, undo/redo | command line, history |
| Special | Ctrl/Alt sticky for one key; automatic layer 2 for xterm | navigation row (←/→/HOME/END/UNDO/REDO) is part of keyboard | ↑/↓ navigate history while keyboard is open |
| Missing | clipboard, cursor, Shift for special keys, selection, paste | — | — |

The RU layout in OSK and notes/terminal **differs** in the third row (`\u0431\u044E.` vs
`\u0431\u044E\u044A`), and EN differs in the number of keys in the second row (`asdfghjkl` — 9 in OSK,
`asdfghjkl;` — 10 in notes/terminal; the semicolon is in OSK's `123` layer).
This already results in different behavior on the same hardware.

### 1.2. Three physical keyboard implementations

| Screen | How | What does not work |
|---|---|---|
| files (`files.cpp:95-134`) | `keysym_cp()` + `XQueryKeymap` for Shift | — (most complete) |
| terminal (`terminal.cpp:110-121`) | `ksym_char()`, Cyrillic via table | `XLookupKeysym(...,0)` — uppercase is not received; Shift is lost |
| notes (`notes.cpp:1939-2006`) | special keys only | **physical letters are not entered at all** |

`app.cpp:286` reads `XLookupKeysym(&ev.xkey, 0)`, i.e. level 0 without
modifiers. Therefore each on-screen implementation works around this independently.

### 1.3. OSK defects relevant to the plan

1. **Keycode for every character.** `type_cp()` (`osk.cpp:309-327`) calls
   `XChangeKeyboardMapping` + `XSync` when the symbol changes; the keycode is always
   the maximum (`scratch_keycode`, `osk.cpp:267`). Each such call broadcasts
   `MappingNotify` to all clients; this is noticeable at 1 GHz, and some toolkits
   reread the keymap.
2. **Brute-force focus.** After every key, `apphost_focus_app()`
   (`osk.cpp:306`, `apphost.cpp:535`) calls `XRaiseWindow` + `XSetInputFocus` on
   the top-level. This steals focus from a child window (xterm keeps input on VT100),
   raises the application above chrome, and costs two unnecessary requests per key.
   There is no check that focus is already there.
3. **No sticky Shift.** `shift` affects only character case; `Shift+←`
   (selection) cannot be used in an external application.
4. **No copy/paste/selection.** The terminal layer supports `^C/^D/^Z/^L`,
   but not `Ctrl+Shift+C/V` (xterm) or `Ctrl+C/V` (GTK/FLTK).
5. **Ctrl chords depend on the XKB group.** `latin_of()` maps the \u0419\u0426\u0423\u041A\u0415\u041D→QWERTY
   position, but `XKeysymToKeycode(XK_c)` returns a keycode and the server applies
   the *current* group. If the group is RU, xterm will not receive 0x03. Group 0 (US)
   must be fixed or separate alias keycodes used.
6. **No Ctrl+Shift+letter** (the lower layer does not support two modifiers at once).
7. **Layout rebuilt on every `draw()`** (`osk.cpp:250)—minor, but an unnecessary
   `std::string` per key on every Exposure.
8. RU keys: 11 in a row: `(480-16-10*4)/11 = 38` px wide, with `RH=64`.
   Height is within range; width is below 44 (see §6.1).

### 1.4. Already verified; no need to recheck

From `APP-INTEGRATION-PLAN.md` §1.3 and §4 (host, Xvfb 21.1, 480×800×16):

- clicking the keyboard OR window at (0,512) **does not change** `XGetInputFocus`;
- `XTest` + Unicode keysym (U+0439, etc.) reach xterm and GTK3/zenity;
- resizing xterm 480×712 ↔ 480×472 recalculates the grid;
- `setxkbmap -layout us,ru` did not switch the group in Xvfb, so Unicode keysym is the
  primary path.

The plan does not revisit these facts; it fixes the issues on top of them.

---

## 2. Architecture

### 2.1. Four layers

```
             ┌──────────────────────────────┐
             │  KbModel: layout             │  data only, no X or Gfx
             │  rows + weights + layer/lang │  one table for all screens
             └──────────────┬───────────────┘
                            │ keys(): vector<KbKey>
         ┌──────────────────┴───────────────────┐
         │                                      │
┌───────▼────────┐                    ┌────────▼─────────┐
│ KbView (canvas)│                    │ osk.cpp (X window)│
│ draw + hit     │                    │ Canvas + XPutImage│
└───────┬────────┘                    └────────┬─────────┘
        │ InputEvent                           │ InputEvent
        ▼                                      ▼
┌────────────────┐   ┌─────────────────┐   ┌──────────────────┐
│ ShellInput     │   │ NotesInput      │   │ XTestInput       │
│ (field, UTF-8, │   │ TerminalInput   │   │ (external X win) │
│  caret, undo)  │   │ (adapters)      │   │ keysym + mods    │
└────────────────┘   └─────────────────┘   └──────────────────┘
```

- **KbModel** knows only the layout and its own state (`lang`, `layer`, `shift`).
  It does not know where a character goes.
- **KbView** draws `KbKey` and converts a tap to `InputEvent`. The same view
  works in the canvas and in an X window (with its own `Canvas`).
- **InputSink** is the sink interface: `put_char`, `action`, `put_text`.
- **Host** owns the surface: a screen (480×800 canvas) or `osk.cpp`
  (480×288 window).

### 2.2. Why two hosts, not one

| Option | Advantages | Disadvantages | Decision |
|---|---|---|---|
| Everything in canvas | no X window, no focus, no races | external applications draw in their own X window above our canvas; a keyboard cannot be drawn “in the canvas” beneath them | shell screens only |
| Everything in a separate X window | one surface implementation | the sink (buffer/caret) lives in the screen; screen state would have to be passed through globals and drawn twice; taps go to `osk_event` instead of `ScreenView` | no |
| **Shared model+view, two hosts** | only 40 lines of `PutImage`/`draw` are duplicated | — | **yes** |

Key detail: in the canvas, the keyboard is an ordinary part of the screen; the screen
decides what to draw above it (cursor, hint, field), and `tap()` distributes coordinates.
In the X window it is the reverse: `osk_event()` receives `ButtonRelease` first
(`app.cpp:218`) and must not pass it to the screen.

### 2.3. Files

```
os/include/input.h          new interfaces (KbModel, KbKey, InputSink, TextField)
os/src/input_kb.cpp         layout tables, geometry, KbModel::press
os/src/input_text.cpp       TextField (UTF-8, caret, filter, mask)
os/src/input_xtest.cpp      XTestInput: pre-mapping, modifiers, focus guard
os/src/osk.cpp              remains a thin host: window + event + osk_* API
os/src/app.cpp              physical keyboard → InputEvent (orchestrator)
os/screens/{notes,terminal,files}.cpp   sinks and KbView calls
```

`build.sh` builds `src/*.cpp` and `screens/*.cpp` without recursion
(`build.sh:23`), so new files go **flat in `src/`**—no `build.sh` change is needed.

`osk.h` retains the current API (`osk_visible/toggle/hide/event`), so
`app.cpp` changes very little in P0.

---

## 3. API sketches

### 3.1. `include/input.h`

```cpp
// Unified graphite input. Model and view know nothing about X; XTest is only a sink.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
struct Gfx;

enum KbProfile { KB_NOTES = 0, KB_TERM = 1, KB_XAPP = 2, KB_COMPACT = 3, KB_PIN = 4 };
enum KbLayer   { L_ABC = 0, L_123 = 1, L_TERM = 2, L_NUM = 3 };
enum ShiftMode { SH_OFF = 0, SH_ONCE = 1, SH_CAPS = 2 };

enum KbAct {
    A_CHAR = 0, A_SPACE, A_DEL, A_DEL_FWD, A_ENTER,
    A_SHIFT, A_LAYER, A_LANG,
    A_LEFT, A_RIGHT, A_UP, A_DOWN, A_HOME, A_END, A_PGUP, A_PGDN,
    A_TAB, A_ESC, A_UNDO, A_REDO, A_SELECT_ALL,
    A_PASTE, A_COPY, A_CUT,
    A_MOD_CTRL, A_MOD_ALT, A_MOD_SHIFT
};

enum KbMod { KM_SHIFT = 1, KM_CTRL = 2, KM_ALT = 4 };

struct KbKey {
    int x = 0, y = 0, w = 0, h = 0;
    int act = A_CHAR;
    uint32_t cp = 0;          // for A_CHAR
    std::string label;        // UTF-8, already in the required case
    int mods = 0;             // KM_* — applied with the key (^C, Shift+←)
};

struct KbGeom {               // all coordinates in the 480×800 canvas
    int x = 0, y = 512, w = 480, h = 288;
    int pad = 6, gap = 3, rowh = 52;
};

struct InputEvent {
    int act = A_CHAR;
    uint32_t cp = 0;
    int mods = 0;
    const char *text = nullptr;   // for A_PASTE (owned by caller)
};

struct InputSink {
    virtual ~InputSink() {}
    virtual void put_char(uint32_t cp, int mods) = 0;  // mods — KM_* for this key
    virtual void action(int act, int mods) = 0;   // ←/→/DEL/ENTER/HOME/...
    virtual void put_text(const std::string &) {} // paste
    virtual int  caret_cp() const { return -1; }  // -1 = sink does not expose caret
};

class KbModel {
public:
    void set_profile(KbProfile p, const KbGeom &g);  // rebuild table
    void set_geom(const KbGeom &g);
    void set_lang(bool ru);
    void set_layer(KbLayer l);
    void set_shift(ShiftMode m);
    bool ru() const; KbLayer layer() const; ShiftMode shift() const;
    const std::vector<KbKey> &keys() const;

    int at(int x, int y) const;                       // -1 if outside
    // Changes its state (SHIFT/LAYER/LANG) and emits an event if required.
    bool press(const KbKey &k, InputEvent &out);
private:
    std::vector<KbKey> keys_;
    KbProfile prof_ = KB_NOTES;
    KbGeom geom_;
    bool ru_ = true;
    KbLayer lay_ = L_ABC;
    ShiftMode shift_ = SH_OFF;
    bool ctrl_ = false, alt_ = false;                 // sticky
};
```

`press()` returns `false` only for “service” keys (SHIFT/LAYER/LANG):
`KbView` simply redraws the keyboard afterward. Sticky Ctrl/Alt/Shift are held
in the model and passed in `InputEvent.mods`; the sink does not store modifier state,
and the model clears them after the press.

### 3.2. Layout table (single source)

```cpp
// input_kb.cpp
static const char *kRu[3] = {"\u0439\u0446\u0443\u043A\u0435\u043D\u0433\u0448\u0449\u0437\u0445", "\u0444\u044B\u0432\u0430\u043F\u0440\u043E\u043B\u0434\u0436\u044D", "\u044F\u0447\u0441\u043C\u0438\u0442\u044C\u0431\u044E\u044A"};
static const char *kEn[3] = {"qwertyuiop",  "asdfghjkl",   "zxcvbnm,./"};
static const char *kSym1[3] = {"1234567890", "-/:;()&\"'@", ".,?!_+=*%#~"};

// Terminal layer is for KB_XAPP (XTest) only. It is not needed in the canvas.
struct KbDef { const char *label; int act; uint32_t cp; int mods; };
static const KbDef kTerm1[] = { /* ESC TAB CTRL ALT ← ↑ ↓ → */ };
static const KbDef kTerm2[] = { /* HOME END PGUP PGDN | ~ / - */ };
static const KbDef kTerm3[] = { /* ^C ^D ^Z ^L DEL _ $ ? */ };
```

Rows are described using **weights**, not pixels, so the same row fits in both
288 and 224:

```cpp
struct RowSpec { std::vector<float> w; };   // weight 1.0 = standard key
// bottom row, common to all profiles: {1.15, 1.0, 3.6, 1.0, 2.0} → 123 ENG SPACE DEL ENTER
```

Width: `unit = (inner - gap*(n-1)) / sum(w)`, with the last key taking the remainder
(`notes.cpp:1348-1351` already does this; carry over this approach).

Profiles are data only (row count, `rowh`, `pad`, `gap`, layer set);
layout code, view, and sink are shared:

| Profile | Height | Rows / rowh / pad,gap | For |
|---|---|---|---|
| `KB_NOTES` | 288 | nav + 3 letter rows + bottom, 52, 8/4 | notes (preserve as-is) |
| `KB_TERM` | 288 | 3 letter rows + bottom, 66, 6/3 | terminal in canvas |
| `KB_XAPP` | 288 | 3 letter rows + bottom, layer `L_TERM`, 64, 8/4 | OSK for external X apps |
| `KB_COMPACT` | 224 | 3 letter rows + bottom, 44, 6/2 | files: name/search |
| `KB_PIN` | 288 | 3×3 digits + `C 0 DEL` row, 60, 24/12 | lock/PIN |

Do **not** change the geometry of existing presets: notes uses 52, terminal 66, and
OSK 64—these are deliberate choices for specific surfaces, not accidents.
Character tables, layers, state, and sink become shared; a 2–4 px geometry difference
breaks nothing, whereas a difference in letters does.

### 3.3. Sink for shell fields

```cpp
// input_text.cpp
class TextField : public InputSink {
public:
    std::string v;            // UTF-8
    int caret = 0;            // in code points
    int anchor = -1;          // selection, -1 = none
    int max_cp = 240;
    std::string deny = "/";   // characters not accepted (file names)
    bool mask = false;        // PIN: draw dots

    void put_char(uint32_t cp, int mods) override;  // mods ignored by field
    void action(int act, int mods) override;   // BACKSPACE/DEL/←/→/HOME/END/CLEAR
    void put_text(const std::string &s) override;
    int  caret_cp() const override { return caret; }
    std::string shown(bool masked = false) const;
private:
    void insert(const std::string &s);
    void erase(int dir);
};
```

UTF-8 boundaries are already implemented in `notes.cpp:56-122` and
`files.cpp:80-83`; **move** them to `input_text.cpp`, rather than copying them
a fourth time.

### 3.4. XTest sink

```cpp
// input_xtest.cpp
struct XTestInput : InputSink {
    void put_char(uint32_t cp, int mods) override;  // ready keycode + modifiers
    void action(int act, int mods) override;       // keysym + modifiers
    void put_text(const std::string &) override {}  // paste via application's Ctrl+V
};

bool xtest_init();        // XTestQueryExtension, pre-mapping, fix group 0
int  xtest_remaps();      // remap counter (for test/log)
void xtest_focus_guard(); // restore application focus if it moved
```

Internals:

```cpp
// 1. Find free keycodes once and reserve them in a table.
static int base, n; static uint32_t slot_cp[192];

static bool find_free(int need, int *out);      // as in APP-INTEGRATION-PLAN §4.5
static void premap();                            // ASCII 0x20..0x7E + \u0410-\u044F + \u0401\u0451 + punctuation
static KeyCode code_for(uint32_t cp);            // LRU remapping only for rare characters

// 2. Send without XSync per key.
static void tap(KeyCode kc) {
    XTestFakeKeyEvent(dpy, kc, True, CurrentTime);
    XTestFakeKeyEvent(dpy, kc, False, CurrentTime);
}

void XTestInput::put_char(uint32_t cp, int mods) {
    xtest_focus_guard();
    press_mods(mods, true);
    tap(code_for(cp));
    press_mods(mods, false);
    XFlush(dpy);
}
```

Pre-mapping resolves item 1.3(1): `MappingNotify` is sent once at startup, not
for every character. A rare character (for example, “№”) gets an LRU slot—such
presses are few.

### 3.5. Screen adapters

Do not rewrite Notes: its text model (lines, wrapping, undo, autosave) is already
correct. The adapter only maps events to existing methods:

```cpp
struct NotesInput : InputSink {
    Notes &n;
    void put_char(uint32_t cp, int) override { n.insert_text(utf8(cp)); }
    void action(int a, int) override {
        switch (a) {
        case A_DEL: n.backspace(); break;
        case A_ENTER: n.insert_enter(); break;
        case A_LEFT: n.move_left(); break;
        case A_RIGHT: n.move_right(); break;
        case A_HOME: n.move_home(); break;
        case A_END: n.move_end(); break;
        case A_UNDO: n.do_undo(); break;
        case A_REDO: n.do_redo(); break;
        }
    }
    int caret_cp() const override { return -1; }  // Notes draws its own cursor
};
```

Terminal uses a similar adapter for `submit`, `pop_cp`, `hist`. Files uses
`TextField` directly + `commit_text()`.

The Notes navigation row (←/→/HOME/END/UNDO/REDO) remains **in the layout**, not
in a separate toolbar: it is a profile flag, `rows = NAV + ABC3 + BOTTOM`, and does not
break the shared table.

---

## 4. Scenarios

| # | Scenario | Keyboard | Focus location | Text path | Required |
|---|---|---|---|---|---|
| 1 | Rename file | `KB_NOTES` (or `KB_COMPACT`) in Files canvas | canvas | `TextField` → `commit_text()` | move card above 512; prefill name; filter `/` on input |
| 2 | New folder | same, empty field | canvas | `TextField` | OK disabled when empty; “already exists” error remains a toast |
| 3 | Search/filter (new) | `KB_COMPACT` over list | canvas | `TextField` → in-memory filter | list above 576, keyboard below; Esc clears |
| 4 | Notes | `KB_NOTES` + nav | canvas | `NotesInput` | cursor and undo remain with Notes; Enter splits line |
| 5 | Rename note | `KB_NOTES`, one line | canvas | `TextField` | currently uses its own `rename_buf`; replace with `TextField` |
| 6 | Terminal (built-in) | `KB_TERM` | canvas | `TerminalInput` | ↑/↓ history; Tab not implemented (no completion) |
| 7 | xterm (external) | `KB_XAPP` in X window, auto `L_TERM` | xterm window | `XTestInput` | Ctrl+Shift+C/V, sticky Shift, focus guard |
| 8 | dillo address bar | OSK, `L_ABC` | dillo window | `XTestInput` | tap on field goes to application (shell does not intercept); EN by default; Back = Alt+Left |
| 9 | PIN/password (P2) | `KB_PIN`, digits | lock canvas | `TextField(mask=true)` | no XTest, no paste, no history, zero the buffer |
| 10 | Rename bookmark (P2) | `KB_COMPACT` | canvas | `TextField` | bookmarks not yet in code; if inside dillo, OSK only |

### 4.1. Files: specific changes

Currently: `open_rename()`/`open_mkdir()` (`files.cpp:1164-1195`) set
`modal`, display the hint “Keyboard input”, and accept letters only through `key()`
via `keysym_cp`. There is no hardware keyboard on the phone, so the scenario is dead.

Plan:

1. Replace `modal = M_RENAME/M_MKDIR` with a `TextField field` in the `Files` structure.
2. `draw_modal()` draws a `20,216,440,292` card (was `232`, shifted up 16 px—
   otherwise the card bottom at 524 would be under the keyboard at 512), field `40,272,400,52`
   (was 288), caret from `field.caret`, buttons at `432..480` (was 448..496).
3. Below it, `KbView` at `(0,512,480,288)`; do not draw the Files footer (`y=768`)
   while the keyboard is open—it would be under it anyway.
4. When `modal != M_NONE`, `tap()` first sends coordinates to `KbView`; only
   if the tap misses the keys does it go to the card buttons.
5. Physical `key()` goes to the same `TextField` through `input_event_from_key()`.

Why not `KB_COMPACT` in Files: the card occupies 216..508, and the keyboard at 512..800
fits exactly, without shifting the list. `KB_COMPACT` (224) is needed where the list must
remain visible—search (#3). `KB_COMPACT` keys are 45×44; `KB_NOTES` keys are
38×52 with 11 columns (see §6.1).

### 4.2. xterm: complete chain

1. Tap the keyboard icon in the status bar (area 292..344, `app.cpp:267`) or
   the button in the header → `osk_toggle()`.
2. `osk_toggle` detects `focused_is_xterm()` and sets `L_TERM` (already exists).
3. The application shrinks `768 → 480` (`apphost_set_content_height`, `apphost.cpp:530`),
   and the keyboard occupies 512..800.
4. `XTestInput::put_char('\u043F')` → `code_for(U+043F)` → XTest → xterm → UTF-8 bytes.
5. `Ctrl` (sticky) → `Shift` (sticky) → `C`: press `Control_L`, `Shift_L`, `c`,
   then release in reverse order → xterm copies the selection.
6. Back: first `osk_hide()` (`app.cpp:314`), second Back calls `apphost_back()`.

Focus guard: before every key, call `XGetInputFocus`; if the window is neither `g_win`
nor its descendant, call `XSetInputFocus(target)`, where `target` is the last window from
`FocusIn` (`XSelectInput(g_win, FocusChangeMask)` in `try_adopt`, `apphost.cpp:389`), otherwise
the top-level. Call `XRaiseWindow` only when showing the keyboard, not for every key.

### 4.3. dillo: address bar

dillo has its own location bar; the shell knows nothing about it and should not.
Scenario:

1. Tap the address field—the event goes to the application (the shell handles only
   `y < 40`, `app.cpp:266`), and dillo places the cursor in the field.
2. Keyboard button → OSK, EN language (heuristic: `apphost_is_browser()` → `lang=en`,
   `layer=L_ABC`; not currently implemented).
3. Input `example.com` + `ENTER` → XTest `Return`. dillo may fill from its own
   history—that is normal application behavior, not a shell bug; verify and add to tests.
4. Back = `Alt+Left` (already exists, `apphost_back`); a second Back closes it.

Do not build a custom address bar above dillo. It would require synchronization with
dillo's history/bookmarks and still could not pass a URL to the application except via XTest.

### 4.4. PIN (P2)

The lock currently has no PIN (`SHELL-TODO.md:83`). When one is added:

- `KB_PIN`: 4 rows × 3 columns, keys 136×60, `gap=12`; last row is
  `C 0 DEL`; no letters, space, paste, clipboard, or hints.
- `TextField(mask=true, max_cp=12, deny="")`; show dots, not characters.
- Input only in canvas: XTest is not needed; do not touch X-selection.
- After confirm/cancel, `memset` the buffer; reset `caret`.
- Physical digits work; physical Enter = OK, Esc = cancel.

### 4.5. Bookmarks

There is no bookmark code in the shell or dillo integration. Therefore the
“rename bookmark” scenario splits into:

- **in a future built-in bookmarks screen** — `KB_COMPACT` + `TextField`, nothing
  new;
- **in dillo's own dialog** — an external window: OSK + XTest only. There is no selection
  and no reliable way to “read the current name”; rename means “erase + type”, not “edit”.
  The dillo bookmark storage format (`~/.dillo/…`) must be checked separately before
  promising synchronization.

---

## Placeholders

- No IP addresses, passwords, or personal paths were present in this fragment requiring replacement.

## 5. Focus and XTest

### 5.1. Ten rules to enforce in code

1. The keyboard must **never** receive `XSetInputFocus`. The window is `override_redirect`, `WM_HINTS.input = False`.
2. Focus is only on the task window (top-level or its descendant). Check before every key; restore without `XRaiseWindow`.
3. `XSetInputFocus` only after `MapNotify`/`IsViewable`, otherwise `BadMatch` (already known from `APP-INTEGRATION-PLAN` §1.3(10)).
4. Remember `FocusIn` (`FocusChangeMask` on `g_win`) so focus can be returned to where it was (xterm — the VT100 child).
5. Sticky modifiers: `Ctrl`, `Alt`, `Shift` — each applies to the next key only; clear them when hiding the keyboard (`osk_hide` already clears Ctrl/Alt — preserve this behavior and add Shift).
6. Press order: `Shift`, `Ctrl`, `Alt`, key; release in reverse order.
7. Ctrl+letter always uses the **Latin** keysym: `latin_of()` (Russian-layout-to-QWERTY positional mapping) is already implemented (`osk.cpp:87-107`); move it as is.
8. XKB group for the session is 0 (US). Call `XkbLockGroup(dpy, XkbUseCoreKbd, 0)` at startup; check `XkbGetState`. If this fails, use alias keycodes: map `a..z` to free keycodes in advance and send Ctrl using those.
9. `MappingNotify` → `XRefreshKeyboardMapping` + rebuild the keycode cache.
10. Physical keyboard and OSK do not conflict: physical events go directly to the focused application window (the shell does not intercept them), while OSK adds XTest events. OSK sticky state does not affect the physical keyboard — this is intentional.

### 5.2. Ctrl chords by application

Matrix to verify and encode as a table:

| Application | Copy | Paste | Select all | Comment |
|---|---|---|---|---|
| xterm / st | `Ctrl+Shift+C` | `Ctrl+Shift+V` | `Shift+Home` or mouse | `Ctrl+A` in a terminal = 0x01, not “select all” |
| dillo (FLTK) | `Ctrl+C` | `Ctrl+V` | `Ctrl+A` | FLTK handles chords itself; verify at 480×800 |
| zenity/GTK entry | `Ctrl+C` | `Ctrl+V` | `Ctrl+A` | — |
| Java/AWT | `Ctrl+C` | `Ctrl+V` | — | May not work; do not promise |

The shell identifies the chord using `WM_CLASS` (`focused_is_xterm()` already exists, `osk.cpp:399-427`); default to the GTK variant.

### 5.3. Selection and clipboard

Requirements:

- **Selection in another application** — keyboard only: sticky `Shift` + `←/→/Home/End`, `Ctrl+Shift+←` (by word, if the application supports it). Mouse drag via `XTestFakeMotionEvent` + `ButtonPress` is technically possible, but word targeting depends on the text engine; do not implement in P1.
- **Copying from another application** — send the chord from §5.2. The text then lives in the application's X-selection; it disappears if the application closes. This is an X limitation, not a shell limitation.
- **Pasting into another application** — paste chord. Our clipboard must own `CLIPBOARD`.
- **Shell as CLIPBOARD owner** (P1, ~100 lines): `XSetSelectionOwner` for `CLIPBOARD`, handle `SelectionRequest` (`TARGETS`, `UTF8_STRING`, `STRING`, `TEXT`, `TIMESTAMP`) in the shared `app.cpp` loop. This enables “copy a file name in files → paste into xterm” without an intermediate process.
- **Paste from X into a shell field** — `XConvertSelection` + asynchronous wait for `SelectionNotify` (500 ms timeout), not a blocking loop: drawing and servicing taps must continue.
- **PRIMARY** — optional: selection in another application is usually already in PRIMARY, so “paste selection” can work without Ctrl+C. Cheap, but requires the same asynchronous code.
- `Clip` in `files.cpp` is a **file** clipboard (paths). Call the text clipboard `x_clip` to avoid confusion.

### 5.4. XIM and CJK

Not implemented. `XMODIFIERS=@im=none` is already set at launch (`apphost.cpp`, environment branch). Unicode keysyms cover Cyrillic; CJK requires a preedit window and a full IM — a separate project, not P0–P2.

If an application nevertheless expects XIM and does not accept the keysym (rare, for example Java), the honest response is: “input is unavailable in this application, except through a physical keyboard.” Verify in practice; do not promise.

### 5.5. Physical keyboard: one input path

Currently `XLookupKeysym(...,0)` loses Shift (`app.cpp:286`). We need one `input_event_from_key(XKeyEvent&)`:

```cpp
// group and level come from server state, not index 0
XkbStateRec st; XkbGetState(dpy, XkbUseCoreKbd, &st);
int group = st.group;                                  // 0 = US
int level = (st.mods & ShiftMask) ? 1 : 0;             // Shift is level 1
KeySym ks = XkbKeycodeToKeysym(dpy, ev.keycode, group, level);
int cp = keysym_to_cp(ks);      // table from files.cpp:95-110 moves here
```

Also add a special-key table (Return, BackSpace, Delete, Home, End, arrows, Tab, Escape). This provides consistent behavior in notes, terminal, files, and future fields; notes will finally accept letters.

Add Ctrl+Z/C/V/X to shell fields in the same place (currently undo is only available through the button).

---

## 6. UI and metrics

### 6.1. Grid and dimensions

Canvas 480×800, status bar 0..32 (`ui_status`), application content 32..512 with keyboard, keyboard 512..800. `APP-INTEGRATION-PLAN.md` specifies a 40 px header and 712 px content — that is the design; code currently uses 32 and 768, and the plan below is calculated from the code.

| Profile | 11 columns | 10 columns | row h | Area, 11-column | Total height |
|---|---|---|---|---|---|
| `KB_NOTES` | `(464-40)/11 = 38` | 42 | 52 | 1976 | 5*52+4*4+8 = 284 |
| `KB_TERM` | `(468-30)/11 = 39` | 44 | 66 | 2574 | 4*66+3*3+12 = 285 |
| `KB_XAPP` | 38 | 42 | 64 | 2432 | 4*64+3*4+16 = 284 |
| `KB_COMPACT` | — | 45 | 44 | — | 4*44+3*2+12 = 194 |
| `KB_PIN` | — | 136 (3 col.) | 60 | — | 4*60+3*12 = 276 |

Do not go below 44 in height anywhere; 11 px is the minimum text size (hints, `SHIFT`/`123`), symbols are 16–18 px, `ENTER` is 14–15 px bold.

**Intentional width exception.** The Russian layout has 11 keys per row: at 480 px this gives 38–39 px per key. Area 1976–2574 px² ≥ 44×44 = 1936, height 52–66, but width is formally below 44. Options:

- **A (recommended):** keep the familiar Russian layout, 11/11/10. With a height of 52–66, 38–39 px practically prevents taps landing on an adjacent key, and hiding letters is worse. Record this exception here and in the test (check area, not only width).
- **B (strict):** 10 columns (45 px), and put the Russian letters yo, hard sign, and ha on long press (e→yo, soft sign→hard sign, ze→ha) and as explicit keys in layer `123`. All keys are ≥44, but three letters are hidden — worse for file names and passwords.

EN: 10/9/10 keys — 9 keys per row give 49–50 px, 10 keys give 42–45 px; the constraint does not apply.

### 6.2. Layers and states

- `L_ABC`: RU/EN, SHIFT (one-shot), `123`, `SPACE`, `DEL`, `ENTER`. Double-tap SHIFT = Caps (optional P1), indicator — `theme.sel` + accent.
- `L_123`: digits, symbols, `ABC`. In 288 px profiles, this layer provides `| ~ / - _ $ ?`, avoiding a switch to the terminal layer just to enter `|`.
- `L_TERM` (only `KB_XAPP`): ESC TAB CTRL ALT ← ↑ ↓ → / HOME END PGUP PGDN / ^C ^D ^Z ^L DEL. Also, unlike the current OSK: `Shift` and `^V`/`^A` (P1) — otherwise selection and paste are unavailable.
- `L_NUM`: PIN.
- Pressed key: invert (`surf2` + `fg`) for the duration of `ButtonPress`; no animation or delay — state only. Currently there is no feedback at all.

### 6.3. Cursor and field

- Caret — 2×20 px `theme.accent`, no blinking (the “no animations” rule).
- Field: `surf2` + `line2` border (as in files now), 16 px text, placeholder 11–16 px `dim`.
- If text does not fit — scroll the window with the caret: show the tail with “…” on the left; `g.fit` is unsuitable (clips on the right), so use `TextField::window(maxw, px)`.
- Mask (PIN): 16 px dots at 14 px spacing, caret after the last dot.
- For a receiver where `caret_cp() == -1` (notes, XTest), the screen/application draws the caret itself; `KbView` draws nothing in that case.

### 6.4. Avoiding overlap with bottom panels

Rule: **content bottom = `kb_top()`**, not the constant 800.

```cpp
int kb_top() { return kb_visible ? kb_model.geom().y : 800; }  // 512 | 576
```

- files: footer `y=768` → with keyboard `y = kb_top() - 32`; modal card 216..508 (see §4.1).
- notes: 17 rows × 24 = 104..512 — already matches 512; no change needed.
- terminal: 23 rows with keyboard open (`terminal.cpp:33`) — already correct.
- `KB_COMPACT` in search: list through 576, keys 582..767, hint 772..783, panel ends at 800.
- immersive (480×800, another application): keyboard is an **overlay**; the application is not resized, and the caret may be under it. Honest limitation: “hide” button (44×44) and a hint in the header; do not pretend this is solved.

### 6.5. Themes

All colors come from `theme` (`graphite.h:16-23`): panel `bar`, keys `surf`, pressed/active `sel` + `accent`, `ENTER` — `accent`/`onacc`, inactive (UNDO/REDO) — `dim`. No new tokens. Check `graphite`, `paper`, `ember`: `dim` on `bar` is the weakest point (11 px labels).

---

## 7. Data and state

### 7.1. `InputState` (one per process)

```cpp
struct InputState {
    bool ru = true;            // shared by all screens and OSK
    KbLayer last_layer[3];     // by profile; terminal layer is not remembered
    ShiftMode shift = SH_OFF;  // never persisted
    bool ctrl = false, alt = false;   // sticky, cleared on hide
    int  kb_h = 288;           // 288 | 224
    bool suggest = false;      // predictive input disabled
    bool autocap = false;      // off by default: file names
};
```

Rules:

- `lang` is **shared** by the shell and OSK. Currently OSK has its own `ru` (`osk.cpp:47`), while notes/terminal have their own `lang`; after unification, switching in notes must also change the OSK language.
- `layer` is remembered per profile, but `L_TERM` is set anew on display based on the application (xterm), rather than restored.
- Sticky modifiers are **not** persisted and are cleared on `osk_hide`, task switch, and application close (`DestroyNotify`).
- Enable `autocap` only for notes, if at all; never for file names or passwords.

### 7.2. Persistence

`cfg` reads `<system-settings>` (default path, `graphite.h:78`). Do not write user input settings there: the file is system-wide. Use `settings_save_key(key, value, path)` with path `~/.config/graphite/input.conf` (the function already supports arbitrary paths and merges keys, `settings.cpp:57-95`).

Keys: `kb.lang`, `kb.layer.abc|compact`, `kb.height`, `kb.suggest`, `kb.autocap`. Write on keyboard hide/screen exit, not on every tap (the file is on flash storage; unnecessary writes are harmful). Create directory `~/.config/graphite` at startup, as `Notes::mkdirs()` does (`notes.cpp:425`).

### 7.3. Buffers

| Buffer | Contents | Location | Lifetime |
|---|---|---|---|
| `TextField.v` | field text (name, search, PIN) | screen | while dialog is open |
| note body | lines, undo | notes | file on disk |
| `cmdline` | command line | terminal | session |
| `Files::clip` | **file** paths | files | session |
| `x_clip` | **text** X-selection | shell (owner) | until exit/replacement |

`x_clip` is needed in P1; in P0, chords for copy/paste within another application are sufficient (without ownership by us).

---

## 8. Phases

### P0 — shared module + keyboard in files

Scope:

1. `include/input.h`, `src/input_kb.cpp`, `src/input_text.cpp`, `src/input_xtest.cpp` (new; the XTest portion moves from current `osk.cpp` without improvements — pre-mapping is in P1).
2. `src/osk.cpp` → thin host over `KbModel`/`KbView`/`XTestInput`; `osk.h` is unchanged.
3. `screens/files.cpp` — modal on `TextField` + `KbView`, move card, physical input through shared `input_event_from_key`.
4. `screens/notes.cpp`, `screens/terminal.cpp` — remove their own `build()`/`draw_keys()`, connect the shared view and adapters. Preserve appearance (same 52/66 px, nav row in notes).
5. `src/app.cpp` — one physical keyboard route; `osk_event` first (as now).

Acceptance criteria:

- on host (`GS_FILES_DIALOG=rename|mkdir`) a file can be renamed and a folder created without a hardware keyboard;
- notes and terminal look and behave as before the change (before/after screenshots);
- physical letters work in notes, files, and terminal, including Shift and Cyrillic;
- keys ≥44 px high, 11 px text readable in all three themes;
- `os/build.sh` builds without new warnings, `-Wall -Wextra`.

Boundaries: one agent; `registry.cpp`, `graphite.h`, `app.cpp` — agreed changes only; do not touch the phone.

### P1 — other applications: selection, clipboard, chords

1. Sticky `Shift` + `Ctrl+Shift+←/→`, `Shift+Home/End` in `L_TERM`.
2. Chord table by `WM_CLASS`; `^V`, `^A` (GTK only), copy.
3. Shell as `CLIPBOARD` owner + `SelectionRequest` + asynchronous `XConvertSelection` for pasting into shell fields.
4. Focus guard without `XRaiseWindow`, `FocusChangeMask`, `MappingNotify`.
5. Pre-map keycodes + remapping counter in log.
6. `KB_COMPACT` for file search; Russian yo/hard-sign letters via long press (if option B from §6.1 is selected).

Acceptance: Cyrillic can be typed in xterm, `Ctrl+Shift+C/V` works, `Shift+←` selects, paste from files works in xterm; address entry and `Ctrl+V` work in dillo; `XGetInputFocus` does not change after tapping OSK.

### P2 — PIN, bookmarks, search

1. `KB_PIN` + mask + clear buffer; lock screen.
2. Bookmarks screen (owned by us), rename using `KB_COMPACT`.
3. Search in notes/files (if not done in P1).
4. Do **not** implement predictive input (RAM, 1 GHz, RU dictionary ~1–3 MB, questionable benefit). Do not implement swipe typing. Do not implement XIM.

---

## 9. Tests

### 9.1. Host (Xvfb 480×800×16, `os/preview.sh`)

| What | How | Expected |
|---|---|---|
| Key geometry | debug dump of `KbModel::keys()` (`GS_KB_DUMP=1`) | all `h>=44`; `w>=44` or `w*h>=1936`; no overlaps; `y+h<=800` |
| Keyboard in files | `GS_FILES_DIALOG=rename sh os/preview.sh files` | card not under keys, caret in field, OK active |
| Notes | `sh os/preview.sh notes`, compare with current frame | pixel-identical rows/heights, except corrected layout |
| Terminal | `sh os/preview.sh terminal` | 23 rows with keyboard open, footer not under keys |
| Empty PIN draws no letters | `lock` preview (when available) | digits only |

### 9.2. XTest and focus (Xvfb + clients)

1. **Cyrillic:** `xterm -e cat -v`, OSK → enter the Russian word for “hello” → verify its UTF-8 bytes in cat.
2. **Ctrl:** `cat -v`, sticky Ctrl + `C` → `^C` (0x03). Repeat with RU group: `setxkbmap ru` (if available) and verify the chord still produces 0x03.
3. **Ctrl+Shift:** xterm, select with `Shift+Home`, sticky Ctrl+Shift+`C`, then `Ctrl+Shift+V` → text is restored.
4. **GTK:** `zenity --entry`, Cyrillic; `Ctrl+A`, `Ctrl+C`.
5. **dillo:** tap address, enter `example.com`, Enter; Back = Alt+Left, second Back closes.
6. **Focus:** `xdotool getwindowfocus` before/after tapping a key — same application window, not the keyboard window or `PointerRoot`.
7. **Resize:** xterm `stty size` before/after showing keyboard (47×59 → 31×59).
8. **Keymap:** `xmodmap -pke` before/after 20 presses; remapping counter does not increase for ordinary characters.
9. **MappingNotify:** third-party `xev -event keyboard` does not crash and continues seeing keys after a rare symbol.

### 9.3. Shell fields

- UTF-8 backspace on Cyrillic does not split a character.
- Limits: files 240 bytes (as now), note 96 (`notes.cpp:1097`), PIN 12.
- `/` cannot be entered in a file name; empty name → OK inactive; existing name → toast, field stays open.
- Paste from X into field (P1) with no owner — silently do nothing, without hanging.
- Cursor remains visible when text exceeds field width (tail with “…”).

### 9.4. Device (after P0, read-only observation only)

- 20 consecutive presses: no stuck modifiers, total < ~1.5 s.
- Taps on keys in 512..800 land accurately (touch is already in X coordinates, see `DISPLAY-TOUCH-SOLVED.md`); check bottom row and corners.
- Closing an application while keyboard is open: keyboard hides, content returns to 768, focus goes to shell.
- Back: first hides keyboard, second goes back/closes. Home closes and returns to shell (do not break existing behavior).
- Rotation/UD: coordinates remain the same; keyboard does not shift.
- Screenshots of all three themes, 2× enlargement: 11 px is readable, `dim` on `bar` does not blend in.

### 9.5. Regressions not to miss

- notes: undo/redo, autosave after 2 s, cursor at tap position, line wrapping.
- terminal: history ↑/↓, scrollback, `XTERM` launches xterm as `user`.
- files: multi-select, copy/delete, sorting, properties.
- OSK: language and layer do not remain “stuck” between displays; Ctrl does not remain set after hide.

---

## 10. Risks

| # | Risk | Probability | Impact | Mitigation |
|---|---|---|---|---|
| 1 | Layout splits into three copies again | high | different letters on the same hardware, bugs “only in terminal” | one table; test dump of geometry and labels for all profiles |
| 2 | Keymap remapping on every character | high (current behavior) | `MappingNotify` to all clients, lag at 1 GHz | pre-map + LRU for rare keys; counter in log |
| 3 | Focus moves / `BadMatch` | medium | “keyboard does not work” while window is alive | `XSetInputFocus` after `MapNotify`, check before key, error handler |
| 4 | Ctrl chord does not produce 0x03 in RU group | medium | copy/interrupt do not work | group 0 at startup; alias keycodes; test §9.2(2) |
| 5 | Application ignores Unicode keysym (XIM/Java) | low | input is not delivered | `XMODIFIERS=@im=none`, honest message; do not promise |
| 6 | CLIPBOARD owner hangs on `SelectionRequest` | medium | UI freezes | asynchronous operation, timeouts, never block rendering |
| 7 | Keyboard covers caret in immersive mode | high | “blind” input | overlay + “hide” button; or per-app resize if application supports it |
| 8 | Fixed-size application does not respond to resize | medium | bottom part under keyboard | overlay and manual hide; do not stretch |
| 9 | No XTEST in target Xorg build | low | input into other applications is impossible | `XTestQueryExtension` at startup, log, disable keyboard for X with toast |
| 10 | `XChangeKeyboardMapping` breaks keymap for other clients | low with pre-mapping | other applications' hotkeys shift | occupy only genuinely free keycodes; do not touch 8..~120 |
| 11 | Memory/CPU | low | +276 KB for Canvas (480×288×2), +tables | one OSK canvas, layout cache, no rebuild in `draw()` |
| 12 | Phone test finds touch issue below 512 | low | misses on bottom row | check coordinates before/after on device (read-only) |

---

## 11. Three conclusions

1. **XTest is not the problem.** XTest and Unicode keysym have already been tested and work. The root cause is the lack of a shared model: three layout copies, three physical-key handlers, no clipboard/caret in OSK, and “sledgehammer focus” after every key. Fix the architecture, not by adding buttons.
2. **Two hosts are not a compromise; they are necessary.** While another application is on screen, our canvas is invisible, so its keyboard must be a separate OR window. Inside the shell, a separate window is harmful: the buffer and caret live in the screen, and routing them through global variables repeats the same pitfalls as today. The model, view, and receiver should be shared, not the surface.
3. **P1 is pointless without P0.** Selection, copy, and paste are properties of the receiver and layout (sticky Shift, `^C/^V`, `| ~ $` slots). Without a shared receiver and table, each such button becomes a fourth copy. First the module and files, then the clipboard.

## 12. What to do first

**P0, step 1 — extract the layout and field, then files.** Specifically:

1. `include/input.h` + `src/input_kb.cpp`: RU/EN/123/TERM tables, `KbModel`, presets (`NOTES 288`, `TERM 288`, `XAPP 288`, `COMPACT 224`, `PIN 288`), bottom-row weights, `press()` with sticky Ctrl/Alt/Shift.
2. `src/input_text.cpp`: `TextField` (UTF-8, caret, `/` filter, limits), move UTF-8 helpers from `notes.cpp:56-122` and `files.cpp:80-83`.
3. `screens/files.cpp`: `M_RENAME`/`M_MKDIR` modals using `TextField` + `KbView`, card `216..508`, keyboard `512..800`; physical input via shared `input_event_from_key`.
4. Verification: `GS_FILES_DIALOG=mkdir sh os/preview.sh files` + rename on the host without a hardware keyboard; notes/terminal only after files work, to avoid taking on two risks at once.

Scope: ~4 new files, ~600 lines, changes in three screens. One agent, one worktree, do not touch the phone.

## Placeholder list

- `<system-settings>` — system settings file path.
