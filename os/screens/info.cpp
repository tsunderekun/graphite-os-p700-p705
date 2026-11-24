#include "graphite.h"
#include "mpd.h"

#include <X11/XF86keysym.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

std::string fmt_audio(const std::string &audio) {
    int rate = 0, bits = 0, ch = 0;
    if (std::sscanf(audio.c_str(), "%d:%d:%d", &rate, &bits, &ch) < 1 ||
        rate <= 0)
        return "—";
    char ratebuf[40];
    double khz = rate / 1000.0;
    if (std::fabs(khz - std::floor(khz + 0.5)) < 0.05) {
        std::snprintf(ratebuf, sizeof(ratebuf), "%d кГц",
                      (int)std::floor(khz + 0.5));
    } else {
        std::snprintf(ratebuf, sizeof(ratebuf), "%.1f кГц", khz);
        for (char *p = ratebuf; *p; p++)
            if (*p == '.') *p = ',';
    }
    std::string out = ratebuf;
    if (bits > 0) {
        char b[24];
        std::snprintf(b, sizeof(b), "%d бит", bits);
        out += " · ";
        out += b;
    }
    if (ch > 0) {
        out += " · ";
        if (ch == 1) out += "mono";
        else if (ch == 2) out += "stereo";
        else {
            char c[24];
            std::snprintf(c, sizeof(c), "%d кан.", ch);
            out += c;
        }
    }
    return out;
}

std::string year_of(const std::string &date) {
    if (date.size() < 4) return date.empty() ? "—" : date;
    for (int i = 0; i < 4; i++)
        if (!std::isdigit((unsigned char)date[i]))
            return date.empty() ? "—" : date;
    return date.substr(0, 4);
}

std::string song_title(const MpdSong &s) {
    if (!s.title.empty()) return s.title;
    if (s.file.empty()) return "—";
    size_t slash = s.file.find_last_of('/');
    std::string n = slash == std::string::npos ? s.file : s.file.substr(slash + 1);
    size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n = n.substr(0, dot);
    return n.empty() ? "—" : n;
}

std::vector<std::string> wrap_path(Gfx &g, const std::string &s, int px,
                                   int maxw, int maxlines) {
    std::vector<std::string> out;
    std::string rest = s;
    for (int line = 0; line < maxlines && !rest.empty(); line++) {
        if (g.textw(rest, px) <= maxw) {
            out.push_back(rest);
            break;
        }
        if (line == maxlines - 1) {
            out.push_back(g.fit(rest, px, maxw));
            break;
        }
        size_t pos = 0, last = 0;
        while (pos < rest.size()) {
            unsigned char c = (unsigned char)rest[pos];
            size_t step = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2
                                          : ((c & 0xF0) == 0xE0 ? 3 : 4));
            if (pos + step > rest.size()) step = rest.size() - pos;
            if (g.textw(rest.substr(0, pos + step), px) > maxw) break;
            last = pos + step;
            pos += step;
        }
        if (last == 0) {
            out.push_back(g.fit(rest, px, maxw));
            break;
        }
        out.push_back(rest.substr(0, last));
        rest = rest.substr(last);
        while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }
    return out;
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

struct Info : ScreenView {
    int swipe_ = 0;
    TabZone tabs_[3];
    const char *id() const override { return "info"; }
    bool animated() const override { return true; }

