# SETTINGS-SPEC — LG P700 Settings (graphite shell)

> Date: 24.11.2026. This document covers only the information architecture of settings:
> what is shown, in what order, what is controlled, and where it is located.
> No code or phone changes were made; everything below is a decision based on facts from `STATUS.md`,
> `SYSTEM-STATUS.md`, `SYSTEM-RESEARCH.md`, `POWER-PLAN.md`, `MODEM-PLAN.md`,
> `AUDIO-PLAN.md`, `PLAYER-FEATURES.md`, `THEMES-PROPOSAL.md`,
> `APP-INTEGRATION-PLAN.md`, `notes/shell/*`, `shell/m-*.html`, `metro.css`.
>
> Device: LG Optimus L7 P700 (u0), MSM7227A, 480×800 RGB565, 400 MB,
> pmOS/Alpine, kernel 3.4 #23, X11 on fbdev + custom Xlib shell.
> Working: Wi-Fi, touch, keys, sound/headphones (CAD), SD, battery (no current
> or temperature), backlight, vibration, sensors, flashlight.
> Not working: BT, GPS, camera, SIM/telephony, NFC (no chip).

---

## 0. Decisions at a glance

1. **Root — 11 rows in three groups, with no scrolling.** The 12th row does not fit
   (856 px, calculation in §3.2). “About phone” is a regular row; the footer is
   only the device caption.
2. **Of Android’s 11 sections, three are live for us:** screen/appearance, sound, system.
   Seven items are “planned” (SIM, BT, GPS, PIN, AP, backup, update); many are
   “not needed” (roaming, accounts, DND, font scaling); the rest are “no
   hardware” or “already in the shade.”
3. **Settings represent device state, not a wish list.** The top has three
   “how things are now” rows (power, network, card), because the phone runs
   without a battery on a 500 mA port and without half its radios — this is the first thing to see.
4. **Brightness and volume are not duplicated at the root**: they are already in the shade
   (`shell/m-shade.html`), while the root has only navigation and a footer.
5. **Unavailable features are shown as facts, not promises:** a gray row + short
   “why” + shade with 2–4 sentences and a link to the plan. No “coming soon.”
6. **The only contract between the shell, player, and services is
   `/etc/lgtest-settings`**: typed keys, atomic writes, aliases
   for old values, unknown keys preserved.
7. **Search — P2.** 26 screens and ~140 rows with 11 rows at the root: using a finger
   is faster than using the keyboard. Search is enabled only once the shell keyboard exists
   (it is P1 in `APP-INTEGRATION-PLAN.md`).
8. **EQ, FM, suspend, kernel update — not in the UI.** EQ was deferred by the owner,
   FM hangs I2C, suspend without wakeup sources is irreversible, and the kernel is updated
   only via `dd`/fastboot according to the plan.

### 0.1. What this document does NOT do

- does not describe the shade or lock screen (they are in `SCREENS.md`), only references them;
- does not design notifications — we have no notification system;
- does not introduce a sandbox, app permissions, or accounts — these do not exist in the Alpine session;
- does not promise features absent from the kernel (BT/GPS/SIM/NFC/camera).

---

## 1. Mapping: Android → our system

Statuses: **available** — works and makes sense; **planned** — hardware/software exists or
is nearly there, but does not work now; there is a plan; **no hardware** — physically absent;
**not needed** — exists, but is pointless for this device (with the reason stated).

### 1.1. Connectivity and internet

| Android item | Status | What we show |
|---|---|---|
| Wi-Fi | **available (partial)** | S2: switch, SSID, IP/gateway/MAC, signal; network list — P2. Managed with `wpa_supplicant` + `udhcpc`, not NM: NM hangs on DHCP (`WIFI-SOLVED.md`). |
| Mobile network | **planned** | Gray row “SIM busy.” The V20e modem is alive (`ATI`, IMEI), but UIM does not complete init: APPS has no EFS server `/dev/rmt_storage`, `CONFIG_MSM_RMT_STORAGE_CLIENT` is unset. `MODEM-PLAN.md`, phases 1–2. |
| SIM | **planned** | Same row; no separate screen. `AT+CPIN?` → `+CME ERROR: SIM busy` (CME 14). |
| Roaming | **not needed** | There is no network registration at all — the setting has no purpose. |
| Airplane mode | **not needed** | The only radio is Wi-Fi; “airplane” means turning Wi-Fi off, already available in the shade and S2. A second switch creates the false expectation that the modem can be “turned back on.” |
| Hotspot | **planned (P2)** | Gray row: AP mode on WCN1314 has not been tested; SDIO polling and 500 mA from a PC port make it pointless in v1. |
| VPN | **not needed** | No client and no use case (phone has no mobile network); an SSH tunnel from the terminal is available, but that is not a setting. |
| NFC | **no hardware** | No row at all. There is no NFC chip in the u0 BOM and no driver in the kernel. |

### 1.2. Connections and devices

| Item | Status | What we show / reason |
|---|---|---|
| Bluetooth | **planned** | Gray row “HCI is silent.” Qualcomm Bahama chip is present, but it does not respond before `hci_qcomm_init` (poke B0 + NVM): `btattach` returns `00:00:00:00:00:00`. `hci_smd` is a dead end (no WCNSS on 7227A). |
| Data transfer (USB) | **available (partial)** | S2: “USB network (RNDIS) <phone-ip>”. No MTP and there will be none: gadget is RNDIS only; files use `scp` over SSH or SD. |
| Printers | **no hardware** | `CONFIG_USB_OTG=n`, USB device-only, no VBUS on the connector — no host. |
| Accessories | **no hardware** | Same: OTG is disabled; external devices cannot be connected. |
| External displays | **no hardware** | No HDMI/MHL/SlimPort; DSI panel is internal only. |

### 1.3. Screen and appearance

| Item | Status | What we show |
|---|---|---|
| Brightness | **available** | S4: slider 0..255 (LM3530), default 150. Write to `/sys/class/leds/lcd-backlight/brightness`. |
| Dark/light theme | **available, but different** | S4.1: 7 themes (3 dark, 2 light, 2 gray) × 5 accents, live preview. This is not “dark/light,” but a lightness scale L* 6…95. |
| Font size and scaling | **not needed (v1)** | The 480×800 grid is fixed, with one font (DejaVu) and 44 px targets; scaling breaks the Metro layout and offers no benefit on 4.3″. Book font size is a separate reader setting (P2). |
| Refresh rate | **no hardware** | LG4573B panel — MIPI video mode; frames are output only via `FBIOPAN_DISPLAY`; there is no refresh-rate parameter. `/proc/msm_pm_stats` and the panorama viewer belong in Lab. |
| Wallpaper | **not needed** | There is no wallpaper: the background is the `bg` token + 565 texture, with tiles on top. Wallpaper on 16-bit color and 1 GHz means extra decoding and extra cache. |
| Always On | **no hardware** | The panel does not support partial update/AOD; “screen off” means backlight at 0 while DSI remains active. |

### 1.4. Sound and vibration

| Item | Status | What we show |
|---|---|---|
| Call/notification volume | **not needed** | No telephony and no notification system — nothing to adjust. |
| Media volume | **available** | S5: 0…30 steps (−40…0 dB), MPD `setvol`, software mixer. |
| Ringtones | **not needed** | No calls. |
| Haptic feedback | **available (partial)** | S6: “vibrate on long press,” `/sys/class/timed_output/vibrator/enable` (duration only, no amplitude). Off by default. |
| “Do not disturb” | **not needed** | No notification daemon and no apps that can “disturb”; the only sound is MPD, and pause is already available in the player and shade. |

### 1.5. Notifications and status bar

| Item | Status | What we show / reason |
|---|---|---|
| App alerts | **not needed** | No notification system: no D-Bus service, no app permissions. The shade shows 2 status cards (MPD, Wi-Fi) — these are shell markup, not notifications. |
| Status bar icons | **not needed** | Status bar is fixed: time, Wi-Fi, battery. There are no app icons because no apps have notifications. |
| Quick access panel | **available** | Shade (`m-shade.html`), 8 tiles + 2 sliders. This is not Settings; there is no link to it from the Settings root — it already opens with a swipe from the top. |

### 1.6. Battery and power

| Item | Status | What we show |
|---|---|---|
| Level | **available (unreliable)** | S1: without a battery, `capacity` is a fake 100%; show “no battery · 4.36 V.” With a battery — % and VBAT. |
| Power saving | **available** | S1: “Save on USB” (brightness ≤150, 600 MHz, do not raise Wi-Fi); governor and frequency limit are in Lab. |
| Statistics | **available (partial)** | S10.3: `time_in_state`, `/proc/msm_pm_stats`, loadavg, D-states. No current or temperature. |
| Background activity | **not needed** | No sandbox or quotas; memory is limited by LRU task eviction (P2), not by “background activity.” |
| Temperature | **no hardware** | `batt_therm` = constant 36.0 °C, `chg_therm` 64…120 — raw ADC, `CONFIG_THERMAL=n`, `/sys/class/thermal` is empty. |
| Charging current | **no hardware** | No `current_now`/`power_now`; charger IC registers cannot be read through RPC (`chg_stat_reg=-5`). |

### 1.7. Memory and storage

| Item | Status | What we show |
|---|---|---|
| Used space | **available** | S3: root 1.6/2.7 GB, SD 29.4 GB (`df`). |
| Cache | **available** | S3: cover cache (up to 30 MB), library index; clear it using one button in the shade. |
| Files | **available (outside Settings)** | The “Files” button opens `pcmanfm` in 480×712 content; the file manager itself is an app, not a Settings subscreen. |
| Apps | **available** | S7: list from `.desktop`, pin with ＋/✓ (as in `m-apps`). |
| Media | **available (partial)** | `/media/sd/Music` + MPD index; there is no separate “OS media library” and none is planned. |

### 1.8. Apps

| Item | Status | What we show / reason |
|---|---|---|
| List | **available** | S7, 64 px rows, ＋/✓. |
| Permissions | **not needed** | No sandbox: SELinux is disabled (otherwise `sk_buff` breaks in the stock wlan.ko), one `user` account, everything runs with its permissions, root is via sudo. Permissions would be fictitious. |
| Defaults | **not needed (v1)** | The shell has no MIME handlers or XDG defaults; apps are launched from the tile list. |
| Uninstall | **not needed in UI** | `apk del` is available only in the terminal: the repository is unstable, and one wrong tap can leave the system without libc. In the UI — pinning only. |

### 1.9. Security

