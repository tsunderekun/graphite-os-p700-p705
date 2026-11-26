// Phosphor Regular icons (MIT) -- runtime side.
//
// The glyph geometry is baked by os/tools/bake_icons.py into icons_ph_data.inc:
// closed contours in 1/16 px on a 24 px grid, already optically normalised
// (bbox centred, largest glyph = 0.92 * 24).  Here we only scale them to the
// requested size and fill them with a scanline rasteriser:
//
//   * two sub-scanlines per pixel row (y at +0.25 / +0.75),
//   * analytic horizontal coverage at the span ends (that is 2x2 quality
//     without a 2x2 mask table, and it keeps curves smooth at 100-140 px),
//   * even-odd fill, one int16 coverage row, then Canvas::blend().
//
// No allocation, no filters, no state: the cost for a 34 px glyph is ~25k edge
// tests, for a 140 px placeholder ~100k, i.e. well under a millisecond per
// icon on the phone.
#include "icons_ph.h"
#include "graphite.h"
#include <math.h>
#include <string.h>

#include "icons_ph_data.inc"

#ifdef ICON_PH_DIAG
// host-only instrumentation: worst-case crossings per sub-scanline
int icon_ph_diag_max_cross = 0;
#endif

namespace {

const int   kMaxPx     = 256;   // coverage row buffer / clamp for size
const int   kMaxCross  = 128;   // crossings per sub-scanline (max seen: 12)
const float kInvFixed  = 1.0f / 16.0f;
const int   kCovUnit   = 256;   // coverage units per sub-scanline pixel
const int   kSubRows   = 2;

const IconEnt *find_icon(const char *name) {
    if (!name || !*name) return 0;
    for (int i = 0; i < s_icon_count; i++)
        if (!strcmp(s_icons[i].name, name)) return &s_icons[i];
    return 0;
}

inline void add_span(int16_t *cov, int w, float xa, float xb) {
    if (xa < 0.0f) xa = 0.0f;
    if (xb > (float)w) xb = (float)w;
    if (xb <= xa) return;
    int i0 = (int)xa;
    int i1 = (int)ceilf(xb) - 1;
    if (i0 < 0) i0 = 0;
    if (i1 >= w) i1 = w - 1;
    for (int i = i0; i <= i1; i++) {
        float l = xa > (float)i ? xa : (float)i;
        float r = xb < (float)(i + 1) ? xb : (float)(i + 1);
        if (r > l) cov[i] = (int16_t)(cov[i] + (int)((r - l) * kCovUnit + 0.5f));
    }
}

inline void isort(float *v, int n) {
    for (int i = 1; i < n; i++) {
        float x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
}

} // namespace

bool icon_ph(Gfx &g, const char *name, int x, int y, int size, uint16_t color) {
    const IconEnt *e = find_icon(name);
    if (!e) return false;
    if (size <= 0 || e->npolys == 0) return true;
    if (size > kMaxPx) size = kMaxPx;

    Canvas &cv = g.cv;
    bool body = !(g.clip_bottom <= g.clip_top || y + size <= g.clip_top || y >= g.clip_bottom);
    if (body) y += g.offset_y;
    if (x >= cv.w || y >= cv.h || x + size <= 0 || y + size <= 0) return true;

    const float s = (float)size / 24.0f;
    int ry0 = 0, ry1 = size;
    if (y < 0) ry0 = -y;
    if (y + size > cv.h) ry1 = cv.h - y;
    if (body) {
        if (g.clip_top > y && ry0 < g.clip_top - y) ry0 = g.clip_top - y;
        if (g.clip_bottom < y + ry1) ry1 = g.clip_bottom - y;
    }
    int cx0 = 0, cx1 = size;
    if (x < 0) cx0 = -x;
    if (x + size > cv.w) cx1 = cv.w - x;

    int16_t cov[kMaxPx];
    float xs[kMaxCross];

    for (int py = ry0; py < ry1; py++) {
        memset(cov, 0, sizeof(int16_t) * (size_t)size);
        for (int sr = 0; sr < kSubRows; sr++) {
            const float iy = ((float)py + (sr ? 0.75f : 0.25f)) / s;
            int nc = 0;
            for (int k = 0; k < e->npolys; k++) {
                const IconPoly &p = e->polys[k];
                const int16_t *v = p.pts;
                int n = p.n;
                for (int i = 0; i < n; i++) {
                    int j = (i + 1 < n) ? i + 1 : 0;
                    float y0 = v[2 * i + 1] * kInvFixed;
                    float y1 = v[2 * j + 1] * kInvFixed;
                    if ((y0 <= iy) == (y1 <= iy)) continue;
                    float x0 = v[2 * i] * kInvFixed;
                    float x1 = v[2 * j] * kInvFixed;
                    float t = (iy - y0) / (y1 - y0);
                    if (nc < kMaxCross) xs[nc++] = (x0 + t * (x1 - x0)) * s;
                }
            }
            if (nc < 2) continue;
#ifdef ICON_PH_DIAG
            if (nc > icon_ph_diag_max_cross) icon_ph_diag_max_cross = nc;
#endif
            isort(xs, nc);
            for (int i = 0; i + 1 < nc; i += 2) add_span(cov, size, xs[i], xs[i + 1]);
        }
        for (int px = cx0; px < cx1; px++) {
            int cvg = cov[px];
            if (!cvg) continue;
            int a = (cvg * 255 + (kCovUnit * kSubRows) / 2) / (kCovUnit * kSubRows);
            if (a) cv.blend(x + px, y + py, color, a > 255 ? 255 : a);
        }
    }
    return true;
}

int icon_ph_count() { return s_icon_count; }

const char *icon_ph_name(int i) {
    return (i >= 0 && i < s_icon_count) ? s_icons[i].name : 0;
}
