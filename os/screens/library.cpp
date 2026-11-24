// Библиотека — альбомы из индекса MpdClient (один listallinfo -> library.tsv).
//
// Тап по строке: clear + все треки альбома + play, затем возврат в «Сейчас».
// «ИГРАТЬ ВСЁ» в шапке: clear + вся библиотека + play. «ОБНОВИТЬ»: перечитать
// listallinfo (обычно индекс обновляется сам по db_update / idle database).
#include "graphite.h"
#include "mpd.h"

#include <X11/XF86keysym.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const int HEAD_H = 96;          // ui_header height (32 + 64)
const int TOOL_Y = 96;
const int TOOL_H = 44;
const int LIST_TOP = 140;
const int ROW_H = 80;
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

const char *plural(int n, const char *one, const char *few, const char *many) {
    int m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
    return many;
}

struct Library : ScreenView {
    int scroll_ = 0;
    int swipe_ = 0;
    unsigned now_ = 0;

    const char *id() const override { return "library"; }
    bool animated() const override { return true; }

    void on_show() override {
        scroll_ = 0;
        mpd_client().library_check();   // one stats call per visit, not per tick
    }

    void tick(unsigned now) override {
        now_ = now;
        MpdClient &m = mpd_client();
        m.set_library_watch(true);

        // Thumbnails for the visible rows only, top of the list first.
        const std::vector<LibraryAlbum> &al = m.albums();
        const int n = (int)al.size();
        int first = scroll_ / ROW_H;
        int last = std::min(n, first + LIST_VIEW / ROW_H + 2);
        for (int i = first; i < last; i++)
            m.want_cover(cover_key(al[i].artist, al[i].album, al[i].uri),
                         al[i].uri, false);

        m.tick(now);

        const int maxs = std::max(0, n * ROW_H - LIST_VIEW);
        if (scroll_ > maxs) scroll_ = maxs;
        if (scroll_ < 0) scroll_ = 0;
    }

