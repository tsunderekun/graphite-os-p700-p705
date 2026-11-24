// menu - the hardware Menu key ("…") action sheet.
#include "graphite.h"
#include <cstring>

namespace {

struct Item {
    const char *label;
    const char *sub;
    const char *action;
};

static const Item kItems[] = {
    {"Настройки", "тема, сеть, звук", "set-root"},
    {"Шторка", "быстрые настройки", "shade"},
    {"Правка плиток", "что на Пуске", "edit"},
    {"На главный", "закрыть экран", "home"},
};

struct Menu : ScreenView {
    const char *id() const override { return "menu"; }
    bool overlay() const override { return true; }

    void draw(Gfx &g) override {
        for (int y = 0; y < 800; y++)
            for (int x = 0; x < 480; x++)
                g.cv.blend(x, y, 0, 150);

        int top = 800 - 24 - (int)(sizeof(kItems) / sizeof(kItems[0])) * 56 - 44;
        g.rect(0, top, 480, 800 - top, theme.surf);
        g.rect(0, top, 480, 1, theme.line2);
        g.text(24, top + 12, "КОМАНДЫ", 12, theme.dim);
        int y = top + 44;
        for (const Item &it : kItems) {
            g.hline(0, y, 480, theme.line2);
            g.text(24, y + 7, it.label, 16, theme.fg);
            g.text(24, y + 29, it.sub, 12, theme.dim);
            icon(g, "chevron", 438, y + 20, 16, theme.dim);
            y += 56;
        }
    }

    bool tap(int x, int y) override {
        (void)x;
        int top = 800 - 24 - (int)(sizeof(kItems) / sizeof(kItems[0])) * 56 - 44;
        if (y < top) { app.back(); return true; }
        int row = (y - (top + 44)) / 56;
        if (row < 0 || row >= (int)(sizeof(kItems) / sizeof(kItems[0]))) return false;
        const char *a = kItems[row].action;
        if (!std::strcmp(a, "shade")) {
            app.back();
            app.push("shade");
        } else {
            app.open(a);
        }
        return true;
    }
};

} // namespace

ScreenView *make_menu() { return new Menu(); }
