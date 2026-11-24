# graphite shell architecture

## Implemented

- The session runs Xorg on fbdev at 16 bpp, with the image rotated by 180 degrees. `session-shell` starts `fbpan-ng 10` to refresh the panel. Xorg and the framebuffer player must not own the framebuffer at the same time.
- The shell is a single full-screen Xlib application at 480×800. It has no window manager, no GTK/Qt in the shell itself, and no compositor.
- Each frame is drawn into a software RGB565 `Canvas`. `Gfx` supplies drawing primitives, text, and icons; the frame is copied to an XImage and sent with `XPutImage`.
- `App` owns a `ScreenView` stack and a screen cache. `open()` pushes a page, `replace()` swaps the current page, and `back()` pops the top page; navigating to `home` resets the stack. Overlays can draw over the previous page.
- The shell owns the status bar and shared UI elements; each screen draws its own content. Redraws happen on events and on the clock timer. Animations are disabled by default.
- Touch and physical keys arrive through X input/evdev. The current configuration uses `event0` (touch and some keys), `event1` (`u0_keypad`), and `event5` (`7k_handset`). The shell handles Back, Menu, Home, Search, Power, and volume keys.
- Core gestures: an edge swipe from the left goes back; pulling down from the top edge opens the shade. Holding Home opens the shade, while holding Power opens the power screen.
- Music features use MPD over TCP through the shell client. Playback state, queue, library, metadata, and cover art are exposed by built-in screens.
- `apphost` runs at most one external X application at a time. It launches the process as `user` (uid/gid 10000), with HOME set to `/home/user`, and places a suitable top-level window in the 480×768 area below the status bar. The child window receives focus while the shell retains global handling of hardware keys.
- The available external applications include `xterm` and the Dillo browser; the commands that can be launched are controlled by app settings. An on-screen keyboard for an external app is provided as a separate overlay.
- `session-shell` selects RGB565, starts Xorg, waits for the display socket, disables blanking, and starts `fbpan-ng 10`, followed by `graphite-shell`.
- `/etc/lgtest-mode` selects the `shell`, `desktop`, or `player` profile through the boot mode script. If X fails to start, its socket does not appear, or the shell exits, `session-shell` stops X/pan and execs the `lp7player` framebuffer player as fallback.

## Limitations and status

- This is a single-process shell with one active external GUI window. A full task switcher and background application lifecycle are not implemented.
- The device has 512 MB of installed memory (Linux has less available). The budget limits browsers and other heavy applications; multiple external windows are not supported.
- There is no GPU acceleration. Canvas drawing, frame conversion, and XPutImage are software operations; frequent animation and layered compositing are not design goals.
- The interface targets 480×800 RGB565. Layout and pointer handling are not generic for other panels.
- MPD is a separate service. TCP connectivity and library availability depend on device configuration; a disconnected server must not be treated as working playback.
- `apphost` filters windows by process ownership, type, and size; it is not a full window manager. Multiple applications, retained tasks, and system notifications remain planned rather than implemented.
- Mode selection and fallback are implemented in `os/session-shell` and related scripts. A settings item does not mean a mode change takes effect before reboot.
- The current script starts Xorg with `-ac`. This is a development configuration with relaxed access control, not a production security model; Xauthority is required before a public release.

## Key files

- `os/include/graphite.h` — Canvas/Gfx, screen, theme, and application APIs.
- `os/src/app.cpp` — Xlib window, stack, events, input, and rendering.
- `os/src/registry.cpp` — screen identifier table.
- `os/src/apphost.cpp` — external process launch and window placement.
- `os/session-shell` — shell startup and framebuffer-player fallback.

## Input and rendering loop

`App::run()` waits for X events with a short timeout and services apphost, held keys, toasts, and the clock. A screen receives tap, longtap, drag, and key callbacks through `ScreenView`; a handled event marks the frame dirty.

During rendering, the screen draws its content, then the core adds the status bar, launch overlay, and toast. At 16 bpp RGB565, pixels are copied directly into the XImage; other formats are converted in software. The frame is then sent with `XPutImage` and `XFlush`.

The clock refreshes about every 10 seconds. A screen reporting `animated()` receives periodic `tick` callbacks; this is not a promise of continuous animation. MPD and file operations belong to separate modules, not the graphics API.

## External-window management

`apphost` adopts only a suitable visible top-level window owned by the child process. It moves the window below the 32-pixel status bar and sizes it to 480 px wide. When the on-screen keyboard is shown, the usable window height is reduced.

Launch is asynchronous: the shell displays a waiting state, searches for a window for up to 15 seconds, and does not block drawing. Closing first sends WM_DELETE_WINDOW, then SIGTERM and SIGKILL to the process group if needed.

For browser commands, Back may send Alt+Left; another Back within a short interval closes the app. For other applications, Back closes the window. Home returns to the shell, but retaining the process as a background task is not promised.

## Out of scope for the current implementation

There is no general notification model, multitasking switcher, multi-window persistence, or full Xauthority isolation in the current startup script. These features must not be inferred from the presence of settings screens or external applications.

Browser support depends on Dillo/Netsurf and their installed libraries. The `browser` screen and the external browser are separate layers: the shell manages launch, while the child X client renders page content.

The application API sets `W=480`, `H=800`, and a default depth of 16. The status bar occupies the first 32 pixels; additional system feedback is shown through the shared toast.

Theme and other settings are loaded from `/etc/lgtest-settings`. Configuration writes and system-file permissions depend on the installation; this documentation does not assume every setting is available to an ordinary user.

The X server runs locally on display `:0` without a TCP listener. This limits network exposure, but the `-ac` flag still disables access checks for local clients.

