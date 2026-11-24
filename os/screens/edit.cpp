// edit — tile arrangement for the home screen. Only the five real shell apps
// are known here; the first tile is the live now-playing tile and is fed by the
// shared MPD client.
#include "graphite.h"
#include "mpd.h"
#include <cstdio>
#include <string>
#include <vector>

// Pins are owned by screens/apps.cpp (single load, shared by both screens).
namespace gpins {
const std::vector<std::string> &list();
void remove(const std::string &id);
}

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
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

struct TileInfo {
    const char *id;
    const char *icon;
    const char *label;
    const char *meta;
    bool live;
};

const TileInfo kTiles[] = {
    {"player",   "music",    "",           "",            true},
    {"files",    "folder",   "Файлы",      "/media/sd",   false},
    {"terminal", "terminal", "Терминал",   "shell",       false},
    {"set-root", "sliders",  "Настройки",  "тема и звук", false},
    {"notes",    "note",     "Заметки",    "заметки",     false},
    {"browser",  "globe",    "Браузер",    "dillo",       false},
};

const TileInfo *find(const std::string &id) {
    for (const TileInfo &t : kTiles)
        if (id == t.id) return &t;
    return nullptr;
}

const int kTop = 96;        // under the 64 px edit bar
const int kColW = 212;
const int kGapX = 8;
const int kGapY = 6;
const int kTileH = 176;

struct Item {
    int x, y, w, h;
    std::string id;
    bool add;
    bool live;
};

struct Edit : ScreenView {
    int scroll_ = 0;
    unsigned last_live_ = 0;
    std::string live_title_ = "Нет музыки";
    std::string live_sub_ = "MPD недоступен";
    bool live_play_ = false;

    Edit() { gpins::list(); }

    const char *id() const override { return "edit"; }
    bool animated() const override { return true; }

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

    void on_show() override {
        mpd_client().tick(app.now);
        last_live_ = app.now;
        refresh_live();
    }

    void tick(unsigned now) override {
        mpd_client().tick(now);
        if (now - last_live_ >= 1000) {
            last_live_ = now;
            refresh_live();
        }
    }

    static int max_scroll(const std::vector<Item> &items) {
        if (items.empty()) return 0;
        int bottom = items.back().y + items.back().h + kGapY;
        int m = bottom - 800;
        return m > 0 ? m : 0;
    }

    // Same geometry for drawing and hit-testing. The live music tile always
    // opens the grid; the remaining pins keep their file order.
    void layout(std::vector<Item> &out) const {
        out.clear();
        const std::vector<std::string> &pins = gpins::list();
        if (pins.empty()) {
            out.push_back({20, 176, 440, kTileH, "", true, false});
            return;
        }

        int y = kTop;
        int col = 0;

        bool has_player = false;
        for (const std::string &id : pins) has_player = has_player || id == "player";
        if (has_player) {
            out.push_back({20, y, 440, kTileH, "player", false, true});
            y += kTileH + kGapY;
        }

        for (const std::string &id : pins) {
            if (id == "player") continue;
            if (!find(id)) continue;
            int x = col == 0 ? 24 : 24 + kColW + kGapX;
            out.push_back({x, y, kColW, kTileH, id, false, false});
            if (col == 1) {
                y += kTileH + kGapY;
                col = 0;
            } else {
                col = 1;
            }
        }

        int x = col == 0 ? 24 : 24 + kColW + kGapX;
        out.push_back({x, y, kColW, kTileH, "", true, false});
    }

    void draw_tile(Gfx &g, const Item &it, const TileInfo *t) {
        if (t && t->live) {
            g.rect(it.x, it.y, it.w, it.h, theme.accent);
            icon(g, "music", it.x + 16, it.y + 16, 34, theme.onacc);
            g.text(it.x + 16, it.y + it.h - 52,
                   g.fit(live_title_, 16, it.w - 104), 16,
                   theme.onacc, TF_BOLD);
            g.text(it.x + 16, it.y + it.h - 28,
                   g.fit(live_sub_, 12, it.w - 104), 12, theme.onacc);
            icon(g, live_play_ ? "pause" : "play", it.x + it.w - 46,
                 it.y + it.h - 50, 32, theme.onacc);
        } else {
            ui_tile(g, it.x, it.y, it.w, it.h, t ? t->icon : "grid",
                    t ? t->label : "", t ? t->meta : "", false);
        }

        // Remove control: 22 px mark, 44 px hit area (see tap()).
        g.rect(it.x + it.w - 30, it.y + 8, 22, 22, theme.bg);
        icon(g, "close", it.x + it.w - 26, it.y + 12, 14, theme.accent);
    }

    void draw_add(Gfx &g, const Item &it) {
        g.rect(it.x, it.y, it.w, it.h, theme.surf);
        icon(g, "plus", it.x + it.w / 2 - 17, it.y + 62, 34, theme.accent);
        g.text(it.x + it.w / 2, it.y + 120, "Добавить", 12, theme.dim,
               TF_CENTER);
    }

    void draw(Gfx &g) override {
        ui_bg(g);

        std::vector<Item> items;
        layout(items);
        int maxs = max_scroll(items);
        if (scroll_ > maxs) scroll_ = maxs;
        if (scroll_ < 0) scroll_ = 0;

        const std::vector<std::string> &pins = gpins::list();
        if (pins.empty())
            g.text(240, 130, "На Пуске пока пусто — добавьте плитку", 13,
                   theme.dim, TF_CENTER);

        for (const Item &it : items) {
            Item v = it;
            v.y -= scroll_;
            if (v.y >= 800 || v.y + v.h <= 96) continue;
            if (v.add) draw_add(g, v);
            else draw_tile(g, v, find(v.id));
        }

        // Header last: opaque, keeps scrolled tiles out of the bar.
        g.rect(0, 32, 480, 64, theme.bg);
        if (scroll_ > 0) {
            g.rect(0, 0, 480, 32, theme.bg);
            g.rect(0, 96, 480, 4, theme.bg);   // no glyph bleed under the bar
        }
        g.text(24, 42, "Плитки", 24, theme.fg, TF_LIGHT);
        g.text(456, 50, "ГОТОВО", 13, theme.accent, TF_RIGHT | TF_BOLD);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 376, 32, 104, 64)) {
            app.open("home");
            return true;
        }

        std::vector<Item> items;
        layout(items);
        int cy = y + scroll_;
        for (const Item &it : items) {
            if (!hit(x, cy, it.x, it.y, it.w, it.h)) continue;
            if (it.add) {
                app.open("apps");
                return true;
            }
            if (x >= it.x + it.w - 52 && cy < it.y + 44) {
                gpins::remove(it.id);
                return true;
            }
            app.open(it.id.c_str());
            return true;
        }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x; (void)y; (void)dx;
        std::vector<Item> items;
        layout(items);
        int maxs = max_scroll(items);
        if (maxs <= 0) return false;
        int ns = scroll_ - dy;
        if (ns < 0) ns = 0;
        if (ns > maxs) ns = maxs;
        if (ns == scroll_) return false;
        scroll_ = ns;
        return true;
    }
};

} // namespace

ScreenView *make_edit() { return new Edit(); }
