#include "graphite.h"
#include "icons_ph.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>

// ---------------------------------------------------------------- colors
uint16_t rgb565(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint16_t hex565(const char *s) {
    if (!s) return 0;
    if (*s == '#') s++;
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = hexval(s[i]);
        if (v[i] < 0) return 0;
    }
    int r = v[0] * 16 + v[1], g = v[2] * 16 + v[3], b = v[4] * 16 + v[5];
    return rgb565(r, g, b);
}

// ---------------------------------------------------------------- canvas
void Canvas::clear(uint16_t c) { std::fill(px.begin(), px.end(), c); }

void Canvas::rect(int x, int y, int ww, int hh, uint16_t c) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(w, x + ww), y1 = std::min(h, y + hh);
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++)
        std::fill(px.begin() + (size_t)yy * w + x0,
                  px.begin() + (size_t)yy * w + x1, c);
}

void Canvas::frame(int x, int y, int ww, int hh, uint16_t c) {
    hline(x, y, ww, c);
    hline(x, y + hh - 1, ww, c);
    vline(x, y, hh, c);
    vline(x + ww - 1, y, hh, c);
}

void Canvas::hline(int x, int y, int ww, uint16_t c) { rect(x, y, ww, 1, c); }
void Canvas::vline(int x, int y, int hh, uint16_t c) { rect(x, y, 1, hh, c); }

void Canvas::blend(int x, int y, uint16_t c, int a) {
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
    if (a >= 250) { px[(size_t)y * w + x] = c; return; }
    if (a <= 0) return;
    uint16_t &d = px[(size_t)y * w + x];
    int dr = ((d >> 11) & 31) * 255 / 31;
    int dg = ((d >> 5) & 63) * 255 / 63;
    int db = (d & 31) * 255 / 31;
    int sr = ((c >> 11) & 31) * 255 / 31;
    int sg = ((c >> 5) & 63) * 255 / 63;
    int sb = (c & 31) * 255 / 31;
    int r = dr + (sr - dr) * a / 255;
    int g = dg + (sg - dg) * a / 255;
    int b = db + (sb - db) * a / 255;
    d = rgb565(r, g, b);
}

void Canvas::disc(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::sqrt((double)(r * r - dy * dy));
        hline(cx - dx, cy + dy, dx * 2 + 1, c);
    }
}

void Canvas::ring(int cx, int cy, int r, int t, uint16_t c) {
    int ri = std::max(0, r - t);
    for (int dy = -r; dy <= r; dy++) {
        int xo = (int)std::sqrt((double)(r * r - dy * dy));
        int xi = (dy >= -ri && dy <= ri)
                     ? (int)std::sqrt((double)(ri * ri - dy * dy)) : -1;
        if (xi < 0) hline(cx - xo, cy + dy, xo * 2 + 1, c);
        else {
            hline(cx - xo, cy + dy, xo - xi, c);
            hline(cx + xi + 1, cy + dy, xo - xi, c);
        }
    }
}

void Canvas::triangle(int cx, int cy, int size, bool right, uint16_t c) {
    for (int dy = -size; dy <= size; dy++) {
        int len = (int)((1.0 - std::abs((double)dy) / (size + 0.5)) * size);
        if (len < 1) len = 1;
        if (right) rect(cx - size / 3, cy + dy, len, 1, c);
        else       rect(cx + size / 3 - len, cy + dy, len, 1, c);
    }
}

void Canvas::copy_from(const Canvas &src) {
    size_t n = std::min(px.size(), src.px.size());
    std::memcpy(px.data(), src.px.data(), n * sizeof(uint16_t));
}

