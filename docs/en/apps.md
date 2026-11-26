# graphite shell apps

This page describes current built-in app behavior, not earlier development plans.

## Apps

- **Player** controls MPD playback and shows the current track, queue, track information, and core playback actions.
- **Files** browses directories. `.txt`, `.fb2`, and `.fb2.zip` open in Reader; Reader also supports `.epub`; `.png`, `.jpg`, and `.jpeg` open in the image viewer. Unsupported types show a no-app message.
- **Notes** opens and saves text notes.
- **Reader** supports TXT, FB2, FB2-in-ZIP, and EPUB 2/3. See [Reader](reader.md) for scope and limitations.
- **Terminal** provides the built-in console and a way to launch an external terminal.
- **Browser** is represented in the shell; web capabilities depend on the available configuration.
- **Image viewer** opens PNG/JPEG. Wide photos are displayed in landscape orientation.
- **Clock and timer** show the time. The timer has 1, 5, 10, and 15-minute presets, pause, reset, and one-minute adjustment. It has no sound alert or background operation.
- **USB status** shows the USB state available to the shell.

## Verification

The viewer and clock/timer were verified on the P705: photos open, wide ones rotate to landscape, and the timer counts, pauses and resets. Device verification is done; host tests remain supplementary.

## Next

Check the remaining scenarios on the device (EPUB with images, long scrolls); do not read historical P0/P1 lists as a list of currently missing features.
