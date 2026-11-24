# graphite shell screens

The IDs below are registered in `os/src/registry.cpp`. A registered factory does not by itself mean that a screen is complete or has passed device testing. All screens target 480×800.

## Main screens

| ID | Purpose and controls |
|---|---|
| `home` | Main screen with pinned apps and navigation. Tap a tile to launch or open it; Home returns here and resets the stack. |
| `player` | Current track, cover art, transport, and MPD seeking. Hardware volume keys adjust volume; a gesture across the pivot switches player pages. |
| `library` | Browse the music library and start selected content; available data depends on MPD and indexed files. |
| `queue` | MPD queue; selecting a track starts playback. Its contents and size depend on the server. |
| `info` | Metadata and current-file details, with navigation to related actions. |
| `apps` | List of available applications. Tap to launch; pinning and list contents depend on configuration. |
| `edit` | Edit home-screen tiles by adding or removing pins. Entry and details depend on the `home` state. |
| `files` | Browse files, select items, and perform file operations. Some name-entry actions may require a physical keyboard. |
| `notes` | View and edit text notes. Editing and navigation input are available; format and persistence depend on configuration. |
| `terminal` | Built-in terminal screen with a path to external xterm through apphost. Use the external app for a full command line. |
| `browser` | Browser launch/shell screen; apphost hosts the external browser window. In that window, Back navigates back and a second Back closes the app. |

## Settings and system screens

| ID | Purpose and controls |
|---|---|
| `set-root` | Settings root and links to sections. |
| `set-display` | Display and appearance: theme, accent, brightness, and related options when available on the device. |
| `set-wifi` | Wi-Fi status and controls; actual actions depend on the installed network service. |
| `set-keys` | Hardware-key and key-backlight settings. |
| `set-sound` | Sound and MPD; actual playback volume depends on the service. |
| `set-power` | Power and backlight options. Safe suspend or panel power-off is not promised. |
| `set-storage` | Storage information and actions; operations depend on mounted volumes and permissions. |
| `set-apps` | External-app list and launch settings. Only one external GUI application is supported at a time. |
| `set-access` | Access and interface-behavior settings. |
| `set-system` | System information and actions available to the device profile. |
| `set-lab` | Experimental options; do not treat them as stable user-facing features. |
| `set-about` | Device and shell information. |
| `set-launch` | Choose the shell/session startup mode, saved for the next start. |

## Overlays and special screens

| ID | Purpose and controls |
|---|---|
| `shade` | Quick-settings shade. Pull down from the top edge to open and swipe up to close. Holding Home also opens it. |
| `menu` | Context command sheet; the hardware Menu key opens or closes it. |
| `lock` | Lock screen. A short Power press toggles the lock screen and return to the shell; PIN protection is not claimed. |
| `power` | Power confirmation screen. Holding Power opens it; shutdown depends on system permissions and the command. |
| `reader` | Registered reading screen. A complete reading workflow and supported formats should be considered unverified unless confirmed in a specific build. |
| `video` | Registered video screen. Video playback is not considered a ready feature; hardware decoding is not assumed. |

## Shared controls and limitations

- Back, Escape, and Backspace return to the previous page; an edge swipe starting within about 20 px of the left side calls `back()`.
- Home returns to the main screen. Menu opens the command sheet; Search opens the app list.
- A short Power press toggles the lock screen; holding it opens the power screen. Behavior can differ while an external app is active.
- A downward pull starts at the top edge; do not confuse it with vertical movement on a slider. The player pivot uses a horizontal gesture.
- While apphost is active, the shell intercepts hardware Back/Home/Power. Back may first hide the on-screen keyboard. The external app is closed separately; multitasking is not implemented.
- Not every long screen supports vertical scrolling. Do not assume every list scrolls or that a settings item means a system service is available.
- Check `os/src/registry.cpp` for registered names and actual screen membership; implementations live in `os/screens/`.

## Behavior when services are unavailable

MPD screens are part of the shell, but playback control requires a working MPD service. With the server disconnected, library, queue, metadata, and seeking may be empty or unavailable; the interface must not substitute sample data.

Network settings depend on the Wi-Fi driver and system network manager. The presence of `set-wifi` does not mean that scanning, connecting, or displaying an address is available in every image. Likewise, Bluetooth/GPS entries do not confirm that the corresponding hardware works.

The external browser and terminal run in a separate X window, temporarily covering shell content. Only one such window can run; starting another external app before closing the first is unsupported. If no window appears before the timeout, the shell regains control and reports an error.

## Navigation details

Ordinary Back pops the top page from the stack; at the root, it returns to `home`. Opening `home` clears the previous navigation chain. Screen overlays may draw over the current page and close by gesture or key.

Hardware Home normally returns to the main screen on release. Holding Home is not a task switcher: the current handler opens `shade`. Holding Power opens `power`; closing an external app is handled separately by apphost.

Volume keys control MPD volume when the client is connected. Menu opens the command sheet and Search opens `apps`. External X applications receive ordinary input; reserved Back/Home/Power keys are handled by the shell, and system keys are not sent to the screen below an external window.

## Scope of this document

The table describes registered IDs and shared control behavior, not a guaranteed set of buttons or services in every build. Check `os/screens/`, `os/src/app.cpp`, `os/src/apphost.cpp`, and `os/src/registry.cpp` for implementation and launch conditions.

Reader and video are registered, but that alone does not confirm product readiness or hardware performance. The system shade is also not a full notification infrastructure: a persistent notification queue and heads-up overlays over external windows are not claimed.

