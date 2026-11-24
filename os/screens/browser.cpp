// browser — handoff screen for the real dillo browser. No fake page and no
// fake address bar: the shell shows what it is doing, then apphost gives the
// content area to dillo. Without dillo the screen says so and offers xterm.
#include "graphite.h"
#include "screens.h"
#include "apphost.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

const char *kDilloCmd = "dillo -g 480x768+0+32";
const char *kXtermCmd = "xterm -title graphite -fa 'DejaVu Sans Mono' -fs 11 "
                        "-bg '#101419' -fg '#efebef' -cr '#3fc8d0' -ls";
const int kButtonY = 340;   // 440x56 action row, same geometry in tap()

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

bool exe_present(const char *name) {
    const char *path = std::getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";
    char buf[512];
    while (*path) {
        const char *end = std::strchr(path, ':');
        size_t len = end ? (size_t)(end - path) : std::strlen(path);
        if (len == 0)
            std::snprintf(buf, sizeof buf, "/%s", name);
        else
            std::snprintf(buf, sizeof buf, "%.*s/%s", (int)len, path, name);
        if (access(buf, X_OK) == 0) return true;
        if (!end) break;
        path = end + 1;
    }
    return false;
}

std::vector<std::string> wrap(Gfx &g, const std::string &text, int px, int maxw) {
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        std::string word;
        size_t next;
        if (j == std::string::npos) {
            word = text.substr(i);
            next = text.size();
        } else {
            word = text.substr(i, j - i + 1);
            next = j + 1;
        }
        if (cur.empty()) {
            cur = word;
        } else if (g.textw(cur + word, px) <= maxw) {
            cur += word;
        } else {
            out.push_back(cur);
            cur = word;
        }
        i = next;
    }
    out.push_back(cur);
    return out;
}

int para(Gfx &g, const std::string &text, int x, int y, int px,
         int maxw, uint16_t c, int lh) {
    for (const std::string &ln : wrap(g, text, px, maxw)) {
        g.text(x, y, g.fit(ln, px, maxw), px, c);
        y += lh;
    }
    return y;
}

struct Browser : ScreenView {
    bool present = false;

    const char *id() const override { return "browser"; }

    void on_show() override {
        present = exe_present("dillo");
        if (present && !apphost_active()) apphost_launch(kDilloCmd);
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Браузер", "dillo · офлайн", true);
        ui_section(g, 96, "ВНЕШНИЙ БРАУЗЕР");

        int y = 132;
        y = para(g, "Страницы рисует dillo — отдельное приложение. "
                    "Шелл ждёт его окно и отдаёт ему весь экран.",
                 24, y, 14, 432, theme.fg, 22);
        y += 16;

        icon(g, "globe", 24, y, 28, present ? theme.accent : theme.dim);
        g.text(64, y + 2, present ? "dillo установлен" : "dillo не найден",
               16, theme.fg);
        para(g, present ? "Запускается при открытии экрана. Back — назад "
                          "по странице, второй — закрыть."
                        : "Пакет dillo не установлен или его нет в PATH. "
                          "Без него браузера нет.",
             64, y + 26, 12, 392, theme.dim, 17);

        bool active = apphost_active() && apphost_is_browser();
        const char *label = !present ? "ОТКРЫТЬ XTERM"
                          : active   ? "К ОКНУ DILLO"
                                     : "ОТКРЫТЬ DILLO";
        g.rect(24, kButtonY, 432, 56, theme.accent);
        g.text(240, kButtonY + 19, label, 15, theme.onacc, TF_CENTER | TF_BOLD);

        para(g, present ? "Back в dillo: сначала Alt+Left, потом выход."
                        : "xterm — настоящий терминал от user, не браузер.",
             24, kButtonY + 72, 12, 432, theme.dim, 17);

        ui_section(g, 480, "ЧЕСТНО");
        para(g, "Сайты без JavaScript: многие современные страницы не "
                "откроются. Это ограничение dillo, а не шелла.",
             24, 516, 14, 432, theme.dim, 22);

        g.text(24, 781, present ? "локальные страницы · без JS"
                                : "dillo отсутствует",
               11, theme.dim);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 32, 64, 64)) {
            app.back();
            return true;
        }
        if (hit(x, y, 24, kButtonY, 432, 56)) {
            if (!present) {
                apphost_launch(kXtermCmd);
            } else if (apphost_active() && apphost_is_browser()) {
                apphost_focus_app();
            } else if (!apphost_active()) {
                apphost_launch(kDilloCmd);
            } else {
                app.toast("Уже открыто приложение");
            }
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_browser() { return new Browser(); }
