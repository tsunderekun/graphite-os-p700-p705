// S2 "Сеть" — real Wi-Fi state, addresses and a scan through iwlist.
// The scan runs in the background; tick() parses /tmp/wifi-scan.txt.
#include "graphite.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
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

bool cfg_wifi_on() {
    const std::string v = cfg.get("wifi", "1");
    if (v == "on") return true;
    if (v == "off") return false;
    return cfg.geti("wifi", 1) != 0;
}

bool wifi_iface() { return access("/sys/class/net/wlan0", F_OK) == 0; }

bool wifi_is_up() {
    if (!wifi_iface()) return cfg_wifi_on();
    std::string f = rd("/sys/class/net/wlan0/flags");
    if (f.empty()) return cfg_wifi_on();
    unsigned flags = (unsigned)std::strtoul(f.c_str(), nullptr, 0);
    return (flags & 1u) != 0;                 // IFF_UP
}

std::string dbm_text(int dbm) {
    if (dbm < 0) return "−" + std::to_string(-dbm) + " dBm";
    return std::to_string(dbm) + " dBm";
}

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

std::string read_gateway() {
    FILE *f = std::fopen("/proc/net/route", "rb");
    if (!f) return std::string();
    char line[512];
    std::string out;
    if (std::fgets(line, sizeof(line), f)) {}       // header
    while (std::fgets(line, sizeof(line), f)) {
        char iface[64] = {0}, dest[64] = {0}, gw[64] = {0};
        if (std::sscanf(line, "%63s %63s %63s", iface, dest, gw) != 3) continue;
        if (std::strcmp(iface, "wlan0") || std::strcmp(dest, "00000000")) continue;
        unsigned v = (unsigned)std::strtoul(gw, nullptr, 16);
        char b[32];
        std::snprintf(b, sizeof(b), "%u.%u.%u.%u",
                      v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255);
        out = b;
        break;
    }
    std::fclose(f);
    return out;
}

struct NetInfo {
    bool live = false;
    std::string ip;
    std::string gw;
    std::string mac = "—";
    std::string ssid;
    int dbm = 0;
    bool has_dbm = false;
};

NetInfo read_net() {
    NetInfo n;
    if (!wifi_iface()) return n;
    n.live = true;

    struct ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *a = ifs; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (std::strcmp(a->ifa_name, "wlan0")) continue;
            char b[INET_ADDRSTRLEN] = {0};
            struct sockaddr_in *sin = (struct sockaddr_in *)a->ifa_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, b, sizeof(b))) n.ip = b;
        }
        freeifaddrs(ifs);
    }
    n.gw = read_gateway();
    std::string mac = rd("/sys/class/net/wlan0/address");
    n.mac = mac.empty() ? "—" : mac;
    n.ssid = read_ssid();

    FILE *f = std::fopen("/proc/net/wireless", "rb");
    if (f) {
        char line[512];
        if (std::fgets(line, sizeof(line), f)) {}
        while (std::fgets(line, sizeof(line), f)) {
            if (!std::strstr(line, "wlan0:")) continue;
            unsigned st = 0;
            float link = 0, level = 0;
            if (std::sscanf(line, " %*[^:]: %x %f %f", &st, &link, &level) >= 3 &&
                level != 0) {
                n.dbm = (int)level;
                n.has_dbm = true;
            }
            break;
        }
        std::fclose(f);
    }
    return n;
}

// ---------------------------------------------------------------- scan

struct ScanNet {
    std::string ssid;
    int level = 0;
    bool has_level = false;
};

std::string level_text(const ScanNet &n) {
    if (!n.has_level) return "—";
    if (n.level < 0) return "−" + std::to_string(-n.level) + " dBm";
    return std::to_string(n.level) + "/100";
}

bool parse_scan(const char *path, std::vector<ScanNet> &out) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    char line[512];
    ScanNet cur;
    auto flush = [&]() {
        if (!cur.ssid.empty()) out.push_back(cur);
        cur = ScanNet();
    };
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strstr(line, "Cell ")) { flush(); continue; }
        char *e = std::strstr(line, "ESSID:\"");
        if (e) {
            e += 7;
            char *q = std::strchr(e, '"');
            if (q) *q = 0;
            std::string s = trim(e);
            if (!s.empty()) cur.ssid = s;
            continue;
        }
        char *s = std::strstr(line, "Signal level=");
        if (s) {
            s += 13;
            cur.level = std::atoi(s);
            cur.has_level = true;
        }
    }
    flush();
    std::fclose(f);

    std::sort(out.begin(), out.end(), [](const ScanNet &a, const ScanNet &b) {
        if (a.has_level != b.has_level) return a.has_level > b.has_level;
        return a.level > b.level;
    });
    std::vector<ScanNet> uniq;
    for (const ScanNet &n : out) {
        bool dup = false;
        for (const ScanNet &u : uniq)
            if (u.ssid == n.ssid) { dup = true; break; }
        if (!dup) uniq.push_back(n);
    }
    out.swap(uniq);
    if (out.size() > 7) out.resize(7);
    return true;
}

