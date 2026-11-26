# Test and Stability Plan

**Status, 26 Nov 2026:** host QA `home,files,notes --strict` passed; device
smoke, two 30-minute soaks and a **60-minute soak with two tasks** (xterm +
dillo, 61 samples, stable boot_id, PIDs alive, zero new kernel errors, rootfs
63.9 %) passed. PIN, Tasks, shade, EPUB 2/3 reader, photo viewer (including
landscape for wide photos), clock/timer, backlight power policy (touch counts
as activity), file opening and the X cookie were also checked on the P705.
Not tested: poweroff, recovery/fastboot, a 24-hour soak.

This plan covers boot, the interface, external applications, and extended testing of the test LG P705. The thresholds and criteria below describe the plan; they do not claim that tests have passed.

## 1. Objective, Scope, Assumptions

### 1.1. What We Test
- boot and basic services (pmOS, SSH/RNDIS, Wi-Fi, MPD, `lgjack`, SD);
- fb player: 4 screens, queue, themes, settings, keys, lock, power;
- X11/desktop: `session-desktop`, Xorg on fbdev, `fbpan-ng`, openbox/tint2,
  applications, long session, no D states or reboots;
- graphite shell — per criteria in `<shell-doc-dir>/OS-DEV-HANDOFF.md` §9 and
  `SCREENS.md` (marked `N` — not implemented yet; tests will become acceptance tests);
- network, SD, audio/headphones, power, memory, temperature/current;
- firmware, backup, rollback, and “brick” recognition procedures.

### 1.2. Out of Scope
SIM/calls (`SIM busy`), Bluetooth (`hci_smd`), GPS (`GPSNMEA`), camera, EQ
(frozen) are known blockers, not regressions. Check them only for “no worsening”
(dmesg/processes); do not include them in acceptance.

### 1.3. Fixed Test Setup (recheck `smoke.sh`)
| | |
|---|---|
| Device | LG Optimus L7 P705 (u0), MSM7227A, 1 core 1 GHz, 400 MB; stock `p14` build.prop may report P700 |
| Kernel | `3.4.0-CM #23-postmarketOS`, `msm_fb` patches 0001/0002 |
| Display | 480×800, RGB565 16 bpp, `/dev/fb0`, panel rotated (180°) |
| Boot | `/dev/mmcblk0p9`; current `artifacts/boot-rot.img` (MD5 `F5C3D546D214E2F5CA3857F7C0CB04E8`, 8 093 696 bytes) |
| Rollback | `artifacts/boot-rot-KNOWN-GOOD-r14.img` (MD5 `73730BFD117B1EA1BA7CA4FF07FF2100`, 8 138 752 bytes) |
| SD | `/dev/mmcblk1p1`, vfat, UUID `<sd-uuid>`, `/media/sd` |
| Network | RNDIS `<RNDIS-IP>` (management), Wi-Fi `<device-ip>` (test subject) |
| Access | `user`/`<device-password>`, sudo `<device-password>` |
| Mode | `/etc/lgtest-mode` = `player`; alternative `desktop`; `shell` — planned |

**Discrepancy to resolve before acceptance:** `notes/SYSTEM-STATUS.md`
identifies kernel `#22`, while `DESKTOP-STATUS.md` says patches `#23`. `smoke.sh`
prints `uname -v` and warns if it is not `#23`; for acceptance use
`--expect-kernel 23` (hard FAIL). Results without a recorded `uname -a` are invalid.

### 1.4. Principles
1. **Baseline first, thresholds second.** The absolute values below are initial;
   refine them after the first 30-minute baseline and record them in the report.
2. **Stability means no reboots, user-space D states, fb blocks, or OOM.**
   `loadavg` is unreliable on this device: RPC clients report 8–20.
3. **One heavy experiment at a time.** Do not combine burn-in, `test-x.sh`,
   flashing, and mode changes.
4. **Mark every change.** `mark.sh "..."` plus timestamp in the report.
5. **Artifacts matter more than claims.** PASS without `summary.txt`/logs does not count.

---

## 2. Test Matrix

Notation:
- **P** — priority: P0 blocks acceptance, P1 important, P2 desirable.
- **How**: `A` — script, `S` — script + manual check, `M` — manual,
  `N` — not implemented yet (test for the future shell).

### 2.1. Boot and Basic State
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| B-01 | Cold boot from poweroff | SSH responds ≤ 90 s, player plays, screen is not black | S | P0 |
| B-02 | `reboot -f` | same; `last_kmsg` has no panic/oops | S | P0 |
| B-03 | Time to SSH | ≤ 90 s (baseline); > 180 s — FAIL | A | P0 |
| B-04 | Services | `mpd`, `lgjack`, `dropbear`, `wpa_supplicant`, player; no duplicates | A | P0 |
| B-05 | Mode | `/etc/lgtest-mode` matches the actual session; X and player are not running together | A | P0 |
| B-06 | Display/backlight | 480×800, 16 bpp, stride 960, backlight > 0, correct orientation | A | P0 |
| B-07 | dmesg | no panic/oops/BUG/OOM/EXT4/MMC; display storm < 10 | A | P0 |
| B-08 | Previous boot | `last_kmsg` has no `PC is at`/`Call trace`; “No errors detected” is normal, but not proof | A | P0 |
| B-09 | Boot without SD | system and player are alive, MPD has no library, Wi-Fi works | S | P1 |
| B-10 | USB-only power, no battery | 15 min under X without reboot (supervised, 3 repeats) | M | P0 |
| B-11 | Boot loop | 3 consecutive reboots → stop, collect `last_kmsg`, do not loop | M | P0 |
| B-12 | Time/RTC | year 2025–2027; after reboot time has not drifted by more than a minute | A | P1 |
| B-13 | SSH over RNDIS and Wi-Fi | both addresses respond; Wi-Fi session does not drop during idle | S | P1 |
| B-14 | Unclean shutdown | after `poweroff -f`/power removal FS mounts, no errors | S | P0 |

