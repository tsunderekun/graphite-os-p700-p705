// settings root (S0) — state first, no toggles, eleven rows in three groups.
#include "graphite.h"
#include "pins.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    size_t p = 0;
    while (p < s.size() && s[p] == ' ') p++;
    return s.substr(p);
}

std::string rd(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    char b[256];
    std::string out;
    if (std::fgets(b, sizeof(b), f)) out = trim(b);
    std::fclose(f);
    return out;
}

std::string gb_str(double gb) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.1f", gb);
    std::string s(b);
    for (char &c : s) if (c == '.') c = ',';
    return s + " ГБ";
}

std::string v_str(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", v);
    std::string s(b);
    for (char &c : s) if (c == '.') c = ',';
    return s + " В";
}

std::string db_str(int db) {
    if (db < 0) return "−" + std::to_string(-db) + " дБ";
    return std::to_string(db) + " дБ";
}

struct PowerInfo {
    std::string source = "—";
    std::string ma = "—";
    std::string volt = "—";
    bool known = false;
    bool battery = false;
    int capacity = -1;
};

PowerInfo read_power() {
    PowerInfo p;
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return p;
    bool seen = false, usb = false, ac = false;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string base = std::string("/sys/class/power_supply/") + e->d_name;
        std::string type = rd(base + "/type");
        std::string online = rd(base + "/online");
        if (type == "Battery") {
            seen = true;
            std::string present = rd(base + "/present");
            std::string cap = rd(base + "/capacity");
            std::string vnow = rd(base + "/voltage_now");
            if (!vnow.empty()) p.volt = v_str(std::atof(vnow.c_str()) / 1000000.0);
            p.battery = present != "0";
            if (!cap.empty() && p.battery) p.capacity = std::atoi(cap.c_str());
        } else if (type == "USB") {
            seen = true;
            if (online == "1") usb = true;
        } else if (type == "Mains") {
            seen = true;
            if (online == "1") ac = true;
        }
    }
    closedir(d);
    if (!seen) return p;
    p.known = true;
    if (usb) { p.source = "USB-порт ПК"; p.ma = "500 мА"; }
    else if (ac) { p.source = "Зарядник"; p.ma = "—"; }
    else { p.source = "нет источника"; p.ma = "—"; }
    return p;
}

struct NetInfo {
    std::string ip;
    std::string ssid;
    bool live = false;
};

std::string read_ssid() {
    const char *paths[] = {
        "/etc/wpa_supplicant/wpa_supplicant.conf",
        "/etc/wpa_supplicant.conf",
    };
    for (const char *path : paths) {
        FILE *f = std::fopen(path, "rb");
        if (!f) continue;
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            char *p = std::strstr(line, "ssid=\"");
            if (!p) continue;
            p += 6;
            char *q = std::strchr(p, '"');
            if (q) *q = 0;
            std::fclose(f);
            return p;
        }
        std::fclose(f);
    }
    return std::string();
}

NetInfo read_net() {
    NetInfo n;
    if (access("/sys/class/net/wlan0", F_OK) != 0) return n;
    n.live = true;
    struct ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *a = ifs; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (std::strcmp(a->ifa_name, "wlan0")) continue;
            char buf[INET_ADDRSTRLEN] = {0};
            struct sockaddr_in *sin = (struct sockaddr_in *)a->ifa_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) n.ip = buf;
        }
        freeifaddrs(ifs);
    }
    n.ssid = read_ssid();
    return n;
}

std::string mount_fstype(const char *mp) {
    FILE *f = std::fopen("/proc/mounts", "rb");
    if (!f) return std::string();
    char line[512];
    std::string out;
    while (std::fgets(line, sizeof(line), f)) {
        char dev[256] = {0}, mnt[256] = {0}, fs[64] = {0};
        if (std::sscanf(line, "%255s %255s %63s", dev, mnt, fs) != 3) continue;
        if (std::strcmp(mnt, mp)) continue;
        out = fs;
        break;
    }
    std::fclose(f);
    return out;
}

std::string read_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char b[2048];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) out.append(b, n);
    std::fclose(f);
    return out;
}

