// home — pinned tiles. The first tile is the live now-playing tile; its text
// comes from the shared MPD client and is refreshed once a second. File-system
// counters (free space, processes, notes) are refreshed every 10 s, never in
// draw().
#include "graphite.h"
#include "mpd.h"
#include "pins.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

// Defined in screens/apps.cpp: the real number of entries in the app list.
int gapps_count();

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string home_dir() {
    const char *h = std::getenv("HOME");
    return (h && h[0] == '/') ? std::string(h) : std::string("/home/user");
}

// "21,7" — one decimal, Russian comma.
std::string one_decimal(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string s(b);
    for (char &c : s) if (c == '.') c = ',';
    return s;
}

// Free space on the SD card, falling back to the root file system.
std::string free_text() {
    const char *paths[] = {"/media/sd", "/"};
    for (const char *p : paths) {
        struct statvfs st;
        if (statvfs(p, &st) != 0 || st.f_blocks == 0 || st.f_frsize == 0)
            continue;
        double bytes = (double)st.f_bavail * (double)st.f_frsize;
        double gb = bytes / (1024.0 * 1024.0 * 1024.0);
        if (gb >= 1.0) return one_decimal(gb) + " ГБ свободно";
        double mb = bytes / (1024.0 * 1024.0);
        char b[48];
        std::snprintf(b, sizeof b, "%.0f МБ свободно", mb);
        std::string s(b);
        for (char &c : s) if (c == '.') c = ',';
        return s;
    }
    return "—";
}

bool numeric_name(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return false;
    return true;
}

std::string proc_text() {
    DIR *d = opendir("/proc");
    if (!d) return "—";
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
        if (numeric_name(e->d_name)) n++;
    closedir(d);
    return std::to_string(n) + " процессов";
}

// -1 when the directory cannot be read.
int count_txt(const std::string &dir) {
    DIR *d = opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        const char *nm = e->d_name;
        size_t len = std::strlen(nm);
        if (len <= 4 || std::strcmp(nm + len - 4, ".txt") != 0) continue;
        std::string full = dir;
        if (full.empty() || full.back() != '/') full += '/';
        full += nm;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) n++;
    }
    closedir(d);
    return n;
}

std::string notes_text() {
    const std::string dirs[] = {home_dir() + "/.config/graphite",
                                "/media/sd/Notes"};
    int total = 0;
    bool seen = false;
    for (const std::string &d : dirs) {
        int n = count_txt(d);
        if (n >= 0) {
            seen = true;
            total += n;
        }
    }
    if (!seen) return "—";
    int n = total % 100;
    const char *word = "заметок";
    if (n < 11 || n > 14) {
        if (n % 10 == 1) word = "заметка";
        else if (n % 10 >= 2 && n % 10 <= 4) word = "заметки";
    }
    return std::to_string(total) + " " + word;
}

const char *accent_label() {
    const std::string a = cfg.get("accent", "");
    if (a.empty() || a == "turquoise") return "Бирюза";
    if (a == "amber") return "Янтарь";
    if (a == "blue") return "Синий";
    if (a == "green") return "Зелёный";
    if (a == "red") return "Красный";
    return "Бирюза";
}

