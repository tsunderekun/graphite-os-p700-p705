[Русский оригинал](../ru/index.md) · **English**

# Documentation — technical plans

English versions of the project plans are being translated from the Russian
originals in [`docs/ru/`](../ru/index.md). Until they land, the Russian files
are the source of truth.

Planned documents, one to one with `docs/ru/`:

| file | about |
|---|---|
| `architecture.md` | shell architecture: X session, windows, screens, keyboard, external apps |
| `design-system.md` | tokens, the 480×800 grid, components, states |
| `screens.md` | the 15 Metro screens with sizes and behaviour |
| `metro.md` | what was taken from Windows Phone 7/8, and what was not |
| `themes.md` | 7 themes, 5 accents, textures for RGB565 |
| `settings-spec.md` | settings: 11 Android groups mapped onto ours, 26 screens, 35 keys |
| `input.md` | one on-screen keyboard and input path for our screens and foreign apps |
| `shell-chrome.md` | notification shade, long Home, task switcher |
| `player.md` | player features, MPD 0.24, `idle`, `readpicture` |
| `reader.md` | the reader as a shell screen: TXT, FB2, EPUB, pagination |
| `apps.md` | app and library audit, P0/P1/P2 priorities |
| `usb-modes.md` | USB: RNDIS, mass storage, charge-only, serial |
| `power.md` | power, heat, suspend, running without a battery |
| `hardware.md` | hardware, kernel, services, known traps |
| `qa.md` | test plan, burn-in, the `qa/` scripts |
| `font-medeo.md` | Medeo: tabular figures, baseline, integration |
| `icons.md` | Phosphor/Lucide/Iconoir, selection and integration |
| `master-plan.md` | summary: priorities, contradictions, acceptance criteria |

For now, start with the English [`README.en.md`](../../README.en.md) and
`os/README.md` for the build, and read `docs/ru/` for everything else.
