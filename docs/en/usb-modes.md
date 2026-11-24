# USB-MODES — LG P700 USB port modes (u0)

> Public technical plan. Network addresses and local credentials have been
> anonymized; check your system parameters before running the examples.

> Test date: 24.11.2026. Board: LG Optimus L7 P700 (MSM7227A), pmOS/Alpine,
> kernel `3.4.0-CM #23-postmarketOS`. Gadget is **legacy Android**
> (`CONFIG_USB_G_ANDROID=y`), **not configfs**.
>
> This document is based on three sources: (1) read-only probe of the live
> device over SSH on 24.11.2026 (no writes to `/sys`, modes not switched);
> (2) kernel sources `TeamHackLG/lge-kernel-lproj @ 45d6b0c` (locally
> `<local-kernel-research-path>`); (3) `SYSTEM-STATUS.md`, `SYSTEM-RESEARCH.md`,
> `MODEM-PLAN.md`, `POWER-PLAN.md`, `SETTINGS-SPEC.md`.
>
> Legend: **[F]** fact (live probe/code), **[T]** theory + confidence,
> **[C]** check from the test plan.

---

## 0. TL;DR

1. The kernel supports **everything** needed: RNDIS, mass storage (SD and
   image file), charge-only, serial (CDC ACM), MTP, PTP, adb, diag, rmnet.
   The limitation is **userspace**: MTP/PTP have no responder, and adb has no
   adbd. This is not “not enabled in the kernel” but “there is nobody to respond.”
2. Switching uses three sysfs writes with mandatory read checks:
   `enable=0` → `functions=<list>` → `enable=1`. Both writes may **silently
   fail** (`functions` ignores unknown names, `enable` returns success if
   `usb_add_config` fails), so read-back is not a courtesy; it is part of the
   protocol.
3. Mass storage is the only mode with a real risk of data loss (FAT on SD/in
   the image). Therefore: SD is **read-only** by default, image is **256 MB
   on eMMC via loop** (with I/O statistics and explicit flush), a person
   confirms “host ejected,” and run `fsck.vfat -n` after every RW session.
4. No brick risk: gadget sysfs is **not persistent**; after reboot the kernel
   brings `rndis` up again, and the boot chain is not affected. The risks are
   data on SD/in the image and loss of SSH, not “bricking.”
5. Do **not** release MTP/PTP/adb in v1: the host will see the device and wait
   for a response that never comes; this is worse than an honest “unavailable.”

---

## 1. Verified facts (read-only, 24.11.2026)

### 1.1. Live probe

| What | Value | Comment |
|---|---|---|
| `android0/enable` | `1` | gadget is up |
| `android0/functions` | `rndis` | delimiter is a **comma** |
| `android0/state` | `CONFIGURED` | host selected the configuration |
| `android0/idVendor` | `<rndis-vid>` | Google VID (not LG `1004`) |
| `android0/idProduct` | `<rndis-pid>` | current RNDIS PID |
| `iProduct` / `iManufacturer` | `Android` / `Android` | strings presented to host |
| `iSerial` | `<serial-placeholder>` | placeholder |
| `bDeviceClass` | `0` | composite, classes are on interfaces |
| `rndis0` | `UP`, `<device-ip>/16`, MTU 1500 | address lives in netdev |
| `/dev/mtp_usb` | `crw------- 10:24` | created at probe; does **not** mean “MTP works” |
| `/dev/android_adb` | `crw------- 10:27` | created at probe; no adbd |
| `/dev/ttyGS0` | **absent** | serial has never been enabled |
| `f_mass_storage/lun` | symlink → `.../gadget/lun0` | LUN is **always present** |
| `lun0/{file,ro,nofua,cdrom}` | `` / `0` / `0` / `0` | empty `file` = no media |
| Root filesystem | check free space; 256 MB image needs extra headroom | |
| SD | `<sd-device>`, vfat, capacity depends on card | UUID `<sd-uuid>`, `/media/sd` |
| MPD | `db_file=/var/lib/mpd/tag_cache` (eMMC), `auto_update yes` | database and artwork are **not on SD** |
| `unudhcpd.rndis0` | `supervise-daemon`, server `<device-ip>`, client `<host-ip>` | in runlevel `default` |

Tools on the phone (verified): `flock`, `timeout`, `fuser`, `lsof`,
`losetup`, `mkfs.fat`, `dosfsck`/`fsck.vfat`, `mpc`, `sync` — all present.
Packages `dosfstools`, `util-linux` are installed.

### 1.2. Confirmed by source code

| Fact | Where | Consequence |
|---|---|---|
| `functions` uses a **comma** (`strsep(&b, ",")`) | `android.c:1713` | `mass_storage,rndis`, not a space |
| `functions` while `enabled` → `-EBUSY` | `android.c:1680` | first set `enable=0` |
| Unknown name: `pr_err`, but store returns `size` | `android.c:1717` | **read-back is mandatory** |
| `enable=1` when `android_enable()` fails returns `size`, but `enabled=false` | `android.c:1792` | **read-back is mandatory** |
| `state` = `DISCONNECTED` / `CONNECTED` / `CONFIGURED` | `android.c:1843` | only honest indication that “host selected it” |
| All `f->init` calls happen at probe, not bind | `android.c:1518` | LUN and `/dev/mtp_usb` exist from boot |
| `lun/file` can be written while gadget is disabled | `storage_common.c:1014` | media is ready **before** `enable=1` |
| `ro` changes only while LUN is closed (`-EBUSY`) | `storage_common.c:963` | set `ro` before `file` |
| `file=""` = eject (no `forced_eject`) | `storage_common.c:1014` | eject only by writing an empty string |
| `fsg_lun_open` uses `filp_open(O_RDWR)`, **without** `FMODE_EXCL` | `storage_common.c` | a mounted partition **will not** be locked → check yourself |
| `fsg_lun_close` = `fput`, without `fsync` | `storage_common.c` | after eject, `sync`/`blockdev --flushbufs` is needed |
| `rndis_disable` → `gether_disconnect`: netdev is **not** removed | `u_ether.c` | `rndis0` and IP survive switching |
| `charge_only` is an HID placeholder, not “no device” | `f_charge_only.c` | does not change current draw; host still sees HID |
| LG `serial`: `transports="TTY"` by default | `android.c:708` | local `/dev/ttyGS0` + CDC ACM on host |
| `adb` is misc `android_adb` | `f_adb.c:35` | kernel is ready; no adbd |
| `mtp` is misc `mtp_usb`, bind without userspace | `f_mtp.c:1216` | host sees MTP and waits forever |

