// S9 "Доступ" — real dropbear service status/control and a real SD backup.
#include "graphite.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <utmp.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

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

std::string first_line(const std::string &s) {
    size_t e = s.find('\n');
    return trim(e == std::string::npos ? s : s.substr(0, e));
}

std::string run(const char *cmd, int *rc = nullptr) {
    FILE *p = popen(cmd, "r");
    if (!p) {
        if (rc) *rc = -1;
        return "";
    }
    std::string out;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    int st = pclose(p);
    if (rc) *rc = st;
    return trim(out);
}

std::string dropbear_status() {
    std::string pid = run("pidof dropbear 2>/dev/null");
    if (!pid.empty()) {
        size_t sp = pid.find(' ');
        if (sp != std::string::npos) pid = pid.substr(0, sp);
        return "работает · pid " + pid;
    }
    std::string o = run("rc-service dropbear status 2>&1");
    if (o.find("does not exist") != std::string::npos ||
        o.find("not found") != std::string::npos)
        return "сервис не найден";
    if (o.find("crashed") != std::string::npos) return "сбой";
    if (o.find("started") != std::string::npos) return "работает";
    return "остановлен";
}

std::string iface_ip(const char *name) {
    struct ifaddrs *ifa = nullptr;
    if (getifaddrs(&ifa) != 0) return "";
    std::string out;
    for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (std::strcmp(p->ifa_name, name) != 0) continue;
        char buf[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) out = buf;
    }
    freeifaddrs(ifa);
    return out;
}

std::string sessions_text() {
    setutent();
    int n = 0;
    std::string who;
    struct utmp *u;
    while ((u = getutent()) != nullptr) {
        if (u->ut_type != USER_PROCESS) continue;
        n++;
        if (who.empty()) who = u->ut_user;
    }
    endutent();
    if (!n) return "нет";
    return std::to_string(n) + " · " + who;
}

bool file_ok(const char *p) { return access(p, R_OK) == 0; }

bool copy_file(const char *src, const char *dst) {
    FILE *in = std::fopen(src, "rb");
    if (!in) return false;
    FILE *out = std::fopen(dst, "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    std::fflush(out);
    std::fclose(out);
    std::fclose(in);
    return ok;
}

std::string last_backup() {
    DIR *d = opendir("/media/sd");
    if (!d) return "нет";
    std::string best;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        std::string nm = e->d_name;
        if (nm.compare(0, 7, "backup-") != 0) continue;
        if (nm > best) best = nm;
    }
    closedir(d);
    return best.empty() ? "нет" : best;
}

int kv(Gfx &g, int y, const char *key, const std::string &val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val, 16, 272), 16, muted ? theme.dim : theme.fg);
    return y + 44;
}

int row(Gfx &g, int y, const char *name, const char *sub, const char *value,
        bool chev, bool disabled) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 300), 16, disabled ? theme.dim : theme.fg);
    if (sub && *sub) g.text(24, y + 29, g.fit(sub, 12, 330), 12, theme.dim);
    if (value && *value)
        g.text(428, y + 19, g.fit(value, 12, 170), 12, theme.dim, TF_RIGHT);
    if (chev) icon(g, "chevron", 438, y + 20, 16, theme.dim);
    return y + 56;
}

int switch_row(Gfx &g, int y, const char *name, const char *sub, bool on) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 360), 16, theme.fg);
    g.text(24, y + 29, g.fit(sub, 12, 360), 12, theme.dim);
    ui_switch(g, 412, y + 16, on);
    return y + 56;
}

struct SetAccess : ScreenView {
    bool running = false;
    std::string status = "остановлен";
    std::string usb_ip, wifi_ip;
    std::string sessions = "нет";
    std::string last = "нет";

    SetAccess() { refresh(); }

    void on_show() override { refresh(); }

    void refresh() {
        status = dropbear_status();
        running = status.find("работает") != std::string::npos;
        usb_ip = iface_ip("usb0");
        if (usb_ip.empty()) usb_ip = iface_ip("rndis0");
        wifi_ip = iface_ip("wlan0");
        sessions = sessions_text();
        last = last_backup();
    }

