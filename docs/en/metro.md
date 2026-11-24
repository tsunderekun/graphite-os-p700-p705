# METRO-RESEARCH — WP7/8 → Graphite Shell (480×800)

Research: what from Windows Phone 7/8 (Metro / Modern UI) we have not adopted yet, but should. Research only; no code or device changes were made.

**Notation.** **(fact)** — from Microsoft guides, SDK templates, or documented analyses; **(adapt.)** — my proposal with values for 480×800 and Xlib; **(derived)** — arithmetic based on facts.

**Screen geometry.** 480×800, 4.3" → 217 ppi, 1 px = 0.117 mm. Therefore: 44 px = 5.2 mm, 56 = 6.6, 60 = 7.0, 72 = 8.4, 96 = 11.2. This matters: our 44 px minimum is physically smaller than WP's 7 mm minimum — 44 is suitable only for secondary icons; primary hit areas must be 56—72. **(derived)**
The WP guide contradicts itself: “9 mm or 34 px,” but 34 px at 217 ppi = 4.0 mm; actual WP buttons were 48—80 px. Do not copy “34” literally.

---

## 0. Audit of the current Metro pass

15 screens exist; below is what already conforms to WP and what does not (based on `shell/m-*.html` and `artifacts/shell/m-*.png`).

| Screen | Already WP-like | Missing |
|---|---|---|
| Start (m-home) | “now playing” live tile, grid, photo icons 1.6 | 3 tile sizes, square medium, pause inside tile (violates “one tile — one action”), no counter, no scrolling |
| Apps (m-apps) | alphabetical list, 64 rows, pin | letter headers, search, “Settings” at the bottom, jump grid |
| Player (m-player/queue/info) | 3 “pages,” Light heading | conceptually a pivot, but no swipe/tap on the strip; right-side tab list instead of WP strip; pages are not in the Back stack |
| Tile editing (m-edit) | separate screen with × | WP edits **in place** (unpin + resize on the tile itself), with no navigation |
| Shade (m-shade) | 4×2 tiles, sliders, cards | “clear all,” swipe-to-dismiss cards, notification counter in status bar |
| Lock (m-lock) | 64 clock, swipe up, now playing | 5 quick-status slots, detailed status as a separate slot |
| Settings (m-settings/shsettings) | 52—56 lists, switches, segments | checkbox, disabled state, 2 px on interactive borders |
| Notes (m-notes) | 4-row keyboard, RU/EN, caret | placeholder, clear “×,” focus border, Back does not hide keyboard |
| Files/browser/terminal | 40 header + 48 dock | icon labels, “…” menu |

Small differences from `DESIGN-SYSTEM.md`: in the Metro pass tiles are 216×176 (8 gap, 20 margins) instead of 208×196 (16, 24); the `m-settings` and `m-shsettings` renders have no dock, although `SCREENS.md` requires one for shsettings.

---

## 1. Metro principles we already follow

1. **Content, not chrome.** No shadows, gradients, “glass,” or decorative panels; 1 px separators, screen edges = content edges. **(fact: “content, not chrome”; 0 gradients, shadows, and CornerRadius in SDK themes)**
2. **Typography carries hierarchy.** Large Light heading, bold labels, utility labels 11 + tracking. **(fact: scale 18.667—186.667, Light/SemiLight/Semibold)**
3. **One accent, used only for action/state.** Ours is turquoise; in WP8 the accent became the interactive color (pressed button, focus). **(fact)**
4. **Square corners.** In the Metro era, all 13 theme files had 0 `CornerRadius`; only radio buttons and progress dots are round. **(fact)**
5. **Commands at the edge and hidden.** Dock/app bar at the bottom, Back is hardware, no on-screen close and no window title. **(fact: “no close/back buttons”)**
6. **Dark theme by default + accent themes.** WP has 10 accents in 7 and 20 in 8; we have 3. **(fact / our reduction)**
7. **Large hit areas and dense lists.** Rows 56—60, ellipsis rather than clipping; WP requires list text ≥12 px and rows “sized for a finger.” **(fact + our adjustment)**

---

## 2. What we missed

Columns: pattern → benefit → adopt? (✓ / ◐ adapt / ✗) → values for 480×800 → Xlib → P0/P1/P2 → conflict.