    std::string subtitle(const MpdClient &m) const {
        const int n = (int)m.albums().size();
        if (!m.connected()) return "MPD недоступен";
        if (m.library_state() == LIB_LOADING && n == 0)
            return "Обновление библиотеки…";
        if (n == 0) return "Нет музыки";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d %s · %d %s", n,
                      plural(n, "альбом", "альбома", "альбомов"),
                      m.library_songs(),
                      plural(m.library_songs(), "трек", "трека", "треков"));
        return buf;
    }

    void draw(Gfx &g) override {
        ui_bg(g);

        MpdClient &m = mpd_client();
        const std::vector<LibraryAlbum> &al = m.albums();
        const int n = (int)al.size();
        const bool live = m.connected();
        const int state = m.library_state();

        // List first; the header is repainted over its top edge (no clipping).
        if (n > 0) {
            const int off = scroll_ % ROW_H;
            int y = LIST_TOP - off;
            int first = scroll_ / ROW_H;
            for (int i = first; i < n; i++, y += ROW_H) {
                if (y >= 800) break;
                if (y + ROW_H <= LIST_TOP) continue;
                draw_row(g, y, al[i], i);
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

        // Header + toolbar over the list.
        g.rect(0, 32, 480, LIST_TOP - 32, theme.bg);
        g.hline(0, HEAD_H - 1, 480, theme.line2);

        std::string sub = subtitle(m);
        ui_header(g, "Библиотека", sub.c_str(), true);

        const bool can_play = live && n > 0;
        g.text(24, TOOL_Y + 14, "ИГРАТЬ ВСЁ", 11,
               can_play ? theme.accent : theme.dim);
        g.text(456, TOOL_Y + 14, "ОБНОВИТЬ", 11,
               live ? theme.dim : theme.track, TF_RIGHT);
        g.hline(0, LIST_TOP - 1, 480, theme.line2);

        if (n == 0) {
            if (!live) {
                g.text(240, 320, "MPD недоступен", 18, theme.dim, TF_CENTER);
                g.text(240, 350, "проверьте mpd.start", 12, theme.dim,
                       TF_CENTER);
                g.text(240, 376, "нажмите, чтобы повторить", 11, theme.dim,
                       TF_CENTER);
            } else if (state == LIB_LOADING) {
                g.text(240, 320, "Обновление библиотеки…", 15, theme.dim,
                       TF_CENTER);
                g.text(240, 350, "один проход listallinfo", 11, theme.dim,
                       TF_CENTER);
            } else {
                g.text(240, 320, "В библиотеке нет музыки", 15, theme.dim,
                       TF_CENTER);
                g.text(240, 350, "/media/sd/Music", 12, theme.dim, TF_CENTER);
                g.text(240, 400, "ОБНОВИТЬ БАЗУ", 11, theme.accent, TF_CENTER);
                if (!m.library_error().empty()) {
                    std::string e = m.library_error();
                    g.text(240, 440, g.fit(e, 11, 440), 11, theme.dim,
                           TF_CENTER);
                }
            }
        }
    }

    void draw_row(Gfx &g, int y, const LibraryAlbum &a, int index) {
        if (y < LIST_TOP) {
            // Partially scrolled row: only the visible bottom part is drawn.
        }
        g.rect(20, y + 8, 64, 64, theme.art);
        std::string key = cover_key(a.artist, a.album, a.uri);
        const CoverImage *art = key.empty() ? nullptr : mpd_client().cover(key, 96);
        if (art && art->ok())
            art->blit(g.cv.px.data(), g.cv.w, 20, y + 8, 64);
        else
            icon(g, "music", 40, y + 28, 24, theme.note);

        g.text(96, y + 17, g.fit(a.album, 16, 300), 16, theme.fg);

        std::string sub = a.artist.empty() ? "Неизвестный исполнитель" : a.artist;
        if (!a.year.empty()) sub += " · " + a.year;
        if (!a.tracks.empty()) {
            char nbuf[32];
            std::snprintf(nbuf, sizeof(nbuf), " · %d %s", (int)a.tracks.size(),
                          plural((int)a.tracks.size(), "трек", "трека", "треков"));
            sub += nbuf;
        }
        g.text(96, y + 43, g.fit(sub, 12, 300), 12, theme.dim);

        if (a.duration > 0)
            g.text(456, y + 43, fmt_time(a.duration), 12, theme.dim, TF_RIGHT);

        g.hline(0, y + ROW_H - 1, 480, theme.line2);
        (void)index;
    }

    bool tap(int x, int y) override {
        MpdClient &m = mpd_client();

        if (x < 64 && y < HEAD_H) {          // back
            app.back();
            return true;
        }

        if (y >= TOOL_Y && y < TOOL_Y + TOOL_H) {
            if (x >= 340) {                  // ОБНОВИТЬ
                if (!m.connected()) {
                    m.retry_now();
                    app.toast("Переподключаюсь…");
                } else {
                    m.library_rebuild();
                    app.toast("Обновление библиотеки…");
                }
                return true;
            }
            if (x < 220) {                   // ИГРАТЬ ВСЁ
                if (!m.connected()) {
                    app.toast("MPD недоступен");
                } else if (m.albums().empty()) {
                    if (m.library_state() == LIB_LOADING)
                        app.toast("Библиотека ещё не готова");
                    else
                        app.toast("В библиотеке нет музыки");
                } else {
                    m.play_all();
                    app.toast("Играю всю библиотеку");
                    app.replace("player");
                }
                return true;
            }
            return true;
        }

        if (y >= LIST_TOP) {
            const int idx = (scroll_ + (y - LIST_TOP)) / ROW_H;
            if (!m.connected()) {            // tap = retry now
                m.retry_now();
                app.toast("Переподключаюсь…");
                return true;
            }
            if (idx >= 0 && idx < (int)m.albums().size()) {
                const std::string name = m.albums()[idx].album;
                m.play_album(idx);
                std::string msg = "Играю " + name;
                app.toast(msg.c_str());
                app.replace("player");
                return true;
            }
            if (m.albums().empty()) {        // empty state action
                m.library_rebuild();
                app.toast("Обновление библиотеки…");
            }
            return true;
        }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x;
        (void)dx;
        if (y < LIST_TOP) return false;
        const int n = (int)mpd_client().albums().size();
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

        if (ksym == XF86XK_AudioPlay || ksym == XF86XK_AudioPause) {
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

ScreenView *make_library() { return new Library(); }
