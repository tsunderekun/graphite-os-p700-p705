#include "graphite.h"
#include "mpd.h"
#include "power_policy.h"
#include "lock_auth.h"

#include <cstdio>
#include <ctime>
#include <algorithm>
#include <string>

namespace {

std::string now_hm() {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[8];
    std::snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return std::string(b);
}

std::string now_date() {
    static const char *wd[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    static const char *mo[] = {"янв", "фев", "мар", "апр", "мая", "июн",
                               "июл", "авг", "сен", "окт", "ноя", "дек"};
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[32];
    std::snprintf(b, sizeof(b), "%s, %d %s", wd[tmv.tm_wday], tmv.tm_mday,
                  mo[tmv.tm_mon]);
    return std::string(b);
}

std::string base_name(const std::string &path) {
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

std::string song_title(const MpdSong &s) {
    if (!s.title.empty()) return s.title;
    std::string n = base_name(s.file);
    return n.empty() ? "Без названия" : n;
}

std::string song_state(const MpdStatus &st) {
    if (st.state == "play") return "играет";
    if (st.state == "pause") return "пауза";
    return "остановлен";
}

// Centred text with letter tracking (the API draws glyph by glyph).
void text_tracked(Gfx &g, int cx, int y, const std::string &s, int px,
                  uint16_t c, int track) {
    int x = cx - g.textw(s, px, track) / 2;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char ch = (unsigned char)s[i];
        size_t n = 1;
        if ((ch & 0xE0) == 0xC0) n = 2;
        else if ((ch & 0xF0) == 0xE0) n = 3;
        else if ((ch & 0xF8) == 0xF0) n = 4;
        std::string one = s.substr(i, n);
        g.text(x, y, one, px, c);
        x += g.textw(one, px) + track;
        i += n;
    }
}

void chevron_up(Gfx &g, int cx, int top, int half, uint16_t c) {
    for (int i = 0; i <= half; i++) {
        int y = top + i;
        g.rect(cx - i - 1, y, 2, 2, c);
        g.rect(cx + i - 1, y, 2, 2, c);
    }
}

struct Lock : ScreenView {
    int up = 0;
    std::string pin;
    lock_auth::AttemptLimiter limiter;
    MpdClient *mpd = nullptr;

    Lock() {
        mpd = &mpd_client();
        mpd->set_queue_watch(false);
    }

    const char *id() const override { return "lock"; }
    bool animated() const override { return false; }

    void tick(unsigned now) override { mpd->tick(now); }

    void draw(Gfx &g) override {
        ui_bg(g);

        if (lock_auth::enabled()) {
            g.text(240, 118, "Введите PIN", 22, theme.fg, TF_CENTER);
            for (int i=0;i<(int)pin.size();i++) g.rect(240-((int)pin.size()-1)*18/2+i*18-4, 186, 8, 8, theme.accent);
            const char *keys[]={"1","2","3","4","5","6","7","8","9","⌫","0","OK"};
            for(int i=0;i<12;i++){int x=42+(i%3)*132,y=244+(i/3)*82;g.rect(x,y,112,60,theme.surf);g.text(x+56,y+18,keys[i],20,theme.fg,TF_CENTER);}
            if (limiter.blocked(app.now)) g.text(240, 610, "Слишком много попыток · подождите", 14, theme.accent, TF_CENTER);
            g.text(240, 726, "ЛОКИРОВКА PIN · ТОЛЬКО БАРЬЕР ВХОДА", 11, theme.dim, TF_CENTER);
            return;
        }

        g.text(240, 274, now_hm(), 76, theme.fg, TF_CENTER | TF_LIGHT);
        g.text(240, 372, now_date(), 14, theme.dim, TF_CENTER);

        // the current track, only when MPD actually has one
        if (mpd->connected() && mpd->has_song()) {
            const MpdSong &s = mpd->song();
            std::string sub = s.artist.empty() ? song_state(mpd->status())
                                               : s.artist + " · " +
                                                     song_state(mpd->status());
            g.rect(24, 420, 432, 64, theme.surf);
            g.rect(24, 420, 3, 64, theme.accent);
            g.text(40, 430, g.fit(song_title(s), 14, 396), 14, theme.fg,
                   TF_BOLD);
            g.text(40, 454, g.fit(sub, 11, 396), 11, theme.dim);
        }

        chevron_up(g, 240, 674, 12, theme.dim);
        g.rect(180, 700, 120, 4, theme.line);
        text_tracked(g, 240, 726, "ПРОВЕДИТЕ ВВЕРХ", 12, theme.dim, 2);
    }

    bool tap(int x, int y) override {
        if (lock_auth::enabled()) {
            if (limiter.blocked(app.now)) return true;
            if (y >= 244 && y < 550) {
                int col=(x-42)/132,row=(y-244)/82;
                if(x<42||col<0||col>2||row<0||row>3)return true;
                int idx=row*3+col;
                if(idx==9){if(!pin.empty())pin.pop_back();}
                else if(idx==11){if(lock_auth::verify(pin)==lock_auth::Result::Ok){limiter.success();pin.clear();if(power_policy.wake(app.now))app.open("home");else app.toast("Ошибка записи подсветки");}else{pin.clear();limiter.reject(app.now);}}
                else if(pin.size()<8){int digit=idx==10?0:idx+1;pin.push_back(char('0'+digit));}
            }
            return true;
        }
        (void)x; (void)y;
        up = 0;
        if (!power_policy.wake(app.now)) { app.toast("Ошибка записи подсветки"); return true; }
        app.open("home");
        return true;
    }

    bool drag(int x, int y, int dx, int dy) override {
        if (lock_auth::enabled()) return true;
        (void)x; (void)y; (void)dx;
        if (dy > 0) up = 0;
        else up += dy;
        if (up <= -20) {
            up = 0;
            if (!power_policy.wake(app.now)) { app.toast("Ошибка записи подсветки"); return true; }
            app.open("home");
            return true;
        }
        return false;
    }

    bool key(int ksym, bool down) override {
        (void)ksym; (void)down;
        return lock_auth::enabled();
    }
};

} // namespace

ScreenView *make_lock() { return new Lock(); }