### 2.2. Player: Screens and Controls
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| P-01 | “Now Playing” by default | cover/placeholder, title, transport, progress, VOL | S | P0 |
| P-02 | Cover from tags | `albumart` or `cover.jpg` is displayed, without artifacts/stretching | M | P1 |
| P-03 | No cover | ♪ placeholder + `NO COVER`, layout stays intact | S | P1 |
| P-04 | Long titles | truncated with “...”, does not overflow fields | M | P1 |
| P-05 | UTF-8 (Cyrillic) | correct glyphs, no “squares” | M | P1 |
| P-06 | SeekDrag | dragging slider changes position; release applies it | M | P0 |
| P-07 | prev/play/next | pause/resume, skip, press highlight | M | P0 |
| P-08 | SHUF | on/off, state in MPD, active outline | S | P1 |
| P-09 | REP | on/off, queue does not stop at the end | S | P1 |
| P-10 | MPD unavailable | “MPD unavailable”, progress hidden, app does not crash | S | P0 |
| P-11 | Now → Queue → back | list opens at current item, returns to Now | S | P0 |
| P-12 | Now → Info → Settings → back | gear opens settings, back returns | S | P0 |
| P-13 | HOME from any screen | returns to Now, lock stays engaged | M | P0 |
| P-14 | Rapid taps (10/s) | no freezes, duplicate actions, or “stuck” presses | M | P1 |
| P-15 | Track end | skip/repeat, screen updates, cover changes | S | P1 |
| P-16 | Queue of 100+ | opening/scrolling without stutter, memory does not grow indefinitely | S | P1 |
| P-17 | Info: incomplete tags | `—`/empty fields, path wraps to 3 lines | M | P1 |
| P-18 | Lock: current track | title/artist, clock/date; no clipping off-screen | S | P1 |
| P-19 | Change theme live | all elements recolored, no remnants of old palette | S | P1 |
| P-20 | Screenshot diffs | all 4 screens + lock + confirm within threshold | A | P1 |

### 2.3. Queue
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| Q-01 | Open at current item | current row is visible and highlighted | S | P0 |
| Q-02 | Tap a row | `play N`, return to Now | S | P0 |
| Q-03 | Swipe scrolling | smooth, does not scroll under header | M | P1 |
| Q-04 | Scrollbar | 2 px, matches position | M | P2 |
| Q-05 | Current marker | triangle/background/color only on current item | S | P1 |
| Q-06 | One track | list is correct, next/prev do not break | S | P1 |
| Q-07 | 100+ tracks | scroll and open < 1 s, no OOM | S | P1 |
| Q-08 | Unicode/long names | truncation, no overlap with time | M | P1 |
| Q-09 | MPD track changes while queue is open | marker moves, list does not jump | M | P1 |
| Q-10 | Tap current row | repeated play does not break state | S | P2 |

### 2.4. Themes, Settings, Mode
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| S-01 | GRAPHITE | background/accent/text follow tokens on all screens | S | P0 |
| S-02 | AMBER | same | S | P1 |
| S-03 | FROST | same | S | P1 |
| S-04 | Theme persistence | same theme after reboot (`/etc/lgtest-settings`) | S | P0 |
| S-05 | Invalid theme value | revert to graphite, no crash | S | P1 |
| S-06 | BUTTON LIGHT AUTO | turns off ~5 s after input | M | P1 |
| S-07 | BUTTON LIGHT ON/OFF | always/never; always 0 while locked | M | P1 |
| S-08 | btnlight persistence | unchanged after reboot | S | P1 |
| S-09 | LOCK NOW | lock screen appears immediately | M | P0 |
| S-10 | POWER OFF: cancel | dialog closes, playback continues | M | P0 |
| S-11 | POWER OFF: confirm | `sync` + shutdown, next boot is clean | M | P0 |
| S-12 | Mode switch | writes `/etc/lgtest-mode`, takes effect after reboot | S | P0 |
| S-13 | Settings file | valid `theme=`/`btnlight=`, no junk | A | P1 |

### 2.5. Keys, Lock, Power
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| K-01 | Short Vol± | step ±5 %, no missed presses | M | P0 |
| K-02 | Hold Vol± ≥0.7 s | next/previous exactly once | M | P0 |
| K-03 | Short Power (screen on) | screen turns off, audio continues | M | P0 |
| K-04 | Short Power (screen off) | lock screen, clock/track | M | P0 |
| K-05 | Tap on lock screen | does not unlock | M | P0 |
| K-06 | 60 px swipe | does not unlock, handle returns | M | P0 |
| K-07 | ≥120 px swipe | “release”, unlocks on release | M | P0 |
| K-08 | Lock auto-off | after ~15 s, backlight 0 | M | P1 |
| K-09 | Power ≥1.5 s | CANCEL/POWER OFF dialog | M | P0 |
| K-10 | HOME on lock screen | does not unlock (or documented behavior) | M | P1 |
| K-11 | Vol+Power simultaneously | no stuck state/reboot | M | P1 |
| K-12 | Keys after reboot | all work without reinitialization | M | P0 |
| K-13 | Backlight while locked | always 0, regardless of mode | M | P1 |
| K-14 | Hold threshold 0.6/0.7 s | 0.6 — volume, 0.7 — track (tolerance) | M | P2 |

### 2.6. X11 / Desktop
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| X-01 | `session-desktop` | X ready ≤ 30 s, log has no fatal errors | A | P0 |
| X-02 | X mode | 480×800, 16 bpp, `DPMS is Disabled` | A | P0 |
| X-03 | `fbpan-ng` | exactly one panner, screen updates | A | P0 |
| X-04 | openbox/tint2 | running, panel does not cover content | A | P0 |
| X-05 | xterm | 58 columns, mono 10, not clipped | S | P1 |
| X-06 | 10 min idle | no D, reboot, or black screen | A | P0 |
| X-07 | 60 s stress (screensaver/DPMS) | survives, X is not in D state | A | P0 |
| X-08 | X screenshot | `xwd` capture succeeds, desktop is rendered | A | P1 |
| X-09 | Launch/close applications | xterm/pcmanfm/dillo, windows 480×712 | S | P1 |
| X-10 | Focus/keyboard | input goes to active window, header/dock do not steal it | M | P1 |
| X-11 | Conflict with player | X and `lp7player` do not run simultaneously | A | P0 |
| X-12 | X without battery (USB) | 15 min without reboot, 3 repeats, supervised | M | P0 |
| X-13 | Fallback on X failure | fb player starts after 20 s, mode file unchanged | N | P1 |
| X-14 | `test-x.sh` version on phone | `timeout 3 xdpyinfo` is present (old bug has not returned) | A | P0 |

