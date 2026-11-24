# USB-MODES — режимы USB-порта LG P700 (u0)

> Публичный технический план. Указанные сетевые адреса и локальные реквизиты
> обезличены; проверьте параметры своей системы перед запуском примеров.

> Дата пробы: 24.11.2026. Плата: LG Optimus L7 P700 (MSM7227A), pmOS/Alpine,
> ядро `3.4.0-CM #23-postmarketOS`. Гаджет — **старый Android**
> (`CONFIG_USB_G_ANDROID=y`), **не configfs**.
>
> Документ основан на трёх источниках: (1) read-only проба живого устройства
> по SSH 24.11.2026 (ни одной записи в `/sys`, режимы не переключались);
> (2) исходники ядра `TeamHackLG/lge-kernel-lproj @ 45d6b0c` (локально
> `~/kern_research/...`); (3) `SYSTEM-STATUS.md`, `SYSTEM-RESEARCH.md`,
> `MODEM-PLAN.md`, `POWER-PLAN.md`, `SETTINGS-SPEC.md`.
>
> Легенда: **[Ф]** факт (живая проба/код), **[Т]** теория + уверенность,
> **[П]** проверка из тест-плана.

---

## 0. TL;DR

1. Ядро умеет **всё**, что нужно: RNDIS, mass storage (SD и файл-образ),
   charge-only, serial (CDC ACM), MTP, PTP, adb, diag, rmnet. Ограничение —
   **userspace**: у MTP/PTP нет респондера, у adb нет adbd. Это не «не
   включено в ядре», а «некому отвечать».
2. Переключение — три sysfs-записи с обязательной проверкой чтением:
   `enable=0` → `functions=<список>` → `enable=1`. Обе записи могут
   **молча не сработать** (`functions` игнорирует неизвестные имена,
   `enable` возвращает успех при провале `usb_add_config`), поэтому
   read-back — не вежливость, а часть протокола.
3. Массовая память — единственный режим с реальным риском потери данных
   (FAT на SD/в образе). Поэтому: SD по умолчанию **read-only**, образ
   **256 МБ на eMMC через loop** (есть I/O-статистика и явный flush),
   «хост извлёк» подтверждает человек, `fsck.vfat -n` после каждого RW-сеанса.
4. Brick-риска нет: sysfs гаджета **не персистентен**, после перезагрузки
   ядро снова поднимает `rndis`; загрузочная цепочка не затрагивается.
   Риск — данные на SD/в образе и потеря SSH, а не «окирпичивание».
5. MTP/PTP/adb в v1 **не выпускать**: хост увидит устройство и будет
   ждать ответа, которого нет; это хуже, чем честное «недоступно».

---

## 1. Проверенные факты (read-only, 24.11.2026)

### 1.1. Живая проба

| Что | Значение | Комментарий |
|---|---|---|
| `android0/enable` | `1` | гаджет поднят |
| `android0/functions` | `rndis` | разделитель — **запятая** |
| `android0/state` | `CONFIGURED` | хост забрал конфигурацию |
| `android0/idVendor` | `<rndis-vid>` | Google VID (не LG `1004`) |
| `android0/idProduct` | `<rndis-pid>` | текущий PID RNDIS |
| `iProduct` / `iManufacturer` | `Android` / `Android` | строки для хоста |
| `iSerial` | `<serial-placeholder>` | заглушка |
| `bDeviceClass` | `0` | composite, классы на интерфейсах |
| `rndis0` | `UP`, `<device-ip>/16`, MTU 1500 | адрес живёт в netdev |
| `/dev/mtp_usb` | `crw------- 10:24` | создан при probe, **не** значит «MTP работает» |
| `/dev/android_adb` | `crw------- 10:27` | создан при probe; adbd нет |
| `/dev/ttyGS0` | **нет** | serial ни разу не включали |
| `f_mass_storage/lun` | symlink → `.../gadget/lun0` | LUN есть **всегда** |
| `lun0/{file,ro,nofua,cdrom}` | `` / `0` / `0` / `0` | `file` пуст = нет носителя |
| Корень | свободное место проверить; 256 МБ образ требует места с запасом |
| SD | `<sd-device>`, vfat, объём зависит от карты | UUID `<sd-uuid>`, `/media/sd` |
| MPD | `db_file=/var/lib/mpd/tag_cache` (eMMC), `auto_update yes` | база и обложки **не на SD** |
| `unudhcpd.rndis0` | `supervise-daemon`, server `<device-ip>`, client `<host-ip>` | в runlevel `default` |

Инструменты на телефоне (проверено): `flock`, `timeout`, `fuser`, `lsof`,
`losetup`, `mkfs.fat`, `dosfsck`/`fsck.vfat`, `mpc`, `sync` — всё есть.
Пакеты `dosfstools`, `util-linux` установлены.

### 1.2. Что подтверждено исходниками

| Факт | Где | Следствие |
|---|---|---|
| `functions` — **через запятую** (`strsep(&b, ",")`) | `android.c:1713` | `mass_storage,rndis`, не пробел |
| `functions` при `enabled` → `-EBUSY` | `android.c:1680` | сначала `enable=0` |
| Неизвестное имя: `pr_err`, но store возвращает `size` | `android.c:1717` | **обязателен read-back** |
| `enable=1` при провале `android_enable()` возвращает `size`, но `enabled=false` | `android.c:1792` | **обязателен read-back** |
| `state` = `DISCONNECTED` / `CONNECTED` / `CONFIGURED` | `android.c:1843` | единственный честный признак «хост забрал» |
| Все `f->init` вызываются на probe, не на bind | `android.c:1518` | LUN и `/dev/mtp_usb` есть с загрузки |
| `lun/file` можно писать при выключенном гаджете | `storage_common.c:1014` | носитель готов **до** `enable=1` |
| `ro` меняется только при закрытом LUN (`-EBUSY`) | `storage_common.c:963` | сначала `ro`, потом `file` |
| `file=""` = eject (нет `forced_eject`) | `storage_common.c:1014` | eject только записью пустой строки |
| `fsg_lun_open` — `filp_open(O_RDWR)`, **без** `FMODE_EXCL` | `storage_common.c` | смонтированный раздел **не** будет заблокирован → проверять самому |
| `fsg_lun_close` = `fput`, без `fsync` | `storage_common.c` | после eject нужен `sync`/`blockdev --flushbufs` |
| `rndis_disable` → `gether_disconnect`: netdev **не** удаляется | `u_ether.c` | `rndis0` и IP переживают переключение |
| `charge_only` — HID-заглушка, не «нет устройства» | `f_charge_only.c` | ток не меняет, хост всё равно видит HID |
| `serial` на LG: `transports="TTY"` по умолчанию | `android.c:708` | локальный `/dev/ttyGS0` + CDC ACM на хосте |
| `adb` — misc `android_adb` | `f_adb.c:35` | ядро готово, adbd нет |
| `mtp` — misc `mtp_usb`, bind без userspace | `f_mtp.c:1216` | хост увидит MTP и будет ждать вечно |

### 1.3. Чего в ядре нет

- `CONFIG_USB_CONFIGFS` — **нет**. Все инструкции из интернета с
  `/sys/kernel/config/usb_gadget/...` неприменимы.
- `CONFIG_USB_MASS_STORAGE` (`g_mass_storage`) — **нет**. `modprobe g_mass_storage`
  и `file=` в модуле не существует; есть только `f_mass_storage` внутри
  Android-гаджета.
