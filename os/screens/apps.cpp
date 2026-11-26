#include "graphite.h"
#include "pins.h"
#include "input.h"
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
const char *kDefaults[] = {"player", "files", "terminal", "set-root", "notes", "reader"};

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
// clock or info in v1. Browser is a launcher for dillo.
const Entry kApps[] = {
    {"player",   "Музыка",    "плеер",          "music"},
    {"files",    "Файлы",     "/media/sd",      "folder"},
    {"terminal", "Терминал",  "shell",          "terminal"},
    {"browser",  "Браузер",   "dillo · офлайн", "globe"},
    {"set-root", "Настройки", "тема и звук",    "sliders"},
    {"notes",    "Заметки",   "заметки",        "note"},
    {"reader",   "Читалка",   "TXT-книги",      "note"},
    {"viewer",   "Фотографии", "PNG · JPEG",     "grid"},
    {"clock",    "Часы",      "время · таймер",  "clock"},
    {"usb",      "USB",       "статус · без переключения", "terminal"},
};

const int kCount = (int)(sizeof kApps / sizeof kApps[0]);
const int kTop = 96;
const int kRow = 56;
const int kPinX = 408;   // pin control starts here, 48 px wide

struct Apps : ScreenView {
    TextField query{120};
    KbModel keyboard;
    bool searching = false;
    bool keyboard_open = false;
    int scroll = 0, drag_px = 0;
    Apps() { gpins::list(); }

    const char *id() const override { return "apps"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        std::string sub = searching ? "ПОИСК · " + std::to_string(kCount) : "ВСЕ · " + std::to_string(kCount);
        int y = ui_header(g, "Приложения", sub.c_str(), false);
        if (searching) {
            g.rect(24, 64, 432, 40, theme.surf2); g.frame(24, 64, 432, 40, theme.line2);
            g.text(36, 76, query.text().empty() ? "Поиск приложений…" : query.text(), 14,
                   query.text().empty() ? theme.dim : theme.fg);
            g.text(438, 76, "×", 18, theme.dim, TF_CENTER);
            y = 112;
        }

        int shown = 0;
        for (int i = 0; i < kCount; i++) {
            std::string hay = std::string(kApps[i].name) + " " + kApps[i].meta;
            std::string q = query.text();
            // ASCII-insensitive; UTF-8 Russian remains exact-match friendly.
            for (char &c : hay) if (c >= 'A' && c <= 'Z') c += 32;
            for (char &c : q) if (c >= 'A' && c <= 'Z') c += 32;
            if (searching && !q.empty() && hay.find(q) == std::string::npos) continue;
            int ry = y + shown * kRow - scroll;
            if (ry < y - kRow || ry >= (searching && keyboard_open ? 512 : 800)) { shown++; continue; }
            g.hline(0, ry, 480, theme.line2);
            icon(g, kApps[i].icon, 24, ry + 16, 24, theme.fg);
            g.text(60, ry + 7, g.fit(kApps[i].name, 16, 340), 16, theme.fg);
            g.text(60, ry + 29, g.fit(kApps[i].meta, 12, 340), 12, theme.dim);

            bool pinned = gpins::has(kApps[i].id);
            if (pinned) {
                icon(g, "check", 432, ry + 17, 22, theme.accent);
            } else {
                icon(g, "plus", 433, ry + 18, 20, theme.dim);
            }
            shown++;
        }
        if (searching && keyboard_open) keyboard.draw(g);
        else g.text(24, kTop + kCount * kRow + 16,
               "+ закрепить · ✓ убрать · долгое — правка", 12, theme.dim);
    }

    bool tap(int x, int y) override {
        if (y >= 32 && y < 96 && x >= 64 && x < 300) { searching = !searching; query.clear(); scroll = 0; return true; }
        if (searching && y >= 64 && y < 104) { if (x >= 400) { query.clear(); scroll=0; } else keyboard_open=true; return true; }
        if (searching && keyboard_open && y >= 512) {
            InputEvent e = keyboard.hit(x,y);
            if (e.action == InputAction::Character) query.insert(e.codepoint);
            else if (e.action == InputAction::Space) query.insert(' ');
            else if (e.action == InputAction::Backspace) query.backspace();
            else if (e.action == InputAction::Cancel) keyboard_open = false;
            scroll = 0; return true;
        }
        int start = searching ? 112 : kTop;
        if (y < start || y >= (searching && keyboard_open ? 512 : 800)) return false;
        int target = (y - start + scroll) / kRow;
        int i = -1, n = 0;
        for (int j=0;j<kCount;j++) {
            std::string hay=std::string(kApps[j].name)+" "+kApps[j].meta, q=query.text();
            for(char &c:hay) if(c>='A'&&c<='Z') c+=32;
            for(char &c:q) if(c>='A'&&c<='Z') c+=32;
            if(searching&&!q.empty()&&hay.find(q)==std::string::npos) continue;
            if(n++==target){i=j;break;}
        }
        if(i<0) return true;
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

    bool drag(int, int, int, int dy) override {
        int matches = 0;
        for(int i=0;i<kCount;i++) {
            std::string hay=std::string(kApps[i].name)+" "+kApps[i].meta, q=query.text();
            for(char &c:hay) if(c>='A'&&c<='Z') c+=32;
            for(char &c:q) if(c>='A'&&c<='Z') c+=32;
            if(!searching||q.empty()||hay.find(q)!=std::string::npos) ++matches;
        }
        int max = matches*kRow - (searching ? 7*kRow : 12*kRow);
        if(max<0) max=0;
        drag_px += dy; int rows=drag_px/kRow; if(!rows) return false;
        drag_px -= rows*kRow; int next=scroll-rows*kRow;
        if (next < 0) next = 0;
        if (next > max) next = max;
        bool changed=next!=scroll; scroll=next; return changed;
    }

    bool key(int k, bool down) override {
        if (!searching || !down) return false;
        if (k == 0xff1b || k == 0xff08) { if(keyboard_open) keyboard_open=false; else searching=false; return true; }
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