### 2.7. Graphite Shell (planned; acceptance per `OS-DEV-HANDOFF.md` §9)
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| H-01 | Launcher | 6 tiles 208×196, now-playing 60, dock 48, all within 800 | N | P0 |
| H-02 | Player in shell | 4 screens, cover 384, transport 96, hardware volume | N | P0 |
| H-03 | App header/dock | override-redirect on top, content 480×712 | N | P0 |
| H-04 | Home/Back | Home → launcher, Back → close/back, task remains in stack | N | P0 |
| H-05 | Terminal | 58 columns, mono 10, no clipping | N | P1 |
| H-06 | Notes | 4×42 keyboard, RU/EN, SHIFT/DEL/SPACE, persistence | N | P1 |
| H-07 | Files/browser | pcmanfm/dillo at 480×712, header/dock stay in place | N | P1 |
| H-08 | Shell settings | display/network/audio/keys/memory/power, mode+reboot | N | P1 |
| H-09 | Watchdog | shell crash → restart ≤3, then fb player | N | P0 |
| H-10 | Memory | X+shell+app ≤ 60 MB, no OOM | N | P0 |
| H-11 | evdev | shell and X do not compete for events, app receives input | N | P0 |
| H-12 | 10-minute session | no D/reboot/freeze | N | P0 |

### 2.8. Shade (when available)
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| SH-01 | Open | swipe from y<32, dy>80; does not open from content | M | P0 |
| SH-02 | Close | swipe up >80 or tap dimmed area | M | P0 |
| SH-03 | Tiles | Wi-Fi/BT/audio/brightness/keys/player/lock/power | M | P0 |
| SH-04 | Sliders | volume → MPD, brightness → `/sys/class/leds/lcd-backlight` | M | P1 |
| SH-05 | Notifications | MPD card opens player, Wi-Fi opens settings | M | P1 |
| SH-06 | On every screen | opens over launcher/player/app | M | P1 |
| SH-07 | After closing | content beneath shade is not redrawn with artifacts | M | P1 |
| SH-08 | Lock/power from shade | standard handlers, no double activation | M | P0 |

### 2.9. Applications, Files, Browser, Terminal, Notes
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| A-01 | Terminal | launches < 2 s, 58 columns, input/output | S | P1 |
| A-02 | On-screen keyboard | RU/EN, position does not cover the command line | M | P1 |
| A-03 | Files | pcmanfm at 480×712, navigation, open file | S | P1 |
| A-04 | Browser | dillo/netsurf: local page and internet | S | P1 |
| A-05 | One heavy app | do not keep browser+files open simultaneously (400 MB) | M | P0 |
| A-06 | Close | no zombies, memory is returned | A | P1 |
| A-07 | Home from app | app is hidden, launcher appears, app can be returned to | N | P0 |
| A-08 | Notes | input, save in `<user-notes-dir>/`, still present after reboot | N | P1 |
| A-09 | Keyboard does not lose focus | switch RU/EN, Shift, Backspace | N | P1 |
| A-10 | App left open for 30 min | RSS stable, no OOM | S | P1 |

### 2.10. Network
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| N-01 | Association after boot | WPA2 COMPLETED, DHCP, default route | A | P0 |
| N-02 | DNS | `ya.ru` resolves | A | P1 |
| N-03 | Connectivity | ping gateway, loss < 2 %, stable RTT | A | P1 |
| N-04 | SSH over Wi-Fi | login and command < 1 s (normal) | A | P1 |
| N-05 | 1 h idle | flap count, drops, recovery | A | P0 |
| N-06 | 1 h playback | local MPD, network does not affect audio | A | P1 |
| N-07 | Restart `wpa_supplicant` | IP returns ≤ 60 s, MPD does not crash | S | P0 |
| N-08 | RNDIS + Wi-Fi simultaneously | both addresses, no routing conflict | S | P1 |
| N-09 | MPD loopback-only | `127.0.0.1:6600`, not `0.0.0.0` | A | P0 |
| N-10 | After reboot | network comes up automatically, no manual commands | A | P0 |
| N-11 | Weak signal | reconnects without reboot or D state | S | P1 |
| N-12 | Boot without Wi-Fi | player/lock/keys work, MPD is local | S | P1 |

### 2.11. SD and Storage
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| D-01 | Mount | `/media/sd`, vfat, UUID `<sd-uuid>` | A | P0 |
| D-02 | Music | files visible, MPD scans | A | P0 |
| D-03 | Read | 1 MiB without errors; speed ≥ 50 % of baseline | A | P1 |
| D-04 | Write | small file can be created/deleted (QA directory) | S | P1 |
| D-05 | Library update | new file appears in MPD | S | P1 |
| D-06 | Remove/reinsert | if slot is accessible: no panic, remount succeeds | S | P0 |
| D-07 | Boot without SD | system alive, MPD has no library, no errors | S | P1 |
| D-08 | MMC dmesg | no `mmc1: error`, `I/O error`, `EXT4-fs error` | A | P0 |
| D-09 | Low space | < 500 MB — WARN, writes still work | S | P2 |
| D-10 | fsck | only on unmounted SD, not while live | M | P2 |

### 2.12. Audio and Headphones
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| AU-01 | Speaker | clean audio, channels not swapped | M | P0 |
| AU-02 | Headphones (h2w=1) | route `rx=5`, microphone not tested | S | P0 |
| AU-03 | Headphones without microphone (h2w=2) | `rx=5`, speaker silent | S | P0 |
| AU-04 | Switching | ≤ 1 s, no click/dropout | M | P0 |
| AU-05 | `lgjack` | process alive, log changes on insert/remove | A | P0 |
| AU-06 | Volume | 5 % steps, zero and maximum without clipping | M | P1 |
| AU-07 | Pause/resume | no click, PCM does not XRUN | S | P1 |
| AU-08 | Track change/seek | no underrun, progress is correct | A | P1 |
| AU-09 | Underrun | 0 per run; `state: XRUN` — FAIL | A | P0 |
| AU-10 | Hot insert/remove | route changes, playback is not interrupted | M | P0 |
| AU-11 | Boot with headphones | correct route immediately, no “through speaker” | S | P1 |
| AU-12 | Maximum test volume | no higher than 40 % (20 % on headphones), no noise | S | P1 |
| AU-13 | `hw_params` | 44.1/48 kHz, stereo, S16 | A | P1 |
| AU-14 | No MPD | PCM closed/no XRUN, player shows “MPD unavailable” | S | P1 |