| Item | Status | What we show |
|---|---|---|
| Password / PIN / pattern | **planned (P2)** | S9.1: 4-digit PIN, screen only. No pattern: it is weaker than a PIN (a gesture can be observed) and requires a separate drawing screen; password requires the keyboard (the shell keyboard is needed). |
| Fingerprint | **no hardware** | No sensor. |
| Face | **no hardware** | Front camera MT9V113 is present, but there is no HAL/V4L2 client, and this is not biometrics. |
| Find device | **not needed** | No account, no service, no SIM/mobile data; the phone cannot be “found” from anywhere. |
| Data access | **not needed** | No sandbox. Instead, the actual attack surface is SSH access (S9). |

### 1.10. Accounts and users

| Item | Status | What we show / reason |
|---|---|---|
| Accounts | **not needed** | One user, no trusted networks. |
| Sync | **not needed** | Nothing and nowhere to sync. |
| Backup | **planned (P1)** | S9: “Back up settings to SD” — export `lgtest-settings` + `start.json` as one file. Full system backup — `dd` over SSH, not in the UI. |
| Guest profiles | **not needed** | No multi-user session in Alpine; 400 MB cannot be divided between two. |

### 1.11. System and about phone

| Item | Status | What we show / reason |
|---|---|---|
| Language and region | **not needed (v1)** | Strings are compiled in Russian, there are no localization catalogs, changing language means rebuilding the shell. RU/EN keyboard is a layout, not the OS language. |
| Date and time | **available (partial)** | S11: time, date, time zone; “Sync” button (`time.start`/ntp). RTC `/dev/rtc0` exists. |
| Navigation | **available** | Section §9: Back only in the header, Home — launcher, Vol± — value/selection, Power — lock/dialog. |
| Software update | **planned (P2)** | S11.1: `apk` packages from `cdn.alpinelinux.org` only (`postmarketos` entry is commented out). Kernel/bootloader — fastboot/dd, not from the UI. |
| Reset | **available** | S11.2: reset settings (file + pins), not a “factory reset” — reflashing remains manual. |
| Information | **available** | S11: model, SoC, screen, OS, kernel, build, uptime, IP/MAC, IMEI, SD. |

### 1.12. Summary by section

| Android section | Verdict |
|---|---|
| 1. Connectivity and internet | Wi-Fi available; SIM/BT/GPS — planned and gray row; no NFC; AP/VPN/airplane/roaming not needed |
| 2. Connections | RNDIS available; BT planned; printers/accessories/displays — no hardware |
| 3. Screen | brightness and 7 themes available; scaling/refresh rate/wallpaper/AOD not needed or unavailable |
| 4. Sound | media available; calls/ringtones/DND not needed; vibration partial |
| 5. Notifications | no notification system — section does not exist |
| 6. Power | level and preset available; statistics in Lab; no current/temperature |
| 7. Storage | available, except “media library” |
| 8. Apps | list and pinning available; no permissions/uninstall |
| 9. Security | PIN — P2; no biometrics/find device; SSH is an actual setting |
| 10. Accounts | none; local backup (P1) instead |
| 11. System | time, information, reset available; language and update — not in v1 |

---
## 2. Metrics and screen tree

### 2.1. Metrics (from `shell/settings.css`, `metro.css`, `DESIGN-SYSTEM.md`)

| Element | Height | Comment |
|---|---|---|
| Status bar | 32 | time, Wi-Fi, USB; on all Settings screens |
| Subscreen header | 56 | back 44×44 (x=8), title 24 ExtraLight, caption 11 +2 caps |
| Root header | 64 | title 30 ExtraLight, caption 11 +2 caps |
| Section label | 22 | 11 +2 caps, `dim`, 8 above (11 px row) |
| Row | 56 | icon 24 + name 15 (19 lh) + caption 11 `dim` (14 lh) left; value 12 `dim` right (≤190 px), switch 44×24 or chevron 16 |
| Mini row (kv) | 44 | name 15 + value; read-only/explanatory, tap → shade |
| Tall row | 64 | apps, networks (two metadata lines) |
| Theme row | 44 | swatch 20×20 + name 15 + family 11 caps + ✓ marker 16 |
| Segment bar | 44 | 2–4 buttons in a row, gap 12 (8 for 4) |
| Accent bar | 60 | 5 cells at 20% width (4 at 25% in release), dot 30×30, caption 11 |
| Slider | 44 | label 11 caps + value 12 right, 4 px track, 14 slider thumb, 44 target |
| Footer | 22 | root status line (not clickable) |
| Dialog | 400×220 | 74% dimming, two 160×48 buttons |
| Action shade | 480×(40+56·N) | one 432×52 button, N ≤ 4 |

Rule: **everything clickable is at least 44 px**; mini rows are also 44 and
clickable (open an explanation). The 24 px icon is the only decoration in a row.
If segments are in a separate block (as in `s-display`), they occupy 44 with no
name; if in a named row — 56. In §2.4 calculations, segment rows are counted
as 56 (with a name), so a separate block provides extra room.

### 2.2. How many rows fit

Subscreens: `800 − 32 − 56 = 712 px`. Root: `800 − 32 − 64 = 704 px`
(minus footer 22).

| Composition | Formula | Total |
|---|---|---|
| Only 56 rows (subscreen) | 712 / 56 | 12 rows (672, 40 spare) |
| + 1 label 22 | (712 − 22) / 56 | 12 rows |
| + 2 labels | (712 − 44) / 56 | 11 rows |
| + 3 labels | (712 − 66) / 56 | 11 rows (616, 30 spare) |
| Mixed: 6×56 + 4×44 + 2×22 | 336 + 176 + 44 | 556 px, 156 spare |
| Tall list 64 | 712 / 64 | 11 rows |
| Root: 3 labels + 11 rows + footer | 66 + 616 + 22 | 704 + 96 = 800 |

Conclusion: **11 rows with three labels is the root limit** (exactly 800, no spare);
12 rows = 856 and do not fit. A subscreen with three labels fits 11 rows
with 30 px spare; if a 12th is needed, remove a label or convert a row
to mini. If extra space is needed at the root, remove the footer (−22) or one
row; see §3.2.

### 2.3. Tree

```
S0  Settings (root)
├─ S1  Power
├─ S2  Network
│   └─ S2.1  Wi-Fi networks (P2)
├─ S3  Storage
├─ S4  Screen and appearance
│   ├─ S4.1  Theme
│   ├─ S4.2  Accent
│   └─ S4.3  Texture
├─ S5  Sound
├─ S6  Keys
├─ S7  Apps
├─ S8  Launch
├─ S9  Access
│   └─ S9.1  Lock PIN (P2)
├─ S10 Lab
│   ├─ S10.1 Screen and pan
│   ├─ S10.2 Sound and MPD
│   ├─ S10.3 Power and frequencies
│   ├─ S10.4 Network
│   ├─ S10.5 Sensors
│   ├─ S10.6 Logs
│   └─ S10.7 QA scripts
└─ S11 About phone (root row 11)
    ├─ S11.1 Software update (P2)
    └─ S11.2 Reset settings
```

26 screens total (including Lab subscreens and PIN); 9 of them in v1 (P0):
S0, S4, S4.1–S4.3, S5, S6, S8, S11 plus dialogs.

### 2.4. Screen specifications

Control notation: **switch** (two-position), **radio** (selection from several, marker on the right), **slider** (0..N, draggable), **value** (display only + chevron if it navigates), **segments** (2–3 buttons in a row), **drill-in** (navigate to a subscreen), **sheet** (tap opens a shade from the bottom with one action), **button** (action), **info** (inactive row, tap → explanation), **danger** (action with confirmation).

#### S0 — Settings (root)

See §3 in full: contents, order, rationale, height calculation.

#### S1 — Power

Header: “Power”, subtitle `USB · NO BATTERY` (dynamic: `USB`, `CHARGER`, `NO BATTERY`).

| No. | Row | Control | Range / unit | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Battery | info | — | — | dynamic from `power_supply/*`: “no battery · 4.36 V” or “74 % · 3.8 V” | — |
| 2 | Source | info | — | — | “PC USB port · 500 mA” (ac=0, usb=1) / “Charger” / “none” | — |
| 3 | Charge current | info | — | — | “unavailable: no current sensor” → shade | — |
| 4 | Temperature | info | — | — | “unavailable: no reliable sensor” → shade | — |
| 5 | Save power on USB | switch | 0/1 | 1 | when `ac=0 & usb=1`: brightness ≤150, `cpu_max=600000`, do not bring up Wi-Fi automatically | `usb_power` |
| 6 | Frequency limit | value → S10.3 | 245…1008 MHz | 800 | displays current value; edit in Lab | `cpu_max` |
| 7 | Wi-Fi when idle | switch | 0/1 | 0 | after 10 min without connections: `ifdown` + `rmmod wlan` (P2) | `wifi_idle` |
| 8 | Lock now | button | — | — | backlight 0, input ignored; MPD continues playing | — |
| 9 | Power off… | button → dialog | — | — | `sync` + `/sbin/poweroff`, text §7.5 | — |
| 10 | Sleep (suspend) | info | — | — | “unavailable: no wakeup sources” → shade | — |

Height: 56 + 2×22 + 5×56 + 5×44 = 600; 112 spare —
for the subtitle “source identified as USB-PC”.

#### S2 — Network

Header: “Network”, subtitle `WI-FI`.

| No. | Row | Control | Range | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Wi-Fi | switch | on/off | on | on: `insmod -f` + `wpa_supplicant` + `udhcpc` (3–6 s, row “Connecting…”); off: `ifdown` + `rmmod wlan` | `wifi` |
| 2 | Network | value → S2.1 | — | — | SSID or “not connected”; arrow only if `wifi=1` | — |
| 3 | IP and gateway | info | — | — | “<phone-ip> · <gateway-ip>” | — |
| 4 | Signal | info | dBm / Mbit/s | — | “−52 dBm · 52 Mbit/s” from `/proc/net/wireless` | — |
| 5 | MAC | info | — | — | “<mac>” | — |
| 6 | USB network (RNDIS) | switch | on/off | on | subtitle “<phone-ip> — SSH from PC”; off requires confirmation §7.7 | `usb_net` |
| — | UNAVAILABLE | section label | — | — | — | — |
| 7 | Mobile network · SIM | info | — | — | “SIM busy” → shade §6 | — |
| 8 | Bluetooth | info | — | — | “HCI is silent” → shade §6 | — |
| 9 | GPS | info | — | — | “no NMEA data” → shade §6 | — |
| 10 | Hotspot | info | — | — | “AP not tested” → shade §6 | — |

Height: 56 + 2×22 + 3×56 + 7×44 = 576.

Live Wi-Fi state is read from the system (`ip link`, `/sys/class/net/wlan0`),
not from the file: `wifi=` is the **desired state at boot**, and the switch
writes it. The shade and S2 show the same thing because both read
`wlan0`, not the file.

