// S10 "Лаборатория" — read-only diagnostics; the only action is opening /var/log.
#include "graphite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

std::string slurp(const char *path, size_t cap = 4096) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[512];
    size_t n;
    while (out.size() < cap && (n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return trim(out);
}

std::string comma(std::string s) {
    for (char &c : s)
        if (c == '.') c = ',';
    return s;
}

bool port_open(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0) ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = false;
    if (::connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        if (::select(fd + 1, nullptr, &w, nullptr, &tv) > 0) {
            int err = 0;
            socklen_t l = sizeof(err);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) == 0 && err == 0)
                ok = true;
        }
    }
    ::close(fd);
    return ok;
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

std::string load_text() {
    FILE *f = std::fopen("/proc/loadavg", "r");
    if (!f) return "нет данных";
    char line[128] = {0};
    char *r = std::fgets(line, sizeof(line), f);
    std::fclose(f);
    if (!r) return "нет данных";
    std::string s = line;
    size_t p1 = s.find(' ');
    if (p1 == std::string::npos) return trim(s);
    size_t p2 = s.find(' ', p1 + 1);
    if (p2 == std::string::npos) return trim(s);
    size_t p3 = s.find(' ', p2 + 1);
    return trim(s.substr(0, p3 == std::string::npos ? s.size() : p3));
}

int kv(Gfx &g, int y, const char *key, const char *val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val ? val : "", 16, 272), 16,
           muted ? theme.dim : theme.fg);
    return y + 44;
}

int row(Gfx &g, int y, const char *name, const std::string &sub, bool chev) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 330), 16, theme.fg);
    g.text(24, y + 29, g.fit(sub, 12, 330), 12, theme.dim);
    if (chev) icon(g, "chevron", 438, y + 20, 16, theme.dim);
    return y + 56;
}

std::string power_text() {
    std::string uv = slurp("/sys/class/power_supply/battery/voltage_now");
    std::string cap = slurp("/sys/class/power_supply/battery/capacity");
    if (!uv.empty()) {
        double v = std::atoll(uv.c_str()) / 1000000.0;
        char b[64];
        if (!cap.empty())
            std::snprintf(b, sizeof(b), "%s %% · %.2f В", cap.c_str(), v);
        else
            std::snprintf(b, sizeof(b), "нет АКБ · %.2f В", v);
        return comma(b);
    }
    return "USB · без АКБ";
}

bool dir_ok(const char *p) { return access(p, R_OK) == 0; }

struct SetLab : ScreenView {
    std::string fb = "fb0 · 480×800 · pan 10 Гц";
    std::string mpd = "MPD · TCP 6600";
    std::string power = "USB · без АКБ";
    std::string net = "wlan0 · нет";
    std::string qa = "на устройстве нет · по SSH";
    std::string uptime = "нет данных";
    std::string load = "нет данных";
    static constexpr int row0 = 124;

    SetLab() { refresh(); }

    void on_show() override { refresh(); }

    void refresh() {
        std::string vs = slurp("/sys/class/graphics/fb0/virtual_size");
        if (!vs.empty()) {
            std::string dim;
            for (char c : vs) {
                if (c == ',') dim += "×";
                else dim += c;
            }
            fb = "fb0 · " + dim + " · pan 10 Гц";
        } else {
            fb = "fb0 · нет · pan 10 Гц";
        }

        mpd = port_open(6600) ? "MPD · TCP 6600 · отвечает"
                              : "MPD · TCP 6600 · не отвечает";

        power = power_text();

        std::string st = slurp("/sys/class/net/wlan0/operstate");
        net = st.empty() ? "wlan0 · нет" : "wlan0 · " + st;

        if (dir_ok("/home/user/notes/qa") || dir_ok("../notes/qa") ||
            dir_ok("/path/to/repo/notes/qa") || dir_ok("notes/qa"))
            qa = "notes/qa · smoke · burn-in";
        else
            qa = "на устройстве нет · по SSH";

        uptime = uptime_text();
        load = load_text();
    }

    const char *id() const override { return "set-lab"; }
    bool scrollable() const override { return true; }
    int content_height(int) const override { return 720; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Лаборатория",
                  ("ДИАГНОСТИКА · " + std::string(theme.code)).c_str(), true);
        g.begin_body(96, 760, scroll_y);
        int y = 96;
        y = ui_section(g, y, "РАЗДЕЛЫ");
        y = row(g, y, "Экран и pan", fb, false);
        y = row(g, y, "Звук и MPD", mpd, false);
        y = row(g, y, "Питание", power, false);
        y = row(g, y, "Сеть", net, false);
        y = row(g, y, "Логи", "/var/log · dmesg · открыть в Файлах", true);

        y = ui_section(g, y, "СОСТОЯНИЕ");
        y = kv(g, y, "UPTIME", uptime.c_str());
        y = kv(g, y, "LOADAVG", load.c_str());

        g.end_body();
        g.text(24, 781, "ЛАБОРАТОРИЯ · /var/log", 11, theme.dim);
        g.text(456, 781, "smoke по SSH", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (y < 96 || y >= 760) return false;
        if (hit(x, y, 0, row0 + 5 * 56, 480, 56)) {
            app.open_path = "/var/log";
            app.open("files");
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_lab() { return new SetLab(); }