### 2.13. Stability and Performance
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| ST-01 | 30 min idle | no reboot, D_BAD=0, memory stable | A | P0 |
| ST-02 | 24 h soak | 0 unexpected reboots, 0 FAIL | A | P0 |
| ST-03 | Memory leak | player RSS ±2 MB/h after warm-up; growth >10 MB/h — FAIL | A | P0 |
| ST-04 | CPU | idle average < 20 %; > 90 % for three samples — FAIL | A | P1 |
| ST-05 | D states | user-space D=0; known RPCs are baseline | A | P0 |
| ST-06 | Reboot | `boot_id` changes only on command; otherwise FAIL | A | P0 |
| ST-07 | fb | no read/pan timeouts | A | P0 |
| ST-08 | SSH | p95 < 1.5 s; > 5 s — WARN; > 30 s/hang — FAIL | A | P1 |
| ST-09 | Clock | does not drift; metrics use uptime | A | P1 |
| ST-10 | Temperature/current | battery < 45 °C; > 50 °C — stop | A | P0 |
| ST-11 | OOM | 0 messages; `MemFree` < 10 MB — FAIL | A | P0 |
| ST-12 | Zombies | 0 | A | P2 |

### 2.14. Recovery and Flashing
| ID | Check | Criterion | How | P |
|---|---|---|---|---|
| R-01 | p9 backup | 8 MB image captured, MD5 matches | A | P0 |
| R-02 | `/etc`, `/usr/local/bin` backup | tar with `--no-same-owner`, readable | A | P0 |
| R-03 | Backup verification | MD5 on host and phone matches | A | P0 |
| R-04 | Fastboot flashing | old 157 KB fastboot, `OKAY` on flash and reboot | M | P0 |
| R-05 | Flash via dd | `conv=fsync`, read-back matches | M | P1 |
| R-06 | Post-flash smoke | `uname` #23, smoke PASS, display/audio/network | A | P0 |
| R-07 | Rollback on live system | known image → reboot → #15/#23, smoke PASS | M | P0 |
| R-08 | Rollback via fastboot | device visible, flash OK | M | P0 |
| R-09 | “Brick” | triage using table §8.5, no panic | M | P0 |
| R-10 | Driver/fastboot | VID/PID `18D1:D00D`, not VBoxUSB | M | P0 |
| R-11 | Known image | file and MD5 present before starting | A | P0 |
| R-12 | Low charge | flash only when battery > 50 % | M | P0 |

---

## 3. Automation

### 3.1. Architecture
```
Windows (.)
  <qa-dir>/host/qa.py  ── SSH (RNDIS <RNDIS-IP>) ──┐
  <qa-dir>/host/qadiff.py                            │
                                                     ▼
                                      <device-qa-dir>/{smoke,burnin,collect,probe,latency}.sh
                                                     │
                                      /var/log/qa/  (artifacts)
                                                     │
                                      tar.gz / SFTP ─┘ → artifacts/qa/
```
- **Management over RNDIS only.** Wi-Fi may flap; the watchdog must not
  depend on the test subject.
- All scripts reside on the phone; the host does not pass complex quoted commands
  (PowerShell pitfall: `"a|b"` turns into a pipe).
- `qa.py` runs `sudo -S` non-interactively, using the password from `LGP700_PASS`.

### 3.2. What Is Already Automated
Do not duplicate existing X scripts: `<desktop-test-dir>/test-x.sh` (modifies
the device), `collect-x-state.sh` (read-only), and `restore-player.sh` remain
the primary X-test tools; `qa.py` adds a watchdog, metrics, and artifact collection.

| Check | Location |
|---|---|
| Kernel/cmdline/mode/settings | `smoke.sh` |
| Services, ports (22, 6600 loopback) | `smoke.sh` |
| fb sysfs, X, `fbpan-ng`, DPMS | `smoke.sh` |
| MPD status + position advance over 2 s | `smoke.sh` |
| ALSA state/hw_params, XRUN | `smoke.sh`, `burnin.sh` |
| `lgjack`, `h2w`, last route | `smoke.sh`, `burnin.sh` |
| Wi-Fi/DHCP/DNS/ping | `smoke.sh` |
| SD mount/UUID/read | `smoke.sh`, `collect.sh --full` |
| Memory/CPU/loadavg/RSS | `smoke.sh`, `burnin.sh`, `probe.sh` |
| D states + stacks | `smoke.sh`, `burnin.sh`, `probe.sh` |
| dmesg by category (crit/OOM/FS/display/audio/net/smd) | `lib.sh` |
| `last_kmsg` | `smoke.sh`, `collect.sh`, `boot-capture.start` |
| Screenshots + diffs | `qa.py shot`, `qadiff.py` |
| Reboot via `boot_id`, downtime | `qa.py watch` |
| 24 h soak + UI screens | `burnin.sh` |
| Responsiveness/start/fps proxy | `latency.sh` |

### 3.3. How to Run (non-interactively)
```sh
python <qa-dir>/host/qa.py stage
python <qa-dir>/host/qa.py smoke --shot
python <qa-dir>/host/qa.py smoke --strict --require-playing
python <qa-dir>/host/qa.py collect --label after-x --shot --full
python <qa-dir>/host/qa.py burnin start --hours 24 --label rc1 --watch
python <qa-dir>/host/qa.py burnin status
python <qa-dir>/host/qa.py burnin stop
python <qa-dir>/host/qa.py watch --hours 1 --interval 30
python <qa-dir>/host/qa.py shot --name now --baseline <qa-dir>/baselines/now.png
```
Direct run (if host is unavailable):
```sh
setsid sh <device-qa-dir>/burnin.sh --hours 24 >/var/log/burnin.out 2>&1 </dev/null &
```

### 3.4. Key Checks and How They Work
**Ports.** Parse `/proc/net/tcp` (hex): `0100007F:19C8` — MPD on loopback,
`00000000:19C8` — FAIL (externally exposed). `:0016` — SSH.

**D states.** Iterate `/proc/[0-9]*/stat`, state `D`; classification:
- `known` — `krmt_storagecln`, `krtcclntd`, `kbatteryclntd`, `khsclntd`,
  `kadaptiveup`, `klulzactiveup` (RPC/governors, baseline; `comm` may be
  truncated to `krmt_storagecl` — both variants are accounted for);
- `user` — `lp7player`, `mpd`, `lgjack`, `Xorg`, `fbpan-ng`, `dropbear` →
  immediate FAIL + stack;
- `kernel` — any other D: 3 consecutive samples → FAIL.
`loadavg` is logged only; it is not a criterion.