#### S2.1 — Wi-Fi networks (P2)

| No. | Row | Control | Description |
|---|---|---|---|
| 1 | Scan | button | `wpa_cli scan` → `scan_results` (3–6 s), subtitle “Scanning…” |
| 2…N | network | radio | SSID 16, dim subtitle 12 “WPA2 · −61 dBm · connected”, lock 16 on the right |
| N+1 | Hidden network… | button | opens keyboard (SSID + password) — only when the shell keyboard is available |

Prerequisite: `/etc/wpa_supplicant.conf` must contain
`ctrl_interface=/run/wpa_supplicant` and `update_config=1` — **it is absent now**,
so S2.1 is P2 together with the config edit (one line, separate commit).
Network passwords live only in this file (0600 root) and are not copied into
`lgtest-settings`. Empty states — §10.

#### S3 — Storage

Header: “Storage”, subtitle `ROOT + SD`.

| No. | Row | Control | Range | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | System | info + 4 px bar | — | — | “1.6 GB of 2.7 GB (59 %)” (`df /`) | — |
| 2 | SD card | info | — | — | “29.4 GB · vfat · <sd-uuid>” / “not found” | — |
| 3 | Eject card | button | — | — | `umount /media/sd`; if MPD is playing — dialog §7.6; then “Card can be removed” | — |
| 4 | Music | info | — | — | “126 tracks · 1.8 GB · /media/sd/Music” (from MPD index) | — |
| 5 | Cover cache | sheet | 0…60 MB | 30 | shade: “Clear cache” 432×52 → delete `/var/lib/lp7player/art/*` | `cache_mb` |
| 6 | Library index | sheet | — | — | shade: “Update database” → MPD `update`, “Rebuild” → `rescan` | — |
| 7 | Swap | info | — | — | “256 MB on eMMC · 0 %” → shade “turn off swap” (P2) | — |
| 8 | Format SD… | danger | — | — | P2; text §7.3, hold for 3 s | — |
| 9 | Files | button | — | — | open `pcmanfm` in content area 480×712 | — |

Height: 56 + 2×22 + 5×56 + 4×44 = 556. SD empty state — §10, item 4.

#### S4 — Screen and appearance

Header: “Screen and appearance”, subtitle `THEME · LIGHT`.

| No. | Row | Control | Range / unit | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Brightness | slider | 0…255 (display %) | 150 | `write()` to `lcd-backlight/brightness` throttled at 100 ms; to file on release | `brightness` |
| 2 | Turn off after | segments | 15 / 30 / 60 / 120 / NONE | 30 s | timer with no input + MPD paused → lock screen (backlight 0) | `timeout` |
| 3 | Dim after 10 s | switch | 0/1 | 1 | 10 s with no input → brightness 90 (2 steps) | `dim_after` |
| 4 | Theme | value → S4.1 | 7 values | graphite | immediate redraw 30–50 ms, no reboot | `theme` |
| 5 | Accent | value → S4.2 | 4 released | theme default | 3 tokens change (`accent`, `accent2`, `onacc`) | `accent` |
| 6 | Background texture | switch | ON / OFF (default: automatic by family) | on | when disabled `tex=off`, no tile rebuild needed | `tex` |
| 7 | Rotation | info | — | — | “180° is set by the panel; touch coordinates are already in screen orientation” | — |
| 8 | Refresh rate | info | — | — | “not configurable: video mode + FBIOPAN” → shade | — |
| 9 | Wallpaper | info | — | — | “no wallpaper: tiles and texture” → shade | — |
| 10 | Always On | info | — | — | “panel does not support AOD” → shade | — |

Height: 56 + 2×22 + 6×56 + 4×44 = 612.

#### S4.1 — Theme

| Block | Height | Description |
|---|---|---|
| Preview | 96 | 432×96: theme background, `surf` tile, `accent` bar, “Aa 15”; shows the result **before** applying |
| List | 7×44 | by lightness: Graphite → Coal → Slate → Book → Book · neutral → Paper → Paper · neutral |
| Hint | 24 | 11 `dim`: “no grain on dark themes” |

Theme row: swatch 20×20 x=20, name 15 x=52, family on right 11 caps
(DARK / LIGHT / GRAY), marker ✓ 16. Tap = apply immediately (radio), without
“Save” button: `theme=`, rebuild tiles, `XClearWindow`, redraw.
Total 56 + 96 + 7×44 + 24 = 484; 228 spare — for the subtitle
“Theme applies to all screens” and, if desired, a second preview row
(same theme in “list” and “player”).

#### S4.2 — Accent

Five cells, each 20 % of the width, in a 60 px strip: 30×30 dot and 11 label below
(Turquoise, Amber, Blue, Red, Green). Selected — `fg` outline 2 px,
pressed — `accent`. Release has four cells at 25 %: red is not released; it
is reserved for “power/danger” semantics (power-off dialog), and a play button
in that color reads as “record”. At the bottom, a 44 mini-row: “Red is not
released: reserved for power semantics”. The `shell/s-display.html` mockup
still draws five — this is a discrepancy, see §3.5.

Tap: `accent=`, 3 tokens change, full redraw 30–50 ms. Reset:
long-press the current accent → “theme default accent” (empty
`accent=` value).

#### S4.3 — Texture

A single master switch, not segments: “like / dislike” is a
binary decision, while the choice between grain and laid texture has already
been made in `tex=auto` by theme family. Manual modes (`grain`, `laid`,
`both`) remain in the file for debugging, but are not shown in the UI.

| No. | Row | Control | Description |
|---|---|---|---|
| 1 | Background texture | switch 44×24 | `tex=off` when disabled, return to previous value (usually `auto`). Subtitle: “roughness · auto” / “off · plain background” |
| 2 | Hint | info | “AUTO: grain on paper and book; no grain on dark themes, only laid texture” / “Texture is disabled everywhere; background remains the theme color” |

Height: 48 + 22 = 70 px — 26 px less than three segments with a preview,
so the screen still fits within 800 px without scrolling.
Disabling applies to all screens immediately: background becomes a plain theme
color, tiles are not drawn, `--tex: none` in Xlib.

#### S5 — Sound

Header: “Sound”, subtitle `MPD · HEADPHONES`.

| No. | Row | Control | Range / unit | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Volume | slider | 0…30 steps (−40…0 dB) | −24 dB | MPD `setvol round(100·10^(dB/20))` on move; to file on release | `vol_db` |
| 2 | Output | info | — | — | “headphones (with microphone)” / “speaker”; route is set by `lgjack`, do not switch manually | — |
| 3 | Warn above −16 dB | switch | 0/1 | 1 | first time per boot, banner “Loud!” for 3 s | `vol_warn` |
| 4 | Pause when headphones are unplugged | switch | 0/1 | 1 | `h2w/state → 0` while MPD is playing → `pause 1` | `hp_pause` |
| 5 | Sleep timer | segments | OFF / 15 / 30 / 45 / 60 | OFF | on expiry: `pause 1`; remaining time — on second line (“12:34 remaining”) | `sleep_timer` |
| 6 | Test channels | button | — | — | 1 kHz tone, −20 dBFS, 3 s, through current output; headphones on the table, not in ears | — |
| 7 | Do not disturb | info | — | — | “no notification system” → shade | — |
| 8 | Ringtone | info | — | — | “no telephony” → shade | — |

Height: 56 + 2×22 + 5×56 + 3×44 = 512.
There is no EQ row at all — the owner deferred EQ to the end of the project; it is not shown in the UI.

#### S6 — Keys

Header: “Keys”, subtitle `VOL · POWER · HOME`.

| No. | Row | Control | Range | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Button backlight | segments | AUTO / ON / OFF | auto | `button-backlight/brightness` 255/0; AUTO — 5 s after input, always 0 on lock screen | `btnlight` |
| 2 | Vol± in lists | segments | VOLUME / SELECT | volume | SELECT: in settings and lists Vol± move focus; in player they still control volume | `vol_keys` |
| 3 | Vibrate on long-press | switch | 0/1 | 0 | `vibrator/enable` for 30 ms on long-press (650 ms) | `haptic` |
| 4 | Hold Vol for 0.7 s | info | — | — | “next/previous track” | — |
| 5 | Power | info | — | — | “short press — lock screen; 1.5 s — power-off dialog” | — |
| 6 | Headset button | info | — | — | “play/pause; double-press — next” (P1, `KEY_MEDIA` event4) | — |
| 7 | Test keys | button | — | — | Lab screen: last key code/name, 5 history rows | — |
| 8 | Menu | info | — | — | “there is no Menu key on the case” | — |

Height: 56 + 2×22 + 4×56 + 4×44 = 500.
In v1 (P0), rows 1, 4–6 and 8 work in S6; rows 2 (Vol± in lists),
3 (vibration) and 7 (test keys) appear in P2 — until then they are absent
from the list: they are not “unavailable due to hardware”, they are not yet
implemented, and must not be shown in gray.
`KEY_END` (107) arrives on `event5`, but is unused and is not assigned
to Back: there is no label on the case, so the user will not find it.

#### S7 — Applications

Header: “Applications”, subtitle `N APPLICATIONS` (dynamic).

| Block | Height | Description |
|---|---|---|
| List | 64 × N | icon 24, name 15, subtitle 11 `dim` (category/Exec), ＋ or ✓ on the right |
| Memory | 44 | “182 / 400 MB free” (info) |
| Unload background apps | 56 | switch, default 1 (P2; LRU, except active task and player) |
| Hint | 24 | “＋ — add to Start · ✓ — remove” |

Tap a row to launch (and close settings); ＋/✓ pins/unpins in
`~/.config/graphite/start.json`; long-press opens a shade: “Launch”,
“Pin/Remove”, “Info” (`.desktop` path, Exec).
There are no permissions, “default app”, or “delete” — §1.8.

#### S8 — Startup

Header: “Startup”, subtitle `AFTER REBOOT`.

| No. | Row | Control | Range | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | Mode | segments | SHELL / NO X | shell | writes key, **applied after reboot**; switching to “NO X” requires confirmation §7.2 | `boot` |
| 2 | Takes effect after reboot | info | — | — | shown only if the value ≠ current session | — |
| 3 | If X does not start | info | — | — | “player starts after 20 s; file selection does not change” | — |
| 4 | Watchdog | switch | 0/1 | 1 | shell crash → restart ≤3 times, then fb player | `watchdog` |
| 5 | MPD at boot | switch | 0/1 | 1 | `mpd.start` reads key; off — MPD does not start, S5 shows “unavailable” | `mpd_auto` |
| 6 | Desktop (openbox) | info | — | — | “debug only, not a product mode” → shade | — |
| 7 | Reboot | button | — | — | confirmation §7.9 (music stops) | — |
| 8 | Power off… | button | — | — | dialog §7.5 | — |

