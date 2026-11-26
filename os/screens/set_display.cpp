// S4 "Экран и вид" — theme, accent, dim/lock timers and brightness.
//
// Grid: 24 px side margins, 432 px column, 8 px rhythm (see DESIGN-SYSTEM.md).
#include "graphite.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>

namespace {

const int kPad = 24;          // side margin
const int kRight = 456;       // 480 - 24
const int kCol = 432;         // content width

const int kPreviewY = 104;
const int kPreviewH = 80;
const int kThemesY = 212;     // first theme row (7 × 44)
const int kThemeRow = 44;
const int kAccentY = 544;     // first swatch (4 × 108 cells)
const int kAccentCell = 108;
const int kTimerLinkY = 620;
const int kBrightY = 680;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

unsigned now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

const char *const THEMES[7] = {
    "graphite", "ember", "slate", "book", "book-n", "paper", "paper-n"
};
const char *const ACCENTS[4] = { "turquoise", "amber", "blue", "green" };
const char *const ACCENT_NAMES[4] = { "Бирюза", "Янтарь", "Синий", "Зелёный" };

bool texture_on() { return cfg.get("tex", "auto") != "off"; }

const char *family_name(int f) {
    if (f == FAM_LIGHT) return "СВЕТЛАЯ";
    if (f == FAM_GREY) return "СЕРАЯ";
    return "ТЁМНАЯ";
}

bool save_key(const char *k, const char *v) {
    if (!settings_save_key(k, v)) { app.toast("Ошибка записи настроек"); return false; }
    return true;
}

int read_brightness() {
    FILE *f = std::fopen("/sys/class/leds/lcd-backlight/brightness", "rb");
    if (!f) return -1;
    int v = -1;
    if (std::fscanf(f, "%d", &v) != 1) v = -1;
    std::fclose(f);
    if (v < 0) return -1;
    return v > 255 ? 255 : v;
}

bool write_brightness(int v) {
    int fd = ::open("/sys/class/leds/lcd-backlight/brightness", O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    std::string s = std::to_string(v) + "\n";
    ssize_t n = ::write(fd, s.data(), s.size());
    int saved = errno;
    int cr = ::close(fd);
    errno = saved;
    return n == (ssize_t)s.size() && cr == 0;
}

struct DisplayScreen : ScreenView {
    int bright = -1;              // 0..255, -1 = sysfs unavailable
    bool pending = false;
    unsigned pending_at = 0;
    unsigned last_write = 0;

    DisplayScreen() {
        bright = read_brightness();
        if (bright < 0 && cfg.kv.find("brightness") != cfg.kv.end())
            bright = clamp_b(cfg.geti("brightness", 0));
    }

    const char *id() const override { return "set-display"; }

    static int clamp_b(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

    int pct() const { return bright < 0 ? 0 : (bright * 100 + 127) / 255; }

    std::string bright_text() const {
        if (bright < 0) return "—";
        return std::to_string(pct()) + " %";
    }

    void set_bright(int x) {
        int p = (x - kPad) * 100 / kCol;
        p = std::max(0, std::min(100, p));
        int v = (p * 255 + 50) / 100;
        if (v == bright) return;
        bright = v;
        unsigned now = now_ms();
        if (now - last_write >= 100) {
            if (!write_brightness(bright)) app.toast("Ошибка записи яркости sysfs");
            last_write = now;
        }
        pending = true;
        pending_at = now;
    }

    void tick(unsigned now) override {
        if (pending && now - pending_at >= 400) {
            if (!write_brightness(bright)) app.toast("Ошибка записи яркости sysfs");
            else save_key("brightness", std::to_string(bright).c_str());
            pending = false;
        }
    }

    bool animated() const override { return pending; }

    void draw_preview(Gfx &g) {
        const int y = kPreviewY, h = kPreviewH;
        const int c1 = kPad + 216;        // 240: bg | surf | accent
        const int c2 = c1 + 108;          // 348
        g.rect(kPad, y, kCol, h, theme.bg);
        if (texture_on() && theme.family == FAM_DARK) {
            for (int ly = y + 8; ly < y + h - 4; ly += 24)
                for (int lx = kPad; lx < c1; lx++) g.cv.blend(lx, ly, theme.surf2, 26);
        }
        g.rect(c1, y, 108, h, theme.surf);
        g.rect(c2, y, 108, h, theme.accent);
        g.frame(kPad, y, kCol, h, theme.line);
        g.vline(c1, y, h, theme.line);
        g.vline(c2, y, h, theme.line);

        g.text(kPad + 12, y + 8, "ФОН", 11, theme.dim);
        g.text(c1 + 12, y + 8, "SURF", 11, theme.dim);
        g.text(c2 + 12, y + 8, "ACCENT", 11, theme.onacc);
        g.text(kPad + 12, y + 44, "Aa 15", 15, theme.fg);
        g.text(c1 + 12, y + 44, "Aa 12", 15, theme.fg);
        g.text(c2 + 12, y + 44, "Aa 15", 15, theme.onacc, TF_BOLD);
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Экран и вид", "ТЕМА · СВЕТ", true);
        draw_preview(g);

        const std::string cur = theme.id ? theme.id : "graphite";
        const std::string ac = cfg.get("accent", "");
        const char *acp = ac.empty() ? nullptr : ac.c_str();

        // ── ТЕМА: 7 rows × 44, chips 20 × 20 ──────────────────────────
        ui_section(g, 184, "ТЕМА");
        int y = kThemesY;
        for (int i = 0; i < 7; i++) {
            theme_set(THEMES[i], nullptr);
            std::string nm = theme.name;
            int fam = theme.family;
            uint16_t tbg = theme.bg, tsf = theme.surf, tln = theme.line;
            theme_set(cur.c_str(), acp);

            bool on = cur == THEMES[i];
            g.hline(0, y, g.cv.w, theme.line2);
            g.rect(kPad, y + 12, 10, 20, tbg);
            g.rect(kPad + 10, y + 12, 10, 20, tsf);
            g.frame(kPad, y + 12, 20, 20, tln);
            g.text(kPad + 32, y + 12, g.fit(nm, 16, 300), 16,
                   on ? theme.accent : theme.fg);
            g.text(424, y + 16, family_name(fam), 11, theme.dim, TF_RIGHT);
            if (on) icon(g, "check", 434, y + 14, 16, theme.accent);
            y += kThemeRow;
        }

        // ── АКЦЕНТ: 4 cells × 108, swatches 44 × 44 ───────────────────
        ui_section(g, 516, "АКЦЕНТ");
        const char *native = cur == "ember" ? "amber" : "turquoise";
        for (int i = 0; i < 4; i++) {
            theme_set(cur.c_str(), ACCENTS[i]);
            uint16_t col = theme.accent;
            bool on = ac == ACCENTS[i] ||
                      (ac.empty() && !std::strcmp(ACCENTS[i], native));
            int cx = kPad + i * kAccentCell + kAccentCell / 2;
            int sx = cx - 22;
            g.rect(sx, kAccentY, 44, 44, col);
            if (on) {
                g.frame(sx - 4, kAccentY - 4, 52, 52, theme.fg);
                g.frame(sx - 3, kAccentY - 3, 50, 50, theme.fg);
            } else {
                g.frame(sx, kAccentY, 44, 44, theme.line);
            }
            g.text(cx, kAccentY + 50, ACCENT_NAMES[i], 11,
                   on ? theme.fg : theme.dim, TF_CENTER);
        }
        theme_set(cur.c_str(), acp);

        g.hline(0, kTimerLinkY, g.cv.w, theme.line2);
        g.text(kPad, kTimerLinkY + 14, "Таймеры и текстура", 15, theme.fg);
        icon(g, "chevron", kRight - 18, kTimerLinkY + 14, 16, theme.dim);
        g.hline(0, kTimerLinkY + 56, g.cv.w, theme.line2);

        g.hline(0, kBrightY - 4, g.cv.w, theme.line2);
        ui_slider(g, kPad, kBrightY + 16, kCol, pct(), "ЯРКОСТЬ", bright_text().c_str());

        // ── ЯРКОСТЬ: track 24…456, knob 16 ───────────────────────────
        g.text(kPad, 781, "ЭКРАН · LM3530", 11, theme.dim);
        g.text(kRight, 781, "7 ТЕМ · 4 АКЦЕНТА", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }

        if (y >= kThemesY && y < kThemesY + 7 * kThemeRow) {
            int i = (y - kThemesY) / kThemeRow;
            const std::string ac = cfg.get("accent", "");
            if (save_key("theme", THEMES[i])) theme_set(THEMES[i], ac.empty() ? nullptr : ac.c_str());
            return true;
        }

        if (y >= kAccentY - 4 && y < kAccentY + 62 && x >= kPad && x < kRight) {
            int i = (x - kPad) / kAccentCell;
            if (i >= 0 && i < 4) {
                const std::string cur = theme.id ? theme.id : "graphite";
                if (save_key("accent", ACCENTS[i])) theme_set(cur.c_str(), ACCENTS[i]);
                return true;
            }
        }

        if (y >= kTimerLinkY && y < kTimerLinkY + 56) {
            app.push("set-timers");
            return true;
        }

        if (y >= kBrightY + 40 && y < 776) {
            set_bright(x);
            return true;
        }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)dx;
        (void)dy;
        if (y < kBrightY + 40 || y >= 776) return false;
        set_bright(x);
        return true;
    }
};

} // namespace

ScreenView *make_set_display() { return new DisplayScreen(); }