### 1.3. Not present in the kernel

- `CONFIG_USB_CONFIGFS` — **absent**. Online instructions using
  `/sys/kernel/config/usb_gadget/...` do not apply.
- `CONFIG_USB_MASS_STORAGE` (`g_mass_storage`) — **absent**. `modprobe
  g_mass_storage` and module `file=` do not exist; only `f_mass_storage`
  inside the Android gadget is available.
- `CONFIG_USB_OTG` — **absent** (confirmed by `POWER-PLAN.md`), device-only port.
- `CONFIG_USB_G_SERIAL` — **absent**, but `serial` is part of `G_ANDROID`.

---

## 2. Architecture

```
 graphite-shell (root, locally on phone)
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
         ├─ host activity: /sys/block/mmcblk1/stat, /sys/block/loopN/stat
         ├─ SD: mount/umount /media/sd, fuser/lsof, fsck.vfat -n
         ├─ MPD: mpc stop, pkill mpd, /etc/local.d/mpd.start
         └─ network: ip link rndis0, rc-service unudhcpd.rndis0
                  │
         kernel: android composite (G_ANDROID) → msm_hsusb UDC → cable → host
```

Key feature: **sysfs is the source of truth, not a settings file**.
`/etc/lg-usb.conf` stores only “what to bring up at boot” and limits; the
current mode is always read from `functions` + `state`.

### 2.1. Helper states

```
IDLE ──set──► PREFLIGHT ──► DRAIN ──► DISABLE ──► CONFIGURE ──► ENABLE ──► VERIFY
                   │           │          │            │           │          │
                   └───────────┴──────────┴────────────┴───────────┴──► FAILED ──► ROLLBACK
                                                                           │
VERIFY ──► ARMED (30 s) ──confirm──► KEPT                                │
               │                                                          │
               └──────────────── timeout / error ────────────────────────┘
```

`ROLLBACK` is always idempotent and always returns to **RNDIS** (the safe
state), not the “previous mode”: the previous mode may have been mass storage
with the SD already handed to the host.

---

## 3. Switching protocol (legacy Android gadget)

### 3.1. Attributes

| Attribute | Path | Values | Required | When written |
|---|---|---|---|---|
| `enable` | `android0/enable` | `0`/`1` | yes | first and last |
| `functions` | `android0/functions` | `rndis`, `mass_storage`, `charge_only`, `serial`, … comma-separated | yes | only when `enable=0` |
| `idVendor`/`idProduct` | `android0/…` | hex, e.g. `<rndis-vid>`/`<rndis-pid>` | recommended | before `enable=1` |
| `iProduct` | `android0/iProduct` | string ≤ 126 | recommended | before `enable=1` |
| `state` | `android0/state` | ro | — | read afterward |
| `lun/file` | `f_mass_storage/lun/file` | block/file path; `""` = eject | for MS | when `enable=0` |
| `lun/ro` | `f_mass_storage/lun/ro` | `0`/`1` | for MS | **before** `file` |
| `lun/nofua` | `f_mass_storage/lun/nofua` | `0`/`1` | no | leave at `0` (honest FUA/FLUSH) |
| `lun/cdrom` | `f_mass_storage/lun/cdrom` | `0`/`1` | no | do not touch |
| `f_serial/transports` | `android0/f_serial/transports` | write-only, `TTY` | for serial | before first bind |
| `factory_cable` | `android0/factory_cable` | ro, `6000` = factory | — | check: factory mode ignores writes |

`lun` is a symlink to `lun0`; writes through either path are equivalent.

### 3.1.1. VID/PID by mode (recommendation)

Current `<rndis-vid>:<rndis-pid>` are not kernel defaults (defconfig has
`1004:61fc`); they were set by some userspace/diagnostics component (exact
source unconfirmed). **Do not inherit them:** helper explicitly writes VID/PID
before every `enable=1`. A unique PID per mode is needed so Windows does not
reuse a cached instance with a different interface set.

| Mode | VID:PID | Why |
|---|---|---|
| RNDIS (and rollback) | `<rndis-vid>:<rndis-pid>` | already verified on this host; SSH works |
| MS SD | `1004:61fc` | LG VID, mass-storage class; Windows installs USBSTOR |
| MS image | `1004:61fd` | separate PID to distinguish in `lsusb`/device manager |
| Charge-only | `1004:61fe` | HID placeholder |
| Serial | `1004:61ff` | CDC ACM |
| MTP/PTP/ADB (do not release) | `<rndis-vid>:d002`/`d003`/`d004` | experiments only |

Host check: `lsusb -d 1004:` (Linux), `Get-PnpDevice` (Windows).
If Windows shows “Code 10” after a mode change, select “Uninstall device” in
Device Manager and reconnect the cable.

### 3.2. Exact order

```
0) preflight (see §8.7): root, sysfs, factory_cable != 6000, mode allowed,
   for SD: partition is unmounted or can be unmounted,
   for image: file exists/is created, free space ≥ size+150 MB
1) DRAIN:
   - when leaving MS: host has already ejected (confirmation), then
     echo "" > .../lun/file          # eject, close media
   - when handing over SD: mpc stop; pkill -TERM mpd; wait ≤3 s; sync;
     umount /media/sd  (fail → fuser -m, show holders, do not kill)
2) DISABLE: echo 0 > enable;  timeout 5;  read enable == 0
   (state is DISCONNECTED; host sees disconnect)
3) CONFIGURE:
   - echo "<list>" > functions;  read-back == expected set
   - echo 1 > ro (if MS and RO);  echo <path> > lun/file
   - echo <vid> > idVendor; echo <pid> > idProduct; echo "<name>" > iProduct
   - for serial: echo TTY > f_serial/transports (once per boot)
4) ENABLE: echo 1 > enable;  timeout 5;  read enable == 1
5) VERIFY: timeout 5 read state every 100 ms until CONNECTED/CONFIGURED
6) POST:
   - RNDIS: ip link set rndis0 up; rc-service unudhcpd.rndis0 start (idempotent);
     check carrier (ip -br link) and <device-ip>
   - MS: nothing (file has already been handed over)
   - serial: start getty on /dev/ttyGS0 (see §6.4)
7) ARM: watchdog 30 s (see §7.4)
```

**Why set `lun/file` before `enable=1`:** the LUN is created at probe, so
media can be opened in advance; during enumeration the host immediately sees
a ready disk, without “please insert a disk.” This also allows `file` to be
checked (e.g. `EACCES`, `EROFS`) **before** the host sees anything, allowing
rollback without bus noise.

### 3.3. Read-back is part of the protocol

After each write:

