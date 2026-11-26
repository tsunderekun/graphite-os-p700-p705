#include "graphite.h"
#include <cstdio>
#include <string>

namespace {
bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string read_usb(const char *name) {
    std::string path = std::string("/sys/class/android_usb/android0/") + name;
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return "недоступно";
    char buf[128] = {};
    if (!std::fgets(buf, sizeof(buf), f)) { std::fclose(f); return "недоступно"; }
    std::fclose(f);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s.empty() ? "пусто" : s;
}

struct Usb : ScreenView {
    const char *id() const override { return "usb"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "USB", "ТОЛЬКО СТАТУС · БЕЗ ПЕРЕКЛЮЧЕНИЯ", true);
        int y = 104;
        y = ui_section(g, y, "ТЕКУЩЕЕ СОСТОЯНИЕ");
        y = ui_row(g, y, "Функция", "Android USB gadget", read_usb("functions").c_str(), false, true);
        y = ui_row(g, y, "Состояние", "подключение к хосту", read_usb("state").c_str(), false, true);
        y = ui_row(g, y, "RNDIS", "сеть и SSH по USB", "только если включён", false, false);
        y = ui_section(g, y + 4, "ОГРАНИЧЕНИЯ");
        g.rect(24, y, 432, 132, theme.surf2);
        g.text(40, y + 14, "Переключение режимов отключено.", 15, theme.fg, TF_BOLD);
        g.text(40, y + 42, "Mass storage и режим «только зарядка»", 13, theme.dim);
        g.text(40, y + 64, "пока не включены и не проверены на стенде.", 13, theme.dim);
        g.text(40, y + 94, "Не обещаем обмен файлами или рост тока.", 12, theme.accent);
        g.text(24, 781, "USB · READ-ONLY", 11, theme.dim);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 40, 480, 56)) { app.back(); return true; }
        return false;
    }
};
}

ScreenView *make_usb() { return new Usb(); }
