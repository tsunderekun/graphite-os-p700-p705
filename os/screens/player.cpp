#include "graphite.h"
#include "mpd.h"

#include <X11/XF86keysym.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

const int PIVOT_Y = 32;
const int PIVOT_H = 64;
const int PIVOT_RIGHT = 456;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

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

std::string song_artist(const MpdSong &s) {
    return s.artist.empty() ? "—" : s.artist;
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

struct Player : ScreenView {
    unsigned now_ = 0;
    unsigned vol_until_ = 0;
    std::string vol_text_;
    int swipe_ = 0;
    TabZone tabs_[3];

    const char *id() const override { return "player"; }
    bool animated() const override { return true; }

    void tick(unsigned now) override {
        now_ = now;
        MpdClient &m = mpd_client();
        m.set_queue_watch(false);
        if (m.connected() && m.has_song())
            m.want_cover(cover_key(m.song()), m.song().file, true);
        m.tick(now);
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        draw_pivot(g, "Сейчас", "ОЧЕРЕДЬ", "БИБЛИОТЕКА", "ИНФО", tabs_);

        MpdClient &m = mpd_client();
        const bool live = m.connected();
        const bool has = live && m.has_song();
        const MpdSong &s = m.song();

        const int ax = 48, ay = 104, as = 384;
        g.rect(ax, ay, as, as, theme.art);

        std::string key = has ? cover_key(s) : std::string();
        const CoverImage *art = key.empty() ? nullptr : m.cover(key, as);
        if (art && art->ok()) {
            art->blit(g.cv.px.data(), g.cv.w, ax, ay, as);
        } else if (!live) {
            icon(g, "music", 180, 170, 120, theme.note);
            g.text(240, 328, "MPD недоступен", 18, theme.dim, TF_CENTER);
            g.text(240, 356, "проверьте mpd.start", 12, theme.dim, TF_CENTER);
        } else if (!has) {
            icon(g, "music", 190, 180, 100, theme.note);
            g.text(240, 328, "Нет текущего трека", 18, theme.dim, TF_CENTER);
            g.text(240, 356, "выберите альбом в библиотеке", 12, theme.dim,
                   TF_CENTER);
        } else {
            icon(g, "music", 170, 184, 140, theme.note);
            g.text(240, 376, m.cover_missing(key) ? "НЕТ ОБЛОЖКИ" : "ЗАГРУЗКА…",
                   11, theme.dim, TF_CENTER);
        }

        if (has) {
            std::string title = song_title(s);
            g.text(240, 496, g.fit(title, 24, 372), 24, theme.fg,
                   TF_CENTER | TF_LIGHT);

            std::string sub = song_artist(s);
            if (!s.album.empty()) sub += " · " + s.album;
            g.text(240, 530, g.fit(sub, 14, 376), 14, theme.dim, TF_CENTER);
        }

        // shuffle / repeat
        uint16_t edge_off = has ? theme.dim : theme.track;
        icon(g, "shuffle", 56, 566, 24,
             (has && m.status().random) ? theme.accent : edge_off);
        bool rep_on = has && (m.status().repeat || m.status().single);
        icon(g, "repeat", 400, 566, 24, rep_on ? theme.accent : edge_off);
        if (has && m.status().single) {
            char one[2] = {'1', 0};
            g.text(420, 580, one, 11, theme.accent);
        }

        if (has) {
            if (m.status().volume >= 0) {
                char vb[24];
                std::snprintf(vb, sizeof(vb), "VOL %d%%", m.status().volume);
                g.text(240, 572, vb, 11, theme.dim, TF_CENTER);
            }

            double dur = m.status().duration;
            if (dur <= 0) dur = s.time;
            double el = m.elapsed(now_);
            int pw = 384;
            int py = 624;
            g.rect(ax, py, pw, 4, theme.track);
            if (dur > 0) {
                int fill = (int)(pw * el / dur);
                fill = std::max(0, std::min(pw, fill));
                if (fill > 0) {
                    g.rect(ax, py, fill, 4, theme.accent);
                    g.rect(ax + fill - 8, py - 6, 16, 16, theme.accent);
                }
            }
            g.text(ax, 640, fmt_time((int)el), 12, theme.dim);
            g.text(ax + pw, 640,
                   dur > 0 ? fmt_time((int)(dur + 0.5)) : std::string("—"),
                   12, theme.dim, TF_RIGHT);
        }

        // transport
        const int ty = 676, th = 92;
        g.rect(48, ty, 104, th, theme.surf);
        g.rect(328, ty, 104, th, theme.surf);
        g.rect(172, ty, 136, th, has ? theme.accent : theme.surf2);
        uint16_t side_ic = has ? theme.fg : theme.dim;
        uint16_t main_ic = has ? theme.onacc : theme.dim;
        icon(g, "prev", 84, 706, 32, side_ic);
        icon(g, "next", 364, 706, 32, side_ic);
        if (has && m.status().state == "play")
            icon(g, "pause", 221, 703, 38, main_ic);
        else
            icon(g, "play", 221, 703, 38, main_ic);

        // volume feedback (1.5 s)
        if (!vol_text_.empty() && (int)(vol_until_ - now_) > 0) {
            g.rect(176, 596, 128, 28, theme.surf);
            g.rect(176, 596, 3, 28, theme.accent);
            g.text(192, 602, vol_text_, 13, theme.accent);
        }
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x; (void)dy;
        if (y < PIVOT_Y || y >= PIVOT_Y + PIVOT_H) return false;
        swipe_ += dx;
        if (swipe_ <= -55) { swipe_ = 0; app.replace("queue"); return true; }
        return true;
    }

    bool tap(int x, int y) override {
        MpdClient &m = mpd_client();
        int tab = pivot_hit(tabs_, x, y);
        if (tab == 1) { app.replace("queue"); return true; }
        if (tab == 2) { app.push("library"); return true; }
        if (tab == 3) { app.replace("info"); return true; }

        const bool has = m.connected() && m.has_song();
        const bool live = m.connected();

        if (hit(x, y, 48, 556, 44, 44)) {              // shuffle
            if (!has) {
                app.toast(live ? "Нет текущего трека" : "MPD недоступен");
                return true;
            }
            m.random(!m.status().random);
            return true;
        }
        if (hit(x, y, 388, 556, 48, 44)) {             // repeat cycle
            if (!has) {
                app.toast(live ? "Нет текущего трека" : "MPD недоступен");
                return true;
            }
            if (!m.status().repeat && !m.status().single) {
                m.repeat(true);
                m.single(false);
            } else if (!m.status().single) {
                m.single(true);
            } else {
                m.repeat(false);
                m.single(false);
            }
            return true;
        }
        if (y >= 608 && y < 660 && x >= 20 && x < 460) {  // seek
            if (!has) {
                app.toast(live ? "Нет текущего трека" : "MPD недоступен");
                return true;
            }
            double dur = m.status().duration;
            if (dur <= 0) dur = m.song().time;
            if (dur > 0) {
                double rel = (x - 48) / 384.0;
                if (rel < 0) rel = 0;
                if (rel > 1) rel = 1;
                m.seek(rel * dur);
            }
            return true;
        }
        if (hit(x, y, 48, 676, 104, 92)) {             // previous
            if (has) m.previous();
            else if (!live) app.toast("MPD недоступен");
            else app.toast("Нет текущего трека");
            return true;
        }
        if (hit(x, y, 172, 676, 136, 92)) {            // play / pause
            if (has) m.toggle();
            else if (!live) app.toast("MPD недоступен");
            else app.toast("Нет текущего трека");
            return true;
        }
        if (hit(x, y, 328, 676, 104, 92)) {            // next
            if (has) m.next();
            else if (!live) app.toast("MPD недоступен");
            else app.toast("Нет текущего трека");
            return true;
        }
        return false;
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
            vol_text_ = buf;
            vol_until_ = now_ + 1500;
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

ScreenView *make_player() { return new Player(); }