std::string net_count_text(int n) {
    int m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return std::to_string(n) + " СЕТЬ";
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
        return std::to_string(n) + " СЕТИ";
    return std::to_string(n) + " СЕТЕЙ";
}

void section(Gfx &g, int y, const char *label, const char *right) {
    ui_section(g, y, label);
    if (right && *right) g.text(456, y + 14, right, 11, theme.dim, TF_RIGHT);
}

void srow(Gfx &g, int y, const char *ic, const char *name, const std::string &sub,
          bool on, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    icon(g, ic, 24, y + 16, 24, theme.dim);
    g.text(56, y + 7, g.fit(name, 16, 330), 16, theme.fg);
    g.text(56, y + 29, g.fit(sub, 12, 340), 12, theme.dim);
    ui_switch(g, 412, y + 16, on);
}

void kv(Gfx &g, int y, const char *k, const std::string &v, bool muted, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(k, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(v, 16, 272), 16, muted ? theme.dim : theme.fg);
}

void save_key(const char *k, const char *v) {
    if (!settings_save_key(k, v)) cfg.set(k, v);
}

struct Wifi : ScreenView {
    unsigned last_refresh = 0;
    NetInfo net;

    bool scanning = false;
    bool scan_done = false;
    pid_t scan_pid = -1;
    int scan_rc = 0;
    unsigned scan_started = 0;
    unsigned scan_checked = 0;
    std::vector<ScanNet> nets;
    std::string scan_err;

    Wifi() { net = read_net(); }

    const char *id() const override { return "set-wifi"; }

    void start_scan() {
        if (!wifi_iface()) { app.toast("Wi-Fi: wlan0 не найден"); return; }
        if (!wifi_is_up()) { app.toast("Wi-Fi выключен"); return; }
        if (scanning) return;

        scan_err.clear();
        nets.clear();
        scan_done = false;
        scan_rc = 0;
        ::unlink("/tmp/wifi-scan.txt");
        scan_pid = fork();
        if (scan_pid == 0) {
            execl("/bin/sh", "sh", "-c",
                  "iwlist wlan0 scan > /tmp/wifi-scan.txt 2>&1", (char *)nullptr);
            _exit(127);
        }
        if (scan_pid < 0) {
            scan_pid = -1;
            scan_err = "не удалось запустить iwlist";
            return;
        }
        scanning = true;
        scan_started = app.now ? app.now : 1;
        scan_checked = scan_started;
    }

    void tick(unsigned now) override {
        if (last_refresh == 0) {
            last_refresh = now;
            net = read_net();
        } else if (now - last_refresh >= 10000) {
            last_refresh = now;
            net = read_net();
        }
        if (!scanning) return;

        if (!scan_done && scan_pid > 0) {
            int st = 0;
            pid_t r = waitpid(scan_pid, &st, WNOHANG);
            if (r == scan_pid) {
                scan_pid = -1;
                scan_done = true;
                scan_rc = st;
            }
        }

        if (now - scan_checked < 1000) return;
        scan_checked = now;

        std::vector<ScanNet> v;
        if (parse_scan("/tmp/wifi-scan.txt", v)) nets = v;

        if (!scan_done && now - scan_started >= 20000 && scan_pid > 0) {
            kill(scan_pid, SIGKILL);
            int st = 0;
            waitpid(scan_pid, &st, 0);
            scan_pid = -1;
            scan_done = true;
            scan_rc = -1;
        }
        if (!scan_done) return;

        if (parse_scan("/tmp/wifi-scan.txt", v)) nets = v;
        scanning = false;
        if (nets.empty()) {
            if (access("/tmp/wifi-scan.txt", F_OK) != 0)
                scan_err = "iwlist: нет /tmp/wifi-scan.txt";
            else if (scan_rc == -1)
                scan_err = "iwlist: таймаут";
            else if (WIFEXITED(scan_rc) && WEXITSTATUS(scan_rc) != 0)
                scan_err = "iwlist: код " + std::to_string(WEXITSTATUS(scan_rc));
            else
                scan_err = "iwlist: сети не найдены";
        }
    }

    bool animated() const override { return true; }