- `CONFIG_USB_OTG` — **нет** (подтверждено `POWER-PLAN.md`), порт device-only.
- `CONFIG_USB_G_SERIAL` — **нет**, но `serial` живёт внутри `G_ANDROID`.

---

## 2. Схема

```
 graphite-shell (root, локально на телефоне)
        │  exec /usr/local/bin/lg-usb-mode <mode>
        ▼
 lg-usb-mode  ── lock /run/lg-usb/lock, state /run/lg-usb/state, log /var/log/lg-usb.log
        │
        ├─ sysfs: /sys/class/android_usb/android0/
        │     enable, functions, idVendor, idProduct, iProduct, iSerial, state
        │     f_mass_storage/lun → /sys/devices/platform/msm_hsusb/gadget/lun0/
        │           file, ro, nofua, cdrom
        │     f_serial/transports   (write-only, "TTY")
        │
        ├─ хост-активность: /sys/block/mmcblk1/stat, /sys/block/loopN/stat
        ├─ SD: mount/umount /media/sd, fuser/lsof, fsck.vfat -n
        ├─ MPD: mpc stop, pkill mpd, /etc/local.d/mpd.start
        └─ сеть: ip link rndis0, rc-service unudhcpd.rndis0
                 │
        ядро: android composite (G_ANDROID) → msm_hsusb UDC → кабель → хост
```

Ключевая особенность: **источник истины — sysfs, а не файл настроек**.
`/etc/lg-usb.conf` хранит только «что поднимать при загрузке» и лимиты;
текущий режим всегда читается из `functions` + `state`.

### 2.1. Состояния helper'а

```
IDLE ──set──► PREFLIGHT ──► DRAIN ──► DISABLE ──► CONFIGURE ──► ENABLE ──► VERIFY
                  │           │          │            │           │          │
                  └───────────┴──────────┴────────────┴───────────┴──► FAILED ──► ROLLBACK
                                                                          │
VERIFY ──► ARMED (30 с) ──confirm──► KEPT                                │
              │                                                          │
              └──────────────── timeout / ошибка ────────────────────────┘
```

`ROLLBACK` всегда идемпотентен и всегда возвращает **RNDIS** (безопасное
состояние), а не «предыдущий режим»: предыдущий мог быть mass storage с
уже отданной SD.

---

## 3. Протокол переключения (legacy Android gadget)

### 3.1. Атрибуты

| Атрибут | Путь | Значения | Обязателен | Когда пишем |
|---|---|---|---|---|
| `enable` | `android0/enable` | `0`/`1` | да | первым и последним |
| `functions` | `android0/functions` | `rndis`, `mass_storage`, `charge_only`, `serial`, … через **запятую** | да | только при `enable=0` |
| `idVendor`/`idProduct` | `android0/…` | hex, напр. `<rndis-vid>`/`<rndis-pid>` | желательно | до `enable=1` |
| `iProduct` | `android0/iProduct` | строка ≤ 126 | желательно | до `enable=1` |
| `state` | `android0/state` | ro | — | читаем после |
| `lun/file` | `f_mass_storage/lun/file` | путь к блоку/файлу; `""` = eject | для MS | при `enable=0` |
| `lun/ro` | `f_mass_storage/lun/ro` | `0`/`1` | для MS | **до** `file` |
| `lun/nofua` | `f_mass_storage/lun/nofua` | `0`/`1` | нет | оставить `0` (честный FUA/FLUSH) |
| `lun/cdrom` | `f_mass_storage/lun/cdrom` | `0`/`1` | нет | не трогать |
| `f_serial/transports` | `android0/f_serial/transports` | write-only, `TTY` | для serial | до первого bind |
| `factory_cable` | `android0/factory_cable` | ro, `6000` = factory | — | проверяем: factory-режим игнорирует записи |

`lun` — симлинк на `lun0`; писать можно по любому пути, они эквивалентны.

### 3.1.1. VID/PID по режимам (рекомендация)

Текущие `<rndis-vid>:<rndis-pid>` — не дефолт ядра (в дефконфиге `1004:61fc`); их выставил
кто-то из userspace/диагностики (точный источник не подтверждён).
**Не наследовать:** helper пишет VID/PID явно перед каждым `enable=1`.
Уникальный PID на режим нужен, чтобы Windows не переиспользовал
закэшированный экземпляр с другим набором интерфейсов.

| Режим | VID:PID | Почему |
|---|---|---|
| RNDIS (и rollback) | `<rndis-vid>:<rndis-pid>` | уже проверен на этом хосте, SSH работает |
| MS SD | `1004:61fc` | LG VID, mass-storage класс; Windows ставит USBSTOR |
| MS образ | `1004:61fd` | отдельный PID, чтобы отличать в `lsusb`/диспетчере |
| Charge-only | `1004:61fe` | HID-заглушка |
| Serial | `1004:61ff` | CDC ACM |
| MTP/PTP/ADB (не выпускать) | `<rndis-vid>:d002`/`d003`/`d004` | только для экспериментов |

Проверка на хосте: `lsusb -d 1004:` (Linux), `Get-PnpDevice` (Windows).
Если Windows после смены режима показывает «Код 10» — «Удалить устройство»
в диспетчере и переподключить кабель.

### 3.2. Точный порядок

```
0) preflight (см. §8.7): root, sysfs, factory_cable != 6000, режим разрешён,
   для SD: раздел не смонтирован или его можно размонтировать,
   для образа: файл есть/создаётся, места ≥ размер+150 МБ
1) DRAIN:
   - если уходим из MS: хост уже извлёк (подтверждение), затем
     echo "" > .../lun/file          # eject, закрыть носитель
   - если отдаём SD: mpc stop; pkill -TERM mpd; wait ≤3 с; sync;
     umount /media/sd  (fail → fuser -m, показать держателей, не убивать)
2) DISABLE: echo 0 > enable;  timeout 5;  read enable == 0
   (state при этом DISCONNECTED; хост видит отключение)
3) CONFIGURE:
   - echo "<list>" > functions;  read-back == ожидаемый набор
   - echo 1 > ro (если MS и RO);  echo <путь> > lun/file
   - echo <vid> > idVendor; echo <pid> > idProduct; echo "<name>" > iProduct
   - для serial: echo TTY > f_serial/transports (один раз за загрузку)
4) ENABLE: echo 1 > enable;  timeout 5;  read enable == 1
5) VERIFY: timeout 5 читать state каждые 100 мс до CONNECTED/CONFIGURED
6) POST:
   - RNDIS: ip link set rndis0 up; rc-service unudhcpd.rndis0 start (идемпотентно);
     проверить carrier (ip -br link) и <device-ip>
   - MS: ничего (файл уже отдан)
   - serial: запустить getty на /dev/ttyGS0 (см. §6.4)
7) ARM: watchdog 30 с (см. §7.4)
```

**Почему `lun/file` до `enable=1`:** LUN создаётся на probe, поэтому носитель
можно открыть заранее; хост при перечислении сразу видит готовый диск, без
«вставьте диск». Это же позволяет проверить `file` (например, `EACCES`,
`EROFS`) **до** того, как хост что-то увидит, и откатиться без мусора на шине.

### 3.3. Read-back — часть протокола

После каждой записи:

| Запись | Проверка | Если не совпало |
|---|---|---|
| `enable=0` | `cat enable` = `0` | 3 попытки по 1 с; затем `FAILED` |
| `functions=…` | множество из `cat functions` == запрошенное | `FAILED` + вернуть прежнюю строку |
| `lun/file=…` | `cat lun/file` == путь | `FAILED` (частые причины: `EACCES`, `ENOENT`, `EBUSY` на `ro`) |
| `enable=1` | `cat enable` = `1` | `FAILED` (провал `usb_add_config`) |
| — | `state` ∈ {CONNECTED, CONFIGURED} ≤5 с | `ARMED`, но статус «хост не забрал» |