| Write | Check | If it does not match |
|---|---|---|
| `enable=0` | `cat enable` = `0` | 3 attempts 1 s apart; then `FAILED` |
| `functions=…` | set from `cat functions` == requested | `FAILED` + restore previous string |
| `lun/file=…` | `cat lun/file` == path | `FAILED` (common causes: `EACCES`, `ENOENT`, `EBUSY` on `ro`) |
| `enable=1` | `cat enable` = `1` | `FAILED` (`usb_add_config` failed) |
| — | `state` ∈ {CONNECTED, CONFIGURED} within 5 s | `ARMED`, but status “host did not select it” |

### 3.4. Timeouts (empirical, with margin)

| Step | Timeout | Why |
|---|---|---|
| `echo 0 > enable` | 5 s | `fsg_unbind` waits for the current SCSI command to finish |
| `echo <list> > functions` | 2 s | list only, no I/O |
| `echo … > lun/file` | 3 s | block-device/file `filp_open` |
| `echo 1 > enable` | 5 s | `usb_add_config` + `usb_gadget_connect` |
| wait for `state` | 5 s | Linux/Windows send SET_CONFIGURATION in 0.1–2 s; 5 s provides margin |
| `umount /media/sd` | 5 s | usually < 1 s after `mpc stop` |
| watchdog | 30 s | enough time for a person to see and press “Keep” |

### 3.5. Rollback on error

Any `FAILED` → `ROLLBACK`:

```
1) timeout 5 echo 0 > enable
2) echo "" > lun/file                # if MS was touched
3) echo rndis > functions            # read-back
4) echo "$RNDIS_VID" > idVendor; echo "$RNDIS_PID" > idProduct; echo Android > iProduct
5) timeout 5 echo 1 > enable; read-back
6) ip link set rndis0 up; rc-service unudhcpd.rndis0 start
7) if SD was mounted before operation: mount /media/sd; check;
   if MPD was running: /etc/local.d/mpd.start
8) log: reason, step, state before/after
```

If rollback fails (for example, `enable=0` hangs), **do not loop**:
write `REBOOT_REQUIRED` to the log, show “Reboot required” in the UI, and do
not touch SD. Reboot restores `rndis` (sysfs is not persistent), SD is mounted
by `fstab`, and MPD starts from `local.d`.

### 3.6. Pitfalls verified from code

1. `functions=mass_storage rndis` (space): the second “function” becomes
   `mass_storage rndis`, an unknown name; `pr_err`, **store succeeds**.
2. `functions=rndis,rndis`: `list_add_tail` twice, bind twice; helper
   deduplicates.
3. `enable=1` when already `1`: `pr_err "already enabled"`, no changes;
   helper does not write if read-back already matches.
4. `ro` after `file` → `-EBUSY`; only valid order is `ro` → `file`.
5. `file` for a **mounted** partition opens (no `FMODE_EXCL`); SD data will
   diverge. Checking `/proc/mounts` is mandatory.
6. `factory_cable=6000`: `functions`/`enable` writes are ignored, store
   returns success. Helper must refuse.
7. `rndis0` **remains** after `enable=0` (carrier off), IP is preserved;
   no need to run `ip addr add` again, but check `ip link set up` and `unudhcpd`.
8. `/dev/ttyGS0` **remains** after serial is first enabled; device presence
   does not mean the mode is active — read `functions`.

---

## 4. Mode table

| Mode / option | `functions` | What host sees | Readiness | Main risk |
|---|---|---|---|---|
| **RNDIS** (default) | `rndis` | RNDIS/CDC-Ether, `<rndis-vid>:<rndis-pid>`, DHCP `<host-ip>`, SSH `<device-ip>` | works | none |
| **SD card** | `mass_storage` | SCSI disk 29.5 GB, FAT (p1 as “superfloppy”) | requires `umount` + stop MPD | FAT corruption, MPD unavailable |
| **256 MB image** | `mass_storage` | SCSI disk 256 MB, FAT32, `/dev/loopN` | image created once | image only may be corrupted |
| **Charging only** | `charge_only` | HID device (placeholder), no data | works | SSH lost, current draw does **not** increase |
| **Gadget disabled** | — (`enable=0`) | nothing (device disappears) | works | can only restore locally/by reboot |
| **Console** | `serial` | CDC ACM: Linux `ttyACM0`, Windows COMx | requires getty | anyone with a cable can log in |
| MTP | `mtp` | MTP device | **no responder** | host hangs waiting for timeouts |
| PTP | `ptp` | PTP camera | **no responder** | same |
| ADB | `adb` | ADB interface | **no adbd** | “device is not responding” |
| diag | `diag` | QCDM/diag | LG protocol not analyzed | modem debugging, not for UI |
| rmnet / mbim | `rmnet*`, `mbim` | WWAN | no SIM/QMI | pointless currently |
| audio_source | `audio_source` | USB sound card (source) | no userspace route | potentially possible later |
| accessory | `accessory` | AOA | requires host AOA | not needed |
| cdrom_storage | `cdrom_storage` | CD-ROM (LUN2) | requires ISO | exotic (autorun) |

---

## 5. Mass storage

### 5.1. SD card: hand over `<sd-device>`

**Hard prerequisites:**
- `mountpoint -q /media/sd` or partition is unmounted;
- `<sd-device>` is a block device, size equals configured capacity, not `mmcblk0*`;
- MPD is stopped (not merely paused): `mpc stop; pkill -TERM mpd`;
- no holders: `fuser -m /media/sd`, `lsof +f -- /media/sd`;
- charge/battery: without a battery, large copies on a 500-mA port risk VBAT
  droop (`POWER-PLAN.md` §4); UI displays a warning.

**Steps:**

```sh
mpc stop 2>/dev/null
pkill -TERM mpd 2>/dev/null; sleep 1
sync
umount /media/sd || { fuser -vm /media/sd; exit 1; }
# gadget disabled, functions already mass_storage
# if previous media was open: echo "" > .../lun/file
echo 1 > /sys/class/android_usb/android0/f_mass_storage/lun/ro   # RO by default
echo <sd-device> > /sys/class/android_usb/android0/f_mass_storage/lun/file
echo 1 > /sys/class/android_usb/android0/enable
```

**What the host sees:** one SCSI disk (LUN0) with a FAT volume at the start.
Windows mounts a drive letter; Linux sees `/dev/sdX` (not `sdX1`: there is no
partition table; the partition itself is exported). This is normal and safer
than handing over the entire `mmcblk1` (the host cannot repartition the card).