int count_desktop() {
    DIR *d = opendir("/usr/share/applications");
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        std::string fn = e->d_name;
        if (fn.size() <= 8 || fn.compare(fn.size() - 8, 8, ".desktop")) continue;
        std::string text = read_file("/usr/share/applications/" + fn);
        if (text.empty()) continue;
        bool nodisplay = false, has_name = false, type_ok = true;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "NoDisplay=true") nodisplay = true;
            else if (line.compare(0, 5, "Name=") == 0) has_name = true;
            else if (line.compare(0, 5, "Type=") == 0)
                type_ok = line.substr(5) == "Application";
            pos = end + 1;
        }
        if (!nodisplay && has_name && type_ok) n++;
    }
    closedir(d);
    return n;
}

const char *accent_name() {
    const std::string a = cfg.get("accent", "");
    if (a.empty() || a == "turquoise") return "Бирюза";
    if (a == "amber") return "Янтарь";
    if (a == "blue") return "Синий";
    if (a == "green") return "Зелёный";
    if (a == "red") return "Красный";
    return "Бирюза";
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

std::string brightness_text() {
    int b = read_brightness();
    if (b < 0 && cfg.kv.find("brightness") != cfg.kv.end())
        b = cfg.geti("brightness", 0);
    if (b < 0) return "—";
    if (b > 255) b = 255;
    return std::to_string((b * 100 + 127) / 255) + " %";
}

std::string volume_text() {
    if (cfg.kv.find("vol_db") == cfg.kv.end()) return "—";
    int db = cfg.geti("vol_db", 0);
    if (db < -40) db = -40;
    if (db > 0) db = 0;
    return db_str(db);
}

std::string keys_text() {
    std::string b = cfg.get("keyled");
    if (b.empty()) b = cfg.get("btnlight");
    if (b == "on" || b == "255") return "вкл";
    if (b == "off" || b == "0") return "выкл";
    if (b == "auto") return "авто";
    std::string raw = rd("/sys/class/leds/button-backlight/brightness");
    if (!raw.empty()) return std::atoi(raw.c_str()) > 0 ? "вкл" : "выкл";
    return "—";
}

std::string launch_text() {
    return cfg.get("boot", "shell") == "player" ? "Плеер" : "Оболочка";
}

std::string launch_sub() {
    return cfg.get("boot", "shell") == "player" ? "текущий режим · player"
                                                : "текущий режим · shell";
}

std::string access_sub() {
    bool usb = cfg.geti("ssh_usb", 1) != 0;
    bool wifi = cfg.geti("ssh_wifi", 1) != 0;
    if (usb && wifi) return "SSH по USB и Wi-Fi";
    if (usb) return "SSH по USB";
    if (wifi) return "SSH по Wi-Fi";
    return "SSH выключен";
}

std::string access_val() {
    return cfg.geti("pin_set", 0) ? "PIN есть" : "PIN нет";
}

std::string route_text() {
    std::string s = rd("/sys/class/switch/h2w/state");
    if (s.empty()) s = rd("/sys/devices/virtual/switch/h2w/state");
    if (s.empty()) return "выход · —";
    return std::atoi(s.c_str()) == 0 ? "динамик · MPD" : "наушники · MPD";
}

void section(Gfx &g, int y, const char *label) {
    ui_section(g, y, label);
}

void row(Gfx &g, int y, const char *ic, const char *name, const std::string &sub,
         const std::string &val, bool chevron, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    icon(g, ic, 24, y + 16, 24, theme.dim);
    g.text(56, y + 7, g.fit(name, 16, 274), 16, theme.fg);
    g.text(56, y + 29, g.fit(sub, 12, 274), 12, theme.dim);
    if (!val.empty()) g.text(428, y + 19, g.fit(val, 12, 170), 12, theme.dim, TF_RIGHT);
    if (chevron) icon(g, "chevron", 438, y + 20, 16, theme.dim);
}

struct Root : ScreenView {
    unsigned last_refresh = 0;
    PowerInfo pwr;
    NetInfo net;
    std::string stor_sub = "SD не найдена";
    std::string stor_val = "—";
    std::string bright_text = "—";
    int pins_n = 0;
    int apps_n = -1;

    Root() { refresh(); }

    const char *id() const override { return "set-root"; }

    void refresh() {
        pwr = read_power();
        net = read_net();
        pins_n = (int)gpins::list().size();
        apps_n = count_desktop();
        bright_text = brightness_text();

        stor_sub = "SD не найдена";
        stor_val = "—";
        std::string fs = mount_fstype("/media/sd");
        if (!fs.empty()) {
            struct statvfs st;
            if (statvfs("/media/sd", &st) == 0 && st.f_blocks > 0) {
                double free_gb = (double)st.f_bavail * st.f_frsize / (1024.0 * 1024.0 * 1024.0);
                stor_val = gb_str(free_gb);
                stor_sub = "SD · " + fs + " · свободно";
            }
        }
    }

    void tick(unsigned now) override {
        if (last_refresh == 0) {
            last_refresh = now;
            refresh();
            return;
        }
        if (now - last_refresh >= 10000) {
            last_refresh = now;
            refresh();
        }
    }

    bool animated() const override { return true; }

    void draw(Gfx &g) override {
        ui_bg(g);
        const bool wifi_on = cfg.geti("wifi", 1) != 0;
        const std::string net_sub = !wifi_on ? "Wi-Fi выключен"
            : (net.ip.empty() ? "Wi-Fi · нет IP"
                              : "Wi-Fi · " + (net.ssid.empty() ? "сеть" : net.ssid));
        const std::string net_val = !wifi_on ? "—" : (net.ip.empty() ? "—" : net.ip);

        std::string pwr_sub, pwr_val;
        if (!pwr.known) {
            pwr_sub = "нет данных";
            pwr_val = "—";
        } else {
            pwr_sub = pwr.source + " · " +
                (pwr.battery ? (pwr.capacity >= 0 ? std::to_string(pwr.capacity) + " %" : "АКБ")
                             : "без АКБ");
            pwr_val = pwr.ma;
        }

        const std::string apps_sub = std::to_string(pins_n) + " на Пуске";
        const std::string apps_val = apps_n >= 0 ? std::to_string(apps_n) : "—";

        g.text(24, 38, "Настройки", 28, theme.fg, TF_LIGHT);
        g.text(24, 70, std::string("УСТРОЙСТВО · ") + theme.code +
               " · LG P700", 11, theme.dim);

        int y = 96;
        section(g, y, "СОСТОЯНИЕ");
        y += 28;
        row(g, y, "battery", "Питание", pwr_sub, pwr_val, true, true); y += 56;
        row(g, y, "wifi", "Сеть", net_sub, net_val, true, false); y += 56;
        row(g, y, "storage", "Хранилище", stor_sub, stor_val, true, false); y += 56;

        section(g, y, "ВИД И ЗВУК");
        y += 28;
        row(g, y, "sliders", "Экран и вид",
            std::string(theme.name) + " · " + accent_name(),
            bright_text, true, true); y += 56;
        row(g, y, "volume", "Звук", route_text(), volume_text(), true, false); y += 56;
        row(g, y, "keyboard", "Клавиши", "подсветка кнопок", keys_text(), true, false); y += 56;

        section(g, y, "СИСТЕМА");
        y += 28;
        row(g, y, "grid", "Приложения", apps_sub, apps_val, true, true); y += 56;
        row(g, y, "power", "Запуск", launch_sub(), launch_text(), true, false); y += 56;
        row(g, y, "lock", "Доступ", access_sub(), access_val(), true, false); y += 56;
        row(g, y, "lab", "Лаборатория", "fb/pan · MPD · jack · QA", "", true, false); y += 56;
        row(g, y, "info", "О телефоне", "pmOS edge · ядро #23", "P700", true, false);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 124, 480, 56)) { app.open("set-power"); return true; }
        if (hit(x, y, 0, 180, 480, 56)) { app.open("set-wifi"); return true; }
        if (hit(x, y, 0, 236, 480, 56)) { app.open("set-storage"); return true; }
        if (hit(x, y, 0, 320, 480, 56)) { app.open("set-display"); return true; }
        if (hit(x, y, 0, 376, 480, 56)) { app.open("set-sound"); return true; }
        if (hit(x, y, 0, 432, 480, 56)) { app.open("set-keys"); return true; }
        if (hit(x, y, 0, 516, 480, 56)) { app.open("set-apps"); return true; }
        if (hit(x, y, 0, 572, 480, 56)) { app.open("set-launch"); return true; }
        if (hit(x, y, 0, 628, 480, 56)) { app.open("set-access"); return true; }
        if (hit(x, y, 0, 684, 480, 56)) { app.open("set-lab"); return true; }
        if (hit(x, y, 0, 740, 480, 56)) { app.open("set-about"); return true; }
        return false;
    }
};

} // namespace

ScreenView *make_set_root() { return new Root(); }