**dmesg.** Categories (case-insensitive):
`panic|Oops|kernel BUG|Unable to handle kernel...`; OOM; EXT4/MMC; display
(`mdp_disable_irq_nosync`, `multiple power-off request`, `chip_lcd_off`,
`lcd_on START`, `esd_int`); audio (`underrun|xrun`); WLAN; SMD/RPC.
Counters are compared with the startup baseline; a decrease means the ring buffer
was overwritten, not that the issue “recovered”.

**Screenshots.** `dd if=/dev/fb0 of=... bs=960 count=800` = 768 000 bytes,
RGB565, rotated 180°. All reads run in the background with `timeout 8–10`; the
first timeout = `FB_BLOCKED`, capture stops. `qadiff.py` computes max-channel
delta, masks, and bbox; presets `player`, `lock`, `shade`, `queue`.
Baseline must come from the device, not design renders.

**Responsiveness.** `latency.sh`:
- pause → stable frame → `mpc next` → poll frame every 300 ms;
  this is end-to-end “MPD changed state → pixels on screen”;
- player startup: kill → launch `settings` → time to pid and to frame change;
- X: launch `xterm` → window time; count frame changes for 6 s (fps proxy).
Exact **tap** latency cannot be measured this way (see §3.6).

**Watchdog.** `qa.py watch` every 30 s:
- `probe.sh` (uptime, boot_id, pid/RSS, MPD, memory, D, dmesg, network, battery);
- tracks `boot_id`: change → `REBOOT`, records downtime, fetches
  `collect.sh --shot` and `last_kmsg`;
- TCP:22 open but command hangs → `SSH_HANG`; TCP closed → `DOWN`;
- `D_BAD`, `DM_CRIT`, memory < 20 MB → separate events;
- JSONL + p50/p95/max SSH latency summary.

### 3.5. Limitations and Workarounds
1. **The player reads fixed `event0/1/5`.** uinput creates a new node — the
   player will not see it. Therefore tap/swipe/lock/jack are automated only via:
   - `mpc` (track change/pause/volume/seek) — checks UI response;
   - launch player with an argument (`queue/info/settings/lock/confirm`) —
     checks screen rendering;
   - manual checklist (§3.6).
   For exact tap latency, use a QA build of `lp7player` with
   `clock_gettime` on input/pan (temporary binary in `/tmp`, not in firmware)
   or record with a 120–240 fps camera.
2. **fb reads are hazardous.** No `dd` loops without timeout; on `FB_BLOCKED`,
   do not retry; run `collect.sh` and reboot.
3. **No `MemAvailable`** — an estimate is used; report it as
   `avail_est`, not `MemFree`.
4. **`mpc seek %` depends on the mpc version** — the script has a `+10` fallback.
5. **Time.** RTC may drift; intervals use `/proc/uptime`,
   CSV includes both epoch and uptime.

### 3.6. Manual Checklist (required; scripts do not replace it)
Perform before and after the 24-hour run, 10–15 minutes:
- [ ] tap cover/bar/transport — highlight and action;
- [ ] swipe seek, release, return;
- [ ] Vol± short/hold, Power short/long, HOME;
- [ ] lock: tap, 60 swipe, 120 swipe, 15 s auto-off;
- [ ] shutdown dialog: cancel and confirm (then boot);
- [ ] insert/remove headphones 3 times during playback;
- [ ] change theme back and forth, verify after reboot;
- [ ] open queue at current item, scroll, tap a row;
- [ ] check Info with a track without tags and with a long path;
- [ ] (X) xterm input, pcmanfm, dillo, Home/focus;
- [ ] (X) 10-minute session, then `collect.sh --label after-manual`.

---

## 4. 24-hour burn-in

### 4.1. Goal and hypotheses
Verify that the player and system can withstand a day of continuous playback
and periodic UI scenarios without:
- unexpected reboots (including without the battery under X — a separate supervised test);
- user-space D states and fb lockups;
- memory leaks/OOM at 400 MB;
- audio degradation (XRUN), network issues (flaps), or SD errors.

### 4.2. Prerequisites
- `smoke.sh --strict --require-playing` PASS; `collect.sh --label pre` captured;
- battery > 60%, charger connected (USB 5 V, preferably 1–2 A);
- SD with music, MPD queue ≥ 8 tracks (otherwise WARN); repeat is enabled
  by the script and restored at the end;
- control over RNDIS verified; Wi-Fi is under test;
- no running `test-x.sh`, flashing, or mode change;
- known-good boot image and MD5 recorded (for rollback);
- `boot-capture.start` installed (optional, but strongly recommended).

### 4.3. Test schedule
| Time | Action |
|---|---|
| 0:00–0:10 | warm-up: play, volume 40 (20 with headphones), baseline dmesg/D/memory |
| every 60 s | sample: uptime, boot_id, CPU, memory, RSS, MPD, ALSA, h2w, Wi-Fi, ping, D, dmesg, battery |
| every 10 min | playback cycle: next / volume / seek 30% / pause 3 s / seek 70% / mid volume |
| every 2 h | screen sweep: `queue` → `info` → `settings` → `lock` → `confirm`, 60 s each, capture a frame on each |
| every 2 h (X) | app sweep: xterm → pcmanfm → dillo for 12 s each, one at a time, check window/memory/D |
| 12:00 (manual) | `mark.sh "jack in"` → plug in headphones → 2 min → `mark.sh "jack out"` → unplug |
| 23:50–24:00 | final smoke + `collect.sh --label post` + manual checklist |
| all the time | host `watch` every 30 s, over RNDIS |

`burnin.sh` itself:
- restarts the player if it dies (counter, maximum 12);
- restarts MPD/lgjack no more than once every 5 minutes;
- restores `next`/pause if the queue stalls;
- after 3 Wi-Fi-down samples — reassociates + udhcpc, counts this as FAIL;
- stops on user-space D, temp ≥ 50 °C, memory < 12 MB,
  Xorg/fbpan down, or > MAX_RESTARTS.

### 4.4. Failure criteria
| Event | Threshold |
|---|---|
| Unexpected reboot | 1 is already FAIL (collect `last_kmsg`) |
| Panic/oops/BUG/OOM/EXT4/MMC error | any new occurrence |
| User-space D (`lp7player/mpd/lgjack/Xorg/fbpan`) | any |
| `FB_BLOCKED` (frame read timeout) | any |
| ALSA `XRUN` | any |
| Memory `avail_est` | < 20 MB FAIL, < 50 MB WARN |
| Player RSS | growth > 10 MB/h after warm-up |
| CPU | ≥ 90% for three consecutive samples |
| Battery temperature | ≥ 50 °C — stop |
| Wi-Fi | 100% loss for 3 samples, no recovery within 10 min |
| MPD | down/not playing for 3 samples, restart did not help |
| SD | read/mount error |
| Screen sweep | player died/did not return, or frame not captured |