**Read-only vs RW:**
- RO (`ro=1`) — default mode: backup, “copy music out,” browse files. The host
  physically cannot write; no `fsck` is needed afterward.
- RW (`ro=0`) — only with explicit confirmation. After the session, always run
  `fsck.vfat -n` (check only, without `-a`); on errors, offer a decision to
  the user; do not repair automatically.

**Eject (order matters):**
1. On the **host** — “Safely Remove” / `umount` / `eject`. Only the host can
   flush its cache; `sync` on the phone does not replace this.
2. In the UI — “Host ejected” (person confirms).
3. Phone: `echo "" > .../lun/file` (eject), `sync`.
4. `mount /media/sd` (or `mount -a`), check `mountpoint`.
5. `mpc update --wait` **only if** export was RW; then
   `/etc/local.d/mpd.start`.
6. Player: artwork and database are on eMMC (`/var/lib/mpd/tag_cache`,
   `/var/lib/lp7player/art`), so the library remains after SD is returned;
   tracks are accessible again, and `auto_update yes` will pick up changes.

**If `umount` fails:** show `fuser -vm` (who is holding it), offer “Stop MPD
and retry.” Do not run `fuser -km` automatically: an unrelated process could
be caught (for example, SSH with cwd on SD — unlikely, but the cost of an
error is high).

**If the host wrote and the cable was pulled:** FAT is almost certainly dirty.
`fsck.vfat -n` will show this; `-a` only via a “Repair” button with a warning
that automatic repair may lose files. Make an image/backup of important data
beforehand, not afterward.

### 5.2. `usbshare.img` image (safe alternative)

**Why an image, not SD:** SD stays in the system and MPD keeps playing; if the
cable is pulled, only the image file is damaged, and it can be recreated.

**Size:** 256 MB by default. 1004 MB is free; 512 MB also fits, but leaves
about 490 MB for logs/swap — allow only after checking “free space ≥ size +
150 MB.”

**Verified read-only:** `CONFIG_BLK_DEV_LOOP=y`,
`CONFIG_BLK_DEV_LOOP_MIN_COUNT=8`, `/dev/loop0…7` created; `mkfs.fat`
(dosfstools), `dosfsck`/`fsck.vfat`, `losetup` (util-linux 2.42) installed.
`CONFIG_CONFIGFS_FS` and `CONFIG_USB_MASS_STORAGE` are **not set**, so loop is
the only way to get a “real” block device with I/O statistics.

**Location:** `/var/lib/lg-usb/usbshare.img` on **eMMC** (rootfs), not SD
(otherwise the image is on the media being handed over — recursion and risk).

**Create (once):**

```sh
mkdir -p /var/lib/lg-usb
img=/var/lib/lg-usb/usbshare.img
[ -f "$img" ] || {
    tmp="$img.new"
    fallocate -l 256M "$tmp" || \
        dd if=/dev/zero of="$tmp" bs=1M count=256 || exit 1
    mkfs.fat -F 32 -n USBSHARE "$tmp" || { rm -f "$tmp"; exit 1; }
    mv "$tmp" "$img"          # atomic: unfinished image will not become “usable”
}
```

`mkfs.fat` is present (`dosfstools`) and works on a regular file. Alternative
without a package on the phone: create the image on a PC (`mkfs.fat` in WSL)
and upload it with `scp` — but the package is already installed, so that is
unnecessary.

**Export via loop (primary method):**

```sh
LOOP=$(losetup -f)                       # e.g. /dev/loop0
losetup "$LOOP" /var/lib/lg-usb/usbshare.img
echo 0 > .../lun/ro
echo "$LOOP" > .../lun/file
echo 1 > .../enable
```

**There is no need to mount the image on the phone** — `losetup` is sufficient:
`f_mass_storage` reads/writes the block device itself. Do not keep the image
mounted and handed to the host simultaneously (two independent caches →
corruption). To add files in advance: `losetup` → `mount -t vfat` → copy →
`umount` → `losetup -d`, and only then export.

**Why loop instead of a file directly:** `f_mass_storage` also supports a
regular file (`fsg_lun_open` supports `S_ISREG`), but loop provides:
- `/sys/block/loopN/stat` — indicates whether the host read/wrote (for UI “host ejected”);
- `blockdev --flushbufs /dev/loopN` — explicit flush of the image cache;
- familiar block-device path (512-byte sectors, `fsck` on the loop device).

Direct file is a fallback if `losetup` fails for some reason; then “host
activity” is not visible, and flush is `sync`.

**Permissions:** helper runs as root. `/dev/loop-control` is `root:disk 0660`,
`/dev/loop0…7` already exist (`CONFIG_BLK_DEV_LOOP_MIN_COUNT=8`); as user
`user`, `losetup -f` returns `Permission denied`, which is normal.

**Eject:**

```sh
echo "" > .../lun/file          # eject
sync
blockdev --flushbufs /dev/loopN 2>/dev/null
losetup -d /dev/loopN
```

**Cache and `nofua`:** leave `nofua=0` so FLUSH/FUA from the host reach the
image (slower, but correct). `nofua=1` is for speed tests only, with an
explicit warning. After an RW session: `fsck.fat -n
/var/lib/lg-usb/usbshare.img` (dosfsck supports files), or run it on the loop
device before `losetup -d`.

**Space and growth:** `fallocate` reserves space immediately; with `truncate`
the file is sparse and grows as the host writes — monitor `df /` in the UI.
The image is not a backup; it is a “transfer area,” not storage.

### 5.3. Why not hand over all of `mmcblk1`

The host will see the MBR and can repartition/format the card. Handing over
`p1` prevents changes to the partition table; at worst, FAT inside p1 can be
corrupted, which can be fixed with `fsck`/recreation. This is a deliberate
compromise.

### 5.4. Quick reference: RNDIS → image → RNDIS manually

For bench use only, after T0, as root, **not over SSH through the same RNDIS**
(the session will be disconnected) — use a local terminal or Wi-Fi. This
shows the full protocol explicitly; helper does the same with checks.