void Gfx::begin_body(int top, int bottom, int scroll) {
    offset_y = -scroll; clip_top = top; clip_bottom = bottom;
}
void Gfx::end_body() { offset_y = clip_top = clip_bottom = 0; }
void Gfx::rect(int x, int y, int w, int h, uint16_t c) {
    bool body = !(clip_bottom <= clip_top || y + h <= clip_top || y >= clip_bottom);
    if (body) {
        int yy = y + offset_y;
        int y0 = std::max(yy, clip_top);
        int y1 = std::min(yy + h, clip_bottom);
        cv.rect(x, y0, w, y1 - y0, c);
    } else cv.rect(x, y, w, h, c);
}
void Gfx::frame(int x, int y, int w, int h, uint16_t c) {
    hline(x, y, w, c); hline(x, y + h - 1, w, c);
    vline(x, y, h, c); vline(x + w - 1, y, h, c);
}
void Gfx::hline(int x, int y, int w, uint16_t c) { rect(x, y, w, 1, c); }
void Gfx::vline(int x, int y, int h, uint16_t c) { rect(x, y, 1, h, c); }

// ---------------------------------------------------------------- text
unsigned long gfx_text_us = 0, gfx_textw_us = 0;
namespace {
struct TScope {
    struct timespec a;
    unsigned long *acc;
    explicit TScope(unsigned long *p) : acc(p) { clock_gettime(CLOCK_MONOTONIC, &a); }
    ~TScope() {
        struct timespec b;
        clock_gettime(CLOCK_MONOTONIC, &b);
        *acc += (unsigned long)(b.tv_sec - a.tv_sec) * 1000000UL +
                (unsigned long)((b.tv_nsec - a.tv_nsec) / 1000);
    }
};
}
static FT_Library ft_lib = nullptr;
static const char *ft_paths[4] = {nullptr, nullptr, nullptr, nullptr};
struct FaceKey {
    int style, px;
    bool operator<(const FaceKey &o) const {
        return style != o.style ? style < o.style : px < o.px;
    }
};
static std::map<FaceKey, FT_Face> faces;

struct GlyphKey {
    int style, px;
    uint32_t cp;
    bool operator<(const GlyphKey &o) const {
        if (style != o.style) return style < o.style;
        if (px != o.px) return px < o.px;
        return cp < o.cp;
    }
};
struct CachedGlyph {
    int w, h, left, top, pitch, advance;
    std::vector<unsigned char> bitmap;
};
static std::map<GlyphKey, CachedGlyph> glyphs;
static std::deque<GlyphKey> glyph_fifo;
static size_t glyph_bytes = 0;
static const size_t kGlyphLimit = 4096;
static const size_t kGlyphBytesLimit = 8 * 1024 * 1024;
static FT_Face face_for(int style, int px);

static const CachedGlyph *glyph_for(int style, int px, uint32_t cp) {
    GlyphKey key{style, px, cp};
    auto found = glyphs.find(key);
    if (found != glyphs.end()) return &found->second;
    FT_Face f = face_for(style, px);
    if (!f || FT_Load_Char(f, cp, FT_LOAD_RENDER)) return nullptr;
    FT_GlyphSlot gl = f->glyph;
    CachedGlyph g;
    g.w = (int)gl->bitmap.width;
    g.h = (int)gl->bitmap.rows;
    g.left = gl->bitmap_left;
    g.top = gl->bitmap_top;
    g.pitch = gl->bitmap.pitch;
    g.advance = gl->advance.x >> 6;
    const int stride = std::abs(g.pitch);
    g.bitmap.resize((size_t)stride * g.h);
    for (int row = 0; row < g.h; ++row) {
        const unsigned char *src = gl->bitmap.buffer + row * g.pitch;
        std::memcpy(g.bitmap.data() + (size_t)row * stride, src, stride);
    }
    const size_t bytes = g.bitmap.size();
    while (!glyph_fifo.empty() &&
           (glyphs.size() >= kGlyphLimit || glyph_bytes + bytes > kGlyphBytesLimit)) {
        auto old = glyphs.find(glyph_fifo.front());
        if (old != glyphs.end()) { glyph_bytes -= old->second.bitmap.size(); glyphs.erase(old); }
        glyph_fifo.pop_front();
    }
    if (bytes > kGlyphBytesLimit) return nullptr;
    glyph_fifo.push_back(key);
    glyph_bytes += bytes;
    return &glyphs.emplace(key, std::move(g)).first->second;
}