### 4.5. X session: separate and supervised
Do not run X for 24 h: a reboot under X without the battery is known, and an
fb deadlock requires a reboot. Instead:
1. `test-x.sh --seconds 600 --xtest` — 10 min;
2. `test-x.sh --stress --seconds 60` — screensaver/DPMS;
3. live `session-desktop` for 30 min with `watch`;
4. battery removed, USB only — 15 min × 3, collect `last_kmsg` after each;
5. criterion: 0 reboots, 0 D, screen refreshes; otherwise investigate kernel/power,
   not “one more run.”

### 4.6. Burn-in limitations
- Touch/keys/jack are not operated by the script; UI scenarios switch screens
  through argv + `mpc`, not real gestures.
- The script cannot see its own reboot (the process dies); a reboot is recorded
  only by host `watch` + `boot-capture.start`.
- `burnin.sh` changes volume/track/repeat; it restores the state at the end,
  but not the exact track position.
- UI sweep restarts the player (5 screens every 2 h), so a clean RSS trend for
  one process cannot be built from the 24-hour CSV. For a leak, run separately
  with `--no-ui --hours 8` and compare RSS.
- Do not run at the same time as flashing or `test-x.sh`.

---

## 5. Crash and artifact collection

### 5.1. What to collect and from where
| Source | Path | When |
|---|---|---|
| Kernel (current boot) | `dmesg` | at any time |
| Kernel (previous boot) | `/proc/last_kmsg` | **immediately after reboot**, before the next one |
| pstore | `/sys/fs/pstore/*` | if present |
| Xorg | `/var/log/Xorg.0.log` | after X |
| Player | `/var/log/lp7player.log`, `/var/log/session-player.log` | after startup |
| MPD | `/var/log/mpd/mpd.log` | always |
| Headphones | `/var/log/lgjack.log` | always |
| X test | `/var/log/test-x.log` | after `test-x.sh` |
| Services | `rc-status -a`, `/var/log/messages`, `/var/log/logbookd/*`, `journalctl -b` (if present) | during investigation |
| State | `/proc/*`, `/sys/class/graphics/fb0/*`, power_supply, switch, asound | when hung |
| Frame | `/dev/fb0` (768000 B) | guarded only, one at a time |

`collect.sh` packs everything into tar.gz and **redacts** `credential/password/secret`;
`/etc/shadow` and SSH keys are not copied.

### 5.2. Procedure after reboot
1. Do not reboot again unless necessary.
2. Wait for SSH (`qa.py watch` waits itself, or use `wait_boot`).
3. Immediately: `collect.sh --label reboot --shot` (or `qa.py collect`).
4. Extract `last_kmsg` separately, even if tar collection failed:
   `cat /proc/last_kmsg > /tmp/last_kmsg.txt`.
5. Check `last_kmsg` for:
   - `PC is at`, `LR is at`, `Call trace`, `Unable to handle` → oops/panic;
   - `No errors detected` + empty → power/PMIC/watchdog reset, not panic;
   - storm of `mipi_lg4573b_chip_lcd_off`/`lcd_on` + `mdp_disable_irq_nosync`
     → display/ESD/power (known X-without-battery scenario);
   - `Out of memory`, `mmc1: error`, `EXT4-fs error` → corresponding branch.
6. Correlate the time with `events.log` (`mark.sh`) and `watch.jsonl` (downtime).
7. Save: `crash-YYYYmmdd-HHMM-<bootid8>/` + `last_kmsg`, `dmesg`,
   `Xorg.0.log`, `events.log`, `watch.jsonl`, screenshot, note.
8. Only after saving, decide on rollback/reflashing.

### 5.3. If SSH is dead
- ping RNDIS `<RNDIS-IP>` and Wi-Fi `<device-ip>`;
- if ping works but TCP:22 does not — userspace is hung (as with `hci_smd`): reflash boot
  through fastboot; do not touch rootfs;
- if there is no USB at all — check Device Manager: `18D1:D00D` (fastboot),
  RNDIS, or `QHSUSB_DLOAD` (emergency mode — stop, see §8.5);
- photograph the screen: this is the panel's only “log.”

### 5.4. Storage
- On the phone `/var/log/qa`; if SD is present, mirror to `/media/sd/qa`
  (do not rely on it: SD is under test).
- On the host `artifacts/qa/` (gitignore); keep the 3 latest runs.
- In git, text-only findings with no secrets or large logs.
- For each run: `meta.txt` (kernel, boot_id, mode, image, MD5),
  `summary`, `events`, `fails`, raw CSVs, screenshots.

---

## 6. Metrics and thresholds

### 6.1. Table
| Metric | How measured | Baseline | WARN | FAIL |
|---|---|---|---|---|
| Reboot | `boot_id` in `probe.sh`/watch | 0 | — | ≥1 unexpected |
| Time to SSH | host, from power-on | ≤ 90 s | > 120 s | > 180 s / none |
| Player startup (pid) | `latency.sh` | ≤ 1.5 s | > 2.5 s | > 5 s |
| Player first frame | `latency.sh` | ≤ 2.5 s | > 4 s | > 8 s / none |
| Response to `mpc next` | `latency.sh` | p95 ≤ 1.2 s | > 1.5 s | > 2 s / no frame |
| `mpc status` | `smoke`/`burnin` | p95 ≤ 200 ms | > 500 ms | > 1 s |
| SSH command | `watch` | p95 ≤ 1.5 s | > 3 s | > 30 s / hung |
| CPU busy (idle) | `/proc/stat` | ≤ 20% | > 50% | ≥ 90% ×3 |
| Loadavg | `/proc/loadavg` | 8–20 (RPC) | > 25 | not a criterion |
| User-space D | `/proc/*/stat` | 0 | — | ≥1 |
| Known RPC D | same | 6–12 | > 20 | not a criterion |
| `MemFree` | `/proc/meminfo` | > 40 MB | < 25 MB | < 10 MB |
| `avail_est` | Free+Buffers+Cached | > 80 MB | < 50 MB | < 20 MB |
| Player RSS | `/proc/<pid>/statm` | ±2 MB/h | +5 MB/h | +10 MB/h |
| OOM | dmesg | 0 | — | ≥1 |
| Battery temperature | `power_supply/battery/temp` | 25–40 °C | ≥ 45 °C | ≥ 50 °C (stop) |
| Current/voltage | `current_now`/`voltage_now` | log | — | drop relative to baseline* |
| ALSA XRUN | `pcm0p/sub0/status`, dmesg | 0 | — | ≥1 |
| Wi-Fi loss | gateway ping | < 2% | > 20% | 100% ×3 |
| Wi-Fi flaps | `wpa_state`/operstate | 0 | > 2/h | no network for 10 min |
| SD read | `dd` 1 MiB | baseline | < 50% | error |
| X duration | session | 30 min | — | reboot/D/black screen |
| X frames | change counter | ≥ 5/s with text | < 3/s | 0/s |
| Screenshot diff | `qadiff.py` | 0 | > 0.3% | > 0.5% (masked) |
| Zombies | `ps stat` | 0 | ≥1 | — |