### 2.1. Start and tiles

| WP pattern | Benefit | Adopt? | 480×800: values | Xlib | P | Conflict / note |
|---|---|---|---|---|---|---|
| 3 tile sizes: small / medium / wide | Density vs emphasis; “showcase” the primary item + small shortcuts | ✓ | grid: margins 24, content 432, gap 16 → small **96×96**, medium **208×208**, wide **432×208**; small = 4 columns (4*96+3*16=432), medium = 2 (2*208+16=432), medium = 2*small+16 (WP fact: 100/210/430×210, gap 10, margins 25 (derived); our step 8 instead of 12) | `XFillRectangle` + `XftDrawString`, all into one screen `Pixmap` | P1 | Vertical budget: 32 status + 16 + 3*208 + 2*16 + 60 now-playing + 48 dock = **812 > 800** → either now-playing becomes a wide tile at the top (752), or Start scrolls |
| Live tile, static alternative | Show “what’s happening now” without opening | ✓ | now-playing 432×208: icon 24, title 15 bold, artist 12 dim, progress 2 px, time 11; update once per second | redraw 432×208 tile = 180 KB, once per second | P1 | In WP a live tile flips/cycles (animation). We take only the static state. A tile = **one action** (fact), so remove pause from the tile (currently 32×32 inside — also <44) |
| Tile counter | “5 notes,” “2 processes” without opening | ◐ | 16×16 at bottom right, inset 8, 11 bold; 0 = hidden; 1—99 (fact: count 1—99, 0 hides) | 2 Xft calls | P2 | We already have a 12 dim metadata line — the counter duplicates it; use only for unread items |
| Tile groups with headers | Order in a long Start screen | ✗ | — | — | — | **(fact)** Named groups are Windows 8, not phone. In WP, Start is one long column with no headers; in 8.1 Update 1 only **folders** appeared (drag a tile onto a tile → folder of 2×2 mini-tiles) |
| Folder tiles | 4—9 apps in one cell | ◐ | folder 208×208: padding 8, 2×2 mini-tiles 92×92 (2*92+8=192), gap 8, name 11 +1 | same Pixmap + hit rect across 4 quadrants | P2 | Useful only with 10+ tiles; currently 6 |
| Vertical Start scrolling | More tiles than fit | ◐ | step 208+16=224; 1:1 drag-follow, no inertia; stop at edge, no “rubber band” | `XCopyArea` from `Pixmap` 480×1600 (1.5 MB), dx=0,dy | P1 | Needed if we introduce square medium. No “bounce” animation (fact: WP rubber-band is animation; we do not adopt it) |
| “All apps” arrow | Enter the list | ✓ already | 44×44 at bottom right of launcher / swipe left (fact: WP uses swipe left) | hit rect | P2 | Our `›` chevron is 44×26. Add swipe left across Start — more WP-like and costs no pixels |

### 2.2. Lists, apps, search

| WP pattern | Benefit | Adopt? | 480×800: values | Xlib | P | Conflict / note |
|---|---|---|---|---|---|---|
| Alphabetical groups + sticky letter header | Orientation in a long list | ✓ | header 32: letter 15 bold dim, baseline +22, inset 16; rows 56—60; header sticks at y=40 (below header) | separate text layer over list `XCopyArea` | P1 | **(fact)** Letters appear only at ≥45 apps (otherwise list is “sparse”). We have 10 — enable at threshold 24 |
| Jump grid on letter tap | Jump to section | ◐ | 6 columns × 64 + 5*8 = 424, margins 28; cell 64×64, letter 20 bold; only letters that exist | one screen, tap → `scroll_y = index*60` | P2 | **(fact)** On phones, semantic zoom is triggered by **tapping the header**, not pinching (pinch is the tablet model). We do not need pinch at all |
| Search apps list | Find by name | ◐ | field 40 (icon 24 + placeholder 13 dim), results are 56 rows; empty → 2 lines (see empty state) | `XftDrawString` + array filter | P2 | **(fact)** In 8.1 the search icon is at the top left of the list, filtering as you type. Cost: second text input → needs Notes keyboard. Not worthwhile for 10 apps |
| “Settings” at bottom of list | Separate settings from apps | ✓ | final row 56 after last letter, with no letter header | — | P2 | Small detail, but makes the list feel “real”; our “Settings” is alphabetically under “S” |
| Long-press app → menu | pin / delete / info | ✓ | hold 650 ms, movement >12 px cancels; menu 240×(n*48+8), item 48: icon 24 + text 14 | override-redirect 240×N at touch point, flip at edges | P1 | **(fact)** In WP a long press opens a menu, not a mode. Our pin is always-visible 40×40 icon (<44) |
| Multi-select list (checkbox) | Delete multiple files/notes | ✓ | long-press → checkbox 24×24 (2 px line, accent check) at x=16, text from x=64; header “2 SELECTED” 13; app bar: DELETE / ALL / CANCEL | 2 `XDrawLine` lines | P1 | **(fact)** WP: selection enables checkboxes and changes app bar; Back clears selection. We have no multi-select anywhere |
| Empty state with action | Not a dead end | ✓ | 2 centered lines in content: 15 dim + 13 accent action, 44 hit area | 2 Xft calls | P1 | **(fact)** An empty pivot page is not removed; it shows a placeholder. Ours has one line with no way out |
| Launch placeholder (“launching…”) | Avoid “flicker” when starting pcmanfm/dillo | ✓ | 40 header + centered 13 dim, y=380; removed on MapNotify | text in background, `XMapNotify` | P1 | **(fact)** WP shows splash and “resuming…” with dots; we do not take the dot animation — text only |