```sh
G=/sys/class/android_usb/android0
L=$G/f_mass_storage/lun

# --- prerequisites
cat $G/enable $G/functions $G/state
cat $G/factory_cable                     # 6000 = factory; stop here
mountpoint -q /media/sd && echo "SD is mounted — do not touch it now"
losetup -f                                # /dev/loop0
ls -l /var/lib/lg-usb/usbshare.img

# --- disable and prepare image
echo 0 > $G/enable; cat $G/enable         # wait for 0
losetup /dev/loop0 /var/lib/lg-usb/usbshare.img
echo mass_storage > $G/functions; cat $G/functions
echo 0 > $L/ro
echo /dev/loop0 > $L/file; cat $L/file
echo 1004 > $G/idVendor; echo 61fd > $G/idProduct
echo "LG P700 USB image" > $G/iProduct

# --- enable and check
echo 1 > $G/enable; cat $G/enable
i=0; while [ $i -lt 50 ]; do
    s=$(cat $G/state); [ "$s" = CONFIGURED ] && break
    i=$((i+1)); usleep 100000 2>/dev/null || sleep 0.1
done
echo "state=$s"

# --- rollback (always possible)
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

If `cat enable` remains `0` after `echo 1 > enable`, `usb_add_config` failed;
check `dmesg | tail -30`; do not blindly retry.

---

## Placeholder list

- `<rndis-vid>` — observed RNDIS vendor ID; anonymized.
- `<rndis-pid>` — observed RNDIS product ID; anonymized.
- `<serial-placeholder>` — anonymized serial string.
- `<device-ip>` — device-side network address; anonymized.
- `<host-ip>` — host-side network address; anonymized.
- `<sd-device>` — SD block-device path; anonymized.
- `<sd-uuid>` — SD filesystem UUID; anonymized.
- `<local-kernel-research-path>` — local kernel-source research path; anonymized.
- No password was present in this fragment.

## 6. Other modes

### 6.1. Charge-only

`functions=charge_only` is an **HID stub** (`f_charge_only.c`, report descriptor, 1 endpoint), made by LG specifically so the phone enumerates on ICS+ hosts without bringing up RNDIS/ADB/storage.

- **Current does not change.** `CONFIG_USB_GADGET_VBUS_DRAW=500` -> `bMaxPower=250` (port current) in the configuration; a PC port still supplies no more than its rated current, while a charger follows its own DCP detection. `charge_only` does not "speed up charging."
- **What it actually provides:** the host does not install RNDIS/disk, there are fewer events and drivers, no network interface or SSH, making it cleaner to "leave it overnight."
- **A true "turn gadget off"** is `enable=0`: the device disappears from the bus, there are no USB interrupts, and the phone's own consumption is minimal. This is a separate "Do not appear to host" checkbox on the charging screen. **[T 0.5]** On a strict host that actually limits current to the configuration, `enable=0` may provide 100 mA instead of 500; on an ordinary PC and a charger there is no difference. Thus "charging" in the UI means `charge_only` (enumerated, receives its port current), while `off` is for silence and minimum interrupts, not "to charge faster."
- **When to use:** overnight charging, demonstrating "the phone just charges," debugging whether RNDIS interferes with sleep/power.
- **Risk:** SSH over USB disappears. The mode can be restored only from the phone (UI) or by rebooting. Therefore, **never** make charge-only the default mode without a local UI and reserve charge.

### 6.2. Serial (console)

On LG, `serial_transports` defaults to `"TTY"` (verified in `android.c:708`), so `functions=serial` provides:
- locally `/dev/ttyGS0`;
- on the host CDC ACM (IAD + COMM + CDC_DATA): Linux `cdc_acm` -> `/dev/ttyACM0`, Windows 10+ -> "USB Serial Device (COMx)".

**Start getty (only on explicit action, not in inittab):**

```sh
setsid /sbin/getty -L 115200 ttyGS0 vt100 \
    </dev/ttyGS0 >/dev/ttyGS0 2>&1 &