Height: 56 + 2×22 + 5×56 + 3×44 = 512.

#### S9 — Access

Header: “Access”, subtitle `SSH · PIN`.

| No. | Row | Control | Range | Default | On change | Key |
|---|---|---|---|---|---|---|
| 1 | SSH over USB | switch | 0/1 | 1 | dropbear listens on `usb0` (<phone-ip>) | `ssh_usb` |
| 2 | SSH over Wi-Fi | switch | 0/1 | 1 | dropbear listens on `wlan0` | `ssh_wifi` |
| 3 | SSH password | value → keyboard | — | — | change `user` password (stored in `/etc/shadow`); P2, keyboard required; risk §7.8 | — |
| 4 | Lock-screen PIN | value → S9.1 | — | “not set” | — | `pin_set` |
| 5 | Lock at boot | switch | 0/1 | 0 | lock screen immediately after shell startup | `lock_boot` |
| 6 | Disk encryption | info | — | — | “none: rootfs is not encrypted” → shade | — |
| 7 | Fingerprint and face | info | — | — | “no sensor” | — |
| 8 | Find device | info | — | — | “no service or SIM” | — |
| 9 | Back up settings to SD | button | — | — | copy `lgtest-settings` + `start.json` to `/media/sd/lgtest-backup-*.txt` (P1) | — |

Height: 56 + 2×22 + 6×56 + 3×44 = 568.

#### S9.1 — PIN (P2)

| No. | Row | Control | Description |
|---|---|---|---|
| 1 | Status | info | “PIN not set” / “PIN set · 4 digits” |
| 2 | Set / Change PIN | button | numeric keypad 4×3 (1…9, 0, DEL, OK), 4 digits, enter twice |
| 3 | Remove PIN | button | requires current PIN; hidden when empty |
| 4 | What it protects | info | “screen only. Disk is not encrypted; data is accessible over SSH and from SD” |
| 5 | Forgot PIN | info | “reset only via SSH or recovery (p17); not possible from the lock screen” |

Storage: not in `lgtest-settings`. Separate `/etc/lgtest-pin` (0600, root):
`salt=…`, `hash=sha256(salt+pin)`, `len=4`. Settings contain only `pin_set=0|1`.

#### S10 — Lab

Header: “Lab”, subtitle `DIAGNOSTICS`.

| No. | Row | Control | Contents |
|---|---|---|---|
| 1 | Screen and pan | drill → S10.1 | fb0, panner, MDP errors, frame capture |
| 2 | Sound and MPD | drill → S10.2 | format, idle, xrun, jack, CAD, tone test |
| 3 | Power and frequencies | drill → S10.3 | VBAT, time_in_state, idle, loadavg, governor |
| 4 | Network | drill → S10.4 | SSID, RSSI, bitrate, lease, reconnect |
| 5 | Sensors | drill → S10.5 | accelerometer, magnetometer, light, proximity, vibration, flashlight |
| 6 | Logs | drill → S10.6 | dmesg, last_kmsg, mpd/lgjack, copy to SD |
| 7 | QA scripts | drill → S10.7 | smoke, capture, burn-in, latest report |
| 8 | Dangerous operations | info | “SSH and planned procedure only: suspend, rmt_storage, baseband” |
| 9 | Diagnostics version | info | “qa 1.0 · 24.11.2026” |

Height: 56 + 22 + 7×56 + 2×44 = 558.

Diagnostic values are not stored: they are read from `/sys` and `/proc` when
the screen opens. Only `cpu_gov`, `cpu_max`, `cpu_hispeed`
(§5.2) are written to the file — they are changed in S10.3; other buttons are
one-off actions.

##### S10.1 — Screen and pan

Header: “Screen and pan”, subtitle `FB0 · PAN`.

| Row | Type | Value |
|---|---|---|
| fb0 | info | “480×800 · 16 bpp · RGB565 · mipi dsi video panel” |
| Panner | info | “fbpan-ng · 10 Hz” / “player · event-driven” |
| Last pan | info | “12 ms” / “—” (last `FBIOPAN_DISPLAY`) |
| Dropped frames | info | `-EBUSY` counter from patch #23 (0 normally) |
| MDP errors | info | number of `mdp_*` lines in `dmesg` for the session |
| Capture frame | button | `/media/sd/shot-<ts>.565` (when pan is stable) |
| Restart panner | danger | restart `fbpan-ng`; warning if X is open |

##### S10.2 — Sound and MPD

Header: “Sound and MPD”, subtitle `MPD · ALSA`.

| Row | Type | Value |
|---|---|---|
| MPD | info | “0.24.20 · play/pause · TCP 6600” |
| Format | info | `status.audio` — “44.1 kHz · 16 bit · stereo” |
| idle | info | “4 ms” (event delay), “reconnects: N” |
| Underruns | info | `PCMDMAMISSED`/`EPIPE` counter for the session |
| Jack | info | `h2w/state` → “headphones with microphone” / “speaker” |
| CAD route | info | “rx=5 tx=3 · mic_mute=1” |
| 1 kHz test | button | 3 s, −20 dBFS, through current output |
| Restart lgjack | button | `rc-service`/`kill` + start; P2 |
| Equalizer | info | “deferred until the end of the project” |

##### S10.3 — Power and frequencies

Header: “Power and frequencies”, subtitle `VBAT · CPU`.

| Block | Type | Value |
|---|---|---|
| VBAT / source | info | “4.36 V · USB · 500 mA” |
| time_in_state | 7-row table | frequency → percentage (61…1008 MHz) |
| Idle | info | “power collapse: 0 · WFI: N” |
| Suspend | info | `suspend_stats` success/fail |
| loadavg / D tasks | info | “8.4 · D: 3” |
| Governor | segments | ondemand / powersave / smartassV2 / interactive (key `cpu_gov`, default ondemand) |
| Frequency limit | slider | 245…1008 MHz by OPP (key `cpu_max`, default 800000) |
| Power snapshot | button | `power-snapshot.sh` → `/media/sd/power-<ts>.txt` |

Warning: “1 GHz + X + Wi-Fi on a 500 mA port = reboot” — 11 `dim` subtitle
under the slider.

##### S10.4 — Network

Header: “Network”, subtitle `WLAN0`.

SSID, RSSI, bitrate, IP, gateway, MAC, DHCP lease, “Reconnect” (button),
“Unload Wi-Fi module” (danger: loss of SSH over Wi-Fi).

##### S10.5 — Sensors

Header: “Sensors”, subtitle `I2C · SYSFS`.

| Row | Type | Value |
|---|---|---|
| Accelerometer | info | x/y/z, 10 Hz → “0.02 −0.98 0.11 g” |
| Magnetometer | info | x/y/z µT |
| Light | info | lux (gp2ap) |
| Proximity | info | “open / closed” |
| Vibrate 200 ms | button | `vibrator/enable` (one-off action) |
| Flashlight | switch | `brightness_uA` 0 / 100000 (off by default, not retained after lock screen) |

##### S10.6 — Logs

Header: “Logs”, subtitle `DMESG`.

Last 20 lines of `dmesg` (mono 10/14), `last_kmsg` (if present),
`/var/log/mpd/*`, `lgjack.log`; buttons “Copy to SD”, “Clear dmesg”.
Text is not truncated by words — only by characters, with “…” .

##### S10.7 — QA scripts

Header: “QA”, subtitle `SMOKE · BURN-IN`.

| Button | What it runs | Subtitle |
|---|---|---|
| Smoke | `qa.py smoke --shot` | “~40 checks, 2 min” |
| Capture | `collect.sh` | “archive on SD” |
| Burn-in 24 h | `burnin.sh` | “player only, X in short sessions; confirmation” |
| Latest report | open `/media/sd/qa-*/report.txt` | “from 24.11, 21:40” |

If scripts are absent from the device, buttons are `dim` with subtitle “qa.py not found”.

#### S11 — About phone

Header: “About phone”, subtitle `P700 · U0`.

| Row | Type | Value |
|---|---|---|
| Model | info | “LG Optimus L7 P700 (u0)” |
| SoC | info | “MSM7227A · Cortex-A5 1 GHz · Adreno 200” |
| Screen | info | “480×800 · RGB565 · LG4573B” |
| Memory | info | “400 MB · 256 MB swap on eMMC” |
| System | info | “postmarketOS edge (Alpine)” |
| Kernel | info | “3.4.0-CM #23-postmarketOS” |
| Build | info | kernel date |
| Uptime | info | “3 h 12 min” |
| Network | info | “<phone-ip> · <mac>” |
| IMEI | info | “<imei>” (from modem; SIM does not work) |
| SD | info | “29.4 GB · <sd-uuid>” |
| Camera | info | “HI542/MT9V113 sensors present, no HAL” |
| Software update… | drill → S11.1 | — |
| Reset settings… | drill → S11.2 | — |
| Licenses | info | “GPL-2.0 kernel, MIT shell; texts on SD” |

Height 2×56 (navigation rows) + 13×44 (mini) + 56 (header) = 740 > 712 →
**scrolling** (drag, 2 px bar, scroll by 28 px).
This is the only settings screen with scrolling; acceptable because it is
informational.

##### S11.1 — Software update (P2)

Header: “Update”, subtitle `APK`.

| Row | Type | Value |
|---|---|---|
| Sources | info | “cdn.alpinelinux.org edge · postmarketos disabled” |
| Check for updates | button | `apk update`, list of N packages, size |
| Update packages | danger | confirmation §7.4; kernel/bootloader are not updated |
| Kernel and bootloader | info | “fastboot/dd only, according to plan; not available from UI” |

##### S11.2 — Reset settings

Header: “Reset”, subtitle `LGtest-settings`.

| Row | Type | Value |
|---|---|---|
| What returns to defaults | info | “theme, accent, texture, brightness, timeouts, volume, pins” |
| What remains | info | “music on SD, notes, MPD library, SSH password” |
| Reset | danger | hold for 3 s; text §7.1; `lgtest-settings` → `lgtest-settings.bak` |

---

## 3. Root: contents and order

## 3. Root: composition and order

### 3.1. What is on the root

