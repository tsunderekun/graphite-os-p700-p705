#include "graphite.h"
#include "apphost.h"
#include "osk.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstring>
#include <string>

static Canvas *bg_cache = nullptr;
static std::string bg_key;

static void build_bg(Canvas &c) {
    c.clear(theme.bg);
    std::string tex = cfg.get("tex", "auto");
    if (tex == "off") return;

    bool grain = tex == "grain" || tex == "both";
    bool laid = tex == "laid" || tex == "both";
    if (tex == "auto") {
        if (theme.family == FAM_DARK) laid = true;
        else grain = true;
    }
    if (theme.family == FAM_DARK) grain = false;   // +40% brightness per 565 code

    if (laid) {
        int step = theme.family == FAM_DARK ? 24 : 32;
        uint16_t line = theme.family == FAM_DARK ? theme.surf2 : theme.line2;
        int alpha = theme.family == FAM_DARK ? 26 : 40;
        for (int y = step / 2; y < c.h; y += step)
            for (int x = 0; x < c.w; x++)
                c.blend(x, y, line, alpha);
    }
    if (grain) {
        // deterministic monochrome speckle, ~12% coverage, 1-2 codes
        for (int y = 0; y < c.h; y++) {
            for (int x = 0; x < c.w; x++) {
                uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
                h = (h ^ (h >> 13)) * 1274126177u;
                if (((h >> 16) % 100) < 12)
                    c.blend(x, y, theme.fg, 10);
            }
        }
    }
}

void ui_bg(Gfx &g) {
    std::string key = std::string(theme.id) + "|" + cfg.get("tex", "auto") +
                      "|" + std::to_string(theme.family);
    if (!bg_cache || bg_key != key || bg_cache->w != g.cv.w || bg_cache->h != g.cv.h) {
        if (!bg_cache) bg_cache = new Canvas(g.cv.w, g.cv.h);
        build_bg(*bg_cache);
        bg_key = key;
    }
    g.cv.copy_from(*bg_cache);
}

static int read_int_file(const char *path, int def = -1) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return def;
    int v = def;
    if (std::fscanf(f, "%d", &v) != 1) v = def;
    std::fclose(f);
    return v;
}

static bool read_word_is(const char *path, const char *word) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    char b[64] = {0};
    bool ok = std::fgets(b, sizeof(b), f) && std::strstr(b, word);
    std::fclose(f);
    return ok;
}

struct StatusCache {
    unsigned at = 0;
    int cap = -1;
    bool present = false, usb = false, charging = false;
    int h2w = 0;
    bool wifi = false;
};
static StatusCache sc;

static void status_read() {
    unsigned now_s = (unsigned)time(nullptr);
    if (sc.at && now_s - sc.at < 2) return;
    sc.at = now_s;
    sc.present = read_int_file("/sys/class/power_supply/battery/present", 0) != 0;
    sc.cap = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    sc.usb = read_int_file("/sys/class/power_supply/usb/online", 0) != 0 ||
             read_int_file("/sys/class/power_supply/ac/online", 0) != 0;
    sc.charging = read_word_is("/sys/class/power_supply/battery/status", "Charging") ||
                  read_word_is("/sys/class/power_supply/battery/status", "Full");
    sc.h2w = read_int_file("/sys/class/switch/h2w/state", 0);
    sc.wifi = read_word_is("/sys/class/net/wlan0/operstate", "up");
}

void ui_status(Gfx &g) {
    char t[16];
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    std::snprintf(t, sizeof(t), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    g.text(16, 8, t, 12, theme.fg);

    status_read();
    if (apphost_active())
        icon(g, "keyboard", 296, 7, 18,
             osk_visible() ? theme.accent : theme.dim);
    int x = 460;

    if (sc.present && sc.cap >= 0) {
        char pct[12];
        std::snprintf(pct, sizeof(pct), "%d%%", sc.cap);
        uint16_t pc = sc.charging ? theme.accent : theme.fg;
        g.text(x, 9, pct, 12, pc, TF_RIGHT);
        int tw = g.textw(pct, 12);
        int bx = x - tw - 26;
        g.frame(bx, 9, 22, 11, theme.fg);
        g.rect(bx + 22, 12, 2, 4, theme.fg);
        int fill = 18 * (sc.cap < 0 ? 0 : (sc.cap > 100 ? 100 : sc.cap)) / 100;
        if (fill > 0) g.rect(bx + 2, 11, fill, 7, pc);
        x = bx - 10;
    } else if (sc.usb) {
        int uw = g.textw("USB", 11);
        g.text(x, 9, "USB", 11, theme.dim, TF_RIGHT);
        x -= uw + 6;
    }

    if (sc.usb) {
        icon(g, "usb", x - 14, 9, 14, theme.dim);
        x -= 22;
    }
    if (sc.h2w) {
        icon(g, "headphones", x - 18, 8, 18, theme.fg);
        x -= 24;
    }
    icon(g, "wifi", x - 16, 9, 16, sc.wifi ? theme.fg : theme.dim);
}

// ── layout constants (see notes/shell/DESIGN-SYSTEM.md) ────────────────
// 8 px rhythm, 24 px side margins, 432 px content column, 32 px status bar.
const int kPadX = 24;          // left/right margin
const int kRight = 456;        // 480 - 24
const int kContentW = 432;     // 480 - 2 * 24
const int kHeaderH = 64;       // header block below the 32 px status bar

// Uppercase tracked text: the font API has no letter-spacing, so glyphs are
// placed one by one. Sections and tab pivots use it.
void ui_caps(Gfx &g, int x, int y, const std::string &s, int px, uint16_t c,
             int track, int flags = 0) {
    int cx = x;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char ch = (unsigned char)s[i];
        size_t n = 1;
        if ((ch & 0xE0) == 0xC0) n = 2;
        else if ((ch & 0xF0) == 0xE0) n = 3;
        else if ((ch & 0xF8) == 0xF0) n = 4;
        std::string one = s.substr(i, n);
        g.text(cx, y, one, px, c, flags);
        cx += g.textw(one, px) + track;
        i += n;
    }
}

