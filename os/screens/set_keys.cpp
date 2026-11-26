// S6 "Клавиши" — button backlight on/off (real sysfs write) and key facts.
#include "graphite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>

namespace {

const char *kLed = "/sys/class/leds/button-backlight/brightness";

bool put_sys(const char *path, const char *val) {
    int fd = ::open(path, O_WRONLY);
    if (fd < 0) return false;
    ssize_t n = ::write(fd, val, std::strlen(val));
    ::close(fd);
    return n == (ssize_t)std::strlen(val);
}

// -1 when the node is missing, otherwise the raw 0..255 value.
int get_sys(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return -1;
    char b[32] = {0};
    char *r = std::fgets(b, sizeof(b), f);
    std::fclose(f);
    if (!r) return -1;
    return std::atoi(b);
}

int note(Gfx &g, int y, const char *text) {
    g.text(24, y + 6, g.fit(text, 11, 432), 11, theme.dim);
    return y + 24;
}

int kv(Gfx &g, int y, const char *key, const char *val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val ? val : "", 16, 272), 16,
           muted ? theme.dim : theme.fg);
    return y + 44;
}

struct SetKeys : ScreenView {
    bool on = true;
    int seen = -1;
    static constexpr int seg_y = 124;

    SetKeys() { refresh(); }

    void refresh() {
        seen = get_sys(kLed);
        if (seen >= 0) on = seen > 0;
        else on = cfg.get("btnlight", "on") != "off";
    }

    void on_show() override { refresh(); }

    const char *id() const override { return "set-keys"; }
    bool scrollable() const override { return true; }
    int content_height(int) const override { return 760; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Клавиши", "VOL · POWER · HOME · MENU", true);
        g.begin_body(96, 760, scroll_y);
        int y = 96;
        y = ui_section(g, y, "ПОДСВЕТКА");

        static const char *const items[] = {"ВКЛ", "ВЫКЛ"};
        ui_seg(g, 24, y, 432, items, 2, on ? 0 : 1);
        y += 44;
        y = note(g, y, "ВКЛ — 255 · ВЫКЛ — 0 · button-backlight");

        y = ui_section(g, y, "ЖЕЛЕЗО");
        y = kv(g, y, "VOL± КОРОТКО", "громкость ±3 % · event1/5");
        y = kv(g, y, "POWER КОРОТКО", "экран блокировки · event5");
        y = kv(g, y, "POWER 1,5 С", "диалог выключения · event5");
        y = kv(g, y, "HOME", "Пуск · event0 + event1");
        y = kv(g, y, "МЕНЮ", "КОМАНДЫ · event0 KEY_MENU 139");
        y = kv(g, y, "BACK", "назад по стеку · event0 KEY_BACK 158");
        y = kv(g, y, "SEARCH", "приложения · event0 KEY_SEARCH 217");
        y = kv(g, y, "ГАРНИТУРА", "плей/пауза · event4 KEY_MEDIA");

        y = note(g, y, "event-коды: VOL 114/115 · POWER 116 · HOME 102 · MENU 139 · BACK 158");
        y = ui_section(g, y, "СОСТОЯНИЕ");

        char now[48];
        if (seen < 0)
            std::snprintf(now, sizeof(now), "узел не найден");
        else
            std::snprintf(now, sizeof(now), "%d · %s", seen, seen ? "горит" : "погашена");
        y = kv(g, y, "СЕЙЧАС", now, seen < 0);
        y = kv(g, y, "ДРАЙВЕР", "event0 · event1 · event5 — живы");

        y = note(g, y, "/sys/class/leds/button-backlight · 0…255");

        g.end_body();
        g.text(24, 781, "КЛАВИШИ · EVDEV", 11, theme.dim);
        g.text(456, 781, "event0/1/5", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (y < seg_y || y >= seg_y + 44) return false;
        int bw = (432 - 8) / 2;
        int i = -1;
        for (int k = 0; k < 2; k++) {
            int bx = 24 + k * (bw + 8);
            if (x >= bx && x < bx + bw) i = k;
        }
        if (i < 0) return false;

        bool want = i == 0;
        if (!put_sys(kLed, want ? "255" : "0")) {
            app.toast("Нет /sys/class/leds/button-backlight");
            return true;
        }
        on = want;
        seen = want ? 255 : 0;
        if (!settings_save_key("btnlight", want ? "on" : "off"))
            app.toast("Подсветка изменена, но настройки не сохранены");
        app.toast(want ? "Подсветка кнопок: 255" : "Подсветка кнопок: 0");
        return true;
    }
};

} // namespace

ScreenView *make_set_keys() { return new SetKeys(); }