echo $! > /run/lg-usb/getty.pid
```

`-L` means local, without modem control (otherwise getty waits for DCD and stays silent). Login is `user` / password, then `sudo`. Direct root access would require adding `ttyGS0` to `/etc/securetty`; **not recommended**: weak password + cable = root access for anyone who walks up.

**Security:**
- This is a full system login. In the product, this belongs only in "Laboratory," with confirmation and a reminder to "turn it off after use."
- Do not leave getty running "just in case": it is an unnecessary daemon and attack surface.
- The host sees the COM port even without getty; this is normal.

**When useful:** RNDIS is broken, SSH does not come up, X/player does not start -- last resort without reflashing. Check with `screen /dev/ttyACM0 115200` (Linux) or PuTTY at 115200 8N1 without flow control (Windows).

### 6.3. MTP / PTP

- Kernel: `functions=mtp` (or `ptp`) binds, the host sees an MTP/PTP interface, and `/dev/mtp_usb` is open to userspace. **But** MTP is not a "disk driver"; it is a protocol with objects, a tree, and an event channel. A userspace responder must handle it (`Mtpd`/`MtpServer` on Android).
- Alpine/pmOS has no such daemon. Without one, the host sends `OpenSession` and gets a timeout: "MTP device not responding," sometimes with the file manager hanging.
- Writing a responder is a separate project (PTP containers, object tree, event endpoint, filesystem access while MTP is active). Not v1.
- PTP is the same, plus it requires camera access (its HAL is absent).
- **Conclusion:** show a grey "MTP: responder required, unavailable" row in the UI; do not enable it in code. Files: `scp` over SSH, SD, or mass storage.

### 6.4. ADB

- Kernel is ready: `functions=adb` + misc `/dev/android_adb` (10:27).
- There is no `adbd` in the system; modern `adbd` from `android-tools` uses FunctionFS (`/dev/usb-ffs/adb`), which this kernel does not have (`CONFIG_USB_CONFIGFS`/`F_FS` is not for this gadget). Old adbd (Android 4.0--4.1), which supported `/dev/android_adb`, would need porting and would still need Android properties.
- Without adbd the host sees "Android ADB Interface" and `adb devices` is empty.
- **Conclusion:** do not enable. SSH provides the same capabilities (shell, files, ports) and already works.

### 6.5. diag, rmnet, mbim, accessory, audio_source, cdrom

| Function | What it is | Verdict |
|---|---|---|
| `diag` | QCDM/diag modem channel to host (QPST/LG tools) | kernel support exists, LG protocol not analyzed; potentially useful for modem, but not in UI and not now |
| `rmnet`, `rmnet_smd`, `rmnet_sdio`, `rmnet_smd_sdio` | modem WWAN interface | no SIM/data; `CONFIG_MSM_SMD_QMI=n`; pointless |
| `mbim` | MBIM modem | same |
| `accessory` | Android Open Accessory (phone is an accessory to the host) | requires an AOA host and application; not needed |
| `audio_source` | phone as USB audio source (UAC) | technically interesting (MPD -> PC), but needs an ALSA userspace route; later |
| `cdrom_storage` | CD-ROM LUN (lun2), e.g. ISO with README | exotic; Windows autorun is disabled; not v1 |

---

## 7. UI: "USB" screen (S2.2 in `SETTINGS-SPEC.md` terminology)

### 7.1. Location in the tree

- S2 "Network," row 6 currently: "USB network (RNDIS)" + switch. **Change** to drill-in: "USB" -> value "RNDIS · <device-ip>" -> S2.2.
- S2.2 "USB," header caption `PORT MODE · <MODE>`.
- Add S2.2 to §2.3 of `SETTINGS-SPEC.md`, and the specification below to §2.4; clarify §1.2 "MTP is not available and never will be" as "the kernel function exists, but there is no userspace responder" (see §6.3).
- The status bar (32 px) already has room for a USB indicator: `accent` dot when `state=CONFIGURED`, `dim` otherwise.

### 7.2. Layout 480x800

```
┌──────────────────────────────────────────────┐ 32  status bar
│ ‹  USB                         PORT MODE     │ 56  header
├──────────────────────────────────────────────┤
│ Current       RNDIS · <device-ip>            │ 44  info
│ Host          connected · USB 2.0 HS         │ 44  info
│ Power         PC USB port · port current · no battery │ 44  info (warn)
│ MODE                                         │ 22  label
│ ◉ RNDIS            SSH <device-ip> · usually │ 56  radio
│ ○ SD card          29.5 GB · MPD will stop   │ 56  radio
│ ○ Image 256 MB     SD remains in system      │ 56  radio
│ ○ Charging only    no data · SSH will be lost│ 56  radio
│ ○ Console (serial) ttyGS0 <-> COM · debugging  │ 56  radio
│ DETAILS                                      │ 22  label
│ Speed and ID      <rndis-vid>:<rndis-pid> · high speed    │ 44  info
│ What this means   tap -> sheet                │ 44  info
├──────────────────────────────────────────────┤
│ [ Revert in 27 s ]             [ Keep ]      │ 44  banner (ARMED only)
└──────────────────────────────────────────────┘
```

Height: 56 + 3x44 + 22 + 5x56 + 22 + 2x44 = **600**; 112 px remain (44 banner + 22 footer + margin). Every tappable item is >=44; text is >=11; there are no animations, and the counter is redrawn once per second (digits only).

### 7.3. Mode rows

| Row | Caption 11 `dim` | What it does |
|---|---|---|
| RNDIS | "SSH <device-ip> · default" | `functions=rndis`, `<rndis-vid>:<rndis-pid>` |
| SD card | "29.5 GB · MPD will stop" | `mass_storage` + `mmcblk1p1`, RO by default |
| Image 256 MB | "eMMC share · SD remains" | `mass_storage` + loop on `usbshare.img` |
| Charging only | "no data · SSH will be lost" | `charge_only` (+ "do not appear to host" checkbox = `enable=0`) |
| Console (serial) | "ttyGS0 <-> COM · for debugging" | `serial` + getty after confirmation |

Do **not** show MTP/PTP/ADB in the list -- show a grey "MTP/PTP/ADB: no userspace responder" row with an explanation in "Laboratory."

### 7.4. Confirmations and automatic rollback

**400x220 dialog** (74% dimming, two 160x48 buttons):

- **SD card:** "Hand over SD card?" / "The SD card will be disconnected from the phone: music and files will become unavailable. MPD will stop; the library and covers will remain. On the PC, eject only using 'Safely Remove'." -> "Hand over" / "Cancel".
- **Charging only:** "SSH and files over USB will be lost. Charging will not be faster: the PC port still supplies its port current." -> "Enable" / "Cancel".
- **Console:** "Anyone who connects a cable will get a system login (username + password). For debugging only." -> "Enable" / "Cancel".
- **Return from SD:** "Return SD to the system? Music will become available again, and MPD will start." -> "Return" / "Cancel".

**Sheet 480x(40+56·N):** "What this means" -- 3--4 lines for the mode, no actions; "Image" -- size, free space, "Recreate..." button (danger, with confirmation).

**30 s automatic rollback (watchdog):**

1. After `ENABLE`, show a "Revert in 30 s" banner + "Keep".
2. If `state=CONFIGURED`, show "Host connected · Keep"; the timer keeps running (the user must confirm).
3. "Keep" -> `KEPT`, watchdog is removed.
4. Timer expires -> `ROLLBACK` -> RNDIS, SD is returned, MPD starts.
5. Automatic rollback is mandatory for SD mode even when `CONFIGURED`: otherwise the phone remains without music if the user leaves.
6. Same timer for charge-only/serial; not needed for RNDIS.
7. "Add 30 s" extends the timer (slow host/driver installation); after `KEPT`, a state row remains in the S2.2 header and sheet: "SD handed to host · Return" -- returning it must not require the user to "remember where that was."

**Status: "host claimed / did not claim":**

| Mode | Indicator |
|---|---|
| RNDIS | `state=CONFIGURED` **and** carrier on `rndis0` (`ip -br link`) |
| SD | `state=CONFIGURED`; I/O -- delta of `read sectors`/`write sectors` in `/sys/block/mmcblk1/stat` |
| Image | `state=CONFIGURED`; I/O -- delta of `/sys/block/loopN/stat` |
| Charging | `state=CONFIGURED` (host enumerated HID); no data or network |
| Gadget off | `state=DISCONNECTED`, `enable=0` -- "phone is not visible to host" |

If `state=DISCONNECTED` after 5 s -- "host did not claim it": cable, port, host asleep, hub. Do not treat this as a mode error; the timer still returns RNDIS.

### 7.5. Warnings (text)

- SD: "SD will be disconnected; MPD will stop; SSH over RNDIS will be lost."
- Image: "Changes on the PC are saved to a file; eject using 'Safely Remove'."
- Charging: "SSH will be lost; current will not increase; restore from the phone only."
- Console: "This is a system login; turn it off after use."
- Power without battery: "If power is insufficient, large copies may reboot the phone" (check `batt_volt_raw=0`).
- With `factory_cable=6000`: "Factory mode: switching is blocked by the kernel" -- do not show modes.

### 7.6. UI errors

| Code | Text | Action |
|---|---|---|
| `EBUSY_FUNCTIONS` | "Gadget did not turn off" | retry `enable=0`, otherwise reboot |
| `EBUSY_UMOUNT` | "SD is busy: <pid> <comm>" | "Stop MPD and retry" |
| `ENOSPC` | "Not enough space for image" | reduce size/clean up |
| `EACCES_LUN` | "Could not open medium" | check `ro`, permissions, mount |
| `NO_HOST` | "Host did not claim mode" | cable/port; rollback |
| `FACTORY` | "Factory mode" | disconnect factory cable |
| `REBOOT_REQUIRED` | "Reboot required" | do not touch SD, reboot |

---

## 8. Design of helper `/usr/local/bin/lg-usb-mode`

### 8.1. Interface

```
lg-usb-mode status [--json]        # current mode, state, host, power
lg-usb-mode list                   # available modes and reasons for rejection
lg-usb-mode set <mode> [--detach] [--timeout N] [--keep]
lg-usb-mode confirm                # remove watchdog (KEPT)
lg-usb-mode rollback [--reason X]  # force rollback to RNDIS
lg-usb-mode recover                # boot recovery
lg-usb-mode selftest               # read-only checks (see §9.1)
```

Modes: `rndis`, `ms-sd`, `ms-img`, `charge`, `off`, `serial`.

`--detach` -- double-fork + `setsid`, so the process survives an SSH disconnection (the session breaks when switching RNDIS -> MS). UI calls without `--detach` (it is already local); CLI over SSH uses `--detach`.

### 8.2. Files

| Path | Purpose | Permissions |
|---|---|---|
| `/usr/local/bin/lg-usb-mode` | helper itself (POSIX sh) | `0750 root:root` |
| `/etc/lg-usb.conf` | config (see §8.3) | `0644` |
| `/run/lg-usb/lock` | `flock` | `0600` |
| `/run/lg-usb/state` | `key=value`: mode, phase, prev, pid, deadline, boot_id, keep, sd_was_mounted, mpd_was_running | `0600` |
| `/run/lg-usb/getty.pid` | getty PID (serial) | `0600` |
| `/var/log/lg-usb.log` | log, rotation 256 KB x 2 | `0600` |
| `/var/lib/lg-usb/usbshare.img` | image | `0600` |

`/run` is tmpfs: after reboot there is no state; recover sees a "clean slate."

### 8.3. Config

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

`default_mode` is only `rndis` in v1. `charge`/`off` are prohibited by default: SSH would be lost without a local UI.

### 8.4. Lock and watchdog

- **Lock:** `exec 9>/run/lg-usb/lock; flock -n 9 || exit 3` ("busy" code). State stores PID and `boot_id`; if the PID is dead or `boot_id` differs, the lock is considered stale (for `recover`).
- **Watchdog:** after `VERIFY`, the helper launches a separate process `setsid sh -c 'sleep $auto_revert_s; ...'`, which reads state:
  - `phase=ARMED` and `keep=0` -> calls `lg-usb-mode rollback --reason timeout`;
  - `phase=KEPT` -> exits;
  - no state -> exits.
  The watchdog is a **separate** process: if the helper crashes (OOM, kill), rollback is not cancelled.
- **Duplicate invocation:** a second `set` gets "busy" and displays the PID.

### 8.5. Idempotency and recovery

- `rollback` can be called any number of times: each operation checks read-back and skips steps already completed.
- `recover` (boot): check `factory_cable`; if `functions != default` and `enable=1` -- write `enable=0`, `functions=default`, `enable=1`; check SD (`mountpoint` or `mount /media/sd`), `rc-service unudhcpd.rndis0 start`, remove stale lock, write a log line.
- **OpenRC:** not a separate service, but `/etc/local.d/aa-lg-usb.start` (lexicographically before `mpd.start`), so SD is mounted before MPD starts. The script calls `recover` and exits.
- **Initramfs:** do not touch. Gadget sysfs is not persistent; after reboot the kernel raises the default itself. The only requirement is to mount SD (`fstab` does this) and not start MPD too early.

### 8.6. Logging

One line per event:

```
2026-11-24T21:40:12Z set ms-img from=rndis pid=4321
2026-11-24T21:40:12Z disable rc=0 enable=0 state=DISCONNECTED (0.04s)
2026-11-24T21:40:12Z functions=mass_storage readback=mass_storage
2026-11-24T21:40:12Z lun ro=0 file=/dev/loop0
2026-11-24T21:40:12Z enable rc=0 enable=1 state=CONNECTED
2026-11-24T21:40:13Z state=CONFIGURED (0.8s) host=took
2026-11-24T21:40:43Z confirm keep=1
```

Also on `set`: `df`, `mount`, `mpc status`, `batt_volt_raw`, `ac/usb online`, `factory_cable` -- so incident analysis does not rely on guesswork.

### 8.7. `set` pseudocode

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

## 9. Tests

### 9.1. T0 -- read-only baseline (can be done now)

```sh
# on the phone (ash does not understand {a,b} -- list explicitly)
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