int ui_header(Gfx &g, const char *title, const char *sub, bool back) {
    const int top = 32;
    if (back) {
        // 44 × 44 tap zone at x = 4…48, icon centred inside it.
        int by = top + (kHeaderH - 44) / 2;
        icon(g, "back", 15, by + 11, 22, theme.fg);
        g.text(60, top + 10, title, 24, theme.fg, TF_LIGHT);
        if (sub && *sub) g.text(60, top + 40, g.fit(sub, 11, 392), 11, theme.dim);
    } else {
        g.text(kPadX, top + 10, title, 24, theme.fg, TF_LIGHT);
        if (sub && *sub)
            g.text(kPadX, top + 40, g.fit(sub, 11, kContentW), 11, theme.dim);
    }
    return top + kHeaderH;
}

// Section label: 12 px caps, bold, tracked, with a 2 × 12 accent tick. Air
// above (16) keeps it clear of the previous block, 28 px total advance.
int ui_section(Gfx &g, int y, const char *label) {
    g.rect(kPadX, y + 15, 2, 12, theme.accent);
    if (label && *label) ui_caps(g, kPadX + 12, y + 12, label, 12, theme.fg, 2, TF_BOLD);
    return y + 28;
}

int ui_row_h() { return 56; }

int ui_row(Gfx &g, int y, const char *name, const char *sub,
           const char *value, bool chevron, bool accent) {
    g.hline(0, y, g.cv.w, theme.line2);
    uint16_t nc = accent ? theme.accent : theme.fg;
    std::string nm = g.fit(name ? name : "", 16, 300);
    g.text(kPadX, y + 7, nm, 16, nc);
    if (sub && *sub) g.text(kPadX, y + 29, g.fit(sub, 12, 330), 12, theme.dim);
    if (value && *value)
        g.text(kRight, y + 19, g.fit(value, 12, 190), 12, theme.dim, TF_RIGHT);
    if (chevron) icon(g, "chevron", kRight - 18, y + 20, 16, theme.dim);
    return y + 56;
}

void ui_switch(Gfx &g, int x, int y, bool on) {
    if (on) {
        g.rect(x, y, 44, 24, theme.accent);
        g.rect(x + 23, y + 4, 16, 16, theme.onacc);
    } else {
        g.frame(x, y, 44, 24, theme.track);
        g.rect(x + 4, y + 4, 16, 16, theme.dim);
    }
}

// Label 12, track 4, knob 16 — the whole block is 44 px tall for the finger.
void ui_slider(Gfx &g, int x, int y, int w, int pct,
               const char *label, const char *value) {
    if (label) g.text(x, y, label, 12, theme.dim);
    if (value) g.text(x + w, y, value, 12, theme.dim, TF_RIGHT);
    int ty = y + 30;
    g.rect(x, ty, w, 4, theme.track);
    int fw = w * std::max(0, std::min(100, pct)) / 100;
    g.rect(x, ty, fw, 4, theme.accent);
    g.rect(x + fw - 8, ty - 6, 16, 16, theme.accent);
}

void ui_seg(Gfx &g, int x, int y, int w, const char *const *items, int n, int cur) {
    int gap = 8;
    int bw = (w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        int bx = x + i * (bw + gap);
        bool on = i == cur;
        g.rect(bx, y, bw, 44, on ? theme.accent : theme.surf);
        g.text(bx + bw / 2, y + 14, items[i], 12,
               on ? theme.onacc : theme.fg, TF_CENTER);
    }
}

void ui_chevron(Gfx &g, int x, int y, uint16_t c) {
    icon(g, "chevron", x, y, 16, c);
}

void ui_toast(Gfx &g, const char *text) {
    if (!text || !*text) return;
    int y = 36;   // just under the status bar: never covers keyboards or action bars
    g.rect(kPadX, y, kContentW, 48, theme.surf);
    g.rect(kPadX, y, 3, 48, theme.accent);
    g.text(kPadX + 16, y + 15, g.fit(text, 13, kContentW - 32), 13, theme.fg);
}

void ui_tile(Gfx &g, int x, int y, int w, int h, const char *icon_name,
             const char *label, const char *meta, bool live) {
    g.rect(x, y, w, h, live ? theme.accent : theme.surf);
    uint16_t ic = live ? theme.onacc : theme.fg;
    icon(g, icon_name, x + 16, y + 16, 34, ic);
    uint16_t lc = live ? theme.onacc : theme.fg;
    uint16_t mc = live ? theme.onacc : theme.dim;
    if (label) g.text(x + 16, y + h - 52, g.fit(label, 16, w - 32), 16, lc, TF_BOLD);
    if (meta && *meta) g.text(x + 16, y + h - 28, g.fit(meta, 12, w - 32), 12, mc);
}
