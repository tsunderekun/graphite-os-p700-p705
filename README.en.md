[RU](README.md) | [**English**](README.en.md)

# graphite shell — a Linux shell for the LG Optimus L7

Graphite shell is a full-screen Xlib interface for the LG Optimus L7 running Linux. The idea is to give an old phone a small, practical job: play music, browse files and notes, and see how much interface fits into a 480×800 display and 512 MB of RAM.

This is not a claim to have built a complete operating system, nor a way to turn the phone into a modern smartphone. The shell runs on the device, but it is still a prototype: some system features are unfinished, and a full release-level test cycle remains to be done.

## A few screens

![Home screen](docs/assets/w-home2.png)
*Home: tiles and shortcuts.*

![Music library](docs/assets/w-library.png)
*The library for the MPD player.*

![Dillo browser](docs/assets/w-dillo.png)
*Dillo running in an external window hosted by the shell. It won't make this phone a powerhouse for browsing, though.*

## Hardware

- LG Optimus L7 P700/P705, Qualcomm MSM7227A.
- 480×800 display, 16-bit RGB565.
- 512 MB RAM and 4 GB eMMC.
- Alpine Linux / postmarketOS on the archived downstream 3.4 kernel.

The P705 is close to the P700, but differs in its NFC configuration. A component being listed in the hardware specs does not mean it is supported by the system.

## What works

At the center is a single full-screen Xlib shell, with no window manager. It has home, player, library, queue, settings, apps, files, notes, terminal, browser, shade and lock screens, plus themes and system settings.

Touch, physical keys and basic gestures work. Wi-Fi, audio and headphones, the SD card, battery indication and screenshots have worked on the tested setup. Results depend on the image and the individual phone; a host preview is no substitute for testing on the device.

## What does not (yet)

Bluetooth, GPS, SIM and calls, the camera and GPU acceleration are not considered working. A reader, system notifications and full task management, USB modes and a recovery release are still planned. The project is under development; an item appearing on screen does not necessarily mean the system feature behind it is ready.

## Installation

The postmarketOS port is archived and based on a downstream 3.4 kernel. Installation is experimental: an unlocked bootloader, V20 baseband and TWRP are required. It can erase data or leave the phone unable to boot. Read the [full guide](docs/en/install.md) or the [Russian guide](docs/ru/install.md) first, and have a recovery path ready.

There is no ready-to-flash recovery ZIP yet; one is in preparation. The guide covers a manual installation path and does not guarantee it will work on every device revision.

## Build and run

On a configured Alpine/postmarketOS device:

```sh
cd os && ./build.sh
```

For a host build and preview, you will need Xvfb:

```sh
cd os
sh host-build.sh && sh preview.sh home
```

See [`os/README.md`](os/README.md) for dependencies, running the shell and more build details.

## Repository layout

- `os/` — the C++ shell, build and run scripts.
- `shell/` — HTML/CSS/JS mock-ups and themes.
- `docs/ru/` and `docs/en/` — technical documentation.
- `docs/assets/` — interface screenshots.

## Documentation

- [Russian documentation index](docs/ru/index.md)
- [English documentation index](docs/en/index.md)

Some notes are still working engineering drafts, and a few links may point to files from the original working tree that are not included here.

## Licenses

Code is MIT-licensed. The Medeo font is under SIL OFL 1.1, DejaVu has its own license, and Phosphor icons are MIT-licensed. See [`THIRD-PARTY.md`](THIRD-PARTY.md) for details and license texts.
