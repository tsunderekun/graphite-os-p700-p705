# Power, heat and sleep mode

This document records observations and a power-verification plan for the LG P700. Estimates are distinguished from measurements; conclusions apply to the stated configuration and require verification on the target device.

## 1. What was collected and with which tools

| Source | Information provided |
|---|---|
| `/sys/class/power_supply/{battery,ac,usb}` | VBAT, “capacity,” status, source type |
| `/sys/devices/platform/msm-battery/*`, `lge_pm_device/*` | `batt_volt`, `batt_therm`, `chg_therm`, `chg_stat_reg`, `usb_cable`, `chg_current_change` |
| `/sys/devices/system/cpu/cpu0/cpufreq/*` | governor, OPP, `stats/time_in_state` (time spent at each frequency) |
| `/proc/msm_pm_stats` | idle histogram: WFI / power collapse / failures |
| `/proc/stat`, `/proc/<pid>/stat` | CPU and per-process load |
| `/proc/interrupts` | interrupt frequency by source |
| `/proc/wakelocks`, `/sys/kernel/debug/wakeup_sources` | wakelocks and wakeup-capable devices |
| `/sys/kernel/debug/suspend_stats` | actual system sleep count |
| `/proc/msm_pm_stats`, `/sys/module/pm2/parameters/*` | idle parameters (20 ms threshold, `sleep_time_override`) |
| `/sys/kernel/debug/smsm/state` | modem handshake bits (DEM) |
| `/sys/class/leds/lcd-backlight/{brightness,drvstat}` | LM3530 brightness code |
| `/proc/swaps`, `/proc/diskstats`, `/sys/class/mmc_host/*/power` | swap, I/O, card runtime PM |
| `dmesg`, `/proc/last_kmsg` | traces from the previous reboot |
| Kernel config (WSL, `config-lg-p700.armv7`) + `lge-kernel-lproj` sources | enabled/disabled features and rationale |

Unavailable without further work: current/energy counters, SoC temperature, `/sys/class/thermal`, and userspace control of core voltage.

Measurement caveat: the SSH session over RNDIS (`usb online=1`) itself draws 8–15 mA, and reading `time_in_state`/`/proc/stat` slightly wakes the CPU. All measurements below include this background load; A/B tests (§8.4) must use the same background load.

---

## 2. Current state — observations

### 2.1. Power and source

```
battery: status=Full  present=1  capacity=100  voltage_now=4360000 µV  temp=360
ac:      online=0
usb:     online=1
msm-battery: batt_volt=4361  batt_therm=36  chg_therm=64…120 (fluctuating)
             usb_cable=11  chg_stat_reg=-5  chg_en_reg=-5  chg_current_change=0
lge_pm_device: batt_volt_raw=0  batt_therm_8bit_raw=0  pcb_version=203  msm_batt_level=2
```

Interpretation:

* The phone is connected to a **PC USB port** (`usb online=1`, `ac online=0`), so the source budget is **500 mA / 2.5 W**, not the 700 mA of a “charger.”
* There is no battery: `batt_volt_raw=0` and `batt_therm_8bit_raw=0` mean the modem's raw ADC readings are empty. `voltage_now = 4.36 V` versus `voltage_max_design = 4.2 V` is **not cell voltage**; it is a floating VBAT rail. The charger IC is in CV mode, with no battery to clamp the rail.
* `batt_therm = 36.0 °C` does **not change at all** (checked for 15 minutes): it is a placeholder, not a measurement. `chg_therm` fluctuates from 64 to 120; it is an uncalibrated raw ADC value. **The device has no reliable temperature reading.**
* `chg_stat_reg = -5` and `chg_en_reg = -5`: charger IC registers cannot be read through RPC. `usb_cable = 11`: cable type is undetermined (the same issue as jack detection). Consequently, **charge current, current limit and CC/CV mode are not observable**, and the phone probably always treats the source as “USB-PC” (500 mA).

### 2.2. What is actually running

```
mode=player, but: Xorg=3274, openbox=3319, tint2=3321, fbpan-ng=3307, lp7player ABSENT
mpd=1427 (volume 5%, nothing playing), wpa_supplicant=1487, lgjack=1517
wlan0 associated at 2.437 GHz, 52 Mbit/s, Tx-Power 17 dBm
```

The phone is currently in its **worst power state**: X with a panner at 10 Hz, display on (backlight 200/255), no player, no music. This is left over from the `session-desktop` test (it was not removed according to `DESKTOP-STATUS.md`). The figures below describe “idle X,” not the player.

### 2.3. CPU and governor