| No. | Row | Label (11 `dim`) | Value on right (12 `dim`) | Destination |
|---|---|---|---|---|
| — | **STATUS** | label | — | — |
| 1 | Power | “PC USB port · no battery” / “no battery · 4.36 V” | “500 mA” | S1 |
| 2 | Network | “Wi-Fi · Home-Wi-Fi” / “Wi-Fi off” | “<placeholder>” | S2 |
| 3 | Storage | “SD · vfat” / “SD not found” | “21.7 GB” | S3 |
| — | **APPEARANCE AND SOUND** | label | — | — |
| 4 | Screen and appearance | “<Theme> · <Accent>” | “70%” | S4 |
| 5 | Sound | “headphones · MPD” | “−24 dB” | S5 |
| 6 | Keys | “button backlight” | “auto” | S6 |
| — | **SYSTEM** | label | — | — |
| 7 | Apps | “6 pinned to Start” | “12” | S7 |
| 8 | Startup | “applies after reboot” / “current mode” | “shell” | S8 |
| 9 | Access | “SSH over USB and Wi-Fi” | “no PIN” | S9 |
| 10 | Lab | “fb/pan · MPD · jack · QA” | — | S10 |
| 11 | About phone | “pmOS edge · kernel #23” | “P700” | S11 |
| — | footer | “LG P700 · pmOS edge” | “480×800 · RGB565” | not clickable |

All rows 1–11 have one control — a 16 px chevron (navigation); `val` on the
right is a value, not a control. No switches are used on the root.
The 22 px footer is a device and current theme label; it is not clickable:
“About phone” is already a separate row and does not compete with it for space.
Values update when the root opens and every 10 s while it is on screen.

### 3.2. Height calculation

```
status 32
root header 64                  = 96
3 labels × 22                   = 66    → 162
11 rows × 56                    = 616   → 778
footer 22                       = 800
spare                           = 0
```

A 12th row would make 856 — it does not fit. Therefore 11 is the limit, and it
was chosen precisely: “About phone” became a row (rather than the footer),
“Security” stayed inside “Access” (one entity — who can sign in to the device),
“Mobile network” and other unavailable items are in S2, not on the root.
Spare space 0 is intentional: all heights are fixed (`flex-basis`), and the
DejaVu fonts have known metrics; if more space is needed, remove the footer
(−22) or one row.

### 3.3. Why this order

1. **Status first.** The phone runs on nonstandard power (no battery, 500 mA
   PC port) and without three radios. The user must be able to see at a glance
   “what is true now”: voltage, whether there is a network, whether there is a
   card. These are not settings; this is an instrument panel, and it also
   provides entry to the corresponding screens.
2. **The three daily items together.** Theme/brightness, volume, and key
   backlight are changed most often; they are in one group so they are easy to
   find.
3. **Rare and hazardous items at the bottom.** Apps, startup, access, lab, and
   information are not daily actions; mistakes here have higher consequences
   (mode change, SSH, QA).
4. **No switches on the root.** Wi-Fi, brightness, and volume are already in
   the shade; two places for one action guarantee desynchronization. The root
   is a map; the shade contains quick controls.
5. **The footer is a label, not an entry point.** “About phone” became a
   regular row because there was room after moving to 11 rows; the footer
   remains a status line (model, resolution, theme) and does not compete with
   navigation.

### 3.4. What was deliberately left off the root

| Candidate | Why not |
|---|---|
| Brightness slider | Already in the shade; on the root it would take 56 px to duplicate it |
| Volume slider | Same; plus hardware Vol± |
| Wi-Fi switch | Same (shade), plus S2; a switch on the root would duplicate the status |
| Mobile network / SIM | All unavailable radio is grouped in S2 so the root does not look broken |
| “Do not disturb” | No notifications |
| Separate “Battery/power saving” | This is S1, and status is already in row 1 |
| “Security” | The two actual rows (SSH + PIN) are inside S9 |
| “Update” / “Reset” | Inside S11, to keep hazardous actions more than one tap from the root |
| “About phone” in the footer | The footer became a label; information is in the last row, 11, because 11 rows fit |

### 3.5. Comparison with the existing `shell/s-*.html` mockups

The repository contains a parallel set of settings mockups (`s-root`, `s-display`,
`s-sound`, `s-power`, `s-storage`, `s-wifi`, `s-apps`, `s-security`, `s-system`,
`s-launch`, `s-lab`, `s-about`, `settings.css/js`). They established the actual
metrics adopted above: 32 / 64 / 56 / 22 / 56 / 44 / 22. Content differences
are listed below; this is not “a different design”, but places where the mockup
is ahead of or behind the facts.

| Item | In the `s-*` mockup | In this spec | Decision |
|---|---|---|---|
| Root composition | CONNECTIVITY (Wi-Fi with switch, Mobile network), DEVICE (Screen, Sound, Power, Memory, Apps), SYSTEM (Security, System, Startup, Lab) | STATUS (Power, Network, Storage), APPEARANCE (Screen, Sound, Keys), SYSTEM (Apps, Startup, Access, About phone, Lab) | Both fit within 800 px; remove “Mobile network” (never works) from the root, move the Wi-Fi switch to S2/shade, add “Keys”, combine “Security+System” as “Access” + “About phone” |
| Metric | `.grp` 22, `.shead.root` 64, `.r` 56, `.kv`/`.trow` 44, `.foot` 22 | adopted without changes | The draft spec's labels at 34 and info rows at 44/48 were corrected to actual values |
| Themes | Flat list of 7 rows with family tag (DARK/LIGHT/GRAY) | Same + 96 px preview at the top | Adopt the mockup and add the preview: without it, choosing a theme from a 20×20 swatch is guesswork |
| Accents | 5 cells, including red | 4 in the UI; red only in the file | Remove the fifth cell: red is reserved for power semantics |
| Font | SMALL / REGULAR / LARGE segments | No UI scale | Remove: the 480×800 grid is fixed, one DejaVu font, 44 px targets; font size is a reader setting (P2) |
| Texture | 4 segments (AUTO/GRAIN/LAID/BOTH) | ON/OFF master switch + `tex=auto` by default; manual modes via file only | Adopt the switch: “checked to disable” is clearer than segments and saves 26 px |
| “34 °C” in power row | Present | No: `batt_therm` is a constant, `chg_therm` is noise, `CONFIG_THERMAL=n` | Replace with “no battery · USB 500 mA”; do not show temperature |
| “eMMC 3.1 / 4.0 GB” | Present | rootfs 2.7 GB, 1.6 GB used | Correct to actual figures (`df /`) |
| “no modem” | Present | modem works (`ATI`, IMEI), SIM busy: no APPS-EFS | Replace with “SIM: no EFS server” or remove the row from the root |
| “2 default” (apps) | Present | There is no concept of “default” | Replace with “N · M on Start” |
| “Russian · NTP” (system) | Present | Language cannot be switched; NTP not confirmed by `time.start` | “time 14:35 · Europe/Moscow” + sync button |
| “PIN · 30 s auto-lock” | Present | PIN is P2 and not implemented; auto-lock is a screen setting | “SSH over USB and Wi-Fi · no PIN”; timeout is in S4 |

The mockups remain a visual framework (icons, grid, segments); before
implementation, row text and composition are to be aligned with the facts in
this spec.

---

## 4. Custom sections not found in Android

### 4.1. “Screen and appearance” (S4 → S4.1–S4.3)

Android: 2 themes, wallpaper, scale. Here: **7 themes × 5 accents × 3 texture
states** and a live 432×96 preview. Reasons:

- The 7 themes are three families by lightness (L* 6–10, 79–80, 94–95), not
  “aesthetic options”: the phone is read in the dark in the evening and in the
  shade during the day, and one dark theme does not cover both cases. All seven
  are exposed in the UI because 44 px rows fit without scrolling (484 px out
  of 712 with preview) — unlike an Android list, where each theme would be a
  card.
- 5 accents, 4 in the UI: an accent changes three tokens, so it applies
  immediately and does not require backgrounds to be redrawn. Red is not
  exposed (power semantics).
- Texture: grain is physically possible only on light themes (one 565 code on
  graphite = +40% brightness), so `tex=auto` is the default, and the hint
  explains why “ON” does nothing on graphite.
- A live preview is required: at 16 bits and with seven themes, choosing “by
  eye” from a 20×20 swatch is impossible.

### 4.2. “Startup” (S8)

Android has no concept of “boot without graphics”. Here it is a real risk and
there is a real fallback: X on fbdev conflicts with the panoramator, the shell
may crash, and the fb player is the emergency mode. Therefore:

- “SHELL / NO X” is one 208×44 segment; the value is written immediately and
  applied after reboot. The “applies after reboot” label is visible only if
  the value differs from the current session.
- Watchdog (≤3 restarts, then player) and “MPD on boot” are next to each other
  because they are one startup policy.
- The “Reboot” button is here too: changing the mode without rebooting is
  pointless.
- Automatic fallback **does not change** the selection in the file; otherwise
  one X failure would permanently switch the device to the player.

### 4.3. “Lab” (S10)

This is not “for geeks”, but an acceptance tool: half of the device problems
(pan, xrun, loadavg, D-states, jack) are visible only in numbers. Sections:

| Subscreen | Why it is in the product |
|---|---|
| Screen and pan | Check that there is only one panoramator and frames are not dropped after changing the theme |
| Sound and MPD | xrun/glitches and headphone routing — without this, “crackling” cannot be distinguished from “bad file” |
| Power and frequencies | time_in_state and PC/WFI are the only way to tell that the phone is not sleeping |
| Network | RSSI and lease — why “Wi-Fi is up but SSH is not” |
| Sensors | Check that sensors are alive after reboot (they start automatically) |
| Logs | dmesg/last_kmsg without SSH |
| QA | Smoke and burn-in are run from the phone, not only from a PC |

Rule: Lab **is read-only**, except for explicitly marked buttons (restart the
panoramator, change governor, tone test, flashlight, clear dmesg). Hazardous
operations (suspend, rmt_storage, baseband) are not present in the UI — SSH and
the plan only.

### 4.4. “Keys” (S6)

Android hides this under “Accessibility”. Here the three keys are the primary
input after touch, so:

- backlight (auto/on/off) — already exists in the player; move it as is;
- Vol± as “select” in lists — compensation for resistive touch: hitting a 56 px
  row with a finger is easier than hitting a 136×44 segment at the edge;
- haptic feedback — the only tactile channel (no amplitude control, 30 ms only);
- “Test keys” — a screen showing the last code: an inexpensive way to see
  whether `event1/event5/event4` are alive, without connecting SSH.

### 4.5. “Access” (S9)

Instead of Android “Security”, this contains what we actually have: SSH over
USB/Wi-Fi, screen PIN (P2), and local backup. And an honest line “disk
encryption: no”: the PIN locks the screen, but not the data — this is stated
directly, not hidden.

---

## 5. Storage: `/etc/lgtest-settings`

### 5.1. Format

```
# /etc/lgtest-settings — the single source of truth for the shell, player, and services
# key=value, one pair per line, no spaces around '='
# '#' is a comment; unknown keys are preserved as is
cfg_ver=2
theme=graphite
accent=turquoise
tex=auto
brightness=150
...
```