### 2.3. Navigation and shell

| WP pattern | Benefit | Adopt? | 480×800: values | Xlib | P | Conflict / note |
|---|---|---|---|---|---|---|
| Pivot (3—4 pages of one type) | Fast switching within an app | ◐ | strip 56 (status 32 + 56 = 88, content starts at 96), active heading 30 ExtraLight, inactive 13 dim, gap 16; next heading visible by 24 px | 3 `Pixmap` 480×720 (675 KB each) + `XCopyArea` | P0 | **(fact)** ≤4 pages, headings 1—2 words, cyclic, no slider/editing inside. We have a seek strip on “Now” → swipe conflicts, so **tap the heading** (adapt.); swipe only on “Queue/Info” |
| Panorama | Hub overview | ✗ | — | — | — | **(fact)** Long horizontal canvas, 187 px heading (170 in WP8), layer parallax, 24 px section preview. Parallax + 187 px on a 480-wide screen wastes 1.5 screens; we have no content hub |
| App bar: icons + labels | Make icons understandable | ✓ | bar 48 → 64 when expanded (icon 24, label 11 baseline 792), content 712 → 696 | redraw bar, `XResizeWindow` content | P1 | **(fact)** 72 px, up to 4 buttons, icon 48 with glyph 26, labels appear via “…”. 72 is impossible for us — dock already occupies 48; 64 is a compromise |
| “…” menu (up to 5 text items) | Rare commands without icons | ✓ | panel 480×(4*44+8)=480×184 above bar, item 44 (text 14), 1 px separator; “…” 44×44 at right of bar | override-redirect window, no slide animation | P1 | **(fact)** ≤5 items, text 14—20 characters, lowercase. Our style is Sentence case: keep ours (adapt.) |
| Hardware Menu key | Same “…” without a button | ✓ | KEY_MENU 139 (check on event1/u0_keypad — currently not read) | evdev, like POWER | P0 | P700 has Home + Back + Menu **(fact: LG-P700 manual)**. Android convention: Menu = context. Fits the WP menu perfectly |
| Back stack: transient → page → app → Start | Predictability | ✓ | order: shade/dialog/menu/keyboard → subpage → app → launcher; pivot page is **not** in stack | array `{screen, state}` in shell | P0 | **(fact)** “Back means back”; pivot item is not a page (Back from pivot exits app); MessageBox/keyboard are canceled by Back. Back is currently not specified at all |
| Long-press Back → task switcher | Return to terminal/files | ◐ | overlay 480×720: heading 13 +2, rows 88 (icon 32, name 15, × 44 at right), maximum 3 | `XMapRaised`/`XUnmapWindow`; no previews | P2 | **(fact)** WP uses cards with screenshots; tap = switch, × = close. Previews are unavailable to us (a minimized window has no content) and cost RAM; 400 MB with 182 occupied → maximum 3 apps, LRU eviction |
| In-place tile editing (unpin/resize) | No separate screen | ✓ | hold 650 → tile gets two 44×44 icons: unpin (top right, inset 4), resize (bottom right); tap outside = exit; drag = move | overlay icons on tile | P1 | **(fact)** WP does not navigate to an editing screen. Our m-edit is a separate screen with × 22×22 (<44) |
| Queue row context menu | “Queue,” “play next,” “info” | ✓ | menu 240×200 (4*48+8), anchored to row, flips up if y>560 | same as app menu | P0 | For the player, this is the most frequently missing gesture: currently a tap only plays |
| On-screen Back/Close in header | — | ✗ | — | — | — | **(fact)** “Back buttons are not permitted anywhere; close buttons not needed.” Remove header chevrons (or keep as a duplicate of hardware Back if evdev proves unreliable) |