### 3.4. Таймауты (эмпирические, с запасом)

| Шаг | Таймаут | Почему |
|---|---|---|
| `echo 0 > enable` | 5 с | `fsg_unbind` ждёт завершения текущей SCSI-команды |
| `echo <list> > functions` | 2 с | только список, без I/O |
| `echo … > lun/file` | 3 с | `filp_open` блочного устройства/файла |
| `echo 1 > enable` | 5 с | `usb_add_config` + `usb_gadget_connect` |
| ожидание `state` | 5 с | Linux/Windows шлют SET_CONFIGURATION за 0.1–2 с; 5 с — с запасом |
| `umount /media/sd` | 5 с | после `mpc stop` обычно < 1 с |
| watchdog | 30 с | человек успевает увидеть и нажать «Оставить» |

### 3.5. Откат при ошибке

Любой `FAILED` → `ROLLBACK`:

```
1) timeout 5 echo 0 > enable
2) echo "" > lun/file                # если трогали MS
3) echo rndis > functions            # read-back
4) echo "$RNDIS_VID" > idVendor; echo "$RNDIS_PID" > idProduct; echo Android > iProduct
5) timeout 5 echo 1 > enable; read-back
6) ip link set rndis0 up; rc-service unudhcpd.rndis0 start
7) если SD был смонтирован до операции: mount /media/sd; проверить;
   если MPD работал: /etc/local.d/mpd.start
8) log: причина, шаг, состояние до/после
```

Если откат не удался (например, `enable=0` завис) — **не циклиться**:
записать в лог `REBOOT_REQUIRED`, показать в UI «Нужна перезагрузка»,
не трогать SD. Перезагрузка возвращает `rndis` (sysfs не персистентен),
SD монтируется по `fstab`, MPD стартует из `local.d`.

### 3.6. Грабли, проверенные по коду

1. `functions=mass_storage rndis` (пробел) — второй «функцией» станет
   `mass_storage rndis`, имя неизвестно, `pr_err`, **store успешен**.
2. `functions=rndis,rndis` — `list_add_tail` дважды, bind дважды; helper
   дедуплицирует.
3. `enable=1` при уже `1` — `pr_err "already enabled"`, без изменений;
   helper не пишет, если read-back уже совпал.
4. `ro` после `file` — `-EBUSY`; порядок только `ro` → `file`.
5. `file` на **смонтированный** раздел — откроется (нет `FMODE_EXCL`),
   данные на SD будут разъезжаться. Проверка `/proc/mounts` обязательна.
6. `factory_cable=6000` — `functions`/`enable` игнорируются, store
   возвращает успех. Helper обязан отказаться.
7. `rndis0` после `enable=0` **остаётся** (carrier off), IP сохраняется;
   не нужно заново `ip addr add`, но `ip link set up` и `unudhcpd` проверить.
8. `/dev/ttyGS0` после первого включения serial **остаётся**; наличие
   устройства не означает, что режим активен — читать `functions`.

---

## 4. Таблица режимов

| Режим / опция | `functions` | Что видит хост | Готовность | Главный риск |
|---|---|---|---|---|
| **RNDIS** (по умолчанию) | `rndis` | RNDIS/CDC-Ether, `<rndis-vid>:<rndis-pid>`, DHCP `<host-ip>`, SSH `<device-ip>` | работает | нет |
| **SD-карта** | `mass_storage` | SCSI-диск 29.5 ГБ, FAT (p1 как «superfloppy») | нужен `umount` + стоп MPD | порча FAT, MPD недоступен |
| **Образ 256 МБ** | `mass_storage` | SCSI-диск 256 МБ, FAT32, `/dev/loopN` | образ создаётся один раз | порча только образа |
| **Только зарядка** | `charge_only` | HID-устройство (заглушка), данных нет | работает | SSH пропадёт, ток **не** вырастет |
| **Гаджет выключен** | — (`enable=0`) | ничего (устройство исчезает) | работает | вернуть можно только локально/ребутом |
| **Консоль** | `serial` | CDC ACM: Linux `ttyACM0`, Windows COMx | нужен getty | вход в систему любому с кабелем |
| MTP | `mtp` | MTP-устройство | **нет респондера** | хост висит в таймаутах |
| PTP | `ptp` | PTP-камера | **нет респондера** | то же |
| ADB | `adb` | ADB-интерфейс | **нет adbd** | «устройство не отвечает» |
| diag | `diag` | QCDM/diag | протокол LG не разобран | отладка модема, не для UI |
| rmnet / mbim | `rmnet*`, `mbim` | WWAN | нет SIM/QMI | бессмысленно сейчас |
| audio_source | `audio_source` | USB-звуковая карта (источник) | нет userspace-маршрута | потенциально возможно позже |
| accessory | `accessory` | AOA | нужен хост-AOA | не нужно |
| cdrom_storage | `cdrom_storage` | CD-ROM (LUN2) | нужен ISO | экзотика (autorun) |

---

## 5. Mass storage

### 5.1. SD-карта: отдать `<sd-device>`

**Предусловия (жёсткие):**
- `mountpoint -q /media/sd` или раздел не смонтирован;
- `<sd-device>` — блочное устройство, размер объём, заданный в конфигурации, не `mmcblk0*`;
- MPD остановлен (не только pause): `mpc stop; pkill -TERM mpd`;
- нет держателей: `fuser -m /media/sd`, `lsof +f -- /media/sd`;
- заряд/АКБ: без АКБ большие копии на 500-мА порту — риск просадки VBAT
  (`POWER-PLAN.md` §4); UI показывает предупреждение.

**Шаги:**

```sh
mpc stop 2>/dev/null
pkill -TERM mpd 2>/dev/null; sleep 1
sync
umount /media/sd || { fuser -vm /media/sd; exit 1; }
# gadget выключен, functions уже mass_storage
# если предыдущий носитель был открыт: echo "" > .../lun/file
echo 1 > /sys/class/android_usb/android0/f_mass_storage/lun/ro   # RO по умолчанию
echo <sd-device> > /sys/class/android_usb/android0/f_mass_storage/lun/file
echo 1 > /sys/class/android_usb/android0/enable
```

**Что видит хост:** один SCSI-диск (LUN0) с FAT-томом в начале. Windows
монтирует букву, Linux — `/dev/sdX` (не `sdX1`: таблицы разделов нет,
экспортирован раздел). Это нормально и безопаснее, чем отдавать весь
`<sd-device>` (хост не сможет переразметить карту).

**Read-only vs RW:**
- RO (`ro=1`) — режим по умолчанию: бэкап, «вытащить музыку», показ файлов.
  Хост физически не может писать; `fsck` после не нужен.
- RW (`ro=0`) — только по явному подтверждению. После сеанса обязательно
  `fsck.vfat -n` (только проверка, без `-a`), при ошибках — предложить
  пользователю решение, не чинить автоматически.

**Извлечение (порядок важен):**
1. На **хосте** — «Безопасное извлечение» / `umount` / `eject`. Только хост
   может сбросить свой кэш; `sync` на телефоне его не заменяет.