Rules:

- file ≤ 8 KB, UTF-8 encoding (values are ASCII);
- key: `[a-z0-9_]+`, value: no spaces, `|` is not used;
- booleans are `0`/`1` (not `true/false`): shell scripts also read the file;
- line order is stable: existing lines are not reordered; new ones are
  appended at the end;
- permissions `0644 root:root`; no secrets in the file (PIN is separate, §5.6);
- missing file is not an error: use defaults and **do not create** the file
  until the user changes something.

### 5.2. Keys

| Key | Type | Values | Default | Reader | Effect |
|---|---|---|---|---|---|
| `cfg_ver` | int | 2 | 2 | all | schema version for migration |
| `theme` | enum | graphite, ember, slate, paper, paper-n, book, book-n | graphite | shell, player | 16 tokens + redraw |
| `accent` | enum | turquoise, amber, blue, green, red, `""` | `""` (native) | shell, player | override `accent/accent2/onacc` |
| `tex` | enum | auto, grain, laid, both, off | auto | shell, player | one 256×256 tile per theme; `off` means no texture |
| `brightness` | int | 10…255 | 150 | shell, player, `x.start` | `lcd-backlight/brightness` |
| `dim_after` | int | 0…60 s (0 = off) | 10 | shell, player | first dimming step |
| `timeout` | int | 0,15,30,60,120 s (0 = never) | 30 | shell, player | lock screen on idle |
| `btnlight` | enum | auto, on, off | auto | shell, player | `button-backlight` |
| `vol_db` | int | −40…0 | −24 | shell, player | MPD `setvol` |
| `vol_warn` | bool | 0/1 | 1 | shell, player | “Loud!” banner |
| `hp_pause` | bool | 0/1 | 1 | player | pause when jack is unplugged |
| `sleep_timer` | int | 0,15,30,45,60 min | 0 | shell, player | sleep timer → `pause 1` |
| `haptic` | bool | 0/1 | 0 | shell | 30 ms haptic feedback on long-press |
| `vol_keys` | enum | vol, nav | vol | shell, player | Vol± role in lists |
| `wifi` | bool | 0/1 | 1 | `wifi.start`, shell | desired Wi-Fi state at boot |
| `wifi_idle` | int | 0,10,30 min | 0 | shell, service | `ifdown`+`rmmod` when idle (P2) |
| `usb_net` | bool | 0/1 | 1 | gadget/`usb.start` | RNDIS <placeholder> |
| `ssh_usb` | bool | 0/1 | 1 | dropbear/`sshd.start` | listen on `usb0` |
| `ssh_wifi` | bool | 0/1 | 1 | dropbear/`sshd.start` | listen on `wlan0` |
| `boot` | enum | shell, player | shell | `zz-mode.start` | boot mode |
| `watchdog` | bool | 0/1 | 1 | `session-shell` | restart shell ≤3 times |
| `mpd_auto` | bool | 0/1 | 1 | `mpd.start` | start MPD at boot |
| `usb_power` | bool | 0/1 | 1 | shell, player | limits when `ac=0,usb=1` |
| `cpu_gov` | enum | ondemand, powersave, smartassV2, interactive | ondemand | shell (Lab), service | governor |
| `cpu_max` | int | 245000…1008000 | 800000 | shell (Lab) | `scaling_max_freq` |
| `cpu_hispeed` | int | 245000…1008000 | 480000 | shell (Lab) | `hispeed_freq` for ondemand |
| `pin_set` | bool | 0/1 | 0 | shell | whether a PIN is set (hash stored separately) |
| `lock_boot` | bool | 0/1 | 0 | shell | lock immediately after startup |
| `resume` | bool | 0/1 | 1 | player | resume from saved position |
| `crossfade` | int | 0,2,4,6 s | 0 | player → MPD | `crossfade` |
| `rg` | enum | off, track, album | off | player → MPD | ReplayGain |
| `cache_mb` | int | 5…60 | 30 | player | cover cache limit |
| `video_fit` | enum | fit, fill | fit | video (later) | fit frame |

Non-keys (stored separately and not duplicated in the file): MPD queue/modes
(`state_file`), track position (`/var/lib/lp7player/lastpos`), pinned tiles
(`<placeholder>`), PIN hash (`/etc/lgtest-pin`), boot mode as a legacy file
(`/etc/lgtest-mode`, see §5.5).

### 5.3. Atomic write

One writer at a time, one key at a time:

```c
/* settings_set(key, val) */
1. flock("/run/lgtest-settings.lock", LOCK_EX)      /* timeout 200 ms */
2. read the entire file (≤ 8 KB) into memory
3. replace the "key=" line or append a new one at the end
4. open("/etc/.lgtest-settings.<pid>", O_WRONLY|O_CREAT|O_EXCL, 0644)
5. write(); fsync(); close()
6. rename(tmp, "/etc/lgtest-settings")
7. open("/etc", O_RDONLY|O_DIRECTORY); fsync(); close()
8. flock unlock
```

Why: two processes read the file (shell and fb player), and power may fail at
any moment (no battery). `rename` in the same directory provides “all or
nothing”; without directory `fsync`, metadata may roll back after reboot.
On write error, silently keep the old value and show a line in Lab
(“settings: write failed”).

Sliders write to the file **on release**, not on every movement: 100 writes to
eMMC during one brightness drag would cause wear without benefit.

### 5.4. Reading

- Once at startup: `settings_load()` → an in-memory `key/value` array.
- Unknown key — ignored, but **preserved** on write.
- Unknown enum value → default (for example, `theme=neon` → graphite); the
  file is not rewritten, since the user may have edited it manually.
- No file → all defaults; file is not created.
- File cannot be read/is corrupt (no `=`) → defaults + a line in Lab.

### 5.5. Migration

| Previous | New | How |
|---|---|---|
| `theme=amber` | `theme=ember` | alias on read; canonical value is written on the next theme change |
| `theme=frost` | `theme=slate` | same |
| `btnlight=true/1` | `btnlight=on` | boolean aliases (`true/yes/1` → `on`, `false/no/0` → `off`) |
| no `brightness` | 150 | `x.start` no longer writes 200 directly; it reads the key |
| no `timeout` | 30 | the player currently locks after 15 s — align it with the key |
| no `vol_db` | −24 dB | MPD `state` may have stored 5%; set it to −24 on first startup |
| `/etc/lgtest-mode` (bare word) | `boot` = shell or player | `zz-mode.start`: use `boot=` first, then the old file if absent. `lgtest-mode` CLI writes both for one release, then only the key |
| no `cfg_ver` | `cfg_ver=2` | appended on first write |

Old p10/p11 and other partitions are unrelated: migration affects only the
text settings file and does not touch flash.

### 5.6. Two writers and PIN

- The shell and fb player write only “their own” keys, but each rereads the
  file under lock (read-modify-write); otherwise a theme set in the player
  would overwrite volume set in the shell.
- The PIN hash is not in `lgtest-settings`: `/etc/lgtest-pin` (0600 root),
  `salt=` + `hash=sha256(salt+pin)`. Settings contain only `pin_set`.
- Change PIN: temporary file → `fsync` → `rename`, as in §5.3.
- The emergency fb player (`boot=player` mode) shows only three things:
  theme, key backlight, and the “STARTUP” row — and writes the same keys.
  Network, power, and Lab are not available there: if the shell has not
  started, fix the network over SSH, not in the UI.

---

Placeholders: `<placeholder>` — phone IP address and personal configuration path.

## 6. Unavailable items: how they are shown

### 6.1. Rules

1. The row **is present**, but: label `dim`, no chevron/switch, second line is a
   short fact in 2–3 words (“SIM busy”, “HCI silent”, “no sensor”).
2. Tapping opens a bottom shade (480×(40+56·3), “GOT IT” button 432×52):
   heading, 2–4 factual sentences, one “what is needed” line. No readiness date.
3. Prohibited: “soon”, “in development”, “expected”, spinners, “unavailable”
   without a reason, gray switch in the “off” position (read as “disabled by
   the user, can be enabled”).
4. These rows do **not** appear in search (when available) and do not count as
   “settings” in QA: they cannot be “saved”.
5. The same row is not duplicated at the root: the root shows only the working
   state (Wi-Fi); unavailable items live in S2/S1.

### 6.2. Shade text

| Row | Value | Shade |
|---|---|---|
| Mobile network · SIM | `SIM busy` | “The modem responds to ATI and knows its IMEI, but UIM does not complete initialization: there is no EFS server on the APPS side (`/dev/rmt_storage`), so `AT+CPIN?` returns CME 14. No calls, SMS, or mobile data. A userspace EFS server is needed (`MODEM-PLAN`, phases 1–2).” |
| Bluetooth | `HCI silent` | “The Bahama chip is present, but NVM has not been loaded: the chip does not respond over HCI before `hci_qcomm_init`; BD_ADDR is zero. BlueZ is installed, but there is nothing to enable.” |
| GPS | `no NMEA` | “gpsOne runs in AMSS; the GPSNMEA channel is not offered, and `/dev/nmea` blocks when opened. There is no LOC_API client in userspace.” |
| Hotspot | `AP untested` | “The WCN1314 hotspot mode has not been tested; the SDIO host is polled continuously, and the PC port provides 500 mA. There is nothing to enable.” |
| Charging current | `no sensor` | “There is no `current_now`/`power_now` in the kernel; charger IC registers cannot be read over RPC (`chg_stat_reg=-5`). VBAT and source type remain.” |
| Temperature | `no sensor` | “`batt_therm` is a constant 36.0 °C, `chg_therm` is raw ADC 64…120; `CONFIG_THERMAL=n`, `/sys/class/thermal` is empty. No reliable temperature is available.” |
| Sleep (suspend) | `unavailable` | “Keys, touch, and RTC are not registered as wakeup sources: after `echo mem`, the phone cannot be woken by a key or alarm. First, `device_init_wakeup` (`POWER-PLAN`, phase 3).” |
| Always On | `panel cannot` | “LG4573B is video mode: a frame is output only through `FBIOPAN_DISPLAY`. There is no partial update or AOD.” |
| Refresh rate | `no setting` | “There is no frame-rate parameter: the panel is video mode, and the panner outputs the frame. Its rate and errors are visible in Lab.” |
| Disk encryption | `none` | “The rootfs is not encrypted; the PIN protects only the screen. Data is accessible over SSH and when the SD card is removed.” |
| Fingerprint and face | `no sensor` | “There is no scanner or IR/structured-light sensor on u0; the front camera has no HAL.” |
| Find device | `no service` | “No account, no SIM, and no mobile data: the device is not online when Wi-Fi is off.” |
| Camera (S11) | `no HAL` | “HI542 and MT9V113 sensors are initialized by drivers, but there is no V4L2/MSM client: there is nothing to capture and save a frame.” |
| Equalizer (S10.2) | `deferred` | “The AUDPP object has been analyzed, but enabling it is deferred until the end of the project: the UI does not show EQ settings.” |

