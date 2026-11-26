#include "graphite.h"
#include <sys/statvfs.h>
#include <sys/utsname.h>
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

std::string slurp(const char *path, size_t cap = 8192) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[512];
    size_t n;
    while (out.size() < cap && (n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return out;
}

std::string comma(std::string s) {
    for (char &c : s)
        if (c == '.') c = ',';
    return s;
}

std::string hsize(unsigned long long b) {
    char buf[48];
    if (b >= 1000000000ULL)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", (double)b / 1e9);
    else if (b >= 1000000ULL)
        std::snprintf(buf, sizeof(buf), "%.0f МБ", (double)b / 1e6);
    else
        std::snprintf(buf, sizeof(buf), "%.0f КБ", (double)b / 1e3);
    return comma(buf);
}

std::string hmem(unsigned long long kb) {
    char buf[48];
    if (kb >= 1024ULL * 1024ULL)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", (double)kb / 1048576.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f МБ", (double)kb / 1024.0);
    return comma(buf);
}

std::string os_name() {
    std::string t = slurp("/etc/os-release");
    size_t pos = 0;
    std::string pretty, name;
    while (pos < t.size()) {
        size_t end = t.find('\n', pos);
        if (end == std::string::npos) end = t.size();
        std::string line = t.substr(pos, end - pos);
        if (line.compare(0, 12, "PRETTY_NAME=") == 0) pretty = line.substr(12);
        else if (line.compare(0, 5, "NAME=") == 0) name = line.substr(5);
        pos = end + 1;
    }
    std::string v = pretty.empty() ? name : pretty;
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        v = v.substr(1, v.size() - 2);
    return v.empty() ? "Alpine Linux" : v;
}

std::string fs_of(const char *mp) {
    FILE *f = std::fopen("/proc/mounts", "r");
    if (!f) return "";
    char line[512], dev[160], point[160], type[64];
    std::string out;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "%159s %159s %63s", dev, point, type) != 3) continue;
        if (!std::strcmp(point, mp)) { out = type; break; }
    }
    std::fclose(f);
    return out;
}

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

bool write_text(const char *path, const std::string &text) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    size_t n = std::fwrite(text.data(), 1, text.size(), f);
    std::fflush(f);
    std::fclose(f);
    return n == text.size();
}

int note(Gfx &g, int y, const char *text) {
    g.text(24, y + 6, g.fit(text, 11, 432), 11, theme.dim);
    return y + 24;
}

int kv(Gfx &g, int y, const char *key, const std::string &val) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val, 16, 272), 16, theme.fg);
    return y + 44;
}

int row(Gfx &g, int y, const std::string &name, const char *sub,
        const std::string &value, bool accent) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 300), 16, accent ? theme.accent : theme.fg);
    if (sub && *sub) g.text(24, y + 29, g.fit(sub, 12, 320), 12, theme.dim);
    if (!value.empty())
        g.text(428, y + 19, g.fit(value, 12, 170), 12, theme.dim, TF_RIGHT);
    icon(g, "chevron", 438, y + 20, 16, theme.dim);
    return y + 56;
}

struct SetAbout : ScreenView {
    std::string mem = "400 МБ · свободно 182 МБ";
    std::string sysname = "postmarketOS edge (Alpine)";
    std::string kernel = "3.4.0-CM #23";
    std::string build = "#23 · 24.11.2026";
    std::string sd = "не найдена";
    std::string now = "14:35 · MSK";
    std::string tz = "Europe/Moscow";
    bool reset_armed = false;

    SetAbout() {
        FILE *f = std::fopen("/proc/meminfo", "r");
        unsigned long long total = 0, avail = 0;
        if (f) {
            char line[256];
            while (std::fgets(line, sizeof(line), f)) {
                unsigned long long v = 0;
                if (std::sscanf(line, "MemTotal: %llu kB", &v) == 1) total = v;
                else if (std::sscanf(line, "MemAvailable: %llu kB", &v) == 1) avail = v;
            }
            std::fclose(f);
        }
        if (total) mem = hmem(total) + " · свободно " + hmem(avail);
        sysname = os_name();

        struct utsname u;
        if (uname(&u) == 0) {
            std::string ver = u.version;
            size_t h = ver.find('#');
            std::string hash;
            if (h != std::string::npos) {
                size_t end = ver.find(' ', h);
                hash = ver.substr(h, end == std::string::npos ? std::string::npos
                                                              : end - h);
            }
            kernel = u.release;
            if (!hash.empty()) kernel += " " + hash;
            build = ver.empty() ? kernel : ver;
        }

        struct statvfs v;
        if (statvfs("/media/sd", &v) == 0) {
            unsigned long long fr = v.f_frsize ? v.f_frsize : v.f_bsize;
            unsigned long long totalb = (unsigned long long)v.f_blocks * fr;
            std::string fs = fs_of("/media/sd");
            sd = hsize(totalb);
            if (!fs.empty()) sd += " · " + fs;
        }

        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char b[32];
        std::snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        tz = trim(slurp("/etc/timezone"));
        if (tz.empty()) tz = "UTC";
        now = b;
        now += tz.find("Moscow") != std::string::npos ? " · MSK" : "";
    }