2. В UI — «Хост извлёк» (человек подтверждает).
3. Телефон: `echo "" > .../lun/file` (eject), `sync`.
4. `mount /media/sd` (или `mount -a`), проверить `mountpoint`.
5. `mpc update --wait` **только если** экспорт был RW; затем
   `/etc/local.d/mpd.start`.
6. Плеер: обложки и база — на eMMC (`/var/lib/mpd/tag_cache`,
   `/var/lib/lp7player/art`), поэтому после возврата SD библиотека на месте;
   треки снова доступны, `auto_update yes` подхватит изменения.

**Если `umount` не идёт:** показать `fuser -vm` (кто держит), предложить
«Остановить MPD и повторить». Не делать `fuser -km` автоматически: под
раздачу может попасть чужой процесс (например, SSH с cwd на SD — маловероятно,
но цена ошибки высока).

**Если хост писал и кабель выдернули:** FAT почти наверняка грязный.
`fsck.vfat -n` покажет; `-a` — только по кнопке «Починить» с предупреждением,
что автоматическая починка может потерять файлы. Образ/бэкап важных данных —
до, не после.

### 5.2. Образ `usbshare.img` (безопасная альтернатива)

**Почему образ, а не SD:** SD остаётся в системе, MPD играет, «выдернул
кабель» ломает только файл-образ, который можно пересоздать.

**Размер:** 256 МБ по умолчанию. Свободно 1004 МБ; 512 МБ тоже влезает,
но оставляет ~490 МБ на логи/swap — разрешать только с проверкой
«свободно ≥ размер + 150 МБ».

**Проверено read-only:** `CONFIG_BLK_DEV_LOOP=y`, `CONFIG_BLK_DEV_LOOP_MIN_COUNT=8`,
`/dev/loop0…7` созданы; `mkfs.fat` (dosfstools), `dosfsck`/`fsck.vfat`,
`losetup` (util-linux 2.42) установлены. `CONFIG_CONFIGFS_FS` и
`CONFIG_USB_MASS_STORAGE` — **не заданы**, поэтому loop — единственный
способ получить «настоящий» блочный носитель с I/O-статистикой.

**Где:** `/var/lib/lg-usb/usbshare.img` на **eMMC** (rootfs), не на SD
(иначе образ лежит на том, что отдаём, — рекурсия и риск).

**Создание (один раз):**

```sh
mkdir -p /var/lib/lg-usb
img=/var/lib/lg-usb/usbshare.img
[ -f "$img" ] || {
    tmp="$img.new"
    fallocate -l 256M "$tmp" || \
        dd if=/dev/zero of="$tmp" bs=1M count=256 || exit 1
    mkfs.fat -F 32 -n USBSHARE "$tmp" || { rm -f "$tmp"; exit 1; }
    mv "$tmp" "$img"          # атомарно: недоделанный образ не станет «рабочим»
}
```

`mkfs.fat` есть (`dosfstools`), работает по регулярному файлу. Альтернатива
без пакета на телефоне: сделать образ на ПК (`mkfs.fat` в WSL) и залить
`scp` — но пакет уже стоит, это лишнее.

**Экспорт через loop (основной путь):**

```sh
LOOP=$(losetup -f)                       # напр. /dev/loop0
losetup "$LOOP" /var/lib/lg-usb/usbshare.img
echo 0 > .../lun/ro
echo "$LOOP" > .../lun/file
echo 1 > .../enable
```

**Монтировать образ на телефоне не нужно** — достаточно `losetup`: `f_mass_storage`
сам читает/пишет блочное устройство. Держать образ одновременно смонтированным
и отданным хосту нельзя (два независимых кэша → порча). Если надо заранее
положить файлы: `losetup` → `mount -t vfat` → скопировать → `umount` →
`losetup -d`, и только потом экспортировать.

**Почему loop, а не файл напрямую:** `f_mass_storage` умеет и регулярный файл
(`fsg_lun_open` поддерживает `S_ISREG`), но loop даёт:
- `/sys/block/loopN/stat` — видно, читал/писал ли хост (для UI «хост забрал»);
- `blockdev --flushbufs /dev/loopN` — явный сброс кэша образа;
- привычный блочный путь (512-байтные сектора, `fsck` по loop-устройству).

Прямой файл — fallback, если `losetup` почему-то не сработал; тогда
«активность хоста» не видна, а flush = `sync`.

**Права:** helper работает от root. `/dev/loop-control` — `root:disk 0660`,
`/dev/loop0…7` уже созданы (`CONFIG_BLK_DEV_LOOP_MIN_COUNT=8`); под
пользователем `user` `losetup -f` даёт `Permission denied`, это нормально.

**Извлечение:**

```sh
echo "" > .../lun/file          # eject
sync
blockdev --flushbufs /dev/loopN 2>/dev/null
losetup -d /dev/loopN
```

**Кэш и `nofua`:** оставить `nofua=0`, чтобы FLUSH/FUA от хоста доходили
до образа (медленнее, но честно). `nofua=1` — только для тестов скорости,
с явным предупреждением. После RW-сеанса: `fsck.fat -n /var/lib/lg-usb/usbshare.img`
(по файлу dosfsck умеет) или по loop-устройству до `losetup -d`.

**Место и рост:** `fallocate` резервирует сразу; при `truncate` файл
разреженный и растёт по мере записи хоста — следить за `df /` в UI.
Образ не является бэкапом: это «обменник», не хранилище.

### 5.3. Почему не отдавать весь `<sd-device>`

Хост увидит MBR и сможет переразметить/форматировать карту. Отдача `p1`
исключает изменение таблицы разделов; максимум — испортить FAT внутри p1,
что лечится `fsck`/пересозданием. Это осознанный компромисс.

### 5.4. Шпаргалка: RNDIS → образ → RNDIS вручную

Только для стенда, после T0, от root, **не по SSH через тот же RNDIS**
(сессия оборвётся) — с локального терминала или через Wi-Fi. Показывает
весь протокол в явном виде; helper делает то же самое с проверками.

```sh
G=/sys/class/android_usb/android0
L=$G/f_mass_storage/lun

# --- предусловия
cat $G/enable $G/functions $G/state
cat $G/factory_cable                     # 6000 = factory, дальше нельзя
mountpoint -q /media/sd && echo "SD смонтирована — сейчас не трогаем"
losetup -f                                # /dev/loop0
ls -l /var/lib/lg-usb/usbshare.img

# --- выключить и собрать образ
echo 0 > $G/enable; cat $G/enable         # ждём 0
losetup /dev/loop0 /var/lib/lg-usb/usbshare.img
echo mass_storage > $G/functions; cat $G/functions
echo 0 > $L/ro
echo /dev/loop0 > $L/file; cat $L/file
echo 1004 > $G/idVendor; echo 61fd > $G/idProduct
echo "LG P700 USB image" > $G/iProduct

# --- включить и проверить
echo 1 > $G/enable; cat $G/enable
i=0; while [ $i -lt 50 ]; do
    s=$(cat $G/state); [ "$s" = CONFIGURED ] && break
    i=$((i+1)); usleep 100000 2>/dev/null || sleep 0.1
done
echo "state=$s"

# --- откат (всегда возможен)
echo 0 > $G/enable
echo "" > $L/file
losetup -d /dev/loop0
sync
echo rndis > $G/functions
echo "$RNDIS_VID" > $G/idVendor; echo "$RNDIS_PID" > $G/idProduct
echo Android > $G/iProduct
echo 1 > $G/enable; cat $G/enable
ip link set rndis0 up
rc-service unudhcpd.rndis0 start
```

