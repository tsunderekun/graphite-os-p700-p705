#include "graphite.h"
#include "pins.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------- pins store
// One id per line in /home/user/.config/graphite/pins. Shared by the "apps"
// and "edit" screens: state is loaded once and kept in memory.
namespace gpins {

namespace {

const char *kPath = "/home/user/.config/graphite/pins";
const size_t kMax = 16;
const char *kDefaults[] = {"player", "files", "terminal", "set-root", "notes"};

std::vector<std::string> g_ids;
bool g_loaded = false;

void ensure_dir() {
    ::mkdir("/home/user", 0755);
    ::mkdir("/home/user/.config", 0755);
    ::mkdir("/home/user/.config/graphite", 0755);
}

void save() {
    ensure_dir();
    std::string tmp = std::string(kPath) + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    for (const std::string &id : g_ids) std::fprintf(f, "%s\n", id.c_str());
    std::fflush(f);
    ::fsync(fileno(f));
    std::fclose(f);
    std::rename(tmp.c_str(), kPath);
}

void load() {
    if (g_loaded) return;
    g_loaded = true;
    ensure_dir();

    FILE *f = std::fopen(kPath, "rb");
    if (!f) {
        for (const char *id : kDefaults) g_ids.push_back(id);
        save();
        return;
    }
    char line[256];
    while (std::fgets(line, sizeof line, f) && g_ids.size() < kMax) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                              s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        size_t p = 0;
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
        s = s.substr(p);
        if (s.empty() || s[0] == '#') continue;
        g_ids.push_back(s);
    }
    std::fclose(f);
}

} // namespace

const std::vector<std::string> &list() {
    load();
    return g_ids;
}

bool has(const std::string &id) {
    load();
    for (const std::string &s : g_ids)
        if (s == id) return true;
    return false;
}

// false when the home screen already holds the maximum number of tiles.
bool toggle(const std::string &id) {
    load();
    for (size_t i = 0; i < g_ids.size(); i++) {
        if (g_ids[i] == id) {
            g_ids.erase(g_ids.begin() + (long)i);
            save();
            return true;
        }
    }
    if (g_ids.size() >= kMax) return false;
    g_ids.push_back(id);
    save();
    return true;
}

void remove(const std::string &id) {
    load();
    for (size_t i = 0; i < g_ids.size(); i++) {
        if (g_ids[i] == id) {
            g_ids.erase(g_ids.begin() + (long)i);
            save();
            return;
        }
    }
}

} // namespace gpins

// ---------------------------------------------------------------- apps
namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

struct Entry {
    const char *id;
    const char *name;
    const char *meta;
    const char *icon;
};

// Only screens that exist in this shell: no camera, gallery, calculator,
// clock, reader or info in v1. Browser is a launcher for dillo.
const Entry kApps[] = {
    {"player",   "Музыка",    "плеер",          "music"},
    {"files",    "Файлы",     "/media/sd",      "folder"},
    {"terminal", "Терминал",  "shell",          "terminal"},
    {"browser",  "Браузер",   "dillo · офлайн", "globe"},
    {"set-root", "Настройки", "тема и звук",    "sliders"},
    {"notes",    "Заметки",   "заметки",        "note"},
};

const int kCount = (int)(sizeof kApps / sizeof kApps[0]);
const int kTop = 96;
const int kRow = 56;
const int kPinX = 408;   // pin control starts here, 48 px wide

struct Apps : ScreenView {
    Apps() { gpins::list(); }

    const char *id() const override { return "apps"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        std::string sub = "ВСЕ · " + std::to_string(kCount);
        int y = ui_header(g, "Приложения", sub.c_str(), false);

        for (int i = 0; i < kCount; i++, y += kRow) {
            g.hline(0, y, 480, theme.line2);
            icon(g, kApps[i].icon, 24, y + 16, 24, theme.fg);
            g.text(60, y + 7, g.fit(kApps[i].name, 16, 340), 16, theme.fg);
            g.text(60, y + 29, g.fit(kApps[i].meta, 12, 340), 12, theme.dim);

            bool pinned = gpins::has(kApps[i].id);
            if (pinned) {
                icon(g, "check", 432, y + 17, 22, theme.accent);
            } else {
                icon(g, "plus", 433, y + 18, 20, theme.dim);
            }
        }

        g.text(24, kTop + kCount * kRow + 16,
               "+ закрепить · ✓ убрать · долгое — правка", 12, theme.dim);
    }

    bool tap(int x, int y) override {
        if (y < kTop || y >= kTop + kCount * kRow) return false;
        int i = (y - kTop) / kRow;
        if (x >= kPinX) {
            if (!gpins::toggle(kApps[i].id))
                app.toast("Сначала уберите плитку");
            return true;
        }
        if (hit(x, y, 0, kTop + i * kRow, 480, kRow)) {
            app.open(kApps[i].id);
            return true;
        }
        return false;
    }

    bool longtap(int x, int y) override {
        if (y < kTop || y >= kTop + kCount * kRow) return false;
        if (x >= kPinX) return false;
        app.open("edit");
        return true;
    }
};

} // namespace

// Shared with home.cpp: the real number of entries in the app list.
int gapps_count() { return kCount; }

ScreenView *make_apps() { return new Apps(); }
