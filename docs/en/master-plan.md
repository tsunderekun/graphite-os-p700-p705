# MASTER-PLAN — graphite shell / LG P700

Snapshot: 25.11.2026. Consolidation plan; this document does not itself confirm a new device test.

## Verdict

Graphite already boots as an Xlib shell on the P700: the main screens, settings, hardware-key input, shade, lock, apphost, browser/terminal, and P0 player/files/notes have documented device checks. It is a working mobile OS prototype, but not yet a release-ready shell: full multitasking and system notifications are missing, text input is not unified, UX edge cases remain, and there is no complete QA gate. The next 1–2 weeks should stabilize what exists and complete everyday workflows; do not start low-level projects.

## Status

| Done and device-tested (according to STATUS) | In progress / incomplete | Deferred or researched only |
|---|---|---|
| Xlib shell boots through `session-shell`, with fallback to the player; kernel #23 passed X + a 300-second stress test. | No confirmation of the complete QA gate or 24-hour soak; the shell itself has not passed formal Gate 3. | QA-PLAN is a plan/scripts; it has not been run on the phone. |
| Screen framework, themes/settings, Start, MPD pivots, files, notes, shade, lock, power; key and status-bar tests. | Status-icon alignment, vertical scrolling on long screens; some actual settings actions and name entry in Files need finishing. | Chrome/notifications and switcher are plans only. apphost currently supports one GUI process; does Home minimize it? STATUS says “minimizes”, but SHELL-CHROME-PLAN describes current behavior as closing it; check code/version before changing behavior. |
| Apphost Stage 1: xterm uid 10000, Back closes; dillo launches; OSK and terminal layout; tested. | Multi-app support, task return, lifecycle and notifications are absent; apphost remains single-window Stage 1. | Production Xauthority instead of dev `-ac` is a security gate before production. |
| Player P0: `idle`, `readpicture`, covers/cache, library, resume; Files copy/move/delete/rename and Notes cursor/list/autosave/undo are reported as device-tested. | Repeat smoke/device acceptance after integration changes; inline OSK for file names. | Player P1: search/playlists/favorites; not a priority for the next wave. |
| The device is currently in `player` mode, X stopped (STATUS 25.11). | Resolve power mode/process composition before any load tests. | POWER/LOCK: sleep and blank have not been confirmed safe. USB modes and modem are research/plans, not UI features. |
| Metro and Settings have layouts/renders; research notes on formats/hardware are prepared. | Port only selected UX; do not substitute mockups for the real device. | Reader implementation has not started; video/GPU/modem-RMT have separate gates. |

**Fact boundary:** `STATUS.md` records device checks; `READER-V2`, `USB-MODES`, `LOCK-POWER-RESEARCH`, `VIDEO-STRATEGY`, `MODEM-STRATEGY`, and `QA-PLAN` explicitly state that the corresponding research did not change the code or phone. Do not treat experiments described there as completed.

## Discrepancies and unified decisions

| Topic | Discrepancy | Decision for the plan |
|---|---|---|
| Reader | The old `READER-PLAN` specifies a separate `lgreader`; `APPS-RESEARCH` and the newer `READER-V2` specify a shell screen. | Choose a `reader` screen in the shell, with one UI/Canvas and internal shelf/read/TOC/settings modes. PDF can later use a separate `mupdf-x11` process. Before delegation, check that `READER-V2` matches the actual APIs/build. |
| Apphost / tasks | Integration sources show a header/dock and 480×712; implemented Stage 1 in `APPS-RESEARCH` is status bar 32 + 480×768. Home is described as both closing and minimizing. | Fix the current geometry at 32+768; do not rebuild it to match the old mockup. Until a task manager is implemented and tested, keep Home on the actually verified STATUS contract (return to shell, without promising a background process); do not add a switcher separately from an apphost refactor. Back closes. Confirm window ownership and Home behavior in code before the next feature. |
| Power / lock | POWER-PLAN proposes future panel-off/suspend; the more cautious LOCK-POWER-RESEARCH accounts for the ESD/off-on loop and recommends backlight-only. QA/STATUS may sound as if the display is actually switched off. | Lock currently turns off only the backlight; do not call `FBIOBLANK`, `lcd_onoff`, or suspend. First reduce load/remove X from the background and measure on stable power. Panel blanking is a separate kernel/device gate; suspend is deferred R&D. |
| USB | USB-MODES describes SD mass storage and an image as possible alternatives, but it is a read-only audit and there is no userspace helper. | If the product needs file exchange, prefer a 256 MB image on eMMC via loop: SD remains available to the player and data-loss risk is lower. Do not include SD export in v1. Before implementation, expose only RNDIS; do not present MTP/PTP/ADB as working (there is no responder). Any switching requires local UI, read-back, and rollback. |
| Video | General research allows a conditional GO after a pan test; panel/power notes emphasize the risk. | Do not include video code in the near-term plan. Proceed only with a separate owner decision and supervised 10-minute pan gate on stable power; any `lcd_off/on`, reset, or ioctl timeout means stop. Even PASS does not mean the player is ready. |
| Fonts, icons, UI | Shell chrome has inconsistent status icons; Reader proposes DejaVu Serif for book text and shell Sans for UI; old mockups do not always match current dimensions. | Keep current shell tokens/font rules; unify status slots at 18×18 and optical centers per SHELL-TODO. Serif is for book body text only; do not change system labels/icons for Reader. 480×800, tap targets ≥44, text ≥11, no animation. |
| Reader libraries | `APPS-RESEARCH` says `libhyphen` is installed; `READER-V2` checked and found it absent. Package lists in general are not a device fact for a particular snapshot. | For builds, treat `libhyphen`/dictionary and `libzip` as absent until a fresh inventory proves otherwise. Check runtime and `-dev` packages separately; do not expand dependencies for S0. |
| QA/kernel | QA-PLAN notes a #22/#23 discrepancy and requires uname; status says #23. | Start the gate with fresh `uname -a`, `boot_id`, image/hash, and `smoke --strict`; old logs/renders are not PASS for the current state. |