No shade contains delivery-time promises. The plan is referenced as text (“MODEM-PLAN,
phases 1–2”), without file links in the UI.

---

## 7. Dangerous actions

General rules:

- a dangerous action is **never** first in the list and does not have an
  accent fill by default: accent is for safe “GOT IT/CANCEL” actions;
- destructive confirmation requires **holding for 3 s** on the button (no
  keyboard and no “type a word”): while held, the line fills from left to right;
  release early to cancel;
- there is no red in the palette, so danger is conveyed by wording, holding,
  and button order, not color;
- after the action, run `sync` (for writes) and show an explicit result on
  screen (“Shutting down…”, “Card unmounted”), with no toasts that have nowhere to appear.

### 7.1. Reset settings (S11.2)

```
HEADING:    Reset settings?
TEXT:       The following will return to defaults: theme, accent, texture, brightness,
            timeouts, volume, key backlight, and pinned tiles.
            The following will remain: music on SD, notes, MPD library, SSH password.
            The settings file will be saved as /etc/lgtest-settings.bak,
            pinned items as start.json.bak.
BUTTONS:    [CANCEL 160×48]  [RESET — hold for 3 s 160×48]
AFTER:      sync; reload settings into memory; redraw; show the line
            “Settings reset · .bak file saved” for 3 s.
```

Lost: values in `lgtest-settings` and `start.json`. Preserved: music,
notes, MPD database, SSH keys, PIN (it is in a separate file and is reset
with a separate button in S9.1).

### 7.2. Change boot mode (S8)

```
HEADING:    Boot without the shell?
TEXT:       The next boot will start the fb player without X11. The shell,
            applications, file manager, and browser will be unavailable.
            You can switch back using this same row in the player settings.
BUTTONS:    [CANCEL]  [CHANGE]  [CHANGE AND REBOOT]
```

“CHANGE” writes `boot=player` and leaves the current session running; the third button
runs `sync` + reboot. Switching back works the same way, with `boot=shell`.

### 7.3. Format SD (S3, P2)

Two steps:

```
STEP 1  HEADING: Format SD card?
        TEXT:    All files on card <sd-uuid> (29.4 GB) will be deleted,
                 including music (126 tracks) and p700-efs partition backups.
                 They cannot be restored. The phone will keep working without
                 the card, but MPD and the library will be empty.
        BUTTONS: [CANCEL]  [CONTINUE]

STEP 2  HEADING: Are you sure you want to format?
        TEXT:    The card will be unmounted and repartitioned as FAT32.
                 This will take up to a minute; power must be stable.
        BUTTON:  [FORMAT — hold for 3 s]
        AFTER:   umount; mkfs.vfat -F 32; mount; “Card formatted”.
```

Preconditions: MPD is stopped (`pause 1` + `stop`), the card is mounted,
`ac`/`usb` are normal. If the card is not found, show a `dim` button with the
label “card not found”.

### 7.4. Package updates (S11.1, P2)

```
HEADING:    Update packages?
TEXT:       N packages (M MB) will be downloaded from cdn.alpinelinux.org and
            installed. The kernel and bootloader are not updated: they require
            fastboot/dd according to the plan. If power is interrupted, some
            packages may be left in an intermediate state; rollback requires
            reflashing the rootfs.
BUTTONS:    [CANCEL]  [UPDATE — hold for 3 s]
```

When `ac=0 & usb=1`, show line 11 above the button in `dim`: “Powered by PC port:
update may be interrupted”. Without a battery, kernel updates from the UI are
prohibited entirely—the button is absent.

### 7.5. Power off (S1, S8)

```
HEADING:    Power off?
TEXT:       Music will stop. The MPD queue will be saved, but not the position
            in the track (up to 15 seconds may be lost). Settings are already saved.
BUTTONS:    [CANCEL 160×48]  [POWER OFF 160×48]
AFTER:      sync; /sbin/poweroff; if power is still on after 8 s —
            sysrq 'o' (fallback, see MISC-HW-SOLVED.md).
```

If writing to SD or copying logs, first show the line
“Wait for the write to finish” and a `dim` button.

### 7.6. Eject SD during playback (S3)

```
HEADING:    Stop music and eject card?
TEXT:       MPD is playing from this card. Playback will stop,
            the queue will be saved, but the next track will not start
            until the card is inserted.
BUTTONS:    [CANCEL]  [STOP AND EJECT]
```

### 7.7. Disable SSH over USB (S2/S9)

```
HEADING:    Disable SSH over USB?
TEXT:       Remote access will remain available only over Wi-Fi (<placeholder>).
            If Wi-Fi is down, access can be restored only from the phone,
            and a terminal may not be installed in the shell.
BUTTONS:    [CANCEL]  [DISABLE]
```

### 7.8. Change SSH password (S9, P2)

```
HEADING:    Change SSH password?
TEXT:       The new password will be required to sign in as <placeholder> and over Wi-Fi.
            A forgotten password can be recovered only from the phone itself —
            through recovery (p17) or by reflashing.
BUTTONS:    [CANCEL]  [CHANGE]  → keyboard, enter twice
```

### 7.9. Reboot (S8)

```
HEADING:    Reboot?
TEXT:       Music will stop. Settings are already saved; the MPD queue
            will be preserved, but not the position in the track (up to 15 seconds
            may be lost). If a different boot mode was selected, it will apply now.
BUTTONS:    [CANCEL]  [REBOOT]
AFTER:      sync; reboot -f; if reboot does not work — sysrq 'b'
            (known kernel quirk, MISC-HW-SOLVED.md).
```

---

## 8. Search settings

**Decision: no search in v1. P2 — alongside the shell keyboard.**

Rationale:

- 26 screens, ~140 rows; the root has 11 rows without scrolling. Any row is
  two taps away; search with a virtual keyboard takes at least 5 (open, type
  3 characters, select).
- The keyboard arrives in the shell in P1 (`APP-INTEGRATION-PLAN.md` §4); before
  that, there is physically no way to type a search. Searching “by first letters”
  without a keyboard is the worst option.
- Resistive touch with ~200 ms polling: an extra screen with an input field also
  means extra false taps.

When added, it will work as follows:

| Parameter | Decision |
|---|---|
| Index | static table, generated from the same row table as the screens (single source of truth), `ui/search.idx` |
| Fields | `screen_id`, `breadcrumb` (“Screen and appearance · Theme”), `label`, `synonyms` (ru/eng), `keywords` |
| Matching | case-insensitive substring; equivalent `e` characters treated as equal; label first, then synonyms |
| Results | up to 12 rows of 56: name 15 + `dim` caption 11 on the second line (breadcrumbs); tap → screen, row highlighted `sel` until next input |
| Entry point | “⌕” icon 44×44 in the root header (only when the shell keyboard is available — P1) |
| Excluded | unavailable rows, Lab (P2), “About phone” |
| Keyboard | 480×288 at bottom, content 712→472; input field 44, debounce 250 ms (local index — no network) |
| Empty | “No results” + caption “Try: theme, brightness, sound” |
| Estimate | 1.5 days (index 0.5, screen 0.5, integration 0.5) |

Priority is **P2**: after P0 screens and shell stabilization; two-tap
navigation is sufficient for v1.

---

## 9. Keys and gestures in settings

### 9.1. Hardware keys (fact)

| Device node | Keys |
|---|---|
| `event1` `u0_keypad` | KEY_HOME 102, VOL− 114, VOL+ 115 |
| `event5` `7k_handset` | VOL±, KEY_POWER 116, KEY_END 107, KEY_MEDIA 226 |
| `event4` `h2w` | KEY_MEDIA 226 (headset button) |

**There is no Back or Menu key on the case.** KEY_END is received, but there is
no label on the case — do not map it to Back (the user will not find it, and
this would break expectations in applications).

### 9.2. Behavior table

| Context | Key/gesture | Action |
|---|---|---|
| Any settings screen | Home (short press) | close settings → Launcher (changes are already applied) |
| Any screen | Power (short press) | lock screen: backlight 0, input ignored; music continues |
| Any screen | Power ≥1.5 s | power-off dialog (§7.5) |
| Any screen | Vol+ / Vol− | volume ±1 step (2 dB) when `vol_keys=vol`; show `VOL −24 dB` in header for 1.5 s |
| List (S0, S2.1, S4.1, S7, S10, S11) | Vol+ / Vol− when `vol_keys=nav` | focus the row above/below; holding repeats 6 times/s |
| List | tap | activate row (navigate/toggle) |
| Slider (brightness, volume, frequency) | tap | focus it and show the value large |
| Focused slider | Vol± | ±1 step (brightness 5, volume 1 step, frequency — per OPP) |
| Slider | drag on 44 px track | value follows finger; write to file on release |
| Dialog | tap outside panel | “Cancel” (except during hold — release cancels it) |
| Shade | swipe down from top, start y<32 | open shade (in settings, only from the status bar so sliders are not obstructed) |
| List | vertical drag | scroll without inertia, 2 px bar on right |
| List | long press 650 ms | only where meaningful: application (action shade), dangerous button (hold) |
| Everywhere | Back gesture/left swipe | **none**: resistive screen + drag scrolling cause false triggers |

### 9.3. Focus visuals

- Focus (after Vol±): `sel` background, `accent` bar 3 px on the left along the
  full row height, `fg` text. While pressed, use `surf2`.
- Current radio value — ✓ marker, 16 `accent`, on the right; focus and selection
  are distinct: focus is the bar on the left, selection is the marker.
- Holding a dangerous button: 4 px `accent` line fills from left to right;
  text does not change (no “3… 2… 1…”). Release early and the line disappears.
- No animations; redraw the row on event.

### 9.4. Back

The only Back control is the 44×44 header button (22 px arrow). Rules:

- from a subscreen — to its parent (S4.1 → S4 → S0);
- from the root — close settings → Launcher;
- from a dialog — “Cancel”;
- closing settings does not ask “save?”: everything applies immediately,
  so there is nothing to lose. The only exception is PIN/password entry:
  Back cancels entry and clears the field.

---

## 10. Empty states and errors