```
scaling_governor=ondemand   scaling_cur_freq=1000000   max=1000000   min=61440
sampling_rate=50000  up_threshold=95  hispeed_freq=1000000  down_differential=3
```

`hispeed_freq = 1 GHz` is the maximum: any load spike above 95% sends the core straight to 1 GHz.

Residency (`time_in_state`, 30-second window, HZ=100):

| Frequency | Time | Share |
|---|---|---|
| 1,000 MHz | 1908 ticks | **62%** |
| 600–900 MHz | 190 | 6% |
| 480 MHz | 165 | 5% |
| 320 MHz | 171 | 6% |
| 245 MHz | 210 | 7% |
| 122 MHz | 106 | 3% |
| 61 MHz | 321 | 10% |

Over the full 95-minute uptime, 57% was spent at 1 GHz. That is abnormal for an idle phone; the cause is identified below.

### 2.4. Main power consumer: fbpan-ng

Measurement from `/proc/<pid>/stat` (10 s, 1045 ticks total):

```
fbpan-ng    467 user ticks = 45% of one core continuously
kworker/0:1 13, TX_Thread 8, all others < 5
```

`fbpan-ng` is the X-session panner: 10 Hz × 480×800×16 bits ≈ 7.7 MB/s copied by the CPU (also named as the load source in `DESKTOP-STATUS.md`). It explains both 1 GHz residency and heating. In framebuffer mode, `lp7player` draws only on events and polls MPD once per second—two orders of magnitude cheaper.

### 2.5. Idle: power collapse never occurs

`/proc/msm_pm_stats` (over 95 minutes):

```
idle-request: 160 302 entries, 2662 s
idle-wfi:     160 302 entries, 2193 s        <-- WFI only, throughout
idle-power-collapse: count 0, total 0
idle-standalone-power-collapse: count 0
```

Requested-sleep histogram: 84,600 entries in the 4–16 ms range, 31,000 in 16–64 ms, and only 51 entries > 256 ms. Power-collapse threshold: `/sys/module/pm2/parameters/idle_sleep_min_time = 20000000` (20 ms), so the next timer must be more than 20 ms away.

Causes (both visible in `arch/arm/mach-msm/pm2.c:arch_idle()`):

1. **Idle window almost always < 20 ms.** Interrupts: `msm-sdcc (pio)` (irq 26) ~120/s, `dg_timer` ~85/s, `msmdatamover` ~9/s, `msm_hsusb` ~3/s, `smd` ~0.8/s, plus the 10 Hz panner. The result is choppy 4–16 ms idle. The host on irq 26 (eMMC or SDIO) is not yet identified; that is a separate task (`blktrace`/`iotop`) and does not affect this conclusion.
+2. **Interrupt wake-capability check** (`msm_irq_idle_sleep_allowed()`): if any active interrupt cannot wake from PC, PC is disabled entirely. This is very likely on this port (touch, SDCC polling, ISP).

The modem handshake is **not** a blocker: `CONFIG_MSM_IDLE_WAIT_ON_MODEM=0` (the modem is not awaited), and `DEM_MASTER_SMSM_READY` in `SMSM_POWER_MASTER_DEM` is set (`/sys/kernel/debug/smsm/state`, entry 6 = `0x11` = RUN|READY). The modem is “ready,” but PC still does not activate.

**Conclusion: there is no sleep mode in practice.** Even with the display off, the core remains in WFI at 61 MHz with the rails up. This is the largest opportunity for improvement.

### 2.6. D-state tasks and RPC

```
krtcclntd, kbatteryclntd, khsclntd, voicememo_rpc, krtcclntcbd, kbatteryclntcbd
   state=D  wchan=__msm_rpc_read
loadavg = 7.8…8.6  (two D-state tasks add ~2 to loadavg; the rest is real load)
```

They are absent from `ps` because BusyBox `ps` does not show D-state tasks; they are visible through `/proc/*/stat`. They do **not** hold wakelocks (`active_since = 0` for all in `/proc/wakelocks`), so they do not themselves block suspend. They indicate a partially functioning RPC path: the modem does not answer some requests.

### 2.7. Display, backlight and panel

```
lcd-backlight: brightness=200/255   LM3530: full-scale 20 mA, exponential,
               intensity=121/127 (from drvstat)
button-backlight: 255 (on)
fb0: 480x800, 16 bpp, msm_fb_type = "mipi dsi video panel", state=0
mipi_lg4573b.769/lcd_onoff — present (rw), panel currently on
```