\* Voltage/current may not be available; with the battery removed there is no metric —
then use only reboots/`last_kmsg` and an external USB tester.

### 6.2. How to obtain a baseline
1. 30 min in working mode, with no experiments.
2. `burnin.sh --minutes 30 --no-ui --label baseline` → CSV.
3. `latency.sh --iters 10` → p50/p95.
4. `smoke.sh --shot`, 3 frames of each screen → `qadiff` (0).
5. Record in the report: kernel, boot_id, temperature, average CPU, RSS, `mpc_ms`,
   ping loss, SD speed. All subsequent thresholds are relative to these values.

### 6.3. FPS and tap latency — accurately
- The player redraws at ~1 Hz (state poll) and on events; it has no animation-style
  “fps.” The metric is “time to frame change after `mpc next`.”
- X with `fbpan-ng` is 10 Hz, but the panner is change-driven: 0 frames when idle.
  The metric is frame changes with deliberately changing text (`xterm`),
  expecting ≥ 5/s.
- **Tap** latency is measured only with an instrumented build or a camera;
  uinput does not work for the player (fixed event nodes).

---

## 7. Acceptance criteria

### 7.1. Gate 0 — test setup ready
- [ ] `uname -a` recorded (`#23`), `cmdline` is correct.
- [ ] `smoke.sh --strict` PASS (WARNs investigated).
- [ ] p9 + `/etc` + `/usr/local/bin` backed up; MD5 verified on host.
- [ ] Known-good image and old fastboot available.
- [ ] `qa.py stage` + `status` work non-interactively.

### 7.2. Gate 1 — player (release candidate)
- [ ] 24 h burn-in: 0 unexpected reboots, 0 FAIL, 0 user D, 0 XRUN.
- [ ] Memory: no OOM, RSS within limits, `avail_est` > 20 MB.
- [ ] Manual checklist §3.6 passed (touch/keys/lock/jack/power).
- [ ] All 4 screens + lock + confirm: screenshot diff within threshold.
- [ ] Queue of 100+ and 1 track; themes/settings survive reboot.
- [ ] Wi-Fi: 1 h idle without losing control over RNDIS; recovery
  after restarting `wpa_supplicant`.
- [ ] SD: error-free reading, MPD library present.
- [ ] No new dmesg categories relative to baseline.

### 7.3. Gate 2 — X/desktop
- [ ] 10 min `test-x.sh` + 60 s stress — PASS, X not in D.
- [ ] 30 min live session: 0 reboots, screen refreshes.
- [ ] Without battery: 3 × 15 min supervised, 0 reboots (or documented
  defect with `last_kmsg` and a kernel plan).
- [ ] Applications: terminal 58 columns, files/browser at 480×712.
- [ ] Memory: one heavy application, no OOM.
- [ ] Fallback to fb player on X failure (when implemented).

### 7.4. Gate 3 — graphite shell
Only after the binary exists: all items in §9 of `OS-DEV-HANDOFF.md` +
H-01…H-12 from §2.7, a 10-minute session, watchdog, memory ≤ 60 MB.

### 7.5. Known defects that do not block acceptance
SIM/BT/GPS/camera/EQ are out of scope. If they have not added new dmesg
errors, acceptance is not blocked.

---

## 8. Procedures: flashing, smoke, rollback, “brick”

### 8.1. Before flashing (backup)
```sh
# on the phone, root; SD mounted
mkdir -p /media/sd/backup
dd if=/dev/mmcblk0p9 of=/media/sd/backup/p9-boot-$(date +%Y%m%d-%H%M).img bs=1M
md5sum /media/sd/backup/p9-boot-*.img
tar --no-same-owner -czf /media/sd/backup/etc-$(date +%Y%m%d-%H%M).tgz /etc
tar --no-same-owner -czf /media/sd/backup/bin-$(date +%Y%m%d-%H%M).tgz /usr/local/bin
cat /proc/cmdline > /media/sd/backup/cmdline.txt
uname -a >> /media/sd/backup/cmdline.txt
apk info -v > /media/sd/backup/apk.txt 2>/dev/null
sync
```
- Download the image to the host and **verify MD5** (`qa.py pull`, `get_phone.py`, or
  `scp`). Without verification, the backup does not count.
- Check: battery > 50%, cable/port, old `fastboot.exe` (157 KB),
  driver `android_winusb.inf`, image `boot-rot.img` and its MD5.
- Stop burn-in/`test-x.sh`; wait for the sample to finish.
- Record in the log: date, image, MD5, commands, who/what.

### 8.2. Flashing
Primary method — old fastboot:
```
fastboot devices                 # should show the device
fastboot flash boot boot-rot.img # sending/writing ... OKAY
fastboot reboot
```
- New fastboot hangs at `Fetching super` — do not use it.
- If Windows sees `VBoxUSB`, reinstall the driver (not fastboot).
- Alternative from a live system:
```sh
dd if=/tmp/boot-rot.img of=/dev/mmcblk0p9 bs=1M conv=fsync
sync
# verify by reading (8 MiB)
dd if=/dev/mmcblk0p9 bs=1M count=8 2>/dev/null | md5sum
```
The readback MD5 must match the image MD5 (accounting for partition size; if
the partition is larger than the image, compare the first `len(img)` bytes).
- Do not flash with low charge; do not touch p20/other partitions.