std::string base_name(const std::string &path) {
    if (path.empty()) return "";
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

std::string fmt_time(double seconds) {
    int sec = (int)(seconds + 0.5);
    if (sec < 0) sec = 0;
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    char b[32];
    if (h > 0)
        std::snprintf(b, sizeof b, "%d:%02d:%02d", h, m, s);
    else
        std::snprintf(b, sizeof b, "%d:%02d", m, s);
    return b;
}

struct AppDef {
    const char *id;
    const char *icon;
    const char *name;
    const char *meta;   // static fallback; counters are computed in refresh_meta()
};

// Only screens that exist in this shell. Unknown ids from older pin files are
// skipped by find().
static const AppDef kApps[] = {
    {"player", "music", "Музыка", "плеер"},
    {"files", "folder", "Файлы", ""},
    {"terminal", "terminal", "Терминал", ""},
    {"set-root", "sliders", "Настройки", ""},
    {"notes", "note", "Заметки", ""},
    {"apps", "grid", "Приложения", ""},
    {"browser", "globe", "Браузер", "dillo · офлайн"},
};

static const AppDef *find(const std::string &id) {
    for (const AppDef &a : kApps)
        if (id == a.id) return &a;
    return nullptr;
}

// The tile geometry is shared by draw() and tap().
const int kTop = 224;          // first app row under the live tile
const int kTileW = 212;
const int kTileH = 176;
const int kGap = 8;

struct Home : ScreenView {
    struct Cell {
        int x, y, w, h;
        std::string id;
    };
    std::vector<Cell> cells;

    unsigned last_live_ = 0;
    unsigned last_meta_ = 0;
    std::string live_title_ = "Нет музыки";
    std::string live_sub_ = "MPD недоступен";
    bool live_play_ = false;

    std::string meta_files_ = "—";
    std::string meta_term_ = "—";
    std::string meta_notes_ = "—";
    std::string meta_settings_ = "—";
    std::string meta_apps_ = "—";

    const char *id() const override { return "home"; }
    bool animated() const override { return true; }

    std::string meta_for(const std::string &id) const {
        if (id == "files") return meta_files_;
        if (id == "terminal") return meta_term_;
        if (id == "notes") return meta_notes_;
        if (id == "set-root") return meta_settings_;
        if (id == "apps") return meta_apps_;
        if (const AppDef *a = find(id)) return a->meta;
        return "";
    }

    void refresh_live() {
        MpdClient &m = mpd_client();
        if (!m.connected()) {
            live_title_ = "Нет музыки";
            live_sub_ = "MPD недоступен";
            live_play_ = false;
            return;
        }
        if (!m.has_song()) {
            live_title_ = "Нет музыки";
            live_sub_ = "очередь пуста";
            live_play_ = false;
            return;
        }

        const MpdSong &s = m.song();
        live_title_ = s.title.empty() ? base_name(s.file) : s.title;
        if (live_title_.empty()) live_title_ = "—";

        const std::string &state = m.status().state;
        const char *st = state == "play" ? "играет"
                       : state == "pause" ? "пауза"
                                          : "остановлено";
        live_play_ = state == "play";

        std::string sub;
        if (!s.artist.empty()) sub = s.artist + " · ";
        sub += st;
        double dur = m.status().duration;
        if (dur <= 0) dur = s.time;
        if (dur > 0) sub += " · " + fmt_time(dur);
        live_sub_ = sub;
    }

    void refresh_meta() {
        meta_files_ = free_text();
        meta_term_ = proc_text();
        meta_notes_ = notes_text();
        meta_settings_ = std::string(theme.name) + " · " + accent_label();
        meta_apps_ = std::to_string(gapps_count()) + " приложений";
    }

    void on_show() override {
        MpdClient &m = mpd_client();
        m.tick(app.now);
        last_live_ = app.now;
        refresh_live();
        refresh_meta();
        last_meta_ = app.now;
    }

    void tick(unsigned now) override {
        mpd_client().tick(now);
        if (now - last_live_ >= 1000) {
            last_live_ = now;
            refresh_live();
        }
        if (now - last_meta_ >= 10000) {
            last_meta_ = now;
            refresh_meta();
        }
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        cells.clear();

        bool live = gpins::has("player");
        int top = 40;
        if (live) {
            g.rect(24, 40, 432, 176, theme.accent);
            icon(g, "music", 40, 56, 34, theme.onacc);
            g.text(40, 168, g.fit(live_title_, 16, 356), 16,
                   theme.onacc, TF_BOLD);
            g.text(40, 194, g.fit(live_sub_, 12, 330), 12, theme.onacc);
            icon(g, live_play_ ? "pause" : "play", 412, 168, 32, theme.onacc);
            cells.push_back({24, 40, 432, 176, "player"});
            top = kTop;
        }

        // pinned apps, two per row; a lone tile in the last row is wide
        std::vector<const AppDef *> apps;
        for (const std::string &pid : gpins::list()) {
            if (pid == "player") continue;
            if (const AppDef *a = find(pid)) apps.push_back(a);
            if (apps.size() >= 6) break;
        }

        int x0 = 24, y0 = top, tw = kTileW, th = kTileH, row = 0;
        for (size_t i = 0; i < apps.size(); i += 2) {
            int y = y0 + row * (th + kGap);
            bool wide = (i + 1 == apps.size());
            int w = wide ? 432 : tw;
            ui_tile(g, x0, y, w, th, apps[i]->icon, apps[i]->name,
                    meta_for(apps[i]->id).c_str(), false);
            cells.push_back({x0, y, w, th, apps[i]->id});
            if (!wide) {
                ui_tile(g, x0 + tw + kGap, y, tw, th, apps[i + 1]->icon,
                        apps[i + 1]->name, meta_for(apps[i + 1]->id).c_str(),
                        false);
                cells.push_back({x0 + tw + kGap, y, tw, th, apps[i + 1]->id});
            }
            row++;
        }

        if (apps.empty()) {
            g.rect(x0, y0, 432, th, theme.surf);
            g.text(240, y0 + 62, "+", 34, theme.accent, TF_CENTER | TF_LIGHT);
            g.text(240, y0 + 112, "Добавить плитку", 12, theme.dim, TF_CENTER);
            cells.push_back({x0, y0, 432, th, "edit"});
        } else if (apps.size() < 6 && apps.size() % 2 == 0) {
            int y = y0 + row * (th + kGap);
            if (y + th <= 776) {
                g.rect(x0, y, 432, th, theme.surf);
                g.text(240, y + 62, "+", 34, theme.accent, TF_CENTER | TF_LIGHT);
                g.text(240, y + 112, "Добавить плитку", 12, theme.dim, TF_CENTER);
                cells.push_back({x0, y, 432, th, "edit"});
            }
        }

        icon(g, "chevron", 436, 768, 18, theme.dim);
    }

    // Tap on the play/pause corner: toggles without leaving the launcher.
    bool tap_toggle() {
        MpdClient &m = mpd_client();
        if (!m.connected()) {
            app.toast("MPD недоступен");
            return true;
        }
        if (!m.has_song()) {
            app.toast("Нет музыки");
            return true;
        }
        m.toggle();
        refresh_live();
        return true;
    }

    bool tap(int x, int y) override {
        for (const Cell &c : cells)
            if (hit(x, y, c.x, c.y, c.w, c.h)) {
                if (c.id == "player" && hit(x, y, 400, 156, 60, 60))
                    return tap_toggle();
                app.open(c.id.c_str());
                return true;
            }
        if (hit(x, y, 408, 744, 72, 56)) {
            app.open("apps");
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_home() { return new Home(); }