    void refresh_time() {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char b[32];
        std::snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        tz = trim(slurp("/etc/timezone"));
        if (tz.empty()) tz = "UTC";
        now = b;
        now += tz.find("Moscow") != std::string::npos ? " · MSK" : "";
    }

    void on_show() override { refresh_time(); }

    const char *id() const override { return "set-about"; }
    bool scrollable() const override { return true; }
    int content_height(int) const override { return 760; }

    void do_reset() {
        bool bak = copy_file("/etc/lgtest-settings", "/etc/lgtest-settings.bak");
        settings_save_key("theme", "graphite");
        settings_save_key("accent", "");
        settings_save_key("tex", "auto");
        settings_save_key("brightness", "150");
        settings_save_key("timeout", "30");
        theme_set("graphite", nullptr);
        app.invalidate();
        app.toast(bak ? "Настройки сброшены · .bak сохранён"
                      : "Настройки сброшены · умолчания");
    }

    void copy_info() {
        std::string t;
        t += "LG Optimus L7 P700 (u0)\n";
        t += "SoC: MSM7227A · Cortex-A5 1 ГГц\n";
        t += "Экран: 480×800 · RGB565 · LG4573B\n";
        t += "Память: " + mem + "\n";
        t += "Система: " + sysname + "\n";
        t += "Ядро: " + kernel + "\n";
        t += "Сборка: " + build + "\n";
        t += "IMEI / SIM: недоступен · SIM busy\n";
        t += "SD: " + sd + "\n";
        t += "Время: " + now + " · " + tz + "\n";
        if (!write_text("/tmp/graphite-about.txt", t)) {
            app.toast("Не удалось записать /tmp/graphite-about.txt");
            return;
        }
        app.toast("Сведения: /tmp/graphite-about.txt");
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "О телефоне", "P700 · U0", true);
        g.begin_body(96, 760, scroll_y);
        int y = 96;
        y = ui_section(g, y, "СВЕДЕНИЯ");
        y = kv(g, y, "МОДЕЛЬ", "LG Optimus L7 P700 (u0)");
        y = kv(g, y, "SOC", "MSM7227A · Cortex-A5 1 ГГц");
        y = kv(g, y, "ЭКРАН", "480×800 · RGB565 · LG4573B");
        y = kv(g, y, "ПАМЯТЬ", mem);
        y = kv(g, y, "СИСТЕМА", sysname);
        y = kv(g, y, "ЯДРО", kernel);
        y = kv(g, y, "СБОРКА", build);
        y = kv(g, y, "IMEI / SIM", "недоступен · SIM busy");
        y = kv(g, y, "SD", sd);
        y = note(g, y, "SIM: нет сервера EFS · модем жив");

        y = ui_section(g, y, "СИСТЕМА");
        y = row(g, y, "Время и дата", ("RTC · " + tz).c_str(), now, false);
        y = row(g, y, reset_armed ? "Подтвердить сброс" : "Сброс настроек",
                reset_armed ? "ещё один тап · файл .bak"
                            : "тема, акцент, текстура, яркость, таймаут",
                "", reset_armed);

        g.rect(24, y + 8, 432, 48, theme.surf);
        g.text(240, y + 26, "СКОПИРОВАТЬ СВЕДЕНИЯ", 13, theme.fg,
               TF_CENTER | TF_BOLD);
        y += 56;
        g.text(24, y + 6, "текст сведений — /tmp/graphite-about.txt", 11,
               theme.dim);
        y += 24;

        g.end_body();
        g.text(24, 781, "О ТЕЛЕФОНЕ · P700", 11, theme.dim);
        g.text(456, 781, "u0 · RGB565", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (y < 96 || y >= 760) return false;
        if (hit(x, y, 0, 572, 480, 56)) {
            app.open("set-system");
            return true;
        }
        if (hit(x, y, 0, 628, 480, 56)) {
            if (!reset_armed) {
                reset_armed = true;
                app.toast("Ещё один тап — сброс настроек");
            } else {
                reset_armed = false;
                do_reset();
            }
            return true;
        }
        if (hit(x, y, 24, 692, 432, 48)) {
            copy_info();
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_about() { return new SetAbout(); }