Expected: `1 / rndis / CONFIGURED / <rndis-vid> / <rndis-pid>`, empty `lun/file`, `rndis0 UP <device-ip>`, free space matches the configuration. This is the reference point for "before/after" comparison.

### 9.2. Matrix

| # | Host | Action | Expected | Rollback |
|---|---|---|---|---|
| T1 | Linux | `set rndis` (no-op) | nothing changes, state CONFIGURED | -- |
| T2 | Linux | `set ms-img`, 10 cycles | 256 MB disk appears, `enable=1`, state CONFIGURED | `rollback` |
| T3 | Windows | ms-img: write 50 MB, eject, reconnect | file is readable, `fsck.fat -n` clean | recreate image |
| T4 | Linux | ms-img: `dd` read 100 MB | speed 8--15 MB/s (HS), loop stat increases | eject |
| T5 | Linux | `set charge` -> `set rndis` | SSH drops, returns <=10 s; current does not change | reboot |
| T6 | Linux | `set off` -> reboot | phone disappears; RNDIS returns by itself after reboot | -- |
| T7 | Linux | `set serial`, getty, login | `/dev/ttyACM0`, `user` login | `set rndis` |
| T8 | Linux | ms-sd RO: copy 100 MB from card | files readable, `fsck.vfat -n` clean, MPD plays afterward | `rollback` |
| T9 | Windows | ms-sd RW: write 1 file, eject | file is present, fsck clean, `mpc update` sees it | manual `fsck -a` |
| T10 | Linux | unplug cable during ms-img write | helper detects "host lost," rollback; `fsck -n` errors are expected | recreate image |
| T11 | -- | `kill -9` helper in ARMED | watchdog rolls back to RNDIS | reboot |
| T12 | -- | reboot in each mode | RNDIS, SD mounted, MPD playing | -- |
| T13 | -- | 50 switches | no dmesg errors, no lock/loop leak | reboot |

### 9.3. Host-specific checks

