# Installing postmarketOS on the LG P700/P705

> **Warning.** This is an archived 2019 downstream port using Linux 3.4, not a mainline kernel. Installation is experimental, erases Android and its data, and can leave the phone unbootable. Continue only if you accept data loss and know how to restore the device.

## Contents
- [About the port and requirements](#about-the-port-and-requirements)
- [Prepare the host](#prepare-the-host)
- [Build the kernel](#build-the-kernel)
- [Boot image and initramfs](#boot-image-and-initramfs)
- [Prepare the root filesystem](#prepare-the-root-filesystem)
- [Flash the boot image](#flash-the-boot-image)
- [Write the root filesystem to p20](#write-the-root-filesystem-to-p20)
- [First boot](#first-boot)
- [Verify the installation](#verify-the-installation)
- [Recovery](#recovery)
- [Known pitfalls](#known-pitfalls)
- [Next](#next)

## About the port and requirements

The `lg-p700` port was added in 2019 and later archived. It uses LG's Linux 3.4 downstream kernel. The public port page does not confirm most components; the setup described here was verified on one P705 (`u0`) and is not guaranteed on every revision or P700. On the test P705, the stock `p14` partition contains a P700-compatible SlimKat build, so `build.prop` may report `LG-P700`; the board is the shared `u0`, so trust the board rather than the model string.

You need an LG Optimus L7 P700 (board name `u0`) or P705, about 4 GB of eMMC, an SD card, an unlocked bootloader, the V20 baseband, and a ZIP-capable recovery. The verified unit runs ClockworkMod 6.0.3.2; the upstream port mentions TWRP 3.0.2. Prepare a Linux host or Ubuntu on WSL2, `pmbootstrap`, enough disk space for sources and images, and a charged phone. The kernel requires gcc 6; builds made with newer compilers did not boot on this port.

Before starting, verify access to fastboot and recovery and prepare a suitable stock V20 KDZ for recovery. Do not use a recovery image for the other model. A clean installation through recovery has not been retested in this project; a separate recovery release is being prepared.

## Prepare the host

Initialize `pmbootstrap` for ARMv7 and device `lge/p700`. The port is archived and may be missing from the standard list, so add the required pmaports revision manually. Paths vary by setup.

```sh
pmbootstrap init
pmbootstrap install
```

`init` configures the package tree and device; `install` creates the root filesystem but does not flash the phone. Select `config-lg-p700.armv7` and `device/archived/linux-lg-p700/APKBUILD`. Confirm the model and save the original boot and recovery images before writing to the device.

The kernel source is `TeamHackLG/lge-kernel-lproj` at commit `45d6b0c683f3e0178e91c081133c6ebbea1d30fd`. The APKBUILD is under `device/archived/linux-lg-p700`. Install gcc 6 and verify that the build invokes that compiler.

## Build the kernel

The APKBUILD includes the `rel32`, `libra_nopie`, `wlan_volt`, `no_free_splash`, `touch_single`, `sensor_sysfs`, `smd0`, `hci_smd_nopie`, and `mmc_poll` patches. This project adds `0001-msm-fb-pan-reliability.patch` and `0002-mdp-dsi-bounded-wait.patch`. Preserve the order: list the new patches in `source=` after `mmc_poll.patch`. Recalculate checksums and increment `pkgrel` after changing the source list.

```sh
pmbootstrap checksum linux-lg-p700
pmbootstrap build --arch=armv7 --force linux-lg-p700
```

The first command recalculates APKBUILD checksums; the second builds the package. Confirm that the build succeeds. Patches `0001` and `0002` passed a dry run but were not compile-tested; treat them as experimental.

## Boot image and initramfs

Partition p9 is 8192 KiB (8 MiB). The complete boot image must fit within this limit. The initramfs is trimmed by removing the bootsplash/Plymouth stack and tools not needed here: `buffyboard`, vfat/xfs/f2fs `fsck` tools, xfs/f2fs utilities, `parted`, `resize2fs`, some libinput files, and related components. Removing the wrong files can prevent boot.

Obtain `boot.img` and the initramfs from the pmbootstrap environment. `build_recovery_boot.py` checks the `ANDROID!` magic, parses the Android boot-image header, retains the kernel, replaces the ramdisk, and patches the command line. The cmdline includes `androidboot.hardware=u0`, `lge.signed_image=false`, the rootfs UUID, and boot options.

Get the UUID from the prepared root filesystem with `blkid`; do not copy a value from an example. Check for `fits 8MiB` after packing and do not flash an oversized image. The complete packaging procedure has not been verified on arbitrary host setups; check the script paths against your environment.

```sh
blkid rootfs-p700.img
python3 build_recovery_boot.py
```

The exact `blkid` command depends on whether the image is attached as a loop device. A wrong UUID or cmdline can prevent boot.

## Prepare the root filesystem

Create the root filesystem with `pmbootstrap install`, then extract the second GPT partition from the disk image. `extract_rootfs2.sh` parses GPT and copies partition 2; verify its paths before running. In the tested layout, the root filesystem is on p20.

Check ext4, disable `orphan_file` for compatibility with the old kernel, then run `e2fsck` again. In `/etc/fstab`, mount root by UUID and leave `/boot` commented out. The verified layout is: p9 — 8192 KiB boot; p15 — 8192 KiB; p20 — 2,904,064 KiB, ext4 labelled `pmOS_root`. Obtain the actual UUID yourself and substitute it for `<rootfs-uuid>`.

Trim only a backup copy of the root filesystem. The script removes graphics libraries, Python, fonts, locales, documentation, caches, and multimedia components. Leave 1% of the ext4 filesystem reserved.

```sh
e2fsck -fy rootfs.img
tune2fs -O ^orphan_file rootfs.img
e2fsck -fy rootfs.img
tune2fs -m 1 rootfs.img
```

These commands modify the filesystem. Run them on a copy of the image while it is unmounted. Check `/etc/fstab`, the UUID, and free space before writing to the phone.

## Flash the boot image

Modern `fastboot` hung at `Fetching super` on this phone. The tested setup uses an old fastboot executable of about 157 KiB (SDK 2012) and a Windows `android_winusb.inf` driver entry for `USB\VID_18D1&PID_D00D`. Install the driver and confirm `fastboot devices` lists the phone before attaching it to WSL with `usbipd`.

```sh
fastboot devices
fastboot flash boot boot.img
fastboot reboot
```

`fastboot flash boot` overwrites p9. Do not flash an image larger than 8 MiB or one built for another model. Alternatively, a running system can use `dd` to write to `/dev/mmcblk0p9`; a wrong path can damage another partition. Verify the target and input file first.

## Write the root filesystem to p20

`fastboot flash userdata` is unsuitable: fastboot maps `userdata` to p15, which is only 8192 KiB. A larger image fails with `data too large`. Do not write to p14 or p15.

The working project method is to boot into a pmOS/debug shell (initramfs) where p20 is not mounted, bring up USB RNDIS, connect from the host to `172.16.42.1` (the host is typically `.2`), and send the image over TCP. A listener on the phone writes directly to p20, replacing the entire partition. **p20 must not be mounted**: in a running system it is the root filesystem `/`, so writing from there is unsafe. Confirm the target is `/dev/mmcblk0p20` and that the image matches the device.

On the phone, in a root shell:

```sh
nc -l -p 9000 > /dev/mmcblk0p20
sync
```

Send the image from the host to `172.16.42.1:9000`, then compare the image md5 with a readback of the partition. The `nc` client depends on the host; the project script `push_block.py` automates transfer and verification, but check its arguments before running it.

```sh
md5sum rootfs.img
```

Alternatively, use TWRP/adb and `dd` on the correct partition; verify this method for the specific recovery build. A clean TWRP installation has not been retested in this project; a separate recovery release is in preparation.

## First boot

The device configures RNDIS at `172.16.42.1/24`; assign the host an address on the same subnet, typically `172.16.42.2`. Connect over SSH as `<user>` to `172.16.42.1`. Set your own password while preparing the system. The image uses dropbear.

On the tested device, Wi-Fi worked with stock `wlan.ko` and `librasdioif.ko` modules from p14, loaded with `insmod -f`. This forces modules with a mismatched version or ABI and can hang the kernel. Do not automate loading until the modules have been checked. OpenRC services are under `/etc/local.d`.

Do not assume `player`, `desktop`, or `shell` is the default mode. The choice depends on the root filesystem; the verified baseline booted primarily in player mode without X.

## Verify the installation

After boot, check the kernel version, command line, mounts, and free space. Then test the display, touchscreen, audio, and SSH individually. The verified display is 480×800 RGB565; the physical panel is rotated by 180 degrees. Touch and audio worked on the test device, but this is not guaranteed for all units.

```sh
uname -a
cat /proc/cmdline
mount
df -h /
dmesg | grep -i 'MDP IRQ term'
```

These commands only inspect state. No output from the last command is expected when that diagnostic has been downgraded to debug; it does not replace a display test or rule out other errors.

## Recovery

- If the kernel does not boot, enter fastboot with the old fastboot executable and flash a known-good boot image for the exact model.
- For a black screen, first check whether SSH over USB responds. If the system is reachable, the issue may be in framebuffer/X; restoring a known-good boot image is safer.
- For a boot loop, restore known-good boot and rootfs images or use TWRP. Do not flash rootfs as `userdata` or write to p14/p15.
- If fastboot reports `data too large`, stop writing to `userdata`: it maps to p15. Use the running-system/RNDIS method or separately verify TWRP/adb.
- To return to Android, use the stock V20 KDZ and an LG recovery procedure for the model. Exact KDZ versions and tools vary by region and are not presented here as verified.

## Known pitfalls

- A boot image larger than 8 MiB does not fit in p9.
- Kernels built with gcc 7 or later did not boot on this port; use gcc 6.
- Enabling `fbcon` caused console output to cover the image; the tested configuration disables framebuffer console. `fbcon=rotate:2` does not enable it.
- X requires 16bpp RGB565; 32bpp produces incorrect colors.
- The physical panel is rotated by 180 degrees.
- When extracting archives as root, use `--no-same-owner` or system file ownership may be corrupted.
- Do not open `/dev/smd0` before the modem is ready; the driver may hang.
- PowerShell may strip quotes from SSH command arguments. Send a separate script file and run it on the phone.

## Next

- [Architecture](architecture.md)
- [Hardware](hardware.md)
- [Power](power.md)
- [Quality assurance](qa.md)