    void set_ssh(bool want) {
        const char *act = want ? "start" : "stop";
        int rc = 0;
        std::string out = run((std::string("rc-service dropbear ") + act +
                               " 2>&1").c_str(), &rc);
        if (rc != 0)
            out = run((std::string("sudo -n rc-service dropbear ") + act +
                       " 2>&1").c_str(), &rc);
        refresh();
        if (running == want) {
            app.toast(want ? "dropbear запущен" : "dropbear остановлен");
            return;
        }
        std::string why = first_line(out);
        if (why.empty()) why = want ? "не запустился" : "не остановился";
        std::string t = "Не вышло: " + why;
        app.toast(t.c_str());
    }

    void backup() {
        struct statvfs v;
        if (statvfs("/media/sd", &v) != 0) {
            app.toast("SD не смонтирована");
            return;
        }
        char name[48];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        std::strftime(name, sizeof(name), "backup-%Y%m%d-%H%M", &tmv);
        std::string dir = std::string("/media/sd/") + name;
        if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            std::string e = std::string("Не удалось: ") + std::strerror(errno);
            app.toast(e.c_str());
            return;
        }
        bool any = false, ok = true;
        if (file_ok("/etc/lgtest-settings")) {
            ok = copy_file("/etc/lgtest-settings",
                           (dir + "/lgtest-settings").c_str()) && ok;
            any = true;
        }
        if (file_ok("/home/user/.config/graphite/pins")) {
            ok = copy_file("/home/user/.config/graphite/pins",
                           (dir + "/pins").c_str()) && ok;
            any = true;
        }
        if (!any) {
            app.toast("Нечего копировать: нет настроек");
            return;
        }
        if (!ok) {
            app.toast("Не удалось скопировать в /media/sd");
            return;
        }
        last = name;
        std::string msg = "Бэкап: " + dir;
        app.toast(msg.c_str());
    }

    const char *id() const override { return "set-access"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Доступ", "SSH · DROPBEAR", true);
        int y = 96;
        y = ui_section(g, y, "SSH");
        y = switch_row(g, y, "SSH · DROPBEAR", "порт 22 · rndis0 + wlan0", running);
        y = kv(g, y, "СТАТУС", status, !running);

        std::string usb = usb_ip.empty() ? "интерфейса нет" : usb_ip + ":22";
        std::string wifi = wifi_ip.empty() ? "интерфейса нет" : wifi_ip + ":22";
        y = kv(g, y, "SSH ПО USB", usb);
        y = kv(g, y, "SSH ПО WI-FI", wifi);
        y = kv(g, y, "ПАРОЛЬ", "user · /etc/shadow");
        y = kv(g, y, "СЕССИИ", sessions);
        y = kv(g, y, "КЛЮЧИ", "authorized_keys · " +
                    std::string(file_ok("/home/user/.ssh/authorized_keys") ? "есть"
                                                                          : "нет"));

        y = ui_section(g, y, "СЕРВИС");
        y = row(g, y, "Бэкап настроек на SD", "lgtest-settings + pins · mkdir + cp",
                "", true, false);
        y = kv(g, y, "ПОСЛЕДНИЙ БЭКАП", last, last == "нет");

        y = ui_section(g, y, "НЕДОСТУПНО");
        y = kv(g, y, "ОТПЕЧАТОК И ЛИЦО", "нет датчика", true);
        y = kv(g, y, "ПОИСК УСТРОЙСТВА", "нет сервиса и SIM", true);

        g.text(24, y + 6, "start/stop — rc-service dropbear · sudo при отказе",
               11, theme.dim);

        g.text(24, 781, "ДОСТУП · SSH", 11, theme.dim);
        g.text(456, 781, running ? "dropbear" : "dropbear · стоп", 11, theme.dim,
               TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 124, 480, 56)) {
            set_ssh(!running);
            return true;
        }
        if (hit(x, y, 0, 472, 480, 56)) {
            backup();
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_access() { return new SetAccess(); }