**Windows:**
```powershell
Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -like "USB\VID_18D1*" -or $_.InstanceId -like "USB\VID_1004*"}
Get-Disk | Format-Table Number,FriendlyName,Size,BusType
Get-Volume | Format-Table DriveLetter,FileSystemLabel,Size
Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*RNDIS*"}
```
Expected: RNDIS -> network adapter with <device-ip>; ms -> "Removable Disk" (USBSTOR); charge -> HID; serial -> COMx. On mode changes check "Safely Remove" and `pnputil /enum-devices /connected`.

**Linux:**
```sh
lsusb -v -d <rndis-vid>: 2>/dev/null | grep -E 'bInterfaceClass|bNumEndpoints'
lsusb -v -d 1004: 2>/dev/null | grep -E 'bInterfaceClass|bNumEndpoints'
dmesg -w | grep -E 'usb|sd|cdc_acm|rndis'
lsblk -o NAME,SIZE,FSTYPE,LABEL,TRAN
fsck.vfat -n /dev/sdX        # after eject, not during
```

### 9.4. Power

- With battery: large copies (>=100 MB) are normal.
- Without battery, on a PC port: before the copy, run `cat /sys/class/power_supply/battery/voltage_now`, then again afterward; on reboot run `cat /proc/last_kmsg` (expect "No errors detected" = VBAT sag, `POWER-PLAN.md` §4).
- Recommendation: ms-sd/ms-img on a 500 mA port without battery only with brightness <=150 and `scaling_max_freq=600000`; battery is better.

### 9.5. Success criteria

1. Any mode is set within <=5 s (ms-sd with umount -- <=10 s); for data modes, `state` reaches `CONFIGURED`; for `off`, `enable=0` and `state=DISCONNECTED`; the host sees the expected device.
2. Rollback to RNDIS works 10/10, including cable disconnect and helper kill; SSH returns within <=15 s.
3. ms-sd RO: 100 MB copied, `fsck.vfat -n` clean, MPD plays afterward.
4. ms-img: 20 write/eject cycles, `fsck` clean, image does not grow beyond 256 MB.
5. No writes to raw eMMC partitions; the only eMMC write is the image file in ms-img, and only on explicit action. No reboots caused by the helper over 50 switches.

---

## 10. Risks and mitigations

| Risk | Level | Mitigation |
|---|---|---|
| FAT corruption on SD (RW) | **high** | RO by default; "Safely Remove"; `sync`; `fsck -n`; RW only after confirmation |
| Image corruption if unplugged | medium | loop + `blockdev --flushbufs`; `fsck`; image is expendable |
| Exporting the wrong partition | **high** | whitelist: only `mmcblk1p1` or a file in `/var/lib/lg-usb`; reject `mmcblk0*`, `ANDROID!`, size <1 MB/>64 GB |
| Exporting a mounted partition | high | check `/proc/mounts` + `fuser`; reject if it is a mountpoint |
| SSH loss (charge/off/MS) | medium | 30 s auto-rollback; reboot restores RNDIS; serial as rescue; do not make charge/off default |
| `enable=0` hangs | medium | `timeout 5`, 2 attempts, then `REBOOT_REQUIRED`; do not loop |
| "Silently failed" | high | read-back after each write; `factory_cable != 6000` |
| Windows caches VID/PID | medium | unique PID per mode; if glitches occur, "Uninstall device" in Device Manager |
| VBAT sag without battery | medium | UI warning; brightness/frequency limits; battery/charger for large copies |
| MPD "lost" its library | low | database and covers on eMMC; after SD return, run `mpc update` only when RW |
| unudhcpd restart loop | low | netdev is not removed; do not touch service unless needed; check after rollback |
| MTP/adb hangs on host | medium | do not enable; grey rows in UI |
| `getty` without password | high | getty + login, not auto-shell; confirmation only; turn off afterward |
| Factory mode | low | reject when `factory_cable=6000` |

**Brick risks:** none. The gadget is runtime kernel state; sysfs is not written to flash; bootloader, boot partition, EFS, and rootfs are untouched. Worst case is "phone without SSH until reboot" and corrupted FAT, not "won't boot." The only eMMC write is the image file, and only on explicit action.

**Do not:**

1. Do not hand over a mounted partition (the kernel will not block it -- see §1.2).
2. Do not export `mmcblk0*` (rootfs/boot/modem) -- only `mmcblk1p1` or a file in `/var/lib/lg-usb`.
3. Do not write `ro` after `file` (it will return `-EBUSY`); do not write `functions` while `enable=1` (also `-EBUSY`).
4. Do not enable MTP/PTP/adb "just to see": the host will hang on timeouts, while the phone produces neither a response nor an error.
5. Do not make `charge`/`off` the default mode without a local UI: this is the only way to lose SSH until reboot.
6. Do not unplug the cable during an RW session, and do not treat `sync` on the phone as a substitute for "Safely Remove" on the host.
7. Do not run `fsck.vfat -a` automatically or "repair" SD without a backup.
8. Do not touch initramfs for USB: sysfs is not persistent, reboot already restores RNDIS; an initramfs hook is an unnecessary risk.
9. Do not change baseband/EFS as part of USB work (that is `MODEM-PLAN.md`, a different risk profile).

---

## 11. Three conclusions

1. **The kernel is ready for all modes except those requiring userspace.** RNDIS, mass storage (SD/image), charge-only, serial, and "gadget off" are implemented with three sysfs writes and work on this kernel today. MTP/PTP/adb are blocked not by configuration but by the lack of a responder/adbd; they cannot be "enabled" -- they can only be written, and that is a separate project.

2. **The danger of a mode is not USB, but the filesystem and SSH loss.** `f_mass_storage` opens a block device without exclusive acquisition, so the kernel will not prevent handing over a mounted partition; `fsg_lun_close` does not perform `fsync`; the LUN has no `forced_eject`. Hence the strict rules: RO by default, check `mount`, manually verify "host ejected," `sync`/flush, `fsck -n`, auto-rollback, and reboot as the final rollback.

3. **The UI should sell consequences, not "modes."** Users need two things: "what is happening now" (state, host, power) and "what I will lose" (SD disconnects, MPD stops, SSH disappears). Therefore, the screen is built around status and confirmations, and the 30-second rollback is a mandatory part of every dangerous switch, not a "developer feature."

## First safe step

**Capture T0 (read-only) and implement only `lg-usb-mode status`/`selftest`, without a single write to sysfs.** This records the return point (current `enable=1`, `functions=rndis`, `<rndis-vid>:<rndis-pid>`, empty LUN, free space) and gives the UI an honest source of data. The first actual switch comes afterward, on the bench, in this order: **RNDIS -> image 256 MB -> RNDIS** (do not touch SD), checking auto-rollback and cable disconnect. Handing over SD comes last, once the image and rollback have been proven.