### 2.4. Feedback: toast, dialog, status bar

| WP pattern | Benefit | Adopt? | 480×800: values | Xlib | P | Conflict / note |
|---|---|---|---|---|---|---|
| Toast (banner) | Confirm an action without a dialog | ✓ | 480×64 at y=32: icon 24 at left (x=16), heading 13 bold, caption 12 dim, 3 px accent at left edge; 5 s; tap → destination; swipe right >80 px to dismiss | override-redirect 480×64, `select()` timeout | P1 | **(fact)** WP uses an opaque **accent-color** strip, 10 s, ~40—47 characters, icon at left, tap opens app. Our rule “accent is not decoration” → surf background + 3 px accent (adapt.). Needed for “saved,” “queued,” “Wi-Fi connected” |
| Message box | Confirmation/error | ◐ | panel 400×220 (40,280), heading 18—20 (≤2 lines), body 13/20, 2 buttons 160×48; 74% dim | already exists (m-power) | — | **(fact)** Heading ≤2 lines, body unlimited, buttons are verbs (“Shut down,” not “OK”), Back cancels. Our dialog conforms; only add “Back = cancel” |
| Progress indicator | Show that work is in progress | ◐ | 3 static dots 6×6 (gap 8) + text 12 dim, or 2 px bar at 1/3 | 3 `XFillArc`/squares | P2 | **(fact)** WP uses animated dots (5 dots, 3.9 s cycle) and ProgressRing dots. We do not adopt animation: 3 static dots read as “…” |
| Status bar: clock only, other indicators on tap | Cleaner header | ◐ | 32 px; hide Wi-Fi, keep clock + battery %; tap at y<32 shows everything for 8 s | 8 s timer | P2 | **(fact)** 32 px, clock always visible, indicators on tap for ~8 s. Battery % is useful to us → keep it |
| Notification counter in status bar | Show that the shade has content | ✓ | 12×12 icon + number 11 in x=96..128 | 1 Xft call | P1 | **(fact)** In 8.1, a “newspaper” appears in the header. Needed when notifications exist that are not on screen |

### 2.5. Controls and typography