Если после `echo 1 > enable` в `cat enable` осталось `0` — `usb_add_config`
не смог; смотреть `dmesg | tail -30`, не повторять вслепую.


---

## 6. Остальные режимы

### 6.1. Charge-only

`functions=charge_only` — это **HID-заглушка** (`f_charge_only.c`, report
descriptor, 1 endpoint), сделанная LG специально, чтобы телефон
перечислялся на ICS+ хостах, но не поднимал RNDIS/ADB/storage.

- **Ток не меняется.** `CONFIG_USB_GADGET_VBUS_DRAW=500` → `bMaxPower=250`
  (ток порта) в конфигурации; порт ПК всё равно даёт не больше ток порта,
  зарядник — по своему DCP-детекту. `charge_only` не «ускоряет заряд».
- **Что реально даёт:** хост не устанавливает RNDIS/диск, меньше событий
  и драйверов, нет сетевого интерфейса и SSH, чище «оставь на ночь».
- **Настоящее «выключить гаджет»** — `enable=0`: устройство исчезает с шины,
  USB-прерываний нет, собственное потребление телефона минимально. Это
  отдельная галочка «Не показываться хосту» на экране зарядки.
  **[Т 0.5]** На строгом хосте, который реально ограничивает ток до
  конфигурации, `enable=0` может дать 100 мА вместо 500; на обычном ПК
  и на заряднике разницы нет. Поэтому «зарядка» в UI — это `charge_only`
  (перечисляется, получает свои ток порта), а `off` — для тишины и минимума
  прерываний, не «чтобы быстрее заряжалось».
- **Когда применять:** ночная зарядка, демонстрация «телефон просто
  заряжается», отладка «не мешает ли RNDIS сну/питанию».
- **Риск:** SSH по USB пропадает. Вернуть режим можно только с телефона
  (UI) или перезагрузкой. Поэтому **никогда** не делать charge-only
  режимом по умолчанию без локального UI и без запаса заряда.

### 6.2. Serial (консоль)

На LG `serial_transports` по умолчанию `"TTY"` (проверено в `android.c:708`),
поэтому `functions=serial` даёт:
- локально `/dev/ttyGS0`;
- на хосте CDC ACM (IAD + COMM + CDC_DATA): Linux `cdc_acm` → `/dev/ttyACM0`,
  Windows 10+ → «USB Serial Device (COMx)».

**Запуск getty (только по явному действию, не в inittab):**

```sh
setsid /sbin/getty -L 115200 ttyGS0 vt100 \
    </dev/ttyGS0 >/dev/ttyGS0 2>&1 &
echo $! > /run/lg-usb/getty.pid
```

`-L` — local, без modem control (иначе getty ждёт DCD и молчит). Вход —
`user` / пароль, дальше `sudo`. Root напрямую — только если добавить
`ttyGS0` в `/etc/securetty`; **не рекомендую**: слабый пароль + кабель =
root у любого, кто подошёл.

**Безопасность:**
- Это полноценный вход в систему. В продукте — только «Лаборатория»,
  с подтверждением и напоминанием «выключить после использования».
- Не оставлять getty работать «на всякий случай»: это лишний демон и
  поверхность атаки.
- Хост видит COM-порт даже без getty; это нормально.

**Когда полезно:** RNDIS сломан, SSH не поднимается, X/плеер не стартуют —
последний шанс без перепрошивки. Проверить: `screen /dev/ttyACM0 115200`
(Linux) или PuTTY 115200 8N1 без flow control (Windows).

### 6.3. MTP / PTP

- Ядро: `functions=mtp` (или `ptp`) биндится, хост видит MTP/PTP-интерфейс,
  `/dev/mtp_usb` открыт для userspace. **Но** MTP — это не «драйвер диска»,
  а протокол с объектами, деревом, event-каналом; отвечать должен
  userspace-респондер (в Android — `Mtpd`/`MtpServer`).
- В Alpine/pmOS такого демона нет. Без него хост шлёт `OpenSession` и
  получает таймаут: «MTP device not responding», иногда с зависанием
  проводника.
- Написать свой респондер — отдельный проект (PTP-контейнеры, object tree,
  event endpoint, доступ к ФС при живом MTP). Не v1.
- PTP — то же самое, плюс нужен доступ к камере (её HAL нет).
- **Вывод:** в UI — серая строка «MTP: нужен респондер, недоступно»,
  в код не включать. Файлы — `scp` по SSH, SD или mass storage.

### 6.4. ADB

- Ядро готово: `functions=adb` + misc `/dev/android_adb` (10:27).
- `adbd` в системе нет; современный adbd из `android-tools` работает через
  FunctionFS (`/dev/usb-ffs/adb`), которого в этом ядре нет
  (`CONFIG_USB_CONFIGFS`/`F_FS` не для этого гаджета). Старый adbd
  (Android 4.0–4.1), который умел `/dev/android_adb`, пришлось бы портировать
  и всё равно кормить Android-пропертями.
- Без adbd хост видит «Android ADB Interface» и `adb devices` пуст.
- **Вывод:** не включать. SSH даёт то же самое (shell, файлы, порты),
  и уже работает.

### 6.5. diag, rmnet, mbim, accessory, audio_source, cdrom

| Функция | Что это | Вердикт |
|---|---|---|
| `diag` | QCDM/diag-канал модема на хост (QPST/LG tools) | ядро есть, протокол LG не разобран; потенциально полезно для модема, но не в UI и не сейчас |
| `rmnet`, `rmnet_smd`, `rmnet_sdio`, `rmnet_smd_sdio` | WWAN-интерфейс модема | нет SIM/данных; `CONFIG_MSM_SMD_QMI=n`; бессмысленно |
| `mbim` | MBIM-модем | то же |
| `accessory` | Android Open Accessory (телефон — аксессуар для хоста) | нужен AOA-хост и приложение; не нужно |
| `audio_source` | телефон как USB-источник звука (UAC) | технически интересно (MPD → ПК), но нужен userspace-маршрут ALSA; позже |
| `cdrom_storage` | CD-ROM LUN (lun2), напр. ISO с README | экзотика; Windows autorun отключён; не v1 |

---

## 7. UI: экран «USB» (S2.2 в терминах `SETTINGS-SPEC.md`)

### 7.1. Место в дереве

- S2 «Сеть», строка 6 сейчас: «USB-сеть (RNDIS)» + switch.
  **Меняем** на drill-in: «USB» → значение «RNDIS · <device-ip>» → S2.2.
- S2.2 «USB», подпись шапки `РЕЖИМ ПОРТА · <MODE>`.
- В `SETTINGS-SPEC.md` §2.3 добавить S2.2, в §2.4 — спецификацию ниже;
  формулировку §1.2 «MTP нет и не будет» уточнить: «функция в ядре есть,
  userspace-респондера нет» (см. §6.3).
- Статус-строка (32 px) уже имеет место под USB-индикатор: точка
  `accent`, если `state=CONFIGURED`, `dim`, если нет.

### 7.2. Макет 480×800