// Font slots: 0 regular, 1 bold, 2 light/display, 3 mono. Medeo is preferred,
// DejaVu in the same directory and the system DejaVu are the fallbacks, so a
// partial install (or no Medeo at all) still works. Medeo is never installed
// over DejaVu: xterm resolves "DejaVu Sans Mono" through fontconfig.
static const char *kMedeo[4] = {
    "MedeoSans-Regular.ttf", "MedeoSans-Bold.ttf",
    "MedeoSansDisplay-ExtraLight.ttf", "MedeoMono-Regular.ttf"};
static const char *kDejavu[4] = {
    "DejaVuSans.ttf", "DejaVuSans-Bold.ttf",
    "DejaVuSans-ExtraLight.ttf", "DejaVuSansMono.ttf"};

bool gfx_init(const char *dir) {
    if (const char *env = std::getenv("GRAPHITE_FONTS")) dir = env;
    if (FT_Init_FreeType(&ft_lib)) return false;

    // The header's default is still the old DejaVu path; expand it into the
    // Medeo-first system search. Any other explicit directory (GRAPHITE_FONTS
    // or the argument) is authoritative, with the system DejaVu left as a last
    // resort, so a test install can force DejaVu on a device that has Medeo.
    const char *kDefaultDir = "/usr/share/fonts/dejavu";
    std::vector<std::string> dirs;
    if (dir && *dir && std::strcmp(dir, kDefaultDir) != 0)
        dirs.push_back(dir);
    else
        dirs.push_back("/usr/share/fonts/medeo");
    if (std::find(dirs.begin(), dirs.end(), kDefaultDir) == dirs.end())
        dirs.push_back(kDefaultDir);

    static std::string p[4];
    for (int i = 0; i < 4; i++) {
        for (int pass = 0; pass < 2 && p[i].empty(); pass++) {
            const char **names = pass == 0 ? kMedeo : kDejavu;
            for (const std::string &d : dirs) {
                std::string path = d + "/" + names[i];
                FT_Face f = nullptr;
                if (FT_New_Face(ft_lib, path.c_str(), 0, &f) == 0) {
                    p[i] = path;
                    FT_Set_Pixel_Sizes(f, 0, 15);
                    FaceKey k{i, 15};
                    faces[k] = f;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < 4; i++) ft_paths[i] = p[i].empty() ? nullptr : p[i].c_str();
    return !faces.empty();
}

static FT_Face face_for(int style, int px) {
    FaceKey k{style, px};
    auto it = faces.find(k);
    if (it != faces.end()) return it->second;
    FT_Face f = nullptr;
    if (!ft_lib || !ft_paths[0]) return nullptr;
    const char *path = ft_paths[style] ? ft_paths[style] : ft_paths[0];
    if (FT_New_Face(ft_lib, path, 0, &f)) {
        if (style != 0 && !FT_New_Face(ft_lib, ft_paths[0], 0, &f)) {
            // fall back to the regular face (host may lack ExtraLight/Bold)
        } else {
            return nullptr;
        }
    }
    FT_Set_Pixel_Sizes(f, 0, px);
    faces[k] = f;
    return f;
}

static void utf8(const std::string &s, std::vector<uint32_t> &out) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { i++; continue; }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
}

static int style_of(int flags) {
    if (flags & TF_MONO) return 3;
    if (flags & TF_BOLD) return 1;
    if (flags & TF_LIGHT) return 2;
    return 0;
}

int Gfx::textw(const std::string &s, int px, int tracking) {
    TScope ts(&gfx_textw_us);
    std::vector<uint32_t> cps;
    utf8(s, cps);
    int total = 0;
    for (uint32_t cp : cps) {
        const CachedGlyph *g = glyph_for(0, px, cp);
        if (g) total += g->advance;
    }
    if (!cps.empty() && tracking) total += (int)(cps.size() - 1) * tracking;
    return total;
}

int Gfx::text(int x, int y, const std::string &s, int px, uint16_t c, int flags) {
    TScope ts(&gfx_text_us);
    int style = style_of(flags);
    if (s.empty()) return 0;
    std::vector<uint32_t> cps;
    utf8(s, cps);

    int total = 0;
    for (uint32_t cp : cps) {
        const CachedGlyph *g = glyph_for(style, px, cp);
        if (g) total += g->advance;
    }
    if (flags & TF_CENTER) x -= total / 2;
    if (flags & TF_RIGHT) x -= total;

    // Baseline normalization. Medeo's hhea ascender is 1.005 em against
    // DejaVu's 0.928, which would push every line 1-8 px down (hours on the
    // lock screen, headers crowding their captions). Use DejaVu's ratio for
    // any face whose design ascender exceeds 0.97 em, so DejaVu output stays
    // byte-for-byte identical and Medeo lands on the same baseline. The test
    // is on the design ascender, not the pixel one: at 11 px the scaled
    // metrics would misfire.
    //
    // ceil, not round: FreeType's scaled ascender for DejaVu is exactly
    // ceil(0.928 * px) at every size 11-40 (checked), so this reproduces the
    // DejaVu baseline to the pixel at 11/12/24, where round() would sit 1 px
    // high and lift the status bar and the terminal grid off the DejaVu rows.
    FT_Face f = face_for(style, px);
    if (!f) return 0;
    int asc = f->size->metrics.ascender >> 6;
    float dr = f->units_per_EM ? (float)f->ascender / (float)f->units_per_EM : 0.f;
    if (dr > 0.97f) asc = (int)std::ceil(px * 0.928f);
    if (asc <= 0) asc = (int)(px * 0.78f);
    bool body = !(clip_bottom <= clip_top || y + px <= clip_top || y >= clip_bottom);
    int base = y + asc + (body ? offset_y : 0);

    int cursor = x;
    for (uint32_t cp : cps) {
        const CachedGlyph *gl = glyph_for(style, px, cp);
        if (!gl) continue;
        int gx = cursor + gl->left;
        int gy = base - gl->top;
        const int stride = std::abs(gl->pitch);
        for (int row = 0; row < gl->h; row++) {
            const unsigned char *rp = gl->bitmap.data() + (size_t)row * stride;
            for (int col = 0; col < gl->w; col++) {
                int a = rp[col];
                int py = gy + (int)row;
                if (a && (!body || (py >= clip_top && py < clip_bottom)))
                    cv.blend(gx + (int)col, py, c, a);
            }
        }
        cursor += gl->advance;
    }
    return total;
}

std::string Gfx::fit(const std::string &s, int px, int maxw) {
    if (textw(s, px) <= maxw) return s;
    std::string out = s;
    while (!out.empty()) {
        out.pop_back();
        // do not cut inside a UTF-8 sequence
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80)
            out.pop_back();
        if (textw(out + "…", px) <= maxw) return out + "…";
    }
    return "…";
}

// ---------------------------------------------------------------- icons
namespace {
struct Pen {
    Gfx &g;
    int ox, oy;
    float s;
    uint16_t c;
    void r(float x, float y, float w, float h) {
        g.rect(ox + (int)(x * s), oy + (int)(y * s),
               std::max(1, (int)(w * s)), std::max(1, (int)(h * s)), c);
    }
    void line(float x1, float y1, float x2, float y2, float t = 1.4f) {
        float dx = x2 - x1, dy = y2 - y1;
        int n = (int)(std::max(std::abs(dx), std::abs(dy)) * 2) + 1;
        for (int i = 0; i <= n; i++) {
            float t0 = (float)i / n;
            g.rect(ox + (int)((x1 + dx * t0) * s) - (int)(t * s / 2),
                   oy + (int)((y1 + dy * t0) * s) - (int)(t * s / 2),
                   std::max(1, (int)(t * s)), std::max(1, (int)(t * s)), c);
        }
    }
    void outline(float x, float y, float w, float h, float t = 1.4f) {
        g.rect(ox + (int)(x * s), oy + (int)(y * s), (int)(w * s), std::max(1, (int)(t * s)), c);
        g.rect(ox + (int)(x * s), oy + (int)((y + h) * s) - std::max(1, (int)(t * s)),
               (int)(w * s), std::max(1, (int)(t * s)), c);
        g.rect(ox + (int)(x * s), oy + (int)(y * s), std::max(1, (int)(t * s)), (int)(h * s), c);
        g.rect(ox + (int)((x + w) * s) - std::max(1, (int)(t * s)),
               oy + (int)(y * s), std::max(1, (int)(t * s)), (int)(h * s), c);
    }
};
} // namespace

void icon(Gfx &g, const char *name, int x, int y, int size, uint16_t c) {
    if (icon_ph(g, name, x, y, size, c)) return;
    Pen p{g, x, y, size / 24.0f, c};
    std::string n = name ? name : "";
    if (n == "music") {
        p.line(9, 16, 9, 5, 1.6f);
        p.line(9, 5, 15, 3, 1.6f);
        p.line(15, 3, 15, 7, 1.6f);
        g.cv.disc(x + (int)(6.5f * size / 24.0f), y + (int)(17.0f * size / 24.0f),
                  std::max(2, (int)(3.2f * size / 24.0f)), c);
        p.r(14, 13, 1.5f, 3);
    } else if (n == "folder") {
        p.line(2, 7, 10, 7, 1.6f); p.line(2, 7, 2, 19, 1.6f);
        p.line(2, 19, 22, 19, 1.6f); p.line(22, 19, 22, 9, 1.6f);
        p.line(22, 9, 11, 9, 1.6f); p.line(11, 9, 9, 6, 1.6f);
    } else if (n == "terminal") {
        p.outline(2, 4, 20, 16, 1.4f);
        p.line(6, 9, 9, 12, 1.5f); p.line(9, 12, 6, 15, 1.5f);
        p.r(12, 15, 6, 1.6f);
    } else if (n == "globe") {
        g.cv.ring(x + size / 2, y + size / 2, size / 2 - 1, std::max(1, (int)(size / 16.0f)), c);
        p.line(2, 12, 22, 12, 1.2f);
        for (int i = -1; i <= 1; i += 2) {
            for (int k = 0; k <= 12; k++) {
                float t = k / 12.0f;
                float yy = 2 + t * 20;
                float dx = 7.0f * (1.0f - (yy - 12) * (yy - 12) / 144.0f) * i;
                if (k) {
                    float t0 = (k - 1) / 12.0f;
                    float y0 = 2 + t0 * 20;
                    float dx0 = 7.0f * (1.0f - (y0 - 12) * (y0 - 12) / 144.0f) * i;
                    p.line(12 + dx0, y0, 12 + dx, yy, 1.0f);
                }
            }
        }
    } else if (n == "sliders") {
        p.line(3, 6, 21, 6, 1.4f); p.r(8, 4, 4, 4);
        p.line(3, 12, 21, 12, 1.4f); p.r(14, 10, 4, 4);
        p.line(3, 18, 21, 18, 1.4f); p.r(6, 16, 4, 4);
    } else if (n == "note") {
        p.outline(4, 2, 16, 20, 1.4f);
        p.line(8, 8, 16, 8, 1.2f); p.line(8, 12, 16, 12, 1.2f);
        p.line(8, 16, 13, 16, 1.2f);
    } else if (n == "grid") {
        p.outline(3, 3, 7, 7); p.outline(14, 3, 7, 7);
        p.outline(3, 14, 7, 7); p.outline(14, 14, 7, 7);
    } else if (n == "wifi") {
        float cx = 12, cy = 18.5f;
        for (int i = 0; i < 3; i++) {
            float r = 10.0f - i * 3.4f;
            float px = 0, py = 0;
            for (int k = 0; k <= 12; k++) {
                float a = (38.0f + k * 104.0f / 12.0f) * 3.14159f / 180.0f;
                float xx = cx + r * std::cos(a), yy = cy - r * std::sin(a);
                if (k) p.line(px, py, xx, yy, 1.5f);
                px = xx; py = yy;
            }
        }
        p.r(11.0f, 17.0f, 2.0f, 2.0f);
    } else if (n == "lock") {
        p.outline(5, 11, 14, 10, 1.4f);
        g.cv.ring(x + size / 2, y + (int)(9 * size / 24.0f), (int)(5 * size / 24.0f),
                  std::max(1, (int)(1.4f * size / 24.0f)), c);
        p.r(11, 15, 2, 3);
    } else if (n == "power") {
        g.cv.ring(x + size / 2, y + size / 2 + 1, size / 2 - 2,
                  std::max(1, (int)(1.6f * size / 24.0f)), c);
        p.r(11, 2, 2, 9);
    } else if (n == "battery") {
        p.outline(4, 7, 15, 10, 1.4f);
        p.r(20, 10, 2, 4);
        p.r(6, 9, 8, 6);
    } else if (n == "sd") {
        p.outline(4, 6, 16, 14, 1.4f);
        p.r(7, 3, 2, 3); p.r(11, 3, 2, 3); p.r(15, 3, 2, 3);
    } else if (n == "play") {
        g.cv.triangle(x + size / 2, y + size / 2, size / 2 - 2, true, c);
    } else if (n == "pause") {
        p.r(7, 5, 3, 14); p.r(14, 5, 3, 14);
    } else if (n == "next") {
        g.cv.triangle(x + size / 3, y + size / 2, size / 3, true, c);
        g.cv.triangle(x + size * 2 / 3, y + size / 2, size / 3, true, c);
    } else if (n == "prev") {
        g.cv.triangle(x + size / 3, y + size / 2, size / 3, false, c);
        g.cv.triangle(x + size * 2 / 3, y + size / 2, size / 3, false, c);
    } else if (n == "shuffle") {
        p.line(3, 7, 8, 7, 1.2f); p.line(8, 7, 16, 17, 1.2f);
        p.line(16, 17, 21, 17, 1.2f);
        p.line(3, 17, 8, 17, 1.2f); p.line(8, 17, 16, 7, 1.2f);
        p.line(16, 7, 21, 7, 1.2f);
        p.line(19, 4, 21, 7, 1.2f); p.line(21, 7, 19, 10, 1.2f);
    } else if (n == "repeat") {
        p.outline(4, 6, 16, 12, 1.2f);
        p.line(17, 3, 20, 6, 1.4f); p.line(20, 6, 17, 9, 1.4f);
        p.line(7, 21, 4, 18, 1.4f); p.line(4, 18, 7, 15, 1.4f);
    } else if (n == "search") {
        g.cv.ring(x + size * 10 / 24, y + size * 10 / 24, size / 4,
                  std::max(1, (int)(1.4f * size / 24.0f)), c);
        p.line(15, 15, 21, 21, 2.0f);
    } else if (n == "plus") {
        p.r(11, 4, 2, 16); p.r(4, 11, 16, 2);
    } else if (n == "check") {
        p.line(4, 12, 9, 17, 2.2f); p.line(9, 17, 20, 6, 2.2f);
    } else if (n == "close") {
        p.line(5, 5, 19, 19, 2.0f); p.line(19, 5, 5, 19, 2.0f);
    } else if (n == "sun") {
        g.cv.disc(x + size / 2, y + size / 2, size / 5, c);
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159f / 4;
            float x1 = 12 + std::cos(a) * 7, y1 = 12 + std::sin(a) * 7;
            float x2 = 12 + std::cos(a) * 10, y2 = 12 + std::sin(a) * 10;
            p.line(x1, y1, x2, y2, 1.4f);
        }
    } else if (n == "headphones") {
        float cx = 12, cy = 15, r = 7.5f;
        float px = 0, py = 0;
        for (int k = 0; k <= 16; k++) {
            float a = (200.0f + k * 140.0f / 16.0f) * 3.14159f / 180.0f;
            float xx = cx + r * std::cos(a), yy = cy + r * std::sin(a);
            if (k) p.line(px, py, xx, yy, 1.6f);
            px = xx; py = yy;
        }
        p.r(3.5f, 13.0f, 3.0f, 7.0f);
        p.r(17.5f, 13.0f, 3.0f, 7.0f);
    } else if (n == "volume") {
        p.r(4, 9, 4, 6); p.line(8, 12, 13, 6, 1.6f); p.line(8, 12, 13, 18, 1.6f);
        p.line(13, 6, 13, 18, 1.6f);
        g.cv.ring(x + (int)(13 * size / 24.0f), y + size / 2, size / 5,
                  std::max(1, (int)(1.2f * size / 24.0f)), c);
    } else if (n == "keyboard") {
        p.outline(2, 7, 20, 11, 1.2f);
        for (int i = 0; i < 4; i++) p.r(5 + i * 4, 10, 2, 2);
        p.r(8, 14, 8, 2);
    } else if (n == "bluetooth") {
        p.line(12, 3, 12, 21, 1.4f);
        p.line(12, 3, 18, 8, 1.2f); p.line(18, 8, 7, 16, 1.2f);
        p.line(12, 21, 18, 16, 1.2f); p.line(18, 16, 7, 8, 1.2f);
    } else if (n == "sim") {
        p.outline(6, 3, 12, 18, 1.4f);
        p.r(9, 16, 6, 3); p.r(9, 7, 6, 6);
    } else if (n == "info") {
        g.cv.ring(x + size / 2, y + size / 2, size / 2 - 1,
                  std::max(1, (int)(1.4f * size / 24.0f)), c);
        p.r(11, 10, 2, 8);
        p.r(11, 5, 2, 2);
    } else if (n == "back") {
        p.line(15, 5, 7, 12, 2.0f); p.line(7, 12, 15, 19, 2.0f);
    } else if (n == "chevron") {
        p.line(9, 5, 16, 12, 2.0f); p.line(16, 12, 9, 19, 2.0f);
    } else if (n == "usb") {
        p.line(12, 3, 12, 21, 1.4f);
        p.line(12, 3, 9, 7, 1.2f); p.line(12, 3, 15, 7, 1.2f);
        p.r(9, 20, 6, 3);
        p.line(12, 12, 8, 15, 1.2f); p.r(7, 14, 2, 2);
        p.line(12, 9, 16, 12, 1.2f); p.r(15, 11, 2, 2);
    } else if (n == "clock") {
        g.cv.ring(x + size / 2, y + size / 2, size / 2 - 1,
                  std::max(1, (int)(1.4f * size / 24.0f)), c);
        p.line(12, 7, 12, 12, 1.2f); p.line(12, 12, 16, 14, 1.2f);
    } else if (n == "airplane") {
        p.line(12, 2, 12, 22, 1.6f);
        p.line(3, 12, 21, 12, 1.6f);
        p.line(5, 8, 12, 12, 1.2f); p.line(19, 8, 12, 12, 1.2f);
    } else if (n == "storage") {
        p.outline(3, 6, 18, 5, 1.2f); p.outline(3, 13, 18, 5, 1.2f);
        p.r(17, 8, 2, 2); p.r(17, 15, 2, 2);
    } else if (n == "shield") {
        p.line(12, 3, 20, 7, 1.4f); p.line(20, 7, 18, 18, 1.4f);
        p.line(18, 18, 12, 21, 1.4f); p.line(12, 21, 6, 18, 1.4f);
        p.line(6, 18, 4, 7, 1.4f); p.line(4, 7, 12, 3, 1.4f);
    } else if (n == "lab") {
        p.outline(7, 12, 10, 9, 1.2f);
        p.r(4, 4, 2, 2); p.r(11, 4, 2, 2); p.r(18, 4, 2, 2);
        p.line(5, 6, 5, 10, 1.0f); p.line(12, 6, 12, 10, 1.0f); p.line(19, 6, 19, 10, 1.0f);
        p.r(5, 20, 14, 2);
    } else if (n == "trash") {
        p.line(4, 6, 20, 6, 1.6f); p.r(10, 3, 4, 3);
        p.line(6, 7, 7, 21, 1.4f); p.line(18, 7, 17, 21, 1.4f);
        p.line(7, 21, 17, 21, 1.4f);
        p.line(10, 10, 10, 18, 1.0f); p.line(14, 10, 14, 18, 1.0f);
    } else if (n == "restart") {
        g.cv.ring(x + size / 2, y + size / 2, size / 2 - 2,
                  std::max(1, (int)(1.6f * size / 24.0f)), c);
        p.r(16, 2, 3, 3);
    }
}