## P0 — next 1–2 weeks

Estimates are calendar time for one developer/integrator; device windows and owner wait time can increase them. Parallelize only independent file sets; the owner handles integration and the phone.

| # | Goal and file/module scope | Dependencies | Estimate | Done when | Risk |
|---|---|---|---:|---|---|
| 1 | Capture a reproducible baseline and verify the mode: `notes/qa/host/qa.py`, `notes/qa/`, STATUS/report; device work by owner. | Known-good boot, working control, power; verify backup/rollback. | 0.5–1 d | `uname -a` is expected #23, `smoke --strict` PASS, `boot_id` and artifacts saved, processes match the mode; X and player modes are not active simultaneously. | Do not run heavy tests on USB-PC power without a battery; on any reset stop and collect `last_kmsg`. |
| 2 | Resolve ambiguity in apphost behavior and document the current contract: `os/src/apphost.cpp`, `os/src/app.cpp`, corresponding `SHELL-TODO`/`APP-INTEGRATION-PLAN`. No multitasking yet. | Check actual branch/code and current STATUS; do not change another screen without agreement. | 0.5–1 d | Launch, Back, Home, process exit, and focus are documented and verified; 32+768 geometry is consistent across documents and smoke tests. | Home semantics affect user expectations; do not replace working behavior without device verification. |
| 3 | Close dev X access before production: `os/session-shell`, apphost/Xauthority setup, build instructions. | Decide dev vs production profile; verify child GUI launch as uid 10000. | 1–2 d | Production does not start with `-ac`; shell/apphost receive a cookie, xterm/dillo work; an unrelated UID cannot connect. Dev profile is clearly marked. | A wrong cookie can block all of X; retain local fallback/known-good. |
| 4 | Finish Shell chrome UX edges: `os/src/ui.cpp`/status-bar module and only the required `apps`, `set-*`, `shade` screens; check SHELL-TODO against code. | Agreed sizes/tokens and task #2. | 1–2 d | Icons are consistent in graphite/paper/ember, USB appears only when actually online; long lists scroll without conflict with back/slider; hit areas comply. | Do not add gestures that conflict with vertical drag; 480×800 regression. |
| 5 | Shared text input, minimum P0 scope: `os/include/input.h`, `os/src/input_kb.cpp`, `input_text.cpp`, adapters `osk.cpp`, `screens/files.cpp`; Notes/Terminal only if their current appearance is preserved. | First capture current screenshots/behavior; agree file boundaries; INPUT-PLAN. | 3–5 d | Rename and new-folder actions work without a physical keyboard; Cyrillic/EN/Shift/DEL/Enter work; Notes/Terminal do not regress. | XTest/focus may break input in other windows; the shared module must not become a rewrite of every screen. |
| 6 | Settings safety and correct name entry: `os/src/settings.cpp`, `os/screens/files.cpp` (only in the assigned set), host tests. | Separately verify actual save paths and atomicity; do not write to device before integration. | 1–2 d | Write/fsync/rename errors do not report false success; file names are validated/UTF-8 is not truncated; host negative tests pass. | System settings file and root permissions; do not change format without migration. |
| 7 | Service/screen smoke regression: `os/host-build.sh`, `os/preview.sh`, `notes/qa/host/qa.py`, and testable unit components. | Baseline #1; align QA-PLAN criteria with current geometry/actual shell. | 1–2 d | Host build is clean; previews of key screens; device smoke with frames/logs; Back/Home/Power, MPD disconnect/reconnect, and apphost exit pass. | Preview does not prove hardware stability; use guarded framebuffer capture with timeout. |
| 8 | Exact integrated acceptance of current UI + short session: shell/player/apps/files/notes; QA-PLAN Gate 3 subset. | #1–7 integrated and built; battery or stable 5V 1.5–2A, known-good image. | 1–2 d | 10–15 key checks below pass manually on device; 30 minutes without reset/OOM/user D/X-fb hang; artifacts attached. | Do not stress-test at 500mA without a battery; after the first failure, do not repeat until analyzed. |

