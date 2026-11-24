[Русский](README.md) · **English**

# graphite shell — a Linux shell for the LG Optimus L7 (P700 / P705)

A full-screen Xlib shell for an old Android phone running Alpine Linux
(postmarketOS): a single process, one override-redirect X window at 480×800,
its own RGB565 canvas, output through `XPutImage`. No WM, no GTK/Qt, no text
engine other than FreeType.

The repository has two layers:

- `os/` — the working shell in C++17: 27 screens (home, player, queue,
  settings, files, terminal, notes, browser, shade, lock, power), an MPD
  client, an on-screen keyboard and a host for third-party X applications.
- `shell/` — HTML/CSS/JS mock-ups of the same screens in a Metro style:
  tiles, themes, textures. They are the layout reference for the C++ layer.

`docs/` holds the technical plans and research (architecture, design system,
USB, settings, reader, power, QA). The Russian originals live in `docs/ru/`;
the English versions will follow in `docs/en/`.

## Device

| | |
|---|---|
| Model | LG Optimus L7 P700 (P705 — same body, no NFC) |
| SoC | Qualcomm MSM7227A, 1 GHz, Cortex-A5, Adreno 200 |
| Display | 480×800 TFT, 16 bpp (RGB565) |
| Memory | 512 MB RAM (~400 usable), SD card |
| OS | Alpine Linux / postmarketOS edge, kernel 3.4 |
| Stack | Xorg on fbdev, `fbpan-ng`, FreeType, MPD 0.24 |

## Build

On the device (Alpine: `g++`, `libX11-dev`, `freetype-dev`):

```sh
cd os
./build.sh          # -> build/graphite-shell
sudo ./install.sh   # binary, session-shell, zz-mode.start, fonts, input class
```

On a host, without the phone (needs Xvfb to render a screen):

```sh
cd os
sh host-build.sh    # -> build/graphite-host
sh preview.sh home  # renders 480×800 into artifacts/host/home.png
```

Details, fonts, the screen contract and the key map are in `os/README.md`
(Russian; an English version is planned).

## Layout

```
os/          the shell in C++17 (src/, screens/, include/, fonts/, icons/)
shell/       Metro HTML mock-ups, themes, textures
docs/ru/     technical plans (Russian originals)
docs/en/     English versions (in progress)
```

## Status

The shell boots as the main session on the phone (X + `graphite-shell`, with
the old framebuffer player as fallback). Working: home with tiles,
player/queue/info on MPD, 13 settings sub-screens, file manager, terminal with
an on-screen keyboard, notes, browser through dillo, shade, lock, power off.
Open work: swipes, task switcher, reader, video, power management — see
`docs/ru/master-plan.md`.

## License

Code is MIT, see `LICENSE`. Fonts, icons and vendored libraries keep their own
licenses, see `THIRD-PARTY.md`.

Firmware images, partition dumps and device-specific service scripts are not
part of this repository.