```
┌──────────────────────────────────────────────┐ 32  статус-строка
│ ‹  USB                         РЕЖИМ ПОРТА  │ 56  шапка
├──────────────────────────────────────────────┤
│ Сейчас        RNDIS · <device-ip>            │ 44  info
│ Хост          подключён · USB 2.0 HS         │ 44  info
│ Питание       USB-порт ПК · ток порта · без АКБ │ 44  info (warn)
│ РЕЖИМ                                        │ 22  метка
│ ◉ RNDIS            SSH <device-ip> · обычно  │ 56  radio
│ ○ SD-карта         29,5 ГБ · MPD остановится │ 56  radio
│ ○ Образ 256 МБ     SD останется в системе    │ 56  radio
│ ○ Только зарядка   данных нет · SSH пропадёт │ 56  radio
│ ○ Консоль (serial) ttyGS0 ↔ COM · отладка    │ 56  radio
│ ПОДРОБНЕЕ                                    │ 22  метка
│ Скорость и ID      <rndis-vid>:<rndis-pid> · high speed    │ 44  info
│ Что это значит     тап → шторка              │ 44  info
├──────────────────────────────────────────────┤
│ [ Откат через 27 с ]            [ Оставить ] │ 44  баннер (только в ARMED)
└──────────────────────────────────────────────┘
```

Высота: 56 + 3×44 + 22 + 5×56 + 22 + 2×44 = **600**; остаётся 112 px
(баннер 44 + футер 22 + запас). Всё нажимаемое — ≥44; текст ≥11;
анимаций нет, счётчик перерисовывается раз в секунду (только цифра).

### 7.3. Строки режимов

| Строка | Подпись 11 `dim` | Что делает |
|---|---|---|
| RNDIS | «SSH <device-ip> · по умолчанию» | `functions=rndis`, `<rndis-vid>:<rndis-pid>` |
| SD-карта | «29,5 ГБ · MPD остановится» | `mass_storage` + `<sd-device-partition>`, RO по умолчанию |
| Образ 256 МБ | «обменник на eMMC · SD остаётся» | `mass_storage` + loop на `usbshare.img` |
| Только зарядка | «данных нет · SSH пропадёт» | `charge_only` (+ галочка «не показываться хосту» = `enable=0`) |
| Консоль (serial) | «ttyGS0 ↔ COM · для отладки» | `serial` + getty по подтверждению |

MTP/PTP/ADB в списке **не показываем** — в «Лаборатории» серая строка
«MTP/PTP/ADB: нет userspace-респондера» с пояснением.

### 7.4. Подтверждения и авто-откат

**Диалог 400×220** (затемнение 74 %, две кнопки 160×48):

- **SD-карта:** «Отдать SD карту?» / «SD отключится от телефона: музыка и
  файлы станут недоступны. MPD остановится; библиотека и обложки останутся.
  На ПК извлекайте только через “Безопасное извлечение”.» → «Отдать» /
  «Отмена».
- **Только зарядка:** «SSH и файлы по USB пропадут. Быстрее заряжаться не
  станет: порт ПК всё равно ток порта.» → «Включить» / «Отмена».
- **Консоль:** «Любой, кто подключит кабель, получит вход в систему
  (логин + пароль). Только для отладки.» → «Включить» / «Отмена».
- **Возврат из SD:** «Вернуть SD в систему? Музыка снова станет доступна,
  MPD запустится.» → «Вернуть» / «Отмена».

**Шторка (sheet) 480×(40+56·N):** «Что это значит» — 3–4 строки по режиму,
без действий; «Образ» — размер, свободное место, кнопка «Пересоздать…»
(danger, с подтверждением).

**Авто-откат 30 с (watchdog):**

1. После `ENABLE` показываем баннер «Откат через 30 с» + «Оставить».
2. Если `state=CONFIGURED` — текст «Хост подключён · Оставить», таймер
   продолжает идти (человек должен подтвердить).
3. «Оставить» → `KEPT`, watchdog снимается.
4. Таймер истёк → `ROLLBACK` → RNDIS, SD возвращается, MPD стартует.
5. Для SD-режима авто-откат обязателен даже при `CONFIGURED`: иначе
   телефон останется без музыки, если человек ушёл.
6. Для charge-only/serial — тот же таймер; для RNDIS — не нужен.
7. Кнопка «Ещё 30 с» продлевает таймер (медленный хост/установка драйвера);
   после `KEPT` в шапке S2.2 и в шторке остаётся строка-состояние
   «SD отдана хосту · Вернуть» — возврат не должен требовать «вспомнить,
   где это было».

**Статус «хост забрал / не забрал»:**

| Режим | Признак |
|---|---|
| RNDIS | `state=CONFIGURED` **и** carrier на `rndis0` (`ip -br link`) |
| SD | `state=CONFIGURED`; I/O — дельта `read sectors`/`write sectors` в `/sys/block/mmcblk1/stat` |
| Образ | `state=CONFIGURED`; I/O — дельта `/sys/block/loopN/stat` |
| Зарядка | `state=CONFIGURED` (хост перечислил HID); данных и сети нет |
| Гаджет off | `state=DISCONNECTED`, `enable=0` — «телефон не виден хосту» |

Если `state=DISCONNECTED` через 5 с — «хост не забрал»: кабель, порт,
спящий хост, хаб. Не считать это ошибкой режима; таймер всё равно
вернёт RNDIS.

### 7.5. Предупреждения (тексты)

- SD: «SD отключится; MPD остановится; SSH по RNDIS пропадёт».
- Образ: «Изменения на ПК сохраняются в файл; вынимайте через “Безопасное
  извлечение”».
- Зарядка: «SSH пропадёт; ток не вырастет; вернуть — только с телефона».
- Консоль: «Это вход в систему; выключите после использования».
- Питание без АКБ: «При недостаточном питании большие копии могут
  перезагрузить телефон» (проверять `batt_volt_raw=0`).
- При `factory_cable=6000`: «Factory mode: переключение заблокировано
  ядром» — режимы не показывать.

### 7.6. Ошибки в UI

| Код | Текст | Что делать |
|---|---|---|
| `EBUSY_FUNCTIONS` | «Гаджет не выключился» | повторить `enable=0`, иначе ребут |
| `EBUSY_UMOUNT` | «SD занята: <pid> <comm>» | «Остановить MPD и повторить» |
| `ENOSPC` | «Мало места для образа» | уменьшить размер/почистить |
| `EACCES_LUN` | «Не удалось открыть носитель» | проверить `ro`, права, mount |
| `NO_HOST` | «Хост не забрал режим» | кабель/порт; откат |
| `FACTORY` | «Factory mode» | выйти из factory-кабеля |
| `REBOOT_REQUIRED` | «Нужна перезагрузка» | не трогать SD, ребут |

---

## 8. Дизайн helper'а `/usr/local/bin/lg-usb-mode`

### 8.1. Интерфейс

```
lg-usb-mode status [--json]        # текущий режим, state, хост, питание
lg-usb-mode list                   # доступные режимы и причины отказа
lg-usb-mode set <mode> [--detach] [--timeout N] [--keep]
lg-usb-mode confirm                # снять watchdog (KEPT)
lg-usb-mode rollback [--reason X]  # принудительный откат в RNDIS
lg-usb-mode recover                # boot-восстановление
lg-usb-mode selftest               # read-only проверки (см. §9.1)
```

Режимы: `rndis`, `ms-sd`, `ms-img`, `charge`, `off`, `serial`.

`--detach` — double-fork + `setsid`, чтобы процесс пережил обрыв SSH
(при переключении RNDIS → MS сессия рвётся). UI вызывает без `--detach`
(он и так локальный), CLI по SSH — с `--detach`.

### 8.2. Файлы