**Keep out of P0:** EQ, GPU, video code, modem writes/EFS; also keep multi-app/switcher out until there is a separate product policy and resource test.

## P1 — 2–4 weeks

1. **Reader v1** (`os/screens/reader.cpp`, `os/include/reader*.h`, `os/src/reader/*`, build scripts): S0 layout/probe against real books → TXT, then FB2/FB2.zip, shelf, TOC, progress/resume, themes/size. Architecture: shell screen per `READER-V2`, not `lgreader`. Dependency: owner agrees on first-release criteria; check libxml2 headers/build and libzip. Estimate 10–13 working days per READER-V2; 60-minute soak after integration. EPUB only after v1.
2. **Tasks and notifications:** owner first decides multi-app policy. Then apphost task list/suspend/resume, no more than 3–4 tasks and 2 heavy tasks; only then `tasks` UI and system notification model. Modules: `os/src/apphost.cpp`, lifecycle in `app.cpp`, new notification modules and `shade.cpp`. Do not make screenshot cards separately from window lifecycle. Estimate 1–2 weeks.
3. **Power policy without dangerous blanking:** identify supported power sources; test backlight-only lock, timers/brightness/frequency limits one setting at a time. Reduce background consumption; do not treat research current measurements as facts. Panel off and suspend remain behind separate gates. 2–4 days coding plus supervised measurements.
4. **USB file exchange, only by owner decision:** implement helper and UI for a 256 MiB image, RNDIS default, lock/read-back/rollback/watchdog, eject/flush/fsck. Start with host review and read-only selftest; do not test switching over SSH through the same RNDIS connection. 3–5 days plus separate hardware window. No SD export in v1.
5. **Media and keyboard P1:** player search/playlists, OSK focus/shift improvements, input buffer/selection based on actual demand. Do not displace stabilization.

## P2 / deferred

| Area | When to revisit | Limitations / what not to do |
|---|---|---|
| EPUB reader | After stable TXT+FB2, golden tests, memory/soak PASS. | Do not execute CSS; do not promise DRM/illustrations/complex footnotes. PDF only as separate `mupdf-x11`; do not link MuPDF into the shell. |
| Tasks/notifications | After multi-app policy is decided and RSS measured; notifications after minimum lifecycle infrastructure. | Do not run a separate notification daemon for shell-only events; do not promise heads-up over another window without a separate X overlay. |
| USB | After need and safe UI/rollback are confirmed. | Do not enable MTP/PTP/ADB without a responder service; do not expose SD read-write by default; do not manually change gadget functions or do so through a remote session that switching will interrupt. |
| Power / lock blanking | After exact verification of kernel #23 and a separately approved panel-only test on stable power. | Do not write `lcd_onoff`, call `FBIOBLANK`, or try `echo mem` now; current safe mode is backlight=0. |
| Modem | Separate research wave only: host-only analysis of V20F init/rmt_storage/ABI → then separate owner approval for fresh dumps and kernel/client phases. | No EFS writes before fresh p10/p11 backup and proven mapping; p17 is recovery, do not use as FSG; do not substitute p21/p22. Do not make modem features part of shell release. |
| Video | Only after an approved pan-test PASS per `VIDEO-STRATEGY-LUNA`, then a separate decision and staged work. | Do not write player/decode path before PASS. Do not test without a battery/stable high-capacity power; never have X and fb ownership simultaneously. |
| GPU | Separate host-only research budget and explicit approval for device probe. | Do not change Xorg/kernel/driver in the working tree for hypothetical acceleration. Keep CPU/Xorg fbdev baseline. |
| EQ | After shell/reader completion and separate owner go-ahead. | Do not show it in UI or investigate DSP writes now. |
| Never / not planned for now | WebKit/Chromium, custom browser/VT100, HEVC/VP9/AV1/1080p, DRM port to P700, sync/accounts/cloud, MTP without responder. | Disproportionate for 400 MB/1 GHz and current risks; revisit only if requirements/platform change. |

