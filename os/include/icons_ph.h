// Phosphor Regular icon set (MIT) baked into polygons -- see os/tools/bake_icons.py.
// Drop-in companion of icon(): icon_ph() returns false for an unknown name so the
// caller can keep drawing the old hand-made glyph.
#pragma once
#include <stdint.h>

struct Gfx;

// One closed contour.  pts is a flat [x0,y0,x1,y1,...] block in 1/16 px on the
// 24 px icon grid (y down); the contour is implicitly closed and is filled
// even-odd together with the other contours of the same glyph.
struct IconPoly { const int16_t *pts; uint16_t n; };

// One glyph: our icon id plus its contours.
struct IconEnt { const char *name; const IconPoly *polys; uint8_t npolys; };

// Draw a Phosphor glyph at (x, y) in a size x size box, tinted with color
// (565).  Returns false (and draws nothing) when the name is unknown.
bool icon_ph(Gfx &g, const char *name, int x, int y, int size, uint16_t color);

// Introspection for host tests: how many glyphs are baked, and their names.
int icon_ph_count();
const char *icon_ph_name(int i);
