# graphite shell design system

The interface targets the portrait display of the LG P700. Its shared API and UI primitives are defined in `os/include/graphite.h` and `os/src/ui.cpp`.

## Grid and typography

- The canvas is 480×800 pixels in RGB565. The base spacing rhythm is 8 px.
- Side margins are 24 px, leaving 432 px of main content (x=24…456). Row separators may span the full display width.
- The status bar is 32 px high. A standard screen header is 64 px and ends at y=96; pivot screens may use a shorter header.
- Section spacing follows a 28 px step. Section labels use 12 px bold uppercase text, with added tracking to distinguish them from ordinary captions.
- Core type sizes are 24 px for screen titles, 15–16 px for primary text, 12 px for secondary captions, and 11 px for utility labels. Text must not go below 11 px.
- Medeo is the primary font family (Regular, Bold, ExtraLight, and Mono); DejaVu is the fallback. The monospaced slot is used by the terminal.
- Icons use the shared `icon()` set in a Phosphor-like line style, not emoji.

## Palette and themes

The `Theme` tokens cover the `bg` background, `bar` bands, `surf`/`surf2` surfaces, lines, primary and muted text, selection, numbering, notes, and accents. The main visual direction is a graphite dark surface with a teal accent. Accent communicates an action or state rather than decoration.

The current theme mechanism provides 7 themes and 5 accent options. Names and exact values are defined in `os/src/theme.cpp`; screens should use `theme.*` instead of local hexadecimal colors.

The panel displays RGB565, so color choices and textures must account for channel quantization. Textures should remain low-contrast and must not compromise legibility. Any treatment not checked on the panel should be considered unverified.

## Components

- `ui_bg`, `ui_status`, `ui_header`, and `ui_section` provide the background and shared screen structure.
- `ui_row` is a list row with a name, caption, value, chevron, and accent state; `ui_tile` is used for tiles.
- `ui_switch`, `ui_slider`, and `ui_seg` provide a switch, slider, and segmented choice. Where possible, active state should not rely on color alone.
- `ui_chevron` and `icon()` provide shared symbols; `ui_toast` displays brief feedback.
- Components use square geometry and thin separators. Shadows and gradients are not used.

## Interaction sizes and states

- The minimum target tap area is 44×44 px. Do not shrink the hit area to make a visual control more compact.
- A list row is about 56 px high; labels and values must not overlap. Long captions are truncated with a visible ellipsis.
- Press feedback changes fill or color immediately. Selection is shown with an accent, marker, or selected background.
- Provide empty, unavailable-service, loading/action-error, and destructive-confirmation states. A toast must not be the only indication of an irreversible failure.
- Animations, shadows, and gradients are excluded: the CPU is single-core, there is no graphics acceleration, and panel refresh is rate-limited.

Exact spacing may vary by screen, but must preserve the 480×800 canvas, minimum text size, and touch targets. Tokens and components are shared shell contracts; they do not imply that every state on every screen has passed hardware acceptance.

## Screen-level guidance

- Leave room in the header for Back and a short title. Fit long titles to the available width instead of moving them into the status bar.
- Place sections on a 28 px step and rows on a consistent vertical rhythm. Use horizontal separators to group content rather than decorative boxes around every item.
- Keep the music live tile visually distinct from ordinary home shortcuts. Dynamic content needs a neutral state when MPD is unavailable.
- Show only known status in the bar and shade. Do not draw active Wi-Fi, USB power, or headphones based on a static mock-up.
- Sliders should respond to horizontal drag; vertical drag must not accidentally change their values. Buttons and switches respond to taps. Reserve long press for an explicit edit mode.
- If a screen cannot fit its list, provide scrolling or limit the content honestly; do not rely on a gesture that the specific screen does not implement.

## Accessibility and errors

Every action should have a visible state: selected value, current item, disabled control, or brief result message. Color must not be the sole carrier of meaning; add text, a marker, an icon, or switch position.

MPD, Wi-Fi, file-write, and external-window launch errors should leave the interface usable. A blank screen is not an adequate “unavailable” message. Deleting data or shutting down requires a separate confirmation.

Localized strings may be longer in English or Russian. Check them within the 432 px content width at the actual font size; do not routinely shrink type below 11 px to fit translations. Test focus and keyboard control separately from touch gestures.

## Performance and verification

Drawing runs on the CPU in a software Canvas. Screens should avoid unnecessary full redraws and expensive calculations in `draw()`. Network requests and long file operations must not block the X event loop.

Before adding a visual state, check the host preview build, 480×800 geometry, and RGB565 contrast. A host preview does not verify the touchscreen, hardware keys, backlight, panel, or system services on the phone.

The 7 themes and 5 accents must remain legible at the panel’s low color depth. Pay particular attention to muted text, thin lines, and active teal states. A shade or texture not checked on the panel should be explicitly treated as unverified.

