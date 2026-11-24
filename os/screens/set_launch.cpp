// S8 "Запуск" — real boot mode file, live session facts, reboot/poweroff.
#include "graphite.h"
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string read_first(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    char line[128] = {0};
    char *r = std::fgets(line, sizeof(line), f);
    std::fclose(f);
    return r ? trim(line) : "";
}

bool write_mode(const char *val) {
    int fd = ::open("/etc/lgtest-mode", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    std::string s = std::string(val) + "\n";
    ssize_t n = ::write(fd, s.data(), s.size());
    (void)n;
    ::close(fd);
    return true;
}

std::string pids_named(const char *a, const char *b, const char *c = nullptr) {
    DIR *d = opendir("/proc");
    if (!d) return "";
    std::string out;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        std::string nm = e->d_name;
        if (nm.empty() || nm[0] < '0' || nm[0] > '9') continue;
        std::string comm = read_first(("/proc/" + nm + "/comm").c_str());
        if (comm != a && comm != b && (!c || comm != c)) continue;
        if (!out.empty()) out += ",";
        out += nm;
    }
    closedir(d);
    return out;
}

std::string first_pid(const std::string &list) {
    size_t c = list.find(',');
    return c == std::string::npos ? list : list.substr(0, c);
}

std::string uptime_text() {
    FILE *f = std::fopen("/proc/uptime", "r");
    if (!f) return "нет данных";
    double sec = 0;
    if (std::fscanf(f, "%lf", &sec) != 1) sec = 0;
    std::fclose(f);
    long total = (long)sec;
    long d = total / 86400, h = (total % 86400) / 3600, m = (total % 3600) / 60;
    char buf[48];
    if (d) std::snprintf(buf, sizeof(buf), "%ld д %ld ч", d, h);
    else if (h) std::snprintf(buf, sizeof(buf), "%ld ч %ld мин", h, m);
    else std::snprintf(buf, sizeof(buf), "%ld мин", m);
    return buf;
}

int kv(Gfx &g, int y, const char *key, const std::string &val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val, 16, 272), 16, muted ? theme.dim : theme.fg);
    return y + 44;
}

struct SetLaunch : ScreenView {
    bool nox = false;
    bool reboot_armed = false;
    std::string session = "оболочка · X :0";
    std::string procs = "X нет";
    std::string uptime = "нет данных";

    SetLaunch() { refresh(); }

    void on_show() override { refresh(); }

    void refresh() {
        std::string m = read_first("/etc/lgtest-mode");
        nox = m == "player" || m == "nox";

        const char *dpy = std::getenv("DISPLAY");
        session = (dpy && *dpy) ? std::string("оболочка · X ") + dpy
                                : "оболочка · X нет";

        std::string xp = pids_named("X", "Xorg", "Xvfb");
        std::string sp = pids_named("graphite-shell", "graphite-host");
        if (xp.empty() && sp.empty()) procs = "X и шелл не найдены";
        else {
            if (xp.empty()) procs = "X нет";
            else procs = "X " + first_pid(xp);
            if (!sp.empty()) procs += " · шелл " + first_pid(sp);
        }

        uptime = uptime_text();
    }

    const char *id() const override { return "set-launch"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Запуск", "ПОСЛЕ ПЕРЕЗАГРУЗКИ", true);
        int y = 96;
        y = ui_section(g, y, "РЕЖИМ");

        static const char *const items[] = {"ОБОЛОЧКА", "БЕЗ X"};
        ui_seg(g, 24, y, 432, items, 2, nox ? 1 : 0);
        y += 44;

        g.rect(24, y + 8, 432, 80, theme.surf);
        g.text(40, y + 20, "! ПРИМЕНИТСЯ ПОСЛЕ ПЕРЕЗАГРУЗКИ", 11, theme.accent);
        g.text(40, y + 40, "Режим пишется в /etc/lgtest-mode.", 12, theme.dim);
        g.text(40, y + 58, "Без X — только fb-плеер, без приложений.", 12,
               theme.dim);
        y += 88;

        y = kv(g, y, "СЕЙЧАС", session);
        y = kv(g, y, "ПОСЛЕ ПЕРЕЗАГРУЗКИ",
               nox ? "без X · player" : "оболочка · shell");

        y = ui_section(g, y, "СОСТОЯНИЕ");
        y = kv(g, y, "PID", procs);
        y = kv(g, y, "UPTIME", uptime);
        y = kv(g, y, "ЕСЛИ X УПАЛ", "через 20 с плеер · свой выбор");
        y = kv(g, y, "X-СЕРВЕР", "X :0 · 16 bpp · Rotate UD");
        y = kv(g, y, "СЕССИЯ", "session-shell · X + fbpan-ng");
        y = kv(g, y, "ФАЙЛ", "/etc/lgtest-mode");

        g.rect(24, y + 8, 432, 48, reboot_armed ? theme.accent : theme.surf);
        g.text(240, y + 26,
               reboot_armed ? "ПОДТВЕРДИТЬ ПЕРЕЗАГРУЗКУ" : "ПЕРЕЗАГРУЗИТЬ",
               13, reboot_armed ? theme.onacc : theme.fg, TF_CENTER | TF_BOLD);
        y += 56;

        g.rect(24, y + 8, 432, 48, theme.surf);
        g.text(240, y + 26, "ВЫКЛЮЧИТЬ…", 13, theme.fg, TF_CENTER | TF_BOLD);

        g.text(24, 781, "ЗАПУСК · /etc/lgtest-mode", 11, theme.dim);
        g.text(456, 781, "shell / no-X", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (y >= 124 && y < 168) {
            if (x >= 24 && x < 236) {
                nox = false;
                if (!write_mode("shell"))
                    app.toast("Не удалось записать /etc/lgtest-mode");
                else
                    app.toast("После перезагрузки: оболочка");
                return true;
            }
            if (x >= 244 && x < 456) {
                nox = true;
                if (!write_mode("player"))
                    app.toast("Не удалось записать /etc/lgtest-mode");
                else
                    app.toast("После перезагрузки: без X (плеер)");
                return true;
            }
        }
        if (hit(x, y, 24, 652, 432, 48)) {
            if (!reboot_armed) {
                reboot_armed = true;
                app.toast("Ещё один тап — перезагрузка");
            } else {
                reboot_armed = false;
                app.toast("Перезагрузка…");
                sync();
                int rc = std::system("reboot");
                (void)rc;
            }
            return true;
        }
        if (hit(x, y, 24, 716, 432, 48)) {
            app.open("power");
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_launch() { return new SetLaunch(); }
