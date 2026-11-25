# LG P700/P705 hardware platform

## Main specifications

- Device: LG Optimus L7 P700/P705, Qualcomm MSM7227A platform with a single-core Cortex-A5. The verified unit is a P705 (`u0`); stock `p14` may report P700.
- Graphics: Adreno 200. The shell does not use GPU acceleration and does not assume the GPU is operational.
- Memory: 512 MB RAM. Less is available to user processes after the kernel and services are accounted for.
- Storage: 4 GB eMMC. Partition p9 holds the boot image (about 8 MiB); p20 is used as the root filesystem. Do not write partitions without a verified partition map and backup.
- Display: 480×800, RGB565, 16 bpp. The LG4573B panel is physically inverted; display configuration compensates with a 180-degree rotation.
- Touch: Melfas. Input arrives through evdev; coordinates match the screen orientation in the current configuration.
- Hardware keys: `u0_keypad` is typically exposed as `event1`, and `7k_handset` as `event5`; `event0` belongs to the touch panel and may also report system keys.
- Audio: QDSP5/ALSA, with MPD used for playback. Headphone routing depends on system configuration.
- Wi-Fi: WCN1314 controller. The `wlan.ko` and `librasdioif.ko` modules are used; loading them depends on compatibility with the device kernel and build.
- The modem is connected over SMD, but SIM/cellular service does not work in the tested configuration.
- Bluetooth and GPS do not work. Do not treat them as available without a separate driver, firmware, and service check.
- The device has a battery, charging circuitry, and removable SD card. Battery/charging behavior and SD mounting depend on the image; specific thresholds and sleep modes are not guaranteed.

## Known limitations and pitfalls

- The panel requires periodic `FBIOPAN_DISPLAY`, so Xorg fbdev runs with `fbpan-ng 10`. Higher refresh rates increase load on the low-power CPU.
- Xorg must use 16 bpp RGB565. A 32-bit mode can produce incorrect colors in this setup.
- Xorg/shell and the framebuffer player must not run concurrently: competing ownership of `/dev/fb0` can block the framebuffer.
- Reading `/dev/fb0` for capture can hang during `FBIOPAN_DISPLAY`; capture only in a controlled, stable state.
- After repeated X restarts or mode changes, the panel driver may hang. A userspace restart may not restore the display, so avoid restart loops.
- Opening the SMD tty before the modem is ready can block the driver and related operations until reboot. Do not probe the modem early in boot.
- The memory budget and lack of GPU rule out expectations of smooth layered graphics, a heavy browser, or multiple external GUI apps at once.
- Wi-Fi binary-module compatibility is tied to the specific kernel and ABI. Do not load modules from an unrelated build.
- Bluetooth/GPS, SIM, camera, and safe suspend must not be presented as working features without renewed hardware acceptance.

Kernel versions, service state, power conditions, sensors, and actual free eMMC/SD capacity vary by image. Unknown or unmeasured values should be treated as unverified.

## Display and input

Xorg uses fbdev, while the shell draws its own frame in memory. Due to the panel driver behavior, `fbpan-ng` periodically pans the framebuffer. A lower rate reduces load, but display refresh remains separate from X's drawing loop.

Touch coordinates are already aligned with image rotation in the current profile. An additional transformation matrix can mirror the input; do not add one without checking on the device.

The names `event0`, `event1`, and `event5` refer to the current input-device order, which may differ in another build. Identify devices by driver name during diagnostics instead of relying only on event numbers.

## Power and data storage

The lock screen and backlight control are not equivalent to powering off the LCD panel or system suspend. Do not use `FBIOBLANK`, experimental panel commands, or writes to system partitions as ordinary UI actions without separate hardware checks and a recovery plan.

Boot-mode profiles are stored in `/etc/lgtest-mode`; the system may select the framebuffer player as fallback if X does not start. Before updating boot or rootfs, keep a verifiable backup and confirm the image matches partitions p9 and p20.

The SD card is removable storage. Unmount its volume cleanly and wait for writes to finish before removal. The filesystem and mount point are set by the image and may differ.

## Checks before changes

1. Confirm the device's current profile, kernel, and partition layout; do not copy values from another image.
2. Check power and keep a backup before testing the panel, modem, or partitions.
3. Change one layer at a time: X/fb, input driver, Wi-Fi module, or system service.
4. After a change, test startup, touch, hardware keys, audio, Wi-Fi, and fallback. A successful host build does not prove hardware operation.
5. If the display goes black, pan hangs, a process enters D state, or the device reboots unexpectedly, stop retrying and collect diagnostics before another run.

These recommendations describe safe operating and test boundaries, not a flashing procedure. This public hardware summary intentionally omits personal identifiers, network addresses, and credentials.

CPU frequency, actual available RAM, panel revision, and battery parameters should be checked on the specific device. This document does not state them as guarantees for every image.

Unknown modem, camera, and radio-module states are marked unverified, not working merely because a device node or driver file exists in an image.

Panel tests require stable power and a hardware recovery path. A software fallback is not a substitute for a backup of the boot image.