| WP pattern | Benefit | Adopt? | 480×800: values | Xlib | P | Conflict / note |
|---|---|---|---|---|---|---|
| Toggle switch | Immediate on/off | ◐ | 48×24, 1 px border, 16×16 thumb, on = accent fill + bg thumb; hit area 56 | 2 rectangles | — | **(fact)** WP switch is a capsule with round thumb (Windows 8 uses a 50×19 rectangle with 12 px square thumb). Use **Win8/our square variant** — our square corners. Already close |
| Checkbox / Radio | Selection | ✓ | checkbox 24×24, 2 px line, 2 px accent check; radio — **circle** 24 (the only round form in Metro, fact), 8 accent dot; 44 row hit area | border + 2 lines | P1 | **(fact)** WP: 32×32 indicator with 3 px border (7.1) / 21×21 + 23×23 (Win8). Ours is 16 for radio — small; increase to 24 |
| Disabled / pressed / focus | Clear states | ✓ | disabled = fg 40%; pressed = surf2; focus = 2 px accent border (instead of 1) | color/thickness | P1 | **(fact)** WP has no visible focus (keyboard/mouse only on Win8); pressed in WP8 = accent fill + white text |
| 2—3 px control borders | Legibility at 217 ppi | ◐ | interactive controls 2 px, separators 1 px | — | P1 | **(fact)** `PhoneBorderThickness` = 3; Win8 controls = 2 px. We use 1 px everywhere: on this panel checkbox/switch “disappear” |
| Text input: placeholder, clear, focus | Fast input | ✓ | field 48, 1 px line → 2 px accent on focus; placeholder 13 dim; “×” 44×44 at right inside; caret 2×16 accent, no blinking | Xft + 1 line | P1 | **(fact)** WP field has a border, accent on focus, and a clear button. Blinking caret = 500 ms timer: do not adopt (rule: “no animations”), use static caret |
| Typography scale: add a top step | Metro “scale” and breathing room | ✓ | 42 / 32 ExtraLight (only ≥28), 22 semibold, 20/16/13/11; tracking −0.4 at 32—42 | Xft, do not touch kerning | P1 | **(fact)** WP: 18.667 / 20 / 22.667 / 25.333 / 32 / 42.667 / 72 / 186.667; body 20, Title1 72. Our 15 is smaller than even WP “small” (18.667) — retain 15 for dense rows, raise headings to 32/42. DejaVu **ExtraLight is thinner than Segoe Light** → do not use below 20 px (breaks up on RGB565) |
| Weight mapping | Predictability | ✓ | Light 300 → DejaVu ExtraLight (≥28); SemiLight 350 → Regular; Normal 400 → Regular; Semibold 600 → Bold | — | P1 | We currently use Bold for list rows — this is “louder” than WP; keep as a deliberate choice at 15 px |
| Accent themes | Personalization | ◐ | 3 themes (graphite/amber/ice), switching = full 480×800 repaint (768 KB) | `XPutImage` | P2 | **(fact)** WP has 20 accents and a light theme. Do not adopt light (owner preference + white flash at 217 ppi). Add: accent recolors the “now playing” tile too (WP does this) |

---

## 3. Top 7 recommendations (implementation order)

**Wave 1 — navigation and gestures (no new rendering, P0).**

1. **Back stack + hardware keys.** Rules: Back closes overlay (shade, dialog, menu, keyboard) → subpage → app → launcher; Back from a pivot page exits the app (pivot item is not a page); long Back 700 ms opens task switcher; Menu opens “…” menu; Home opens launcher; Back while keyboard is visible only hides keyboard. Check KEY_BACK 158 / KEY_MENU 139 on the input device (currently only VOL/HOME/POWER are read). Acceptance: 6 scenarios, none can get “stuck.”
2. **In-place long-press.** Tile: 650 ms → two 44 icons (unpin, resize), tap outside = exit. Queue: menu 240×200 (4 items, 48: queue / next / info / remove). Apps list: pin / delete. Acceptance: no editing scenario navigates to a separate screen.
3. **Player pivot strip.** 3 pages (Now / Queue / Info), strip 56, active 30 ExtraLight, inactive 13 dim, gap 16, next heading preview 24 px; switch by tap (swipe only where there is no seek). Acceptance: “Queue” to “Info” in one tap, Back from “Info” → “Now,” Back from “Now” → launcher.

**Wave 2 — grid and feedback (P1).**

4. **Three tile sizes.** 96 / 208×208 / 432×208, margins 24, gap 16; medium is square; now-playing is a wide tile **at the top** (then budget is 752 of 800), remove pause from tile (one tile = one action); 16×16 counter only for unread items. Acceptance: 4 small in a row, 2 medium in a row, wide = 2 medium.
5. **App bar: labels + “…”.** Bar 48 → 64 when expanded (icon 24, label 11), menu 4×44 above bar, “…” 44×44; Menu key opens the same menu. Acceptance: each icon is explained with one tap, rare commands do not occupy the bar.
6. **Toast + launch placeholder.** Toast 480×64 at y=32 (icon 24, 13 bold, 12 dim, 3 px accent at left, 5 s, swipe right >80 px); “launching…” 13 dim centered until MapNotify. Acceptance: starting pcmanfm does not look like a hang; “saved” needs no dialog.

**Wave 3 — shade and lock (P1).**

7. **Action center + lock parity.** “CLEAR” 11+1 in shade header; swipe right >100 px dismisses card; 12+11 counter in status bar; on lock — 5 quick-status slots 40×40 at y=612 (icon 20, number 11), now-playing = detailed status (2 lines). Acceptance: dismiss notification with one gesture; see 5 sources on lock screen without unlocking.