| Путь | Назначение | Права |
|---|---|---|
| `/usr/local/bin/lg-usb-mode` | сам helper (POSIX sh) | `0750 root:root` |
| `/etc/lg-usb.conf` | конфиг (см. §8.3) | `0644` |
| `/run/lg-usb/lock` | `flock` | `0600` |
| `/run/lg-usb/state` | `key=value`: mode, phase, prev, pid, deadline, boot_id, keep, sd_was_mounted, mpd_was_running | `0600` |
| `/run/lg-usb/getty.pid` | PID getty (serial) | `0600` |
| `/var/log/lg-usb.log` | лог, ротация 256 КБ × 2 | `0600` |
| `/var/lib/lg-usb/usbshare.img` | образ | `0600` |

`/run` — tmpfs: после ребута состояния нет, recover видит «чистый лист».

### 8.3. Конфиг

```ini
default_mode=rndis
allow_modes=rndis,ms-sd,ms-img,charge,off,serial
auto_revert_s=30
image=/var/lib/lg-usb/usbshare.img
image_mb=256
ms_ro=1
serial_getty=0
require_confirm=1
```

`default_mode` — только `rndis` в v1. `charge`/`off` по умолчанию запрещены:
потеря SSH без локального UI.

### 8.4. Lock и watchdog

- **Lock:** `exec 9>/run/lg-usb/lock; flock -n 9 || exit 3` (код «занято»).
  В state — PID и `boot_id`; если PID мёртв или `boot_id` другой — lock
  считается протухшим (для `recover`).
- **Watchdog:** после `VERIFY` helper запускает отдельный процесс
  `setsid sh -c 'sleep $auto_revert_s; ...'`, который читает state:
  - `phase=ARMED` и `keep=0` → вызывает `lg-usb-mode rollback --reason timeout`;
  - `phase=KEPT` → выходит;
  - state нет → выходит.
  Watchdog — **отдельный** процесс: падение helper'а (OOM, kill) не отменяет
  откат.
- **Двойной запуск:** второй `set` получает «занято» и показывает PID.

### 8.5. Идемпотентность и восстановление

- `rollback` можно звать сколько угодно: каждая операция проверяет
  read-back и пропускает уже выполненные шаги.
- `recover` (boot): проверить `factory_cable`; если `functions != default`
  и `enable=1` — `enable=0`, `functions=default`, `enable=1`; проверить
  SD (`mountpoint` или `mount /media/sd`), `rc-service unudhcpd.rndis0 start`,
  удалить протухший lock, записать строку в лог.
- **OpenRC:** не отдельный сервис, а `/etc/local.d/aa-lg-usb.start`
  (лексикографически раньше `mpd.start`), чтобы SD была смонтирована
  до старта MPD. Скрипт вызывает `recover` и выходит.
- **Initramfs:** не трогаем. Sysfs гаджета не персистентен; после ребута
  ядро само поднимает дефолт. Единственное, что нужно, — смонтировать SD
  (это делает `fstab`) и не стартовать MPD раньше времени.

### 8.6. Логирование

Одна строка на событие:

```
2026-11-24T21:40:12Z set ms-img from=rndis pid=4321
2026-11-24T21:40:12Z disable rc=0 enable=0 state=DISCONNECTED (0.04s)
2026-11-24T21:40:12Z functions=mass_storage readback=mass_storage
2026-11-24T21:40:12Z lun ro=0 file=/dev/loop0
2026-11-24T21:40:12Z enable rc=0 enable=1 state=CONNECTED
2026-11-24T21:40:13Z state=CONFIGURED (0.8s) host=took
2026-11-24T21:40:43Z confirm keep=1
```

Плюс при `set`: `df`, `mount`, `mpc status`, `batt_volt_raw`, `ac/usb online`,
`factory_cable` — чтобы разбор инцидента был без догадок.

### 8.7. Псевдокод `set`

```
set(mode):
  lock || die BUSY
  cfg load; allow(mode) || die DENIED
  preflight(mode) || die <code>
  prev = read_mode()
  save_state(prev, phase=PREFLIGHT)
  if leaving_ms(prev): require_host_ejected(); lun_file=""
  if mode==ms-sd: stop_mpd(); sync; umount_sd() || die EBUSY_UMOUNT
  phase=DISABLE; write enable=0; wait readback 5s || die EBUSY_FUNCTIONS
  phase=CONFIGURE
    write functions=list(mode); verify set
    if ms: write ro; write file; verify
    write vid/pid/iproduct
    if serial: write transports=TTY
  phase=ENABLE; write enable=1; verify 5s || die ENABLE_FAILED
  phase=VERIFY; wait state 5s
  post(mode)
  phase=ARMED; deadline=now+auto_revert_s; spawn watchdog
  print status
```

---

## 9. Тесты

### 9.1. T0 — read-only baseline (можно делать сейчас)

```sh
# на телефоне (ash не понимает {a,b} — перечисляем явно)
for a in enable functions state idVendor idProduct; do
    printf '%s=' "$a"; cat "/sys/class/android_usb/android0/$a"
done
for a in file ro nofua cdrom; do
    printf 'lun.%s=' "$a"; cat "/sys/class/android_usb/android0/f_mass_storage/lun/$a"
done
ls -l /dev/android_adb /dev/mtp_usb /dev/ttyGS0 2>&1
ip -br addr show rndis0
df -h / /media/sd
mpc status
zcat /proc/config.gz | grep -E 'USB_G_ANDROID|VBUS_DRAW|BLK_DEV_LOOP|VFAT|CONFIGFS'
```

Ожидаемо: `1 / rndis / CONFIGURED / <rndis-vid> / <rndis-pid>`, пустой `lun/file`,
`rndis0 UP <device-ip>`, свободное место соответствует конфигурации. Это точка возврата для
сравнения «до/после».

### 9.2. Матрица

| # | Хост | Действие | Ожидание | Откат |
|---|---|---|---|---|
| T1 | Linux | `set rndis` (no-op) | ничего не меняется, state CONFIGURED | — |
| T2 | Linux | `set ms-img`, 10 циклов | диск 256 МБ появляется, `enable=1`, state CONFIGURED | `rollback` |
| T3 | Windows | ms-img: записать 50 МБ, eject, снова | файл читается, `fsck.fat -n` чист | пересоздать образ |
| T4 | Linux | ms-img: `dd` чтение 100 МБ | скорость 8–15 МБ/с (HS), loop stat растёт | eject |
| T5 | Linux | `set charge` → `set rndis` | SSH рвётся, возвращается ≤10 с; ток не меняется | ребут |
| T6 | Linux | `set off` → ребут | телефон исчезает; после ребута RNDIS сам | — |
| T7 | Linux | `set serial`, getty, login | `/dev/ttyACM0`, вход `user` | `set rndis` |
| T8 | Linux | ms-sd RO: скопировать 100 МБ с карты | файлы читаются, `fsck.vfat -n` чист, MPD играет после | `rollback` |
| T9 | Windows | ms-sd RW: записать 1 файл, eject | файл на месте, fsck чист, `mpc update` видит | `fsck -a` вручную |
| T10 | Linux | выдернуть кабель в ms-img во время записи | helper видит «host lost», откат; `fsck -n` — ошибки ожидаемы | пересоздать образ |
| T11 | — | kill -9 helper в ARMED | watchdog откатывает в RNDIS | ребут |
| T12 | — | ребут в каждом режиме | RNDIS, SD смонтирована, MPD играет | — |
| T13 | — | 50 переключений | нет ошибок в dmesg, нет утечки lock/loop | ребут |

### 9.3. Хост-специфика

