// S1 "Питание" — battery/source facts, CPU state, lock and power off.
// The USB-economy switch and the frequency limit only wrote settings keys and
// changed nothing, so they are gone; the real sysfs values stay.
#include "graphite.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <dirent.h>

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

std::string v_str(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", v);
    std::string s(b);
    for (char &c : s) if (c == '.') c = ',';
    return s + " В";
}

struct Pwr {
    std::string source = "—";
    std::string ma = "—";
    std::string volt = "—";
    std::string header = "НЕТ ДАННЫХ";
    bool known = false;
    bool battery = false;
    int capacity = -1;
};

Pwr read_power() {
    Pwr p;
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

    std::string src = usb ? "USB" : (ac ? "ЗАРЯДНИК" : "НЕТ ИСТОЧНИКА");
    std::string bat = p.battery
        ? (p.capacity >= 0 ? std::to_string(p.capacity) + " %" : "АКБ")
        : "НЕТ АКБ";
    p.header = src + " · " + bat;
    return p;
}

std::string cpu_freq() {
    std::string s = rd("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (s.empty()) s = rd("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq");
    if (s.empty()) return "—";
    int khz = std::atoi(s.c_str());
    if (khz <= 0) return "—";
    return std::to_string(khz / 1000) + " МГц";
}

std::string cpu_gov() {
    std::string s = rd("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    return s.empty() ? "—" : s;
}

std::string load_text() {
    FILE *f = std::fopen("/proc/loadavg", "rb");
    if (!f) return "—";
    char b[128] = {0};
    if (!std::fgets(b, sizeof(b), f)) { std::fclose(f); return "—"; }
    std::fclose(f);
    float a = 0, b2 = 0, c = 0;
    if (std::sscanf(b, "%f %f %f", &a, &b2, &c) != 3) return "—";
    char out[64];
    std::snprintf(out, sizeof(out), "%.2f · %.2f · %.2f", a, b2, c);
    std::string s(out);
    for (char &ch : s) if (ch == '.') ch = ',';
    return s;
}

std::string mem_text() {
    FILE *f = std::fopen("/proc/meminfo", "rb");
    if (!f) return "—";
    long total = -1, avail = -1;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        long v = 0;
        if (std::sscanf(line, "MemTotal: %ld kB", &v) == 1) total = v;
        else if (std::sscanf(line, "MemAvailable: %ld kB", &v) == 1) avail = v;
        if (total >= 0 && avail >= 0) break;
    }
    std::fclose(f);
    if (total <= 0 || avail < 0) return "—";
    return std::to_string(avail / 1024) + " / " + std::to_string(total / 1024) + " МБ";
}

void section(Gfx &g, int y, const char *label) {
    ui_section(g, y, label);
}

void kv(Gfx &g, int y, const char *k, const std::string &v, bool muted, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(k, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(v, 16, 272), 16, muted ? theme.dim : theme.fg);
}

void button(Gfx &g, int y, const char *label) {
    g.rect(24, y, 432, 48, theme.surf);
    g.text(240, y + 17, label, 13, theme.fg, TF_CENTER);
}

struct Power : ScreenView {
    unsigned last_refresh = 0;
    Pwr pwr;
    std::string freq = "—";
    std::string gov = "—";
    std::string load = "—";
    std::string mem = "—";

    Power() { refresh(); }

    const char *id() const override { return "set-power"; }

    void refresh() {
        pwr = read_power();
        freq = cpu_freq();
        gov = cpu_gov();
        load = load_text();
        mem = mem_text();
    }

    void tick(unsigned now) override {
        if (last_refresh == 0) { last_refresh = now; refresh(); return; }
        if (now - last_refresh >= 3000) { last_refresh = now; refresh(); }
    }

    bool animated() const override { return true; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Питание", pwr.header.c_str(), true);

        section(g, 96, "СОСТОЯНИЕ");
        std::string batt = pwr.known
            ? (pwr.battery ? (pwr.capacity >= 0 ? std::to_string(pwr.capacity) + " %" : "АКБ")
                           : "нет АКБ")
            : "—";
        kv(g, 124, "БАТАРЕЯ",
           pwr.known ? batt + " · " + pwr.volt : "—", !pwr.known, true);
        kv(g, 168, "ИСТОЧНИК",
           pwr.known ? pwr.source + " · " + pwr.ma : "—", !pwr.known, false);
        kv(g, 212, "ТОК ЗАРЯДА", "нет датчика", true, false);
        kv(g, 256, "ТЕМПЕРАТУРА", "нет датчика", true, false);

        section(g, 300, "СИСТЕМА");
        kv(g, 328, "ЧАСТОТА", freq, freq == "—", true);
        kv(g, 372, "GOVERNOR", gov, gov == "—", false);
        kv(g, 416, "ЗАГРУЗКА", load, load == "—", false);
        kv(g, 460, "ПАМЯТЬ · СВОБОДНО", mem, mem == "—", false);

        section(g, 504, "ДЕЙСТВИЯ");
        button(g, 532, "ЗАБЛОКИРОВАТЬ СЕЙЧАС");
        button(g, 588, "ВЫКЛЮЧИТЬ…");

        kv(g, 654, "СОН", "нет wakeup", true, true);
        kv(g, 698, "РИСК", "пики на 500 мА → ребут", false, false);

        std::string foot = !pwr.known ? "ПИТАНИЕ · нет данных"
                         : (pwr.battery ? "ПИТАНИЕ · АКБ" : "ПИТАНИЕ · без АКБ");
        g.text(24, 781, foot, 11, theme.dim);
        g.text(456, 781, pwr.known ? pwr.ma + " · " + pwr.volt : "нет данных",
               11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }

        if (hit(x, y, 0, 532, 480, 48)) { app.open("lock"); return true; }
        if (hit(x, y, 0, 588, 480, 48)) { app.open("power"); return true; }

        if (hit(x, y, 0, 212, 480, 44)) {
            app.toast("Нет current_now: датчика тока нет");
            return true;
        }
        if (hit(x, y, 0, 256, 480, 44)) {
            app.toast("batt_therm — константа, CONFIG_THERMAL=n");
            return true;
        }
        if (hit(x, y, 0, 654, 480, 44)) {
            app.toast("Нет wakeup-источников: после suspend не разбудить");
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_power() { return new Power(); }