| # | Situation | Where | What is shown | Action |
|---|---|---|---|---|
| 1 | Wi-Fi off | S2 | rows 3–5 `dim`, network value “Wi-Fi off”; arrow to S2.1 hidden | enable with the switch; caption “Turn on Wi-Fi to see networks” |
| 2 | No networks found | S2.1 | “No networks found” centered + “Retry” 432×52 | rescan for 6 s |
| 3 | Incorrect Wi-Fi password | S2.1 | “Could not connect” + caption “Check the password” | “Retry” → keyboard (P2) |
| 4 | SD removed | S3, S0 | S3: “SD card not found” + “Check again”; S0: “No SD”; MPD row “none /media/sd/Music” | attempt `mount`; player pauses |
| 5 | MPD unavailable | S5, S10.2 | “MPD unavailable · retry in 3 s”; volume slider `dim`, value “—” | tap to retry now; automatic retry with backoff |
| 6 | No album art | S3 | cache “0 MB · no artwork” (not an error, simply 0) | “Refresh database” |
| 7 | PIN not set | S9.1 | “PIN not set”; caption “Lock screen — swipe up”; “What it protects” — text about lack of encryption | “Set PIN” |
| 8 | Corrupt settings file | S10 (row) | “settings: file is corrupt, defaults loaded” | “Rewrite file” |
| 9 | No sysfs node (backlight, vibration) | S4, S6 | `dim` row: “no /sys/class/leds/button-backlight” | — (tap → shade with details) |
| 10 | No `.desktop` files | S7 | “No applications found” + “/usr/share/applications is empty” | — |
| 11 | Low memory | S7 | “40 MB free” + caption “Close the browser” | — |
| 12 | No QA scripts | S10.7 | `dim` buttons, caption “qa.py not found” | — |
| 13 | No SD for backup/snapshot | S9, S10.3 | “SD card required” in action shade | — |
| 14 | Power source changed while running | S1 | “Source” row updates every 10 s while screen is open; `dim` banner 11 “Powered by PC port: 500 mA” | — |

Rule: an empty state is **one** centered 14–16 px `dim` line in the content
plus one action; no illustration, no 64 px icon, no “configure everything”.

---

## 11. Acceptance checklist

- [ ] 1. Root fits within 800 px without scrolling: 3 labels + 11 rows +
      22 px footer; a 12th row never appears under any values.
- [ ] 2. There are no switches or sliders at the root — only
      navigation and the footer.
- [ ] 3. Every tappable row is ≥ 44 px high; segment ≥ 44×88;
      dialog button 160×48.
- [ ] 4. Root values (power, network, card) update on entry and every 10 s;
      they do not “freeze” after Wi-Fi changes.
- [ ] 5. Theme applies immediately (< 100 ms, measured on phone), without
      reboot; the same theme remains after reboot.
- [ ] 6. `theme=amber` and `theme=frost` in old files yield ember and slate;
      the file is not rewritten when read.
- [ ] 7. An unknown key in the file is preserved when any other key is written;
      an unknown enum value uses the default without crashing.
- [ ] 8. Killing the process during a write (`kill -9` during `settings_set`)
      does not leave a truncated file: either the old or new value remains.
- [ ] 9. Two writers (shell + fb player) do not lose each other’s keys: 20 cycles
      of “theme in shell → volume in player” retain both values.
- [ ] 10. `brightness`/`vol_db` are written to the file on release, not on every
       movement (check with `strace`/write counter).
- [ ] 11. All 14 unavailable rows have a fact as their value and a shade with
       details; none says “soon/will be/expected”.
- [ ] 12. All dangerous actions require confirmation; reset, SD format, and
       update require a 3 s hold; text lists exactly what will be lost.
- [ ] 13. “Change mode” writes `boot=`, takes effect after reboot; X failure
       fallback does not change the value in the file.
- [ ] 14. Empty states: Wi-Fi off, no SD, no MPD, no artwork,
       PIN not set — exactly one text + one action.
- [ ] 15. Vol± change volume on any settings screen in 2 dB steps, without
       sticking when held; Home closes settings. (The “SELECT” mode in
       lists is P2 and is tested separately after implementation.)
- [ ] 16. Short Power press on any settings screen locks the screen; ≥1.5 s opens
       the dialog; canceling the dialog leaves state unchanged.
- [ ] 17. Changing the theme does not reset screen position or restart the shell.
- [ ] 18. S9 shows the actual SSH state (port listening/not), not
       just the key value.
- [ ] 19. In S1, when `ac=0, usb=1`, “PC USB port · 500 mA” is visible, and
       when `usb_power=1`, brightness does not exceed 150.
- [ ] 20. End-to-end scenario: change theme → volume → “NO X” mode →
       reboot → settings remain, theme is unchanged, player starts.

---

## 12. Priorities

Complexity: **S** ≤ 1 day, **M** 2–3, **L** 4–6 (one developer, manual
Xlib drawing, no toolkit). P0 — in shell v1, P1 — immediately after, P2 — after
stabilization.

| Screen | P | Complexity | Dependencies |
|---|---|---|---|
| S0 Root | **P0** | S | row table + state reading |
| S4 Screen and appearance | **P0** | S | backlight sysfs, tokens |
| S4.1 Theme (7×, preview) | **P0** | M | texture tiles, `THEMES[]` |
| S4.2 Accent | **P0** | S | 3 tokens |
| S4.3 Texture | **P0** | S | 256×256 mask |
| S5 Sound (volume, hp, timer) | **P0** | S | MPD setvol/pause |
| S6 Keys (backlight) | **P0** | S | `button-backlight` |
| S8 Startup (mode, reboot) | **P0** | S | `zz-mode.start`, `boot=` |
| S11 About phone | **P0** | S | `/proc`, uname |
| S7 Applications (＋/✓) | P1 | S | `.desktop`, `start.json` |
| S1 Power | P1 | S | `power_supply`, cpufreq |
| S3 Storage | P1 | M | `df`, mount/umount, cache |
| S2 Network (status, switch) | P1 | M | wpa_supplicant, `wpa_cli` |
| S9 Access (SSH, backup) | P1 | M | dropbear, export |
| S10.1 Screen/pan | P1 | S | fb ioctl, dmesg |
| S10.2 Sound/MPD | P1 | S | MPD, ALSA |
| S10.6 Logs | P1 | S | dmesg, files |
| S10.3 Power/frequencies | P1 | M | cpufreq, snapshot |
| S10.5 Sensors | P1 | S | sysfs i2c |
| S10.7 QA | P1 | S | `qa/` on device |
| S10.4 Network | P1 | S | `/proc/net/wireless` |
| S2.1 Wi-Fi networks | P2 | M | `ctrl_interface` in config, keyboard |
| S9.1 PIN | P2 | M | numeric input, `/etc/lgtest-pin` |
| Search | P2 | M | shell keyboard, index |
| S3 SD format | P2 | S | mkfs.vfat, two confirmations |
| S11.1 Update | P2 | M | apk, power monitoring |
| S11.2 Reset | P1 | S | file backup |
| Vibration, `vol_keys=nav` | P2 | S | vibrator, list focus |
| Wi-Fi while idle | P2 | M | `ifdown`/`rmmod` on timer |
| Backup to SD | P2 | S | tar/copy |

The v1 order (P0) covers what is already drawn in `m-settings.html`/`m-shsettings.html`
(theme, backlight, startup, system), plus “About phone”; network, memory, and power
from `m-shsettings` move to P1 because they require real system data (`ip`, `df`,
`power_supply`), not mockup literals.

---

## 13. Three main conclusions and implementation order

### Conclusion 1. More than half of the Android list does not apply to this device

Of 11 sections, three are live (screen/appearance, sound, system). Another seven
items are “planned”, and four are tied to a specific missing component: SIM — an
EFS userspace server; BT — `hci_qcomm_init`/NVM; GPS — the NMEA channel and
LOC_API client; PIN — the lack of a keyboard and encryption. The other three
(hotspot, backup, update) are work items, not hardware limitations. The rest
either has no hardware (NFC, printers, external displays, temperature/current
sensor, fingerprint, AOD) or makes no sense without layers we do not have
(sandbox, notifications, accounts). Settings are therefore a **map of the real
device**, not an Android copy: 26 screens instead of 11 sections, with 14 rows
honestly grayed out and explained by facts, not promises.

### Conclusion 2. The root has 11 rows, and that is the limit; everything else is a subscreen

32 (status) + 64 (root header) + 3×22 (labels) + 11×56 (rows) + 22 (footer)
= 800 of 800. A twelfth row would be 856 and would not fit. This is not “too
little space”; it is discipline: the root answers “where is it?”, not “how do I
configure it?”. Brightness and volume stay in the shade, “Security” is inside
“Access”, and the footer identifies the device rather than hiding an entry point.
As soon as a slider or switch appears at the root, it stops being a map and starts
duplicating the shade — and two places for the same state on this hardware are
guaranteed to diverge.

### Conclusion 3. The settings file is an interface, not a dump

`/etc/lgtest-settings` is read by the shell, fb player, and four init scripts. If
writing is not atomic (as it currently is: `fopen("w")` in the player), a power-off
without a battery leaves a truncated file, and the theme “resets” — so the user
sees not a bug but “the phone reset”. The specification therefore requires:
typed keys with ranges, read-modify-write under a lock,
tmp+fsync+rename+fsync(dir), preservation of unknown keys, aliases
`amber→ember`/`frost→slate`, a separate `cfg_ver`, and a separate file for the PIN.
This costs less than any feature and removes an entire class of “mysterious” resets.

### Recommended order

1. **File and library (0.5–1 day).** `settings_get/set`, lock, atomic
   write, aliases, `cfg_ver`; test `kill -9` and two writers. Without this,
   implementing anything else is pointless.
2. **S0 + S4 + S4.1–S4.3 (2–3 days).** Root, brightness, timeouts, 7 themes,
   accents, textures. This replaces `m-settings`/`m-shsettings` and continues
   the already-drawn `s-root`/`s-display`, so start here.
3. **S5 + S6 + S8 (1.5–2 days).** Sound, key backlight, boot mode,
   power off/reboot. Move the player to the shared file here as well.
4. **S11 + power-off/reboot dialogs (1 day).** “About phone”, confirmation
   text §7.5/§7.9. Settings reset (§7.1) is step 7, when there is something
   to reset.
5. **S1 + S3 + S2 (3–4 days).** Power (USB preset), storage (`df`,
   card eject, cache), network (status + switch). First real data
   from the system rather than a file.
6. **S10 (3 days).** Lab; also the acceptance tool for steps 1–5.
7. **S7 + S9 + settings reset (2 days).** Applications with ＋/✓, access/SSH/backup,
   §7.1.
8. **P2: S2.1, S9.1, search, SD format, update (6–8 days).** Only after
   shell stability, keyboard, and a 24-hour burn-in.

Each step ends with a working screen and a write to the file — not “all screens
first, storage later”.

Placeholders used: `<placeholder>` — phone IP; SSH login/username.
