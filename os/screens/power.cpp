#include "graphite.h"
#include <cstdlib>
#include <cstring>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

struct Power : ScreenView {
    // Absolute monotonic deadlines (ms). 0 = not armed.
    unsigned arm_power = 0, arm_reboot = 0;

    const char *id() const override { return "power"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        for (int y = 0; y < 800; y++)
            for (int x = 0; x < 480; x++)
                g.cv.blend(x, y, 0, 189);   // 74 % scrim

        g.rect(40, 280, 400, 220, theme.surf);
        g.text(60, 300, "Выключить питание?", 20, theme.fg, TF_LIGHT);

        g.text(60, 332, g.fit("Все несохранённые данные будут потеряны при", 13, 360),
               13, theme.dim);
        g.text(60, 352, "завершении работы.", 13, theme.dim);

        const char *plabel = arm_power ? "Нажмите ещё раз" : "Выключить…";
        int pr = 420, py = 466;
        int pw = g.textw(plabel, 14);
        int cr = pr - pw - 24;

        g.text(cr, py, "Отмена", 14, theme.dim, TF_RIGHT);
        g.text(pr, py, plabel, 14, theme.accent, TF_RIGHT);

        // reboot row under the dialog
        g.rect(40, 520, 400, 56, theme.surf);
        icon(g, "restart", 60, 537, 22, theme.dim);
        g.text(92, 538,
               arm_reboot ? "Нажмите ещё раз" : "Перезагрузить", 14,
               arm_reboot ? theme.accent : theme.fg);
    }

    bool tap(int x, int y) override {
        const char *plabel = arm_power ? "Нажмите ещё раз" : "Выключить…";
        Gfx &g = *app.g;
        int pw = g.textw(plabel, 14);
        int cw = g.textw("Отмена", 14);
        int pr = 420;
        int cr = pr - pw - 24;

        if (hit(x, y, cr - cw - 10, 452, cw + 20, 44)) {
            app.back();
            return true;
        }
        if (hit(x, y, pr - pw - 10, 452, pw + 20, 44)) {
            if (arm_power && app.now < arm_power) {
                arm_power = 0;
                int rc = std::system("poweroff");
                (void)rc;
            } else {
                arm_power = app.now + 3000;
            }
            return true;
        }
        if (hit(x, y, 40, 520, 400, 56)) {
            if (arm_reboot && app.now < arm_reboot) {
                arm_reboot = 0;
                int rc = std::system("reboot");
                (void)rc;
            } else {
                arm_reboot = app.now + 3000;
            }
            return true;
        }
        return false;
    }

    void tick(unsigned now) override {
        if (arm_power && now >= arm_power) arm_power = 0;
        if (arm_reboot && now >= arm_reboot) arm_reboot = 0;
    }

    bool animated() const override { return arm_power != 0 || arm_reboot != 0; }
};

} // namespace

ScreenView *make_power() { return new Power(); }