Next, per the tables: P2 — letters/jump (>24 apps), search, folders, tile counter, task cards, multi-select, checkbox, disabled state, theme repaint.

---

## 4. What not to adopt and why

1. **Animations of any kind** — live-tile flip/cycle (random interval), tilt-press (tilt up to 0.3 rad and drop 25 px), turnstile 350 ms, feather-stagger 40—50 ms, scroll rubber-band, app bar/shade/menu slide-in. Reason: full 480×800×2 B frame = 768 KB; 20 fps = 15 MB/s of copies + text rasterization on a 1 GHz CPU without GPU. This drains the battery and makes input jerky. We already operate without them — and this is closer to Metro’s “discrete” states than it may seem: 184 zero durations in Win8 templates.
2. **Rounded corners and capsules.** WP switch is a capsule with round thumb; ours is 48×24 with a square thumb. Square corners are not our whim: 0 `CornerRadius` in all 13 Metro theme files. The only circles worth keeping are radio and progress dots (real ellipses, not “rounded rectangles”).
3. **Shadows, blur, “glass,” translucent panels** — 0 in Metro and 0 on our hardware; 62—74% dimming already gives the required depth.
4. **Panorama and parallax start wallpaper.** 187 px heading + layers moving at different speeds + 24 px section preview: on a 480-wide screen, 1.5 screens for an effect; parallax continuously redraws while scrolling.
5. **3 tile columns (WP8.1).** At 480, content is 432: three medium tiles with 16 gap = 133×133, small in this grid ≈66. A 48 icon + 15 label do not fit in 133 px, and 66 px is an icon, not a tile. Rejection is arithmetic, not taste.
6. **Pinch semantic zoom.** It was not on phones: jump list opened by tapping a letter (pinch was the tablet model). Also, a multitouch gesture on our driver is unnecessary risk.
7. **Live previews in task switcher.** A minimized X window has no content; `XGetImage` from an invisible window is not possible, and keeping all windows mapped costs memory. Cards = name + icon.
8. **Toast entirely in accent color** (as in WP) — for us, accent means action, not fill; 3 px along the edge provides the same “signal” without violating the palette.
9. **Suggestions above keyboard (Word Flow), Cortana/Bing, light theme, live lock wallpaper.** Russian morphology dictionary, network/service, owner preference, continuous redraw.
10. **System font scaling and “tile with a button.”** A tile is one action; the guide prohibits nested controls inside it (our 32×32 pause is exactly this case).

---

## 5. Sources

**Primary (Microsoft, archived WP8 design library / SDK templates):**

1. Tiles for Windows Phone 8 — template sizes, WVGA/WXGA: learn.microsoft.com/previous-versions/windows/apps/hh202948(v=vs.105)
2. Tile design guidelines for Windows Phone — 3 sizes, live/static, “one tile — one action,” counter 1—99: learn.microsoft.com/previous-versions/windows/apps/jj662929(v=vs.105)
3. App bar for Windows Phone — 72 px, 4 buttons, “…”, ≤5 items, 14—20 chars: learn.microsoft.com/previous-versions/windows/apps/ff431813(v=vs.105)
4. App bar icon buttons — 48×48, 26×26 glyph, “no back button”: learn.microsoft.com/previous-versions/windows/apps/ff431806(v=vs.105)
5. Theme resources for Windows Phone — scale 18.667…186.667, Light/SemiLight/Semibold, `PhoneMargin` 12: learn.microsoft.com/previous-versions/windows/apps/ff769552(v=vs.105)
6. Pivot control design guidelines — ≤4 pages, 1—2 words, no sliders or editing, cyclic, empty page remains: learn.microsoft.com/previous-versions/windows/apps/hh202919(v=vs.105)
7. Panorama control — long canvas, parallax, 24 px preview: learn.microsoft.com/previous-versions/windows/apps/ff941104(v=vs.105)
8. Toasts for Windows Phone 8 + Essential graphics (32 px status bar, 10 s toast, accent bar, ~40—47 chars): learn.microsoft.com/previous-versions/windows/apps/jj662938(v=vs.105), …/hh202884(v=vs.105)
9. Navigation, orientation and gestures — Back stack, pivot item ≠ page, MessageBox/keyboard canceled by Back: learn.microsoft.com/previous-versions/windows/apps/hh202911(v=vs.105)
10. LongListSelector guidelines — 8+ items, groups, jump list, text ≥12: learn.microsoft.com/previous-versions/windows/apps/jj735577(v=vs.105)
11. CheckBox guidelines + 32×32/3 px template (7.1): learn.microsoft.com/previous-versions/windows/apps/hh202897(v=vs.105)