## Open owner decisions

1. **Multi-app policy:** should dillo/xterm remain alive in the background? If so, limit to 3–4 tasks, Home minimizes, Back closes; which apps count as heavy, and is eviction allowed?
2. **USB product:** is USB mass storage in the UI needed at all, and is a 256 MiB image sufficient, or is SD access required (with MPD stopped and FAT risk)? Keep RNDIS only by default.
3. **Lock/PIN:** is a PIN needed in the near-term product? Until decided, treat lock as a screen barrier without authentication; do not promise data protection.
4. **Video go/no-go:** will a separate supervised device window and stable power be allocated for the pan gate? Without PASS and separate approval, video remains deferred.
5. **Release boundary:** what counts as the next release — a reliable shell over the existing player, or shell + Reader v1? This determines P1 sequencing and scope.

## Device acceptance: “feels like a mobile OS”

Use a known kernel/image and record `uname -a` and `boot_id`; run short functional tests on battery or a stable source, and soak only after smoke and with rollback available. Any new reset, OOM, user-space D, fb timeout, or display storm is FAIL/stop.

1. Cold boot: shell starts automatically; local UI/SSH is available within 90 s; mode and processes match, X and fb-player do not compete.
2. Start: open player, apps, and settings; return with Back/Home; no empty/fake app entries.
3. Keys: Back/Menu/Home/Search, short/long Power, short/held Volume; no duplicate events or stuck keys.
4. Gestures: edge-back, pivot swipe, open/close shade; vertical drag does not change a slider, horizontal drag works.
5. Status bar: correct Wi-Fi/USB/headphone/charge status; USB is not shown fictitiously; three themes preserve centering/contrast.
6. Player: play/pause/next/prev/seek, track and cover changes, queue and library; MPD disconnect/reconnect does not hang shell.
7. Files: open/scroll directory, multi-select, copy/move/delete/rename; cancellation/errors are honest and no file is lost.
8. Notes: create/open, insert in middle, undo/redo, autosave; restart does not leave a truncated file.
9. External apps: xterm and dillo run as uid 10000; OSK enters RU/EN; Back hides OSK before closing the app.
10. Shell recovery: crash/close of external window returns to a controllable screen; no zombies; fallback remains available if X fails to start.
11. Memory/responsiveness: one heavy task; 30 minutes without OOM/user D, stable RSS, taps/Back not blocked by network timeout.
12. Lock: Power blocks input and turns off backlight while music continues; Power/approved gesture restores screen; no panel blank/suspend in this acceptance.
13. SD/audio/network: SD mounts, library is available, headphones switch, RNDIS and Wi-Fi recover; MPD listens only on the expected interface.
14. Stability: after smoke/manual gate, 24-hour player burn-in per QA-PLAN with zero unexpected reboot/OOM/XRUN/user D and logs saved; shell/X use separate short supervised cycles, not continuous X soak.

## Immediate action order

1. First inspect repository state and current changes; preserve/verify known-good boot and available rollback. Do not alter the working phone for a “clean start”.
2. On device, capture a read-only baseline and `uname -a`/`boot_id` in the current mode; shell is currently active (X + graphite-shell), player is fallback. Run only the agreed smoke tests and save artifacts.
3. On host, accept P0 #2–7 as small isolated changes; after each integration, build and preview, then one controlled deploy by the owner.
4. Do not change shell, kernel/power, and USB gadget at the same time. Device verification: one change group per maintenance window, short smoke first, then manual checklist.
5. On any reset/hang, do not repeat the scenario; collect `last_kmsg`, `dmesg`, boot_id, and QA logs, then return to known-good using the agreed procedure.
6. After P0 smoke/manual PASS, run a separate 24-hour player QA session. Only then decide release boundary with the owner and start Reader S0/P1.
7. Do not run pan/video, panel blank/suspend, USB mode switching, GPU open/ioctl, or modem/EFS writes now. These require separate decisions and risk windows; they are not routine checks on a working device.

## Three main conclusions

1. **There is already a live shell and tested apps, but this is a prototype, not a passed release gate.** Start with reproducible baseline and smoke, then UX edge cases and security.
2. **Do not mix product work with hardware experiments.** USB, modem, video, GPU, and panel power have separate gates; do not involve the phone in those projects now.
3. **Stability matters more than feature breadth.** Establish one clear shell/apphost contract, working input and file workflows, and confirmed QA; then build the reader. Heavy P2 work requires an explicit owner decision.
