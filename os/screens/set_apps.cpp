// S7 "Приложения" — real .desktop list, real pin count, tap opens the launcher.
#include "graphite.h"
#include "pins.h"
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

std::string read_file(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string base_of(const std::string &s) {
    std::string t = s;
    size_t to = t.find('\t');
    if (to != std::string::npos) t = t.substr(0, to);
    size_t sp = t.find(' ');
    if (sp != std::string::npos) t = t.substr(0, sp);
    size_t slash = t.find_last_of('/');
    if (slash != std::string::npos) t = t.substr(slash + 1);
    return t;
}

struct AppItem {
    std::string name, exec, icon;
};

const char *icon_for(const AppItem &a) {
    std::string s = lower(a.name + " " + a.icon + " " + a.exec);
    if (s.find("музык") != std::string::npos || s.find("music") != std::string::npos ||
        s.find("lp7player") != std::string::npos) return "music";
    if (s.find("файл") != std::string::npos || s.find("files") != std::string::npos ||
        s.find("pcmanfm") != std::string::npos) return "folder";
    if (s.find("терминал") != std::string::npos || s.find("terminal") != std::string::npos ||
        s.find("xterm") != std::string::npos) return "terminal";
    if (s.find("браузер") != std::string::npos || s.find("browser") != std::string::npos ||
        s.find("dillo") != std::string::npos) return "globe";
    if (s.find("замет") != std::string::npos || s.find("note") != std::string::npos) return "note";
    if (s.find("читал") != std::string::npos || s.find("reader") != std::string::npos) return "note";
    if (s.find("час") != std::string::npos || s.find("clock") != std::string::npos) return "clock";
    if (s.find("настрой") != std::string::npos || s.find("setting") != std::string::npos) return "sliders";
    if (s.find("калькул") != std::string::npos || s.find("calc") != std::string::npos) return "grid";
    return "grid";
}

std::vector<AppItem> scan_desktop() {
    std::vector<AppItem> out;
    DIR *d = opendir("/usr/share/applications");
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        std::string fn = e->d_name;
        if (fn.size() < 8 || fn.compare(fn.size() - 8, 8, ".desktop") != 0) continue;
        std::string text = read_file(("/usr/share/applications/" + fn).c_str());
        if (text.empty()) continue;
        AppItem it;
        std::string type;
        bool nodisplay = false;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 5, "Name=") == 0 && it.name.empty())
                it.name = trim(line.substr(5));
            else if (line.compare(0, 5, "Exec=") == 0 && it.exec.empty())
                it.exec = base_of(trim(line.substr(5)));
            else if (line.compare(0, 5, "Icon=") == 0 && it.icon.empty())
                it.icon = trim(line.substr(5));
            else if (line.compare(0, 5, "Type=") == 0)
                type = trim(line.substr(5));
            else if (line == "NoDisplay=true")
                nodisplay = true;
            pos = end + 1;
        }
        if (it.name.empty() || nodisplay) continue;
        if (!type.empty() && type != "Application") continue;
        out.push_back(it);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const AppItem &a, const AppItem &b) {
        return lower(a.name) < lower(b.name);
    });
    return out;
}

std::string mem_text() {
    FILE *f = std::fopen("/proc/meminfo", "r");
    if (!f) return "нет /proc/meminfo";
    // This kernel (3.4) has no MemAvailable, so fall back to the same
    // Free+Buffers+Cached estimate the apphost admission check uses.
    unsigned long long total = 0, avail = 0, free_kb = 0, buffers = 0, cached = 0;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long v = 0;
        if (std::sscanf(line, "MemTotal: %llu kB", &v) == 1) total = v;
        else if (std::sscanf(line, "MemAvailable: %llu kB", &v) == 1) avail = v;
        else if (std::sscanf(line, "MemFree: %llu kB", &v) == 1) free_kb = v;
        else if (std::sscanf(line, "Buffers: %llu kB", &v) == 1) buffers = v;
        else if (std::sscanf(line, "Cached: %llu kB", &v) == 1) cached = v;
    }
    std::fclose(f);
    if (!total) return "нет данных";
    if (!avail) avail = free_kb + buffers + cached;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu / %llu МБ свободно",
                  avail / 1024ULL, total / 1024ULL);
    return buf;
}

std::string plural_apps(size_t n) {
    size_t m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return "ПРИЛОЖЕНИЕ";
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return "ПРИЛОЖЕНИЯ";
    return "ПРИЛОЖЕНИЙ";
}

struct SetApps : ScreenView {
    std::vector<AppItem> items;
    size_t total = 0;
    int row0 = 110;
    static constexpr size_t kMaxRows = 10;

    SetApps() { scan(); }

    void on_show() override { scan(); }

    void scan() {
        std::vector<AppItem> all = scan_desktop();
        total = all.size();
        if (all.size() > kMaxRows) all.resize(kMaxRows);
        items.swap(all);
    }

    const char *id() const override { return "set-apps"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        std::string sub = std::to_string((long long)total) + " " + plural_apps(total);
        ui_header(g, "Каталог приложений", sub.c_str(), true);
        int y = 96;
        ui_section(g, y, "ВСЕ");
        std::string cnt = std::to_string((long long)items.size());
        if (total > items.size()) cnt += "/" + std::to_string((long long)total);
        g.text(456, y + 12, cnt, 11, theme.dim, TF_RIGHT);
        y += 28;

        for (size_t i = 0; i < items.size(); i++) {
            const AppItem &a = items[i];
            g.hline(0, y, g.cv.w, theme.line2);
            icon(g, icon_for(a), 24, y + 16, 24, theme.fg);
            g.text(60, y + 7, g.fit(a.name, 16, 330), 16, theme.fg);
            std::string meta = base_of(a.exec) + " · .desktop";
            g.text(60, y + 29, g.fit(meta, 12, 330), 12, theme.dim);
            y += 56;
        }

        if (items.empty()) {
            g.text(24, y + 7, "нет .desktop в /usr/share/applications", 16,
                   theme.dim);
            y += 56;
        }

        y += 8;
        int pin = (int)gpins::list().size();
        std::string pl = std::to_string(pin) + " закреплено на Пуске";
        g.hline(0, y, g.cv.w, theme.line2);
        g.text(24, y + 7, pl, 16, theme.fg);
        g.text(24, y + 29, "pins · плитки Пуска", 12, theme.dim);
        y += 56;

        g.hline(0, y, g.cv.w, theme.line2);
        g.text(24, y + 11, "ПАМЯТЬ", 11, theme.dim);
        g.text(172, y + 7, g.fit(mem_text(), 15, 284), 15, theme.fg);
        y += 44;

        if (y + 24 <= 770)
            g.text(24, y + 6, "только просмотр · запуск и закрепление не реализованы",
                   11, theme.dim);

        g.text(24, 781, "ПРИЛОЖЕНИЯ · .DESKTOP", 11, theme.dim);
        g.text(456, 781, "только просмотр", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        (void)x; (void)y;
        return false;
    }
};

} // namespace

ScreenView *make_set_apps() { return new SetApps(); }
