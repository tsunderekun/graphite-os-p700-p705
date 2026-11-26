#include "graphite.h"
#include "power_policy.h"

#include <string>

namespace {
const int kPad = 24;
const int kRight = 456;
const int kSegmentW0 = 139;
const int kSegmentW1 = 139;
const int kSegmentW2 = 138;

int segment_at(int x) {
    if (x < kPad || x >= kRight) return -1;
    if (x < kPad + kSegmentW0) return 0;
    if (x < kPad + kSegmentW0 + 8 + kSegmentW1) return 1;
    if (x >= kRight - kSegmentW2) return 2;
    return -1;
}

bool save(const char *key, int value) {
    std::string text = std::to_string(value);
    if (!settings_save_key(key, text.c_str())) {
        app.toast("Ошибка записи настроек");
        return false;
    }
    return true;
}

struct TimerScreen : ScreenView {
    int dim = 10;
    int lock = 30;
    bool texture = true;

    TimerScreen() {
        dim = cfg.geti("dim_after", 10);
        lock = cfg.geti("lock_after", 30);
        texture = cfg.get("tex", "auto") != "off";
    }

    const char *id() const override { return "set-timers"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Таймеры экрана", "ПОДСВЕТКА · ПРОСТОЙ", true);

        ui_section(g, 128, "ТЕКСТУРА");
        g.hline(0, 156, g.cv.w, theme.line2);
        g.text(kPad, 172, "Текстура фона", 16, theme.fg);
        g.text(kRight - 64, 176, texture ? "ВКЛ" : "ВЫКЛ", 12, theme.dim, TF_RIGHT);
        ui_switch(g, kRight - 44, 168, texture);
        g.hline(0, 220, g.cv.w, theme.line2);

        ui_section(g, 244, "ПРИГЛУШИТЬ ЭКРАН");
        const char *dim_labels[] = {"10 с", "20 с", "30 с"};
        g.rect(kPad, 288, kSegmentW0, 44, dim == 10 ? theme.accent : theme.surf);
        g.rect(kPad + kSegmentW0 + 8, 288, kSegmentW1, 44, dim == 20 ? theme.accent : theme.surf);
        g.rect(kRight - kSegmentW2, 288, kSegmentW2, 44, dim == 30 ? theme.accent : theme.surf);
        g.text(kPad + kSegmentW0 / 2, 302, dim_labels[0], 12, dim == 10 ? theme.onacc : theme.fg, TF_CENTER);
        g.text(kPad + kSegmentW0 + 8 + kSegmentW1 / 2, 302, dim_labels[1], 12, dim == 20 ? theme.onacc : theme.fg, TF_CENTER);
        g.text(kRight - kSegmentW2 / 2, 302, dim_labels[2], 12, dim == 30 ? theme.onacc : theme.fg, TF_CENTER);

        ui_section(g, 360, "ЗАБЛОКИРОВАТЬ ЭКРАН");
        const char *lock_labels[] = {"30 с", "60 с", "120 с"};
        g.rect(kPad, 404, kSegmentW0, 44, lock == 30 ? theme.accent : theme.surf);
        g.rect(kPad + kSegmentW0 + 8, 404, kSegmentW1, 44, lock == 60 ? theme.accent : theme.surf);
        g.rect(kRight - kSegmentW2, 404, kSegmentW2, 44, lock == 120 ? theme.accent : theme.surf);
        g.text(kPad + kSegmentW0 / 2, 418, lock_labels[0], 12, lock == 30 ? theme.onacc : theme.fg, TF_CENTER);
        g.text(kPad + kSegmentW0 + 8 + kSegmentW1 / 2, 418, lock_labels[1], 12, lock == 60 ? theme.onacc : theme.fg, TF_CENTER);
        g.text(kRight - kSegmentW2 / 2, 418, lock_labels[2], 12, lock == 120 ? theme.onacc : theme.fg, TF_CENTER);

        g.text(kPad, 484, "При блокировке гаснет только подсветка.", 12, theme.dim);
        g.text(kPad, 508, "Панель и DSI остаются включёнными.", 12, theme.dim);
        g.text(kPad, 781, "ИЗМЕНЕНИЯ ПРИМЕНЯЮТСЯ СРАЗУ", 11, theme.dim);
    }

    bool tap(int x, int y) override {
        if (y >= 32 && y < 96 && x < 64) { app.back(); return true; }
        if (y >= 160 && y < 212) {
            texture = !texture;
            if (!settings_save_key("tex", texture ? "auto" : "off")) {
                texture = !texture;
                app.toast("Ошибка записи настроек");
            }
            return true;
        }
        if (y >= 288 && y < 332) {
            int i = segment_at(x);
            if (i < 0) return false;
            int next = i == 0 ? 10 : (i == 1 ? 20 : 30);
            if (save("dim_after", next)) {
                dim = next;
                power_policy.configure((unsigned)dim * 1000u, (unsigned)lock * 1000u,
                                       cfg.geti("brightness", 150), 40);
            }
            return true;
        }
        if (y >= 404 && y < 448) {
            int i = segment_at(x);
            if (i < 0) return false;
            int next = i == 0 ? 30 : (i == 1 ? 60 : 120);
            if (next < dim) next = dim;
            if (save("lock_after", next)) {
                lock = next;
                power_policy.configure((unsigned)dim * 1000u, (unsigned)lock * 1000u,
                                       cfg.geti("brightness", 150), 40);
            }
            return true;
        }
        return false;
    }
};
}

ScreenView *make_set_timers() { return new TimerScreen(); }
