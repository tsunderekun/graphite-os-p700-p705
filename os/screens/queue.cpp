#include "graphite.h"
#include "mpd.h"

#include <X11/XF86keysym.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace {

const int PIVOT_Y = 32;
const int PIVOT_H = 64;
const int PIVOT_RIGHT = 456;

const int SUB_TOP = 96;
const int SUB_H = 44;
const int LIST_TOP = SUB_TOP + SUB_H;   // 140
const int ROW_H = 56;
const int LIST_VIEW = 800 - LIST_TOP;

std::string fmt_time(int sec) {
    if (sec < 0) sec = 0;
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string base_name(const std::string &path) {
    if (path.empty()) return "";
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

std::string song_title(const MpdSong &s) {
    if (!s.title.empty()) return s.title;
    std::string n = base_name(s.file);
    return n.empty() ? "—" : n;
}

const char *track_word(int n) {
    int m10 = n % 10;
    int m100 = n % 100;
    if (m10 == 1 && m100 != 11) return "ТРЕК";
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return "ТРЕКА";
    return "ТРЕКОВ";
}

std::string queue_summary(const std::vector<MpdSong> &q) {
    int total = 0;
    for (const MpdSong &s : q)
        if (s.time > 0) total += s.time;
    char buf[96];
    if (total > 0)
        std::snprintf(buf, sizeof(buf), "%d %s · %s", (int)q.size(),
                      track_word((int)q.size()), fmt_time(total).c_str());
    else
        std::snprintf(buf, sizeof(buf), "%d %s", (int)q.size(),
                      track_word((int)q.size()));
    return buf;
}

// 0 = title, 1..3 = right-hand tabs (left to right).
struct TabZone {
    int x0 = 0, x1 = 0;
};

void draw_pivot(Gfx &g, const char *title, const char *tab1, const char *tab2,
                const char *tab3, TabZone *zones) {
    g.text(24, 42, title, 24, theme.accent, TF_LIGHT);
    const char *names[3] = {tab1, tab2, tab3};
    int right = PIVOT_RIGHT;
    for (int i = 2; i >= 0; i--) {
        int w = g.textw(names[i], 12);
        g.text(right, 48, names[i], 12, theme.dim, TF_RIGHT);
        if (zones) {
            zones[i].x0 = std::max(0, right - w - 8);
            zones[i].x1 = right + 6;
        }
        right -= w + 14;
    }
}

int pivot_hit(const TabZone *zones, int x, int y) {
    if (y < PIVOT_Y || y >= PIVOT_Y + PIVOT_H) return 0;
    for (int i = 0; i < 3; i++)
        if (x >= zones[i].x0 && x < zones[i].x1) return i + 1;
    return 0;
}

struct Queue : ScreenView {
    int scroll_ = 0;
    bool scroll_init_ = false;
    int swipe_ = 0;
    TabZone tabs_[3];

    const char *id() const override { return "queue"; }
    bool animated() const override { return true; }

    void tick(unsigned now) override {
        MpdClient &m = mpd_client();
        m.set_queue_watch(true);
        m.tick(now);

        const int n = (int)m.queue().size();
        const int maxs = std::max(0, n * ROW_H - LIST_VIEW);
        if (n == 0) {
            scroll_ = 0;
            if (m.queue_loaded()) scroll_init_ = false;
        } else if (!scroll_init_) {
            int cur = m.status().song;
            if (cur < 0) cur = 0;
            scroll_ = std::max(0, cur * ROW_H - LIST_VIEW / 2);
            scroll_init_ = true;
        }
        if (scroll_ > maxs) scroll_ = maxs;
        if (scroll_ < 0) scroll_ = 0;
    }

    void draw(Gfx &g) override {
        ui_bg(g);

        MpdClient &m = mpd_client();
        const bool live = m.connected();
        const bool loaded = m.queue_loaded();
        const std::vector<MpdSong> &q = m.queue();
        const int n = (int)q.size();

        // List first; the header is repainted over its top edge (no clipping
        // support in Canvas, and this keeps pixel-smooth scrolling).
        if (live && loaded && n > 0) {
            const int off = scroll_ % ROW_H;
            int y = LIST_TOP - off;
            int first = scroll_ / ROW_H;
            for (int i = first; i < n; i++, y += ROW_H) {
                if (y >= 800) break;
                bool cur = false;
                if (m.status().songid >= 0 && q[i].id == m.status().songid)
                    cur = true;
                if (!cur && m.status().song >= 0 && q[i].pos == m.status().song)
                    cur = true;
                draw_row(g, y, q[i], i, cur);
            }

            const int content = n * ROW_H;
            const int maxs = std::max(0, content - LIST_VIEW);
            if (maxs > 0) {
                int thumb = std::max(24, LIST_VIEW * LIST_VIEW / content);
                int sy = LIST_TOP + (LIST_VIEW - thumb) * scroll_ / maxs;
                g.rect(476, LIST_TOP, 2, LIST_VIEW, theme.track);
                g.rect(476, sy, 2, thumb, theme.dim);
            }
        }

        // Header over the list (status bar is drawn by the core afterwards).
        g.rect(0, 32, 480, LIST_TOP - 32, theme.bg);
        draw_pivot(g, "Очередь", "СЕЙЧАС", "БИБЛИОТЕКА", "ИНФО", tabs_);

        std::string count;
        if (!live) count = "MPD НЕДОСТУПЕН";
        else if (!loaded) count = "ЗАГРУЗКА…";
        else count = queue_summary(q);
        g.text(24, 110, count, 11, theme.dim);

        bool can_clear = live && loaded && !q.empty();
        g.text(456, 110, "ОЧИСТИТЬ", 11,
               can_clear ? theme.accent : theme.dim, TF_RIGHT);
        g.hline(0, LIST_TOP - 1, 480, theme.line2);

        if (!live) {
            g.text(240, 320, "MPD недоступен", 18, theme.dim, TF_CENTER);
            g.text(240, 348, "проверьте mpd.start", 12, theme.dim, TF_CENTER);
        } else if (loaded && q.empty()) {
            g.text(240, 320,
                   g.fit("Очередь пуста — включите альбом в библиотеке", 15,
                         440),
                   15, theme.dim, TF_CENTER);
        }
    }

    void draw_row(Gfx &g, int y, const MpdSong &s, int index, bool cur) {
        if (cur) g.rect(0, y, 480, ROW_H, theme.sel);
        if (cur) g.rect(10, y + 14, 3, 28, theme.accent);

        char num[16];
        std::snprintf(num, sizeof(num), "%d",
                      s.pos >= 0 ? s.pos + 1 : index + 1);
        g.text(48, y + 20, num, 12, cur ? theme.accent : theme.num, TF_RIGHT);

        g.text(60, y + 7, g.fit(song_title(s), 16, 380), 16,
               cur ? theme.accent : theme.fg);

        std::string artist = s.artist.empty() ? "—" : s.artist;
        g.text(60, y + 29, g.fit(artist, 12, 300), 12, theme.dim);
        g.text(456, y + 29,
               s.time > 0 ? fmt_time(s.time) : std::string("—"), 12,
               theme.dim, TF_RIGHT);
        g.hline(0, y + ROW_H - 1, 480, theme.line2);
    }

    bool tap(int x, int y) override {
        MpdClient &m = mpd_client();
        int tab = pivot_hit(tabs_, x, y);
        if (tab == 1) { app.replace("player"); return true; }
        if (tab == 2) { app.push("library"); return true; }
        if (tab == 3) { app.replace("info"); return true; }

        if (y >= SUB_TOP && y < LIST_TOP && x >= 360) {
            if (!m.connected()) {
                app.toast("MPD недоступен");
            } else if (m.queue().empty()) {
                app.toast("Очередь уже пуста");
            } else {
                m.clear();
                scroll_ = 0;
                scroll_init_ = false;
                app.toast("Очередь очищена");
            }
            return true;
        }

        if (y >= LIST_TOP) {
            const int idx = (scroll_ + (y - LIST_TOP)) / ROW_H;
            if (m.connected() && idx >= 0 && idx < (int)m.queue().size()) {
                const MpdSong &s = m.queue()[idx];
                m.play(s.pos >= 0 ? s.pos : idx);
                return true;
            }
        }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x;
        if (y >= PIVOT_Y && y < PIVOT_Y + PIVOT_H) {
            swipe_ += dx;
            if (swipe_ <= -55) { swipe_ = 0; app.replace("info"); return true; }
            if (swipe_ >= 55) { swipe_ = 0; app.replace("player"); return true; }
            return true;
        }
        if (y < LIST_TOP) return false;
        const int n = (int)mpd_client().queue().size();
        const int maxs = std::max(0, n * ROW_H - LIST_VIEW);
        if (maxs <= 0) return false;
        int before = scroll_;
        scroll_ -= dy;
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > maxs) scroll_ = maxs;
        return scroll_ != before;
    }

    bool key(int ksym, bool down) override {
        if (!down) return false;
        MpdClient &m = mpd_client();

        int step = 0;
        if (ksym == XF86XK_AudioRaiseVolume) step = 3;
        else if (ksym == XF86XK_AudioLowerVolume) step = -3;

        if (step != 0) {
            if (!m.connected()) {
                app.toast("MPD недоступен");
                return true;
            }
            int v = m.status().volume;
            if (v < 0) v = 50;
            v += step;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            m.setvol(v);
            char buf[24];
            std::snprintf(buf, sizeof(buf), "VOL %d%%", v);
            app.toast(buf, 1500);
            return true;
        }

        if (ksym == XF86XK_AudioPlay ||
            ksym == XF86XK_AudioPause) {
            if (m.connected() && m.has_song()) m.toggle();
            return true;
        }
        if (ksym == XF86XK_AudioNext) {
            if (m.connected() && m.has_song()) m.next();
            return true;
        }
        if (ksym == XF86XK_AudioPrev) {
            if (m.connected() && m.has_song()) m.previous();
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_queue() { return new Queue(); }