“Turn off the screen” in the player writes `0` to `/sys/class/leds/lcd-backlight/brightness` (`lp7player.cpp:712`). **The panel and DSI continue operating**: video mode does not stop and nobody calls `FBIOBLANK`. The lock therefore saves only the backlight (40–65 mA), not panel+DSI (10–20 mA).

Do not touch `FBIOBLANK`/`lcd_onoff` yet: `dmesg` and `last_kmsg` show a `mipi_lg4573b_chip_lcd_off` → `lcd_on` loop every ~50–350 ms, with `esd_int_en_wq_handler` and `mdp_block_power_cnt: multiple power-off request`. This is the `msm_fb` bug that caused reboots under X (kernel #23 mitigated it but did not eliminate it).

### 2.8. Cards, swap and Wi-Fi

```
mmc0 (eMMC) runtime=suspended   mmc1 (SD) runtime=suspended
mmc2 (SDIO/Wi-Fi) runtime=ACTIVE     <- never suspends
swap: /swapfile 256 MB on eMMC, 0 used, swappiness=60
```

`mmc2` is active because of `mmc_poll.patch` (`MMC_CAP_NEEDS_POLL` for `pdev->id == 2`): without it Wi-Fi did not work (no SDIO IRQ line). The side effect is polling at ~1 Hz, Wi-Fi LDOs (`ldo05/06/19/11`) remaining enabled, and the SDIO host never entering runtime suspend. The driver has no power-save control (`iwconfig` shows no Power Management, `iw` is absent, and module `wlan` exposes only `driver_mode`/`ioctl_debug`).

### 2.9. Previous reboot evidence

```
/proc/cmdline: lge.reboot=<null>
/proc/last_kmsg (903 B): lcd_off/lcd_on loop + "No errors detected"
suspend_stats: success 0, fail 0   <- suspend never attempted
```

There is no panic, the LG error parser found no error, and the bootloader did not record the reboot cause. This is consistent with a **hardware power reset**, not a software crash (see §4).

---

## 3. Power budget

All currents are at VBAT ≈ 4.0 V. In the type column, **measured** means a direct measurement/sysfs value; **estimate** means calculation from known modes (±40% uncertainty).

### 3.1. Components

| Consumer | State | mA | W | Type |
|---|---|---:|---:|---|
| SoC (CPU+AXI+DDR) | 1 GHz, busy | 130–190 | 0.52–0.76 | estimate |
| SoC | 480 MHz, busy | 60–90 | 0.24–0.36 | estimate |
| SoC | 245 MHz | 35–55 | 0.14–0.22 | estimate |
| SoC | 61 MHz, WFI (idle) | 15–25 | 0.06–0.10 | estimate |
| SoC | power collapse | 2–4 | ~0.01 | **unreachable** |
| Panel + DSI (video mode, backlight 0) | always, until blanked | 10–20 | 0.04–0.08 | estimate |
| LM3530 backlight 200/255 (intensity 121) | display on | 40–65 | 0.16–0.26 | estimate |
| Backlight 255/255 | maximum | 55–80 | 0.22–0.32 | estimate |
| Backlight 0 | locked | ~0 | 0 | measured |
| Button backlight 255 | on | 3–6 | 0.01–0.02 | estimate |
| Wi-Fi WCN1314, associated, SDIO polling, no PS | idle | 15–40 | 0.06–0.16 | estimate |
| Wi-Fi, transmission at 17 dBm | peak | +150–250 | +0.6–1.0 | estimate |
| Wi-Fi unloaded (`rmmod wlan`) | — | 0 | 0 | indirect measurement |
| QDSP5 sound + codec + headphones | playing | 15–25 | 0.06–0.10 | estimate |
| Speaker output | playing | +25–50 | +0.1–0.2 | estimate |
| SD card | idle / read | 2–5 / 20–40 | — | estimate |
| eMMC (rootfs) | idle / write | 2–5 / 15–30 | — | estimate |
| USB PHY + RNDIS (our SSH) | connected | 8–15 | 0.03–0.06 | estimate |
| AMSS modem (no SIM, RF not transmitting) | always | 30–60 | 0.12–0.24 | estimate |
| PMIC + LDO + TCXO | always | 10–15 | 0.04–0.06 | estimate |
| Sensors + touch | always | 2–4 | ~0.01 | estimate |

### 3.2. Scenarios (totals)

| Scenario | mA | W | Includes |
|---|---:|---:|---|
| **A. Player, display 200, playing, Wi-Fi on** | 200–260 | 0.8–1.05 | CPU 245 + panel + backlight + audio + modem |
| B. Player, display 200, paused | 175–235 | 0.7–0.95 | same, without audio |
| C. Player locked (backlight 0, panel NOT off) | 120–175 | 0.5–0.7 | CPU 61–122 + panel + modem + Wi-Fi |
| D. **Idle X (current measurement)** | 220–300 | 0.9–1.2 | CPU at 1 GHz 62% due to fbpan-ng |
| E. X + browser/stress | 300–420 + peaks | 1.2–1.7 | + MDP/DSI, + Wi-Fi Tx |
| F. “Ideal”: locked + panel off + Wi-Fi off + PC | 45–85 | 0.2–0.35 | CPU PC/WFI + modem + PMIC |

The phone is currently in state D; 0.9–1.2 W in a 4.3″ enclosure produces noticeable heat, as confirmed by touch. This cannot be verified numerically because there is no reliable temperature reading (see §2.1); an IR thermometer or external thermocouple is required. For comparison, BL-44JH (≈1540 mAh) would last ~6 h in state A and ~5 h in state D.

**Observed facts:** process set, fbpan-ng at 45%, 62% at 1 GHz, PC=0, mmc2 active, brightness 200, no temperature reading. **Estimates:** every mA figure; verify with an external power meter (§8).

---

## 4. Reboots on USB without a battery

### 4.1. Mechanism

1. **No buffer.** A battery cell has low internal resistance (tens of mΩ) plus hundreds of microfarads of capacitance. Without it, VBAT is held only by charger IC output capacitance (units to tens of µF) and the cable to the port.
2. **No CV loop.** `voltage_now = 4.36 V` with a 4.2 V design maximum: there is nothing to charge, and the rail floats near its upper limit. Headroom above UVLO exists but is “soft”—any load step can pull VBAT down by hundreds of mV.
3. **The 500 mA source is not unlimited.** A PC port supplies 500 mA; above that, cable voltage drop increases (a 1 m thin cable is 0.3–0.5 Ω, i.e. 150–250 mV at 500 mA), then the port enters protection.
4. **Peaks.** Backlight startup (inrush boost), Wi-Fi packet at 17 dBm (+150–250 mA for a few ms), CPU step to 1 GHz, MDP/DSI.
5. Result: VBAT briefly falls below the PMIC threshold → **hardware reset**. There is no panic, `last_kmsg` is clean, and `lge.reboot=<null>`—exactly what is observed.

The empirical average-current threshold for this configuration (PC port, no battery) is **≈ 400 mA**: states A/C remain running, D is borderline, E reboots within tens of seconds. This matches “X rebooted by itself after ~30 s.”

### 4.2. Verification (with and without a meter)

Without a meter, indirectly:
* After reboot, run `cat /proc/last_kmsg`; if it again says “No errors detected” and contains no panic, this points to power, not software.
* Check repeatability: reboot is triggered by load (X + panner + Wi-Fi Tx), not elapsed time.
* Before reboot, run `dmesg | grep -i 'vbus\|chg'`; a timely VBUS warning from the controller implicates the source.

With a meter (preferred): insert a USB tester and observe **voltage sag** during peaks, not only average current. Input below 4.75 V implicates the cable/port.

### 4.3. Mitigations (descending order of effect)

1. **Use a 5 V / 1.5–2 A charger** and a short, thick cable. Even if the phone identifies it as “USB” and requests 500 mA, the charger can supply that without sag. Cheapest and most effective.
2. **Install a battery** (at least a working old BL-44JH). This removes the problem by providing a buffer and CV loop. Essential for a “pocket player.”
3. **Keep load below budget** while there is no battery:
   * brightness ≤ 150 instead of 200 (saves 15–25 mA and reduces inrush peak);
   * `scaling_max_freq = 800000` (or 600000) at idle;
   * turn off Wi-Fi when not needed;
   * do not leave X running with its 10 Hz panner (see §7);
   * do not run a browser alongside the X shell and Wi-Fi Tx.
4. **Optional bench hardware patch, at the owner's discretion:** 470–1000 µF low-ESR electrolytic + 10 µF ceramic across BAT+/BAT− to emulate a battery buffer. Manual work only; verify polarity. Connection carries inrush-current risk.
5. **UI warning:** when `ac/online=0 and usb/online=1`, show “PC power: 500 mA maximum” in the shade and automatically limit brightness/frequency. (A “Power” tile is already in the shade mockup.)

---

## 5. Sleep and suspend

### 5.1. Kernel configuration

| Option | Value | Meaning |
|---|---|---|
| `CONFIG_MSM7X00A_SLEEP_MODE_POWER_COLLAPSE_SUSPEND` | y | suspend = power collapse with XO off |
| `CONFIG_MSM7X00A_IDLE_SLEEP_MODE_POWER_COLLAPSE` | y | idle should also support PC |
| `CONFIG_MSM_IDLE_WAIT_ON_MODEM` | 0 | do not wait for modem before PC (correct) |
| `CONFIG_MSM_SLEEP_TIME_OVERRIDE` | y | `pm2/sleep_time_override` limits sleep |
| `CONFIG_MSM_PM_TIMEOUT_HALT` | y | handshake timeout: `printk` + `for(;;);`, an infinite busy loop |
| `CONFIG_WAKELOCK`, `WAKELOCK_STAT`, `USER_WAKELOCK` | y | wakelocks and `/sys/power/wake_lock` available |
| `CONFIG_EARLYSUSPEND`, `FB_EARLYSUSPEND` | y | framebuffer early suspend |
| `CONFIG_PM_SLEEP`, `PM_RUNTIME` | y | `/sys/power/state = mem` |
| `CONFIG_RTC_DRV_MSM` + `RTC_INTF_ALARM_DEV` | y | `/dev/rtc0`, `/dev/alarm` available |
| `CONFIG_THERMAL` | **n** | no thermal protection |
| `CONFIG_USB_OTG_WAKELOCK` | n | USB does not hold wakelock (good) |
| `CONFIG_LGE_BATTERY_SUSPEND_RESUME` | y | battery driver does not block early suspend |

In platform data (`board-msm7x27a_u0.c`), POWER_COLLAPSE and POWER_COLLAPSE_NO_XO_SHUTDOWN have `suspend_enabled = 1`. Thus `echo mem > /sys/power/state` is formally allowed and enters actual power collapse with a modem handshake.

### 5.2. Blockers and consequences

* **Idle-PC does not work** (0 entries in 95 minutes, §2.5): either the window is < 20 ms or an IRQ is marked as unable to wake. Distinguish with debugging (`pm2/debug_mask`), not guesswork.
* **Suspend has never been attempted:** `suspend_stats: success 0, fail 0`.
* **Only three wakeup devices are registered** (`/sys/kernel/debug/wakeup_sources`): accelerometer `3-0030`, `msm_hsusb_host.0`, `msm_otg`. **Keys, touch, `7k_handset` and RTC are not registered**; they have no `power/wakeup` attribute.
* Practical consequence: issuing `mem` now sends the phone into PC and it **will not wake from a button, touch or alarm**—only a USB event or long Power-button hold (PMIC hardware reset). Without a battery, “pulling the battery” is impossible.
* Separate hazard: `CONFIG_MSM_PM_TIMEOUT_HALT=y`. If modem handshake fails, `msm_pm_timeout()` prints `halting` and enters `for (;;);`—an infinite busy loop **at maximum frequency** (heating, current draw, no response). Neither `CONFIG_MSM_RPC_WATCHDOG` nor `RESET_CHIP` is enabled. Recovery requires a long Power-button press. Suspend on this kernel is not “try it and see”; it is separate work requiring a rollback plan.

### 5.3. Display off is not suspend

The current “lock” writes `brightness=0` (`lp7player.cpp:712`) and ignores input. The CPU continues polling `select()` with a 200 ms timeout (5 wakeups/s); the panel remains in video mode. **Only the backlight is saved.** Energy-efficient sleep requires (a) panel off, (b) PC/WFI without 1 GHz operation, (c) Wi-Fi without polling and, ideally, (d) suspend.

### 5.4. Safe suspend test (owner present only; not now)

`CONFIG_MSM_SLEEP_TIME_OVERRIDE` is a safeguard: a seconds value is applied once as the maximum sleep time, after which the AP **wakes itself** on the timer, even with no wakeup source.

```sh
# DO NOT RUN without a person nearby and an external power meter
echo 20 > /sys/module/pm2/parameters/sleep_time_override
echo mem > /sys/power/state
# expected: phone sleeps ~20 s and returns by itself; verify
cat /sys/kernel/debug/suspend_stats     # success: 1
cat /proc/uptime                        # uptime did NOT jump (suspend, not reboot)
```

If it does not return, holding Power for 8–10 s resets the PMIC. Plan B: do not test suspend until wakeup sources are added (see §10, phase 3) and `CONFIG_MSM_PM_TIMEOUT_HALT` is replaced with `..._RESET_CHIP` in a test build; otherwise handshake timeout causes an infinite `for(;;);`.

---

## 6. Governors, frequencies and voltages

* Current: `ondemand`, `sampling_rate=50 ms`, `up_threshold=95`, `hispeed_freq=1 GHz`, `down_differential=3`, `sampling_down_factor=1`.
* Available without rebuild: `ondemandX hyper wheatley smartmax dancedance badass AssWax SmartassH3 smartassV2 interactive conservative userspace powersave lionheart ondemand performance`. `adaptive`/`lulzactive` are disabled in config (`CPU_FREQ_GOV_ADAPTIVE/LULZACTIVE = n`).
* OPP (from `acpuclock-7627.c`, 1008 MHz table): 61 / 122 / 245 / 320 / 480 / 600 / 1008 MHz, VDD levels `2,3,4,5,6,7`. AMSS sets exact voltages; they are not exposed (`REGULATOR_USERSPACE_CONSUMER` disabled). The code notes: “v1.0 7x27a cannot switch VDD.” On silicon revisions < 1.0, voltage does not change with frequency, so reducing frequency saves less than expected. Verify experimentally (§8).
* CPR is enabled (`/sys/module/msm_cpr/enable = Y`), but is probably a no-op on 7x27A.
* **No thermal protection:** `CONFIG_THERMAL=n`, `/sys/class/thermal` is empty, and there is no SoC temperature sensor. The only protection is PMIC OTP; its activation looks like a reboot, consistent with this case.

Mode policy:

| Mode | Governor | Max | Min | Comment |
|---|---|---:|---:|---|
| Player, display on, playing | `smartassV2` or `interactive` | 600–800 MHz | 61 | 1 GHz is unnecessary: UI at 1 Hz |
| Player, locked | `powersave`/`conservative` | 320–480 MHz | 61 | MPD/ALSA wake as needed |
| X / shell, idle | `interactive` | 600 MHz | 61 | at 10 Hz panning it still reaches 1 GHz |
| X + browser | `ondemand` | 1008 MHz | 61 | only scenario requiring 1 GHz |
| On PC USB port | any | 600 MHz | 61 | reboot protection |

`ondemand` with `hispeed_freq=1 GHz` is the worst choice for this phone: any small spike triggers 1 GHz. At minimum, set `hispeed_freq=480000` and `up_threshold=98` (reversible sysfs change, no rebuild).

---

## 7. Power policy (UI/services requirements)

1. **Auto-dim:** no input for 10 s → brightness 150 → 90 (gradually, 2–3 steps).
2. **Auto-lock:** no input for 30 s **and** MPD paused → lock (backlight 0). If music is playing, the display may turn off, but the player must not stop.
3. **Lock = backlight 0 + (after msm_fb fix) panel sleep + CPU max 480 MHz.** Until fixed, turn off only backlight; leave panel active.
4. **Display during playback:** allow display off after 15 s (as on current lock screen), but do not call `refresh_state()` while locked (already so: `lp7player.cpp:1561`). Also use a 1 s `select()` timeout while locked instead of 200 ms, reducing wakeups by 4/s.
5. **Default brightness 150**, not 200. Maximum 255 manual only, with a warning on USB power.
6. **Button backlight:** 5 s automatic timeout already exists; always 0 while locked, also already implemented.
7. **Wi-Fi idle:** when network is not needed, unload module (`rmmod wlan` + `ifdown wlan0`) to stop SDIO polling, Wi-Fi LDOs and radio path (15–40 mA). Add Wi-Fi tile in shade (already in mockup). Automatic shutdown after 10 min without connections is optional.
8. **Swap:** `/swapfile` 256 MB on eMMC with 400 MB RAM is a poor choice (wear + latency + power). Prefer `zram` (RAM compression) or no swap; keep player and shell within 60–80 MB.
9. **PC USB port:** shade banner + automatic limits (brightness 150, max 600 MHz, Wi-Fi off) while `ac=0, usb=1`.
10. **Timers/logs:** remove `msm_batt_update_psy_status: enter/exit` spam (1 Hz in dmesg causes eMMC log writes); ideally use `pr_debug`.

---

## 8. Measuring power consumption (with and without a meter)

### 8.1. Available and unavailable phone metrics

| Metric | Available? | Location |
|---|---|---|
| VBAT voltage | yes (floating rail without battery) | `power_supply/battery/voltage_now`, `msm-battery/batt_volt` |
| Current/power | **no** | — (no `current_now`, `power_now`, `charge_counter`) |
| Charge/capacity | “yes” (from AMSS; without battery, 100% is fictitious) | `capacity` |
| Temperature | **no reliable value** | `batt_therm` = constant, `chg_therm` = noise |
| Frequency residency | yes | `cpufreq/stats/time_in_state` |
| Idle stats (WFI/PC) | yes | `/proc/msm_pm_stats` |
| Suspend stats | yes | `/sys/kernel/debug/suspend_stats` |
| Interrupt frequency | yes | `/proc/interrupts` |
| Per-process CPU | yes | `/proc/<pid>/stat` |
| Device runtime PM | yes | `.../power/runtime_status` |

**Absolute watts cannot be measured in software.** Software can report load structure (which process, how much, at what frequency, and whether it sleeps); watts require an external meter.

### 8.2. A/B snapshot (read-only; copy to phone and run)

```sh
#!/bin/sh
# power-snapshot.sh — one block of figures for comparing scenarios
echo "ts=$(date -u +%s) up=$(cut -d' ' -f1 /proc/uptime)"
echo "bl=$(cat /sys/class/leds/lcd-backlight/brightness) vbat=$(cat /sys/class/power_supply/battery/voltage_now)"
echo "ac=$(cat /sys/class/power_supply/ac/online) usb=$(cat /sys/class/power_supply/usb/online)"
echo "freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
cat /sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state
head -1 /proc/stat
grep -A1 'idle-power-collapse' /proc/msm_pm_stats | head -2
cat /sys/class/mmc_host/mmc2/device/power/runtime_status
wc -l < /sys/kernel/debug/wakeup_sources
```

Take two snapshots 60 s apart. The `time_in_state` difference gives average frequency, `/proc/stat` difference gives load, and `idle-*` counter differences show whether the CPU slept. This is a low-cost before/after comparison, not an absolute power measurement.

### 8.3. What to request from the owner

1. **USB tester** (5 V/3 A, voltage and mAh; logging preferred)—the primary instrument. Without it, the budget remains an estimate.
2. **5 V / 1.5–2 A charger** and short, thick cable.
3. **BL-44JH battery** (working or lasting at least 10 minutes)—eliminates voltage-sag problems and enables valid `current_now`/temperature readings.
4. Optional: IR thermometer (or “by touch” plus time to reboot) to calibrate “heat ↔ load.”

### 8.4. Measurement plan (one change at a time, 60 s per point)

| No. | Scenario | What it establishes |
|---:|---|---|
| 1 | Player, locked, paused, Wi-Fi off | baseline “sleep” (currently estimated ~120 mA) |
| 2 | Player, display 200, paused | display cost |
| 3 | Player, display 255 vs 0 | backlight cost (difference = LM3530) |
| 4 | Player playing through headphones vs paused | audio-path cost |
| 5 | Wi-Fi on vs `rmmod wlan` | SDIO polling + radio cost |
| 6 | Idle X vs idle player | fbpan-ng cost (expected +150–200 mA) |
| 7 | X, panner at 10 Hz vs 2 Hz vs “on damage” | primary X lever |
| 8 | CPU pinned at 61/245/480/1008 MHz (`cpufreq-set -f`) | voltage-frequency curve; test of “v1.0 without VDD switching” |
| 9 | Load until reboot (binary search) | actual PC-port limit |

---

## 9. Risks

| Risk | Probability | Consequence | Mitigation |
|---|---|---|---|
| `echo mem` without wakeup sources | high | phone will not wake; button reset only | test only with `sleep_time_override` and person present |
| Suspend handshake timeout (`PM_TIMEOUT_HALT`) | medium | `for(;;);` at 1 GHz: permanent hang + heat; Power is only recovery | avoid unnecessary tests; build with `RESET_CHIP` instead of `HALT` |
| Lowering `idle_sleep_min_time` | medium | PC with unreliable wake path → hang | step 20→10→5 ms, log with `pm2/debug_mask` |
| `FBIOBLANK`/`lcd_onoff` on lock | high | `lcd_off/lcd_on` loop and reboot (already observed) | only after `msm_fb` fix; test 10 min |
| Brightness 255 / X + browser on PC USB | high | reboot from VBAT sag | limits + banner + charger |
| Overheating without thermal protection | medium | PMIC OTP = sudden reboot, degradation | do not hold 1 GHz continuously; IR thermometer |
| `rmmod wlan` at a bad time | low | Wi-Fi lost until reboot, D-state tasks | unload only after `ifdown`; check `lsmod` |
| Swap on eMMC | low | card wear, lag | remove swapfile / use zram |
| Kernel changes (wakeup, thermal) | medium | new reboot/hang under normal conditions | separate build + KNOWN-GOOD rollback |

---

## 10. Staged plan

### Phase 0 — baseline (0.5 day, no phone changes)

1. Record current state in this document and with a §8.2 snapshot.
2. Put `power-snapshot.sh` in `notes/` (run over SSH, read-only).
3. Resolve mismatch “`mode=player`, but X is running”: decide the normal state (the phone currently runs in its most power-hungry mode).
4. Ask the owner for USB tester / charger / battery.

### Phase 1 — player policy (1–2 days, userspace, no kernel rebuild)

1. Default brightness 150; dim to 90 after 10 s; lock after 30 s (if paused).
2. `scaling_max_freq`: 800 MHz normally, 480 MHz locked; `hispeed_freq=480000`, `up_threshold=98` (or `smartassV2`).
3. While locked: `select()` timeout 1 s instead of 200 ms; do not poll MPD.
4. “USB-PC 500 mA” tile/banner + automatic limits when `ac=0, usb=1`.
5. Wi-Fi shade switch = `ifdown` + `rmmod wlan` (and reverse when re-enabled).
6. Measurements 1–5 from §8.4. Expected effect: **−60…90 mA** locked, **−30 mA** with display on, with no stability risk.

### Phase 2 — X/shell (2–3 days)

1. `fbpan-ng`: pan **only on damage** (or 2 Hz). Cuts CPU use by 45% and returns idle to 245–480 MHz. Largest single gain.
2. For X: max 600 MHz idle, 1 GHz only during scrolling/browser use.
3. Do not run X and player simultaneously; `restore-player.sh` must stop X.
4. Measurements 6–7. Expected: state D → 0.6–0.8 W.

### Phase 3 — kernel, one build (3–5 days, maintenance window)

1. Complete `msm_fb` blanking fix (check `cont_splash_done`; do not call unblank if panel is already on). Then lock-time `FBIOBLANK` is safe: −10–20 mA and panel actually sleeps.
2. Change 1 Hz `msm_batt_update_psy_status` spam to `pr_debug` (eliminate log writes).
3. Register wakeup sources: keypad (`gpio_kp`/`7k_handset`), touch, `rtc-msm` (`device_init_wakeup`). Suspend then becomes meaningful. In test build, replace `PM_TIMEOUT_HALT` → `PM_TIMEOUT_RESET_CHIP` (chip reset instead of infinite `for(;;);`).
4. Diagnose PC: `echo 1 > /sys/module/pm2/parameters/debug_mask`, capture `allow power_collapse: 0/1`, and identify the blocker (window < 20 ms or IRQ). If the window is the cause, lower `idle_sleep_min_time` to 10/5 ms.
5. Optional: remove `MMC_CAP_NEEDS_POLL` for mmc2 while Wi-Fi is unloaded (or make polling adaptive).
6. Test suspend strictly as in §5.4, using `sleep_time_override` and a power meter.

### Phase 4 — make “sleep” a product feature (after phase 3)

1. Lock → panel off + max 480 MHz + suspend after N minutes.
2. Player alarm on `rtc0`/`/dev/alarm` (wake for sleep timer).
3. Automatic wake by button/touch—verify 20+ cycles.
4. Targets: locked 40–80 mA, suspend 3–8 mA, display + player 180–250 mA.

---

## 11. Three main conclusions

1. **The phone does not sleep and cannot currently sleep: over 95 minutes there were zero power-collapse entries and zero suspend entries, only WFI.** The cause is not the sleep configuration (enabled and correct), but that the idle window is almost always < 20 ms (10 Hz panner + ~120 IRQ/s) and/or an interrupt cannot wake from PC. Until fixed, the budget is determined by “display on and core running”; the main lever is removing `fbpan-ng` at 10 Hz (45% of a core) and its 1 GHz residency (62%).

2. **The spontaneous reboot under X is VBAT sag, not software.** Without a battery there is no buffer or CV loop (the 4.36 V rail floats), the source is identified as USB-PC (500 mA), and X with the panner averages 220–300 mA with peaks around half an amp. No panic, clean `last_kmsg`, `lge.reboot=<null>`. Address in order: 1.5–2 A charger + short cable → battery → brightness/frequency/Wi-Fi limits → UI warning.

3. **Suspend is not yet a goal; it is an R&D task.** Only three wakeup devices are registered (accelerometer, USB, OTG); keys, touch and RTC are not, so `echo mem` is a one-way trip until a long Power-button press. First implement “display off = backlight 0 + panel off + 480 MHz + Wi-Fi off” (80% of the benefit without risk), then add `device_init_wakeup` for keys/touch/RTC and test suspend with `sleep_time_override`.

---

## Redactions

* No IP addresses, passwords, or personal filesystem paths were present in the source; no placeholders were required.
