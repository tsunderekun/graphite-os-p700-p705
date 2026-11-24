# SYSTEM-STATUS — LG P700 (u0), pmOS/Nura, ядро 3.4

Актуально на 24.11.2026. Устройство в режиме **player** (плеер, без X).

## Железо и система
| | |
|---|---|
| Модель | LG Optimus L7 P700 (u0), MSM7227A, 1 ядро 1 ГГц, 400 МБ RAM |
| ОС | postmarketOS edge (Alpine), rootfs `/dev/mmcblk0p20` (2.7 ГБ, занято 59 %) |
| Ядро | `3.4.0-CM #22-postmarketOS`, собрано 23.11.2026 20:26 |
| Ядро-исходник | TeamHackLG/lge-kernel-lproj @ `45d6b0c683f3e0178e91c081133c6ebbea1d30fd`, gcc6, APKBUILD `pkgrel=21` |
| Экран | 480×800, RGB565 16 бит, `/dev/fb0`, панель физически перевёрнута (блит на 180°) |
| Boot | `/dev/mmcblk0p9` ← `artifacts/boot-rot.img` (7 974 912 Б) |
| SD | `/dev/mmcblk1p1`, vfat, UUID `<sd-uuid>`, 29.5 ГБ, `/media/sd` |
| Сеть | wlan0 `<phone-ip>` (WPA2), RNDIS `172.16.42.1` |
| SSH | dropbear, `user` / `<device-password>`; sudo-пароль `<device-password>` |

## Что работает
- **Wi-Fi**: WCN1314, стоковые `wlan.ko` + `librasdioif.ko` (из p14) через
  `insmod -f`; автозапуск `/etc/local.d/wifi.start`. Подробности — `WIFI-SOLVED.md`.
- **Экран**: fbdev 16bpp; **fbcon отключён в ядре** (`CONFIG_FRAMEBUFFER_CONSOLE`
  = n) — иначе консоль рисовала dmesg поверх плеера.
- **Тач**: `<MELFAS> melfas-ts`, `/dev/input/event0`, MT-протокол B, координаты
  уже в экранной ориентации.
- **Звук**: QDSP5/ALSA `hw:0,0`, MPD; программная громкость; **наушники** —
  демон `lgjack` (CAD_SET_DEVICE, см. `PLAYER-STATUS.md`).
- **Клавиши**: `u0_keypad` (event1: Vol−/Vol+/HOME), `7k_handset` (event5:
  Vol±, Power).
- **Батарея/зарядка**, подсветка экрана и клавиш, вибро, датчики
  (bma250, bmm050, gp2ap), фонарь AS3647 — работают и автозапускаются.
- **Modem AT**: `/dev/smd0` отвечает (`ATI` → LG-P700, IMEI <imei>,
  baseband V20e), `AT+CFUN=1` OK. Подробности — `MODEM-AT-WORKS.md`.

## Что не работает / заблокировано
- **SIM**: `+CME ERROR: SIM busy`, `AT+CSQ/CREG?/COPS?` → ERROR. Нет звонков/данных.
- **Bluetooth**: `hci_smd` не открывает командный канал; `btattach` даёт
  адрес `00:00:00:00:00:00` — см. `BT-status.md`.
- **GPS**: `/dev/nmea` открывается в блокировку, канал `GPSNMEA` не предлагается
  модемом — см. `GPS-status.md`.
- **Камера**: не проверялась, драйвер/HAL не настроены.
- **X11/десктоп**: есть, но конфликт `fbpan` и X — см. `DESKTOP-STATUS.md`.
- **Ядерные RPC-клиенты** (`krmt_storagecln`, `krtcclntd`, `kbatteryclntd`,
  `khsclntd`, `kadaptiveup`, `klulzactiveup`) висят в D-состоянии и надувают
  loadavg до 8–20; CPU не жгут, но SSH-команды иногда тормозят.

## Ядро: патчи и конфиг
Патчи в APKBUILD: `rel32`, `libra_nopie`, `wlan_volt`, `no_free_splash`,
`touch_single`, `sensor_sysfs`, `smd0`, `hci_smd_nopie`, `mmc_poll`.
Конфиг:
- `CONFIG_MODULE_FORCE_LOAD=y` (нужно для `insmod -f` стоковых модулей);
- SELinux выключен (secmark ломает `struct sk_buff`);
- `VOLANS_WLAN=m`, `LIBRA_SDIOIF=m`, `BT_HCISMD=m`, `MSM_SMD_NMEA=y`;
- `MSM_RMT_STORAGE_CLIENT`, `CPU_FREQ_GOV_ADAPTIVE`, `CPU_FREQ_GOV_LULZACTIVE` — off;
- **`CONFIG_FRAMEBUFFER_CONSOLE` = n**, `..._DETECT_PRIMARY` = n,
  `..._ROTATION` = n (pkgrel 21);