**Windows:**
```powershell
Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -like "USB\VID_18D1*" -or $_.InstanceId -like "USB\VID_1004*"}
Get-Disk | Format-Table Number,FriendlyName,Size,BusType
Get-Volume | Format-Table DriveLetter,FileSystemLabel,Size
Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*RNDIS*"}
```
Ожидаемо: RNDIS → сетевой адаптер с <device-ip>; ms → «Съемный диск»
(USBSTOR); charge → HID; serial → COMx. При смене режима смотреть
«Безопасное извлечение» и `pnputil /enum-devices /connected`.

**Linux:**
```sh
lsusb -v -d <rndis-vid>: 2>/dev/null | grep -E 'bInterfaceClass|bNumEndpoints'
lsusb -v -d 1004: 2>/dev/null | grep -E 'bInterfaceClass|bNumEndpoints'
dmesg -w | grep -E 'usb|sd|cdc_acm|rndis'
lsblk -o NAME,SIZE,FSTYPE,LABEL,TRAN
fsck.vfat -n /dev/sdX        # после eject, не во время
```

### 9.4. Питание

- С АКБ: большие копии (≥100 МБ) — норма.
- Без АКБ на порту ПК: перед копией `cat /sys/class/power_supply/battery/voltage_now`,
  после — снова; при ребуте `cat /proc/last_kmsg` (ожидаем «No errors
  detected» = просадка VBAT, `POWER-PLAN.md` §4).
- Рекомендация: ms-sd/ms-img на 500-мА порту без АКБ — только с яркостью
  ≤150 и `scaling_max_freq=600000`; лучше с АКБ.

### 9.5. Критерии успеха

1. Любой режим ставится за ≤5 с (ms-sd с umount — ≤10 с); для data-режимов
   `state` доходит до `CONFIGURED`, для `off` — `enable=0` и
   `state=DISCONNECTED`; хост видит ожидаемое устройство.
2. Откат в RNDIS работает 10/10, включая обрыв кабеля и kill helper'а;
   SSH возвращается ≤15 с.
3. ms-sd RO: 100 МБ скопированы, `fsck.vfat -n` чист, MPD играет после.
4. ms-img: 20 циклов запись/извлечение, `fsck` чист, образ не растёт
   сверх 256 МБ.
5. Ни одной записи в raw-разделы eMMC; единственная запись на eMMC —
   файл образа в ms-img, и только по явному действию. Ни одного ребута
   по вине helper'а за 50 переключений.

---

## 10. Риски и меры

| Риск | Уровень | Мера |
|---|---|---|
| Порча FAT на SD (RW) | **высокий** | RO по умолчанию; «Безопасное извлечение»; `sync`; `fsck -n`; RW только по подтверждению |
| Порча образа при выдёргивании | средний | loop + `blockdev --flushbufs`; `fsck`; образ — расходник |
| Экспорт не того раздела | **высокий** | whitelist: только `<sd-device-partition>` или файл в `/var/lib/lg-usb`; отказ на `mmcblk0*`, `ANDROID!`, размер <1 МБ/>64 ГБ |
| Экспорт смонтированного раздела | высокий | проверка `/proc/mounts` + `fuser`; отказ, если mountpoint |
| Потеря SSH (charge/off/MS) | средний | авто-откат 30 с; ребут возвращает RNDIS; serial как rescue; не делать charge/off дефолтом |
| `enable=0` завис | средний | `timeout 5`, 2 попытки, затем `REBOOT_REQUIRED`; не циклиться |
| «Тихо не сработало» | высокий | read-back после каждой записи; `factory_cable != 6000` |
| Windows кэширует VID/PID | средний | уникальный PID на режим; при глюках — «Удалить устройство» в диспетчере |
| Просадка VBAT без АКБ | средний | предупреждение в UI; лимиты яркости/частоты; АКБ/зарядник для больших копий |
| MPD «потерял» библиотеку | низкий | база и обложки на eMMC; после возврата SD — `mpc update` только при RW |
| unudhcpd в цикле рестартов | низкий | netdev не удаляется; сервис не трогаем без нужды, проверяем после rollback |
| MTP/adb «висит» у хоста | средний | не включать; серые строки в UI |
| `getty` без пароля | высокий | getty + login, не auto-shell; только по подтверждению; выключать после |
| Factory mode | низкий | отказ по `factory_cable=6000` |

**Brick-риски:** нет. Гаджет — runtime-состояние ядра; sysfs не пишется
во flash; загрузчик, boot-раздел, EFS и rootfs не затрагиваются. Худший
исход — «телефон без SSH до ребута» и испорченный FAT, а не «не грузится».
Единственная запись на eMMC — файл образа, и только по явному действию.

**Чего не делать:**

1. Не отдавать смонтированный раздел (ядро не заблокирует — см. §1.2).
2. Не экспортировать `mmcblk0*` (rootfs/boot/modem) — только `<sd-device-partition>`
   или файл в `/var/lib/lg-usb`.
3. Не писать `ro` после `file` (будет `-EBUSY`), не писать `functions`
   при `enable=1` (тоже `-EBUSY`).
4. Не включать MTP/PTP/adb «просто посмотреть»: хост повиснет в таймаутах,
   а на телефоне не будет ни ответа, ни ошибки.
5. Не делать `charge`/`off` режимом по умолчанию без локального UI:
   это единственный способ потерять SSH до ребута.
6. Не выдёргивать кабель во время RW-сеанса и не считать `sync` на телефоне
   заменой «Безопасному извлечению» на хосте.
7. Не запускать `fsck.vfat -a` автоматически и не «чинить» SD без бэкапа.
8. Не трогать initramfs ради USB: sysfs не персистентен, ребут и так
   возвращает RNDIS; initramfs-хук — лишний риск.
9. Не менять baseband/EFS в рамках USB-задач (это `MODEM-PLAN.md`, другой
   риск-профиль).

---

## 11. Три вывода

1. **Ядро готово ко всем режимам, кроме тех, где нужен userspace.**
   RNDIS, mass storage (SD/образ), charge-only, serial и «гаджет off»
   реализуются тремя sysfs-записями и работают на этом ядре сегодня.
   MTP/PTP/adb упираются не в конфиг, а в отсутствие респондера/adbd;
   их нельзя «включить» — их можно только написать, и это отдельный проект.

2. **Опасность режима — не в USB, а в файловой системе и в потере SSH.**
   `f_mass_storage` открывает блочное устройство без эксклюзивного захвата,
   поэтому ядро не помешает отдать смонтированный раздел; `fsg_lun_close`
   не делает `fsync`; у LUN нет `forced_eject`. Отсюда жёсткие правила:
   RO по умолчанию, проверка `mount`, «хост извлёк» руками, `sync`/flush,
   `fsck -n`, auto-rollback и ребут как последний откат.

3. **UI должен продавать не «режимы», а последствия.** Пользователю нужны
   две вещи: «что сейчас» (state, хост, питание) и «что я теряю» (SD
   отключится, MPD встанет, SSH пропадёт). Поэтому экран строится вокруг
   статуса и подтверждений, а 30-секундный откат — обязательная часть
   любого опасного переключения, а не «фича для разработчика».

## Первый безопасный шаг

**Снять T0 (read-only) и реализовать только `lg-usb-mode status`/`selftest`,
без единой записи в sysfs.** Это фиксирует точку возврата (текущие
`enable=1`, `functions=rndis`, `<rndis-vid>:<rndis-pid>`, пустой LUN, свободное место)
и даёт UI честный источник данных. Первое реальное переключение — после
этого, на стенде, в порядке: **RNDIS → образ 256 МБ → RNDIS** (SD не
трогаем), с проверкой auto-rollback и обрыва кабеля. Отдача SD — последней,
когда образ и откат уже доказаны.