    void toggle_wifi() {
        if (!wifi_iface()) { app.toast("Wi-Fi: wlan0 не найден"); return; }
        bool on = wifi_is_up();
        int rc = std::system(on ? "ip link set wlan0 down 2>/dev/null"
                                : "ip link set wlan0 up 2>/dev/null");
        if (rc != 0) {
            app.toast(on ? "Wi-Fi: не удалось выключить wlan0"
                         : "Wi-Fi: не удалось включить wlan0");
            return;                       // keep the state we actually have
        }
        save_key("wifi", on ? "0" : "1");
        last_refresh = 0;
    }

    void draw_scan_area(Gfx &g) {
        if (nets.empty()) {
            g.rect(24, 424, 432, 252, theme.surf);
            icon(g, "search", 224, 470, 32, theme.dim);
            const char *title = scanning ? "СКАНИРОВАНИЕ…"
                                         : (scan_err.empty() ? "НЕТ РЕЗУЛЬТАТОВ СКАНА"
                                                             : "СЕТИ НЕ НАЙДЕНЫ");
            std::string sub = scanning ? "iwlist wlan0 scan"
                             : (scan_err.empty() ? "Запустите скан кнопкой выше"
                                                 : scan_err);
            g.text(240, 524, title, 15, scanning ? theme.accent : theme.fg, TF_CENTER);
            g.text(240, 548, g.fit(sub, 11, 400), 11, theme.dim, TF_CENTER);
            g.text(240, 570, scan_err.empty() ? "До 7 сетей" : "iwlist wlan0 scan",
                   11, theme.dim, TF_CENTER);
            return;
        }
        int y = 424;
        for (size_t i = 0; i < nets.size(); i++) {
            const ScanNet &n = nets[i];
            if (i) g.hline(0, y, g.cv.w, theme.line2);
            icon(g, "wifi", 24, y + 8, 20, theme.dim);
            g.text(56, y + 9, g.fit(n.ssid, 16, 330), 16, theme.fg);
            g.text(428, y + 11, level_text(n), 12, theme.dim, TF_RIGHT);
            y += 36;
        }
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Сеть", (std::string("WI-FI · ") + theme.code).c_str(), true);

        const bool on = wifi_is_up();
        const bool linked = net.live && !net.ip.empty();
        std::string sub;
        if (!on) sub = "выключен";
        else if (!net.live) sub = "нет wlan0";
        else if (linked)
            sub = (net.ssid.empty() ? std::string("подключено") : net.ssid) +
                  (net.has_dbm ? " · " + dbm_text(net.dbm) : "");
        else sub = "включён · нет IP";

        srow(g, 96, "wifi", "Wi-Fi", sub, on, true);
        kv(g, 152, "СЕТЬ", on && linked ? net.ssid : "не подключено", !(on && linked), true);
        kv(g, 196, "IP И ШЛЮЗ",
           on && linked ? net.ip + (net.gw.empty() ? "" : " · " + net.gw) : "—",
           !(on && linked), false);
        kv(g, 240, "MAC", on ? net.mac : "—", !on, false);

        bool rndis = cfg.geti("usb_net", 1) != 0;
        srow(g, 284, "usb", "USB-сеть (RNDIS)", "172.16.42.1 · SSH с ПК", rndis, false);

        std::string status;
        if (scanning) status = "СКАН…";
        else if (!nets.empty()) status = net_count_text((int)nets.size());
        else status = "—";
        section(g, 340, "СЕТИ", status.c_str());

        g.rect(24, 376, 432, 44, theme.surf);
        g.text(240, 389, scanning ? "СКАНИРУЮ…" : "СКАНИРОВАТЬ", 13,
               scanning ? theme.dim : theme.fg, TF_CENTER);

        draw_scan_area(g);

        section(g, 684, "НЕДОСТУПНО", nullptr);
        g.text(24, 714, "SIM · SIM busy · нет сервера EFS", 11, theme.dim);
        g.text(24, 732, "Bluetooth · HCI молчит · GPS · NMEA не идёт", 11, theme.dim);
        g.text(24, 750, "Точка доступа · AP не проверен", 11, theme.dim);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }
        if (hit(x, y, 0, 96, 480, 56)) { toggle_wifi(); return true; }
        if (hit(x, y, 0, 284, 480, 56)) {
            bool on = cfg.geti("usb_net", 1) != 0;
            save_key("usb_net", on ? "0" : "1");
            return true;
        }
        if (hit(x, y, 24, 376, 432, 44)) { start_scan(); return true; }
        return false;
    }
};

} // namespace

ScreenView *make_set_wifi() { return new Wifi(); }
