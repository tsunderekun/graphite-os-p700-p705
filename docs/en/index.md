[Русский](../ru/index.md) · **English**

# Documentation — technical plans

Snapshot, 26 Nov 2026: the shell runs on the test P705. Implemented features
include two task slots, a notification shade, PIN lock, a TXT/FB2/FB2.zip/EPUB
2/3 reader, PNG/JPEG viewer, and clock/timer. Some new screens have only been
host-tested; individual pages state the verification status and limitations.
This is not a release image.

Russian originals of the plans and studies for the graphite shell project (LG P700). The texts were written for internal use, in working order: these are engineering notes with numbers and decisions, not polished documentation.

English versions live in `docs/en/`; key status information is kept in sync.

## Map

| file | subject |
|---|---|
| [architecture.md](architecture.md) | shell architecture: X session, windows, screens, keyboard, external applications |
| [install.md](install.md) | installing postmarketOS: kernel, boot image, p9/p20, first boot, recovery |
| [design-system.md](design-system.md) | tokens, 480×800 grid, components, states |
| [screens.md](screens.md) | 15 Metro screens with dimensions and behaviour |
| [metro.md](metro.md) | what is borrowed from Windows Phone 7/8 and what is not |
| [themes.md](themes.md) | 7 themes, 5 accents, textures for RGB565 |
| [settings-spec.md](settings-spec.md) | settings: 11 Android groups mapped to ours, 26 screens, 35 keys |
| [input.md](input.md) | shared on-screen keyboard and input for our own and foreign applications |
| [shell-chrome.md](shell-chrome.md) | notification shade, long Home, task manager |
| [player.md](player.md) | player features, MPD 0.24, `idle`, `readpicture` |
| [reader.md](reader.md) | reader as a shell screen: TXT, FB2, EPUB, pagination |
| [apps.md](apps.md) | application and library audit, P0/P1/P2 priorities |
| [usb-modes.md](usb-modes.md) | USB: RNDIS, mass storage, charge-only, serial |
| [power.md](power.md) | power, heat, suspend, behaviour without a battery |
| [hardware.md](hardware.md) | hardware, kernel, services, known pitfalls |
| [qa.md](qa.md) | test plan, burn-in, `qa/` scripts |
| [font-medeo.md](font-medeo.md) | Medeo: tabular figures, baseline, integration |
| [icons.md](icons.md) | Phosphor/Lucide/Iconoir, selection and integration |
| [master-plan.md](master-plan.md) | summary: priorities, contradictions, acceptance criteria |

## How to read

Start with `master-plan.md`: it contains the summary and the order of work. Then follow your interest: `architecture.md` and `design-system.md` for how the shell is built, `settings-spec.md` for the largest UI chunk, `hardware.md` for platform constraints.

Links inside the texts may point to files of the original working tree (`notes/`, `artifacts/`) that are not present in the public repository.