### 8.3. After flashing (smoke)
1. `qa.py wait-boot --timeout 240` (or `wait_boot.py`).
2. `qa.py smoke --shot --strict --require-playing` → PASS.
3. `uname -v` contains `#23`; `cmdline` is unchanged.
4. `dmesg | grep -i 'frame start'` is empty; `grep -c 'MDP IRQ term'` = 0.
5. `test-x.sh --seconds 600 --xtest` (verify the phone has the version with
   `timeout 3 xdpyinfo`), then `test-x.sh --stress --seconds 60`.
6. `burnin.sh --minutes 60 --no-ui` (short run), then the full 24 h.
7. Capture new baseline frames and compare with the reference.

### 8.4. Rollback
- System is alive: `dd` known-good image to p9 → `sync` → `reboot`;
  after boot — `smoke` + `uname`.
- System does not boot, fastboot available: `fastboot flash boot
  boot-rot-KNOWN-GOOD-r14.img` → `fastboot reboot`.
- System does not boot, no fastboot: check driver/port/cable; if the
  device is in `QHSUSB_DLOAD` — stop (see below).
- Do not touch rootfs: kernel rollback is complete; settings/music remain.

### 8.5. “Brick” triage
| Symptom | Diagnosis | Action |
|---|---|---|
| LG logo cycles, SSH available after 60–90 s | boot/rootfs alive, session/service | `collect`, `smoke`, `restore-player`, check mode |
| Black screen, SSH available, no player | session crashed | `session-player`, log; if D — reboot |
| Ping works, TCP:22 does not | userspace hung (as with `hci_smd`) | reflash boot through fastboot; do not touch rootfs |
| No USB, fastboot `18D1:D00D` | bootloader alive | old fastboot, flash boot |
| USB `QHSUSB_DLOAD`/9008 | emergency mode | **stop**, do not erase partitions; QPST/service image required, outside current toolkit |
| Fastboot hangs at `Fetching super` | new fastboot | old 157-KB version, driver, another port |
| Windows `VBoxUSB` | USB captured by VirtualBox | reinstall driver `android_winusb.inf` |
| Device heats up, reboots | power/battery/ESD | USB 5 V 1–2 A only, `last_kmsg`, do not run X without battery |

### 8.6. Maintenance window rules
- One experiment at a time; pause burn-in before flashing.
- Log all commands (date, command, result, artifact).
- Do not change `/etc/lgtest-mode` and do not rebuild initramfs in the same window
  as flashing (verify the kernel separately first).
- After any failure, run `collect.sh` **before** rebooting again.

---

## 9. Risks and mitigations

| Risk | Symptom | Impact | Mitigation | Detector |
|---|---|---|---|---|
| Reboot under X without battery | LCD off/on storm, `No errors detected`, reset | session lost, test incomplete | X supervised only, USB 1–2 A, not 24 h | watch `boot_id`, `last_kmsg` |
| fb deadlock during frame read | `dd` hangs, D in `Xorg`/`fbpan` | reboot only | guarded read, one timeout → stop | `FB_BLOCKED` |
| Wi-Fi flaps | `wpa_state` changes, ping loss | loss of SSH over Wi-Fi | control over RNDIS, watchdog | `watch`, `NET_CHANGE` |
| RPC D-state | loadavg 8–20, 10 s RPC | slow SSH, false alarms | allowlist, monitor user D/CPU | `probe.sh` |
| OOM at 400 MB | `Killed process`, memory drops | app/player dies | one heavy app, 20 MB threshold | dmesg, `avail_est` |
| SD disconnects | `mmc1: error`, mount disappears | no music, MPD crashes | backup, avoid unnecessary writes | `dmesg`, `df` |
| PowerShell mangles quotes | `grep "a&#124;b"` → pipe | wrong commands | scripts on phone, `qa.py` | command review |
| Time drifts | clock/date incorrect | metrics/diffs break | intervals by uptime, clock masks | `date`, CSV |
| Secrets in logs | `credential=` in archive | leak | redact, do not copy shadow/keys | tar review |
| Wrong kernel | `#22` instead of `#23` | invalid results | pin `uname`, smoke FAIL | `--expect-kernel` |
| Script changes state | track/volume/mode | test confusion | snapshot/restore, `mark.sh` | meta diff |
| No thermal sensor | `CONFIG_THERMAL=n` | unnoticed overheating | battery temp + IR thermometer (`measure_ir.sh`) | `temp` |
| Rapid wear/noise | 24 h at maximum | speaker/hearing | 40% / 20% on headphones | audio check |
| Brick during flashing | no USB/boot | downtime | backup, old fastboot, charge | triage §8.5 |

---

## 10. Artifacts (already present)

```
<qa-dir>/
  README.md                 instructions and limitations
  lib.sh                    shared functions; guarded fb; dmesg categories
  smoke.sh                  health check (read-only, --shot)
  collect.sh                state/log/crash collection (tar.gz, redact)
  probe.sh                  one-line K=V for watchdog
  burnin.sh                 24-hour soak (changes MPD/player)
  latency.sh                response/start/frame rate (changes track)
  mark.sh                   manual marks in events.log
  boot-capture.start        optional OpenRC hook (last_kmsg at boot)
  host/qa.py                non-interactive SSH runner
  host/qadiff.py            PNG/RAW frame diff, masks, threshold
 <QA-plan-file>            this document
```

All scripts passed `dash -n`; Python passed `py_compile`. They have **not been
run** on the device. `<qa-dir>/baselines/` contains only a README: the baseline
is captured from the device, not taken from mockups.

---

## 11. Three main conclusions

1. **The health of this device cannot be measured by loadavg.** Known
   RPC clients in D produce 8–20 while the CPU is alive. Acceptance must rely on
   `boot_id` (reboots), `last_kmsg`, user-space D, fb lockup, OOM, XRUN, and
   response time; loadavg is reference-only.

2. **A 24-hour unattended burn-in is possible only for the player and only with
   a watchdog over RNDIS.** X without the battery has already caused an
   unexpected reboot, and an fb deadlock is fixed only by rebooting; long X
   sessions should use short supervised runs after kernel/power work, not “run
   it for a day and see.”

3. **Automation is limited by the player's fixed evdev paths.**
   Touch/keys/jack/lock are a manual checklist; everything else (MPD, screens via
   argv, network, SD, memory, dmesg, screenshots, reboots) can be automated.
   Exact tap latency requires an instrumented build or a camera, not uinput.

### Redacted values
- `<RNDIS-IP>` — RNDIS IP address.
- `<device-ip>` — device IP address.
- No password or personal path was present in this fragment.