**Secondary, but with numeric values checked against templates:**

12. libwinui, “Metro (MDL)” — 0 radii in all themes, 2 px borders, checkbox 21, radio 23, Win8 switch 50×19, press-inversion, 12 px WP module, Win8 scale: libwinui.com/background/themes/mdl/
13. UI Design and Interaction Guide for Windows Phone 7 v2.0 (July 2010) — 9 mm/34 px touch targets, 2 mm/8 px gap, 7 mm/26 px minimum, status bar, toast (PDF, tableless.github.io mirror)
14. Peter Torr (MS), “Back means back” — Back and long-press rules: learn.microsoft.com/archive/blogs/ptorr/back-means-back…
15. Windows Experience Blog, “Navigation improvements coming in Mango” — search in app list, letters only at ≥45 apps
16. Windows Developer Blog, “Tips and tricks… XAML controls” (2014) — on phone, semantic zoom is triggered by tapping the heading, not pinching
17. Windows Experience Blog, WP8 lock screen Q&A + Thurrott — 1 detailed status + up to 5 quick statuses
18. Windows Central, WP8.1 review + Microsoft Devices Blog — action center: 4—5 quick actions, clear all, all settings, swipe right, works on lock screen, “handle bar” in fullscreen
19. Wikipedia “Windows Phone 8.1” — 3 columns, folders in Update 1
20. StackOverflow 12837720 (“WP8 Start screen tile sizes and margins”) — WVGA tiles 100/210/430×210, gap 10 (verified by identity 430 = 2*210 + 10)
21. LG-P700 User Guide — physical Home + capacitive Back and Menu

**Our files:** `notes/shell/DESIGN-SYSTEM.md`, `SCREENS.md`, `OS-DEV-HANDOFF.md`, `shell/metro.css`, `shell/m-*.html`, `artifacts/shell/m-*.png`, `PRODUCT.md`, `DESIGN.md`.

---

## Conclusions (5—7)

1. **We have adopted almost all of the style, but less than half of the behavior.** Square corners, one accent, Light headings, hidden commands — these are literally from the guide (0 radii, 0 shadows in templates). The gaps are in gestures and states, not the palette.
2. **The biggest omission is long-press as a second control path.** In WP, almost everything “rare” lives under press-and-hold: tile unpin/resize, app pinning, track actions. Our long-press opens a separate editing screen, and does nothing in the queue — this is the cheapest and most noticeable upgrade.
3. **Pivot is for the player, not for adding screens.** Our three pages are already a pivot; what is missing is the 56 strip, large Light heading, and tap on it. Swipe conflicts with the seek strip, so tap is not a compromise but the solution.
4. **The tile grid must become real: 96 / 208×208 / 432×208, with 24 margins and 16 gap.** Square medium does not fit within 800 together with the now-playing strip (812 > 800) — therefore now-playing becomes a wide tile at the top, and pause is removed from it (one tile = one action).
5. **Arithmetic matters more than the literal guide: our 44 px = 5.2 mm, not 9.** At 217 ppi, WP’s “34 px” is 4 mm. Keep 56—60 for rows, 72+ for primary actions, and 2 px for interactive control borders (WP has 2—3 px; we have 1).
6. **Nothing from Metro’s “motion” should be adopted beyond static states.** Flip, tilt, turnstile, parallax, 3 columns, pinch zoom — either cost frames or do not physically fit. Static live tile, static toast, static “launching…” deliver 90% of the feel without a single extra frame.
7. **The P700 hardware gives us the WP model for free:** Home = Start, Back = Back stack, Menu = “…”. This removes the need for on-screen Back/Close (the guide explicitly prohibits them) and frees 44 px on the left of every header — provided KEY_MENU/KEY_BACK work through evdev.

**Redactions:** none. No IP address, password, or personal path appeared in the supplied text.