- `CONFIG_SND_MSM_SOC_MSM7K=y` (ALSA через `msm7k-pcm.c`), `msm_cad`/`msm_snd`.

## Загрузка и initramfs
- Boot-image собран из `boot.img` + исправленный initramfs
  (`notes/build_recovery_boot.py`), прошит в p9.
- В initramfs **остался рековери-хук** (в `init_2nd.sh`): перед `switch_root`
  делает `chown root:root` на `/`, `/etc`, `/root`, `/usr`, `/usr/local`,
  `/usr/share`, копирует `/fix/session-*` и `/fix/zz-mode.start`.
  Он спас систему после поломки владельцев, но каждый boot перезаписывает
  сессионные скрипты — при следующей пересборке initramfs хук убрать.
- cmdline: `androidboot.hardware=u0 lge.signed_image=false pmos_root_uuid=...`
  `pmos_rootfsopts=defaults loglevel=8 ignore_loglevel fbcon=rotate:2`
  (`fbcon` теперь не действует, консоли нет).

## Сервисы OpenRC (`/etc/local.d`)
| файл | что делает |
|---|---|
| `wifi.start` | WPA2 + DHCP |
| `mpd.start` | `mkdir -p /run/mpd`, `mpd /etc/mpd.conf` |
| `sensors.start` | включает датчики/фонарь |
| `time.start` | установка времени (и исторический chown-фикс) |
| `zz-jack.start` | `setsid /usr/local/bin/lgjack` (маршрут наушников) |
| `zz-mode.start` | запускает выбранную сессию (`player`/`desktop`) отсоединённо |
| `buffyboard.start` | экранная клавиатура для консоли |

Режим: `/etc/lgtest-mode` (`player` или `desktop`), настройки плеера:
`/etc/lgtest-settings` (`theme=graphite`, `btnlight=auto`).

## Сборка и прошивка
- pmbootstrap в WSL Ubuntu: `~/pmbootstrap`, pmaports в
  `~/.local/var/pmbootstrap/cache_git/pmaports`,
  APKBUILD — `device/archived/linux-lg-p700/APKBUILD`,
  конфиг — `config-lg-p700.armv7`.
- Ядро: `python3 ./pmbootstrap.py build --arch=armv7 --force linux-lg-p700`
  (~10–20 мин), затем boot-image скриптом и `dd`:
  ```sh
  dd if=/tmp/boot-rot.img of=/dev/mmcblk0p9 bs=1M
  ```
- Fastboot: VID/PID `18D1:D00D`, нужен старый 157-КБ `fastboot.exe` и
  `android_winusb.inf` (`RECOVERY-FASTBOOT.md`). Современный fastboot виснет
  на `Fetching super`.
- `apk` тормозит из-за `mirror.nura.eco`; строка postmarketos в
  `/etc/apk/repositories` закомментирована (бэкап `.bak`), активны
  `cdn.alpinelinux.org/edge/{main,community,testing}`.

## Грабли (проверено на своей шкуре)
1. `tar -xzf ... -C /` от root восстанавливает владельцев из архива — можно
   сломать `/etc`; всегда `--no-same-owner` для деплоя.
2. Запись в `/sys/class/graphics/fb0/console` виснет в ядре (fbcon off — уже
   не актуально, но помнить).
3. `dd if=/dev/fb0` блокируется, пока висит `FBIOPAN_DISPLAY`; снимать кадр
   при стабильном pan.
4. X и плеер не делят `/dev/fb0`; `fbpan` + X = дедлок на `lock_fb_info`.
5. `/dev/smd0` нельзя открывать до готовности модема: `smd_tty_open` держит
   `smd_tty_lock` и вешает даже debugfs до перезагрузки.
6. PowerShell съедает двойные кавычки в аргументах SSH-команд — писать
   скрипты на телефон и запускать их.

## Следующие шаги
1. Модем: разобраться с `SIM busy` (AMSS/симулятор), потом звонки/данные.
2. Bluetooth (`hci_smd` / прошивка) и GPS (`GPSNMEA`) — оба упираются в AMSS.
3. Камера.
4. Десктоп (см. `DESKTOP-STATUS.md`).
5. Пересобрать initramfs без рековери-хука.