    void tick(unsigned now) override {
        MpdClient &m = mpd_client();
        m.set_queue_watch(false);
        if (m.connected() && m.has_song())
            m.want_cover(cover_key(m.song()), m.song().file, false);
        m.tick(now);
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        draw_pivot(g, "Инфо", "СЕЙЧАС", "ОЧЕРЕДЬ", "БИБЛИОТЕКА", tabs_);

        MpdClient &m = mpd_client();
        const bool live = m.connected();
        const bool has = live && m.has_song();
        const MpdSong &s = m.song();

        // cover thumb
        g.rect(24, 104, 100, 100, theme.art);
        const std::string key = has ? cover_key(s) : std::string();
        const CoverImage *art = key.empty() ? nullptr : m.cover(key, 96);
        if (art && art->ok())
            art->blit(g.cv.px.data(), g.cv.w, 24, 104, 100);
        else
            icon(g, "music", 48, 128, 52, theme.note);

        std::string title, artist, album;
        if (!live) {
            title = "MPD недоступен";
            artist = "проверьте mpd.start";
        } else if (!has) {
            title = "Нет текущего трека";
            artist = "очередь пуста";
        } else {
            title = song_title(s);
            artist = s.artist.empty() ? "—" : s.artist;
            album = s.album.empty() ? "—" : s.album;
        }
        g.text(136, 112, g.fit(title, 20, 324), 20, theme.fg, TF_LIGHT);
        g.text(136, 142, g.fit(artist, 13, 324), 13, theme.dim);
        if (!album.empty())
            g.text(136, 164, g.fit(album, 13, 324), 13, theme.dim);

        g.hline(24, 224, 432, theme.line2);

        std::string genre = "—", year = "—", dur = "—", fmt = "—", bitrate = "—";
        if (has) {
            if (!s.genre.empty()) genre = s.genre;
            year = year_of(s.date);
            int seconds = s.time;
            if (seconds <= 0 && m.status().duration > 0)
                seconds = (int)(m.status().duration + 0.5);
            if (seconds > 0) dur = fmt_time(seconds);
            std::string audio = s.format.empty() ? m.status().audio : s.format;
            if (!audio.empty()) fmt = fmt_audio(audio);
            int br = s.bitrate > 0 ? s.bitrate : m.status().bitrate;
            if (br > 0) {
                char b[32];
                std::snprintf(b, sizeof(b), "%d кбит/с", br);
                bitrate = b;
            }
        }

        struct Row {
            const char *label;
            const std::string *value;
        };
        const Row rows[] = {
            {"ЖАНР", &genre},
            {"ГОД", &year},
            {"ДЛИТЕЛЬНОСТЬ", &dur},
            {"ФОРМАТ", &fmt},
            {"БИТРЕЙТ", &bitrate},
        };
        int y = 236;
        for (const Row &r : rows) {
            g.text(24, y + 14, r.label, 11, theme.dim);
            g.text(144, y + 10, g.fit(*r.value, 16, 312), 16, theme.fg);
            y += 44;
        }

        g.hline(24, 456, 432, theme.line2);
        g.text(24, 468, "ФАЙЛ", 11, theme.dim);

        std::string path = has ? s.file : "—";
        if (path.empty()) path = "—";
        std::vector<std::string> lines = wrap_path(g, path, 12, 432, 3);
        if (lines.empty()) lines.push_back("—");
        for (size_t i = 0; i < lines.size(); i++)
            g.text(24, 500 + (int)i * 20, lines[i], 12, theme.dim);

        // "Открыть файл" — opens the track's folder in Файлы.
        g.rect(24, 724, 432, 48, theme.surf);
        g.rect(24, 724, 3, 48, has ? theme.accent : theme.track);
        g.text(40, 740, "Открыть файл", 14,
               has ? theme.accent : theme.dim);
        icon(g, "chevron", 428, 742, 16, has ? theme.accent : theme.dim);
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x; (void)dy;
        if (y < PIVOT_Y || y >= PIVOT_Y + PIVOT_H) return false;
        swipe_ += dx;
        if (swipe_ >= 55) { swipe_ = 0; app.replace("queue"); return true; }
        return true;
    }

    bool tap(int x, int y) override {
        int tab = pivot_hit(tabs_, x, y);
        if (tab == 1) { app.replace("player"); return true; }
        if (tab == 2) { app.replace("queue"); return true; }
        if (tab == 3) { app.push("library"); return true; }

        if (hit(x, y, 24, 724, 432, 48)) {
            MpdClient &m = mpd_client();
            if (!m.connected()) {
                app.toast("MPD недоступен");
            } else if (!m.has_song() || m.song().file.empty()) {
                app.toast("Нет текущего трека");
            } else {
                // MPD URIs are relative to music_directory (/media/sd/Music).
                std::string p = m.song().file;
                if (!p.empty() && p[0] != '/') p = "/media/sd/Music/" + p;
                size_t slash = p.find_last_of('/');
                std::string dir = slash == std::string::npos
                                      ? std::string("/")
                                      : p.substr(0, slash);
                if (dir.empty()) dir = "/";
                struct stat st;
                if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                    app.toast("Папка трека не найдена");
                } else {
                    app.open_path = dir;
                    app.open("files");
                }
            }
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

ScreenView *make_info() { return new Info(); }
