# Third-party components

The MIT license in `LICENSE` covers the code in this repository. The files
below come from other projects and keep their original licenses.

## Fonts

### DejaVu Sans (`shell/fonts/DejaVuSans*.ttf`)

Bitstream Vera Fonts Copyright (c) 2003 Bitstream, Inc. DejaVu changes are in
the public domain. The full text ships with the fonts in
`shell/fonts/LICENSE-DejaVu.txt` (also available at
<https://dejavu-fonts.github.io/License.html> and
`/usr/share/doc/fonts-dejavu*/copyright` on Debian/Alpine).

The license permits free use, modification and redistribution, including in
bundled form, provided the copyright notice and license are kept with the
font files and modified versions are not distributed under the reserved names
"Bitstream" or "Vera".

### Medeo (`os/fonts/medeo/*.ttf`)

Medeo by DNFDEVS (<https://github.com/DNFDEVS/medeo-font>), released under the
SIL Open Font License 1.1. The license text ships next to the fonts in
`os/fonts/medeo/OFL.txt`.

The copies here carry one local patch: tabular figures (`tnum`), applied to the
`cmap` table only. Copyright and `name` records are unchanged, so the modified
fonts stay under the OFL; they are not sold on their own.

## Icons

### Phosphor Icons (`os/icons/ph/*.svg`)

MIT License, Copyright (c) 2023 Phosphor Icons. Full text in
`os/icons/ph/LICENSE`; the mapping from our names to upstream icon names, with
the version and URLs, is in `os/icons/ph/SOURCES.json`.

## Code

### stb_image (`os/src/stb_image.h`)

stb_image v2.34 by Sean Barrett and contributors. Dual licensed: public domain
(Unlicense) or MIT, at your choice. See the header of the file for the full
text.

## Not bundled

The repository does not include LG firmware, KDZ/IMG images, Android platform
tools or firmware extraction utilities. Install those separately from their
own sources and under their own licenses.
