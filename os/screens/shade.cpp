// Shade — quick settings over the home screen. Only real controls: Wi-Fi is
// toggled through ip(8), sound through MPD, brightness through sysfs; the
// notification cards show the MPD track and the actual wlan0 link.
#include "graphite.h"
#include "mpd.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

const int TX0 = 24, TY = 74, TW = 102, TH = 84, TGAP = 8;
const int SLX = 24, SLW = 432;
const int VOL_Y = 168, BRI_Y = 216;       // slider label y (track at +26)
const int CARD_Y = 270, CARD_H = 64, CARD_GAP = 8;
const int PANEL_H = 470;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int tx(int i) { return TX0 + i * (TW + TGAP); }

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    size_t p = 0;
    while (p < s.size() && s[p] == ' ') p++;
    return s.substr(p);
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

std::string fmt_time(int sec) {
    if (sec < 0) sec = 0;
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    char b[24];
    if (h > 0) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, s);
    else std::snprintf(b, sizeof(b), "%d:%02d", m, s);
    return b;
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

// ---------------------------------------------------------------- settings

bool wifi_pref_on() {
    const std::string v = cfg.get("wifi", "1");
    if (v == "on") return true;       // legacy value written by old shade
    if (v == "off") return false;
    return cfg.geti("wifi", 1) != 0;
}

void save_key(const char *k, const char *v) {
    settings_save_key(k, v);
    cfg.set(k, v);                    // keep the in-process settings live
}

// ---------------------------------------------------------------- brightness

int read_brightness() {
    FILE *f = std::fopen("/sys/class/leds/lcd-backlight/brightness", "rb");
    if (!f) return -1;
    int v = -1;
    if (std::fscanf(f, "%d", &v) != 1) v = -1;
    std::fclose(f);
    if (v < 0) return -1;
    return v > 255 ? 255 : v;
}

void write_brightness(int v) {
    int fd = ::open("/sys/class/leds/lcd-backlight/brightness", O_WRONLY | O_TRUNC);
    if (fd < 0) return;
    std::string s = std::to_string(v) + "\n";
    ssize_t n = ::write(fd, s.data(), s.size());
    (void)n;
    ::close(fd);
}

// ---------------------------------------------------------------- network

std::string read_ssid() {
    std::string s = cfg.get("ssid");
    if (s.empty()) s = cfg.get("wifi_ssid");
    if (!s.empty()) return s;

    FILE *p = ::popen("iwgetid -r wlan0 2>/dev/null", "r");
    if (p) {
        char b[128] = {0};
        if (std::fgets(b, sizeof(b), p)) s = trim(b);
        ::pclose(p);
    }
    if (!s.empty()) return s;

    const char *paths[] = {
        "/etc/wpa_supplicant/wpa_supplicant.conf",
        "/etc/wpa_supplicant.conf",
    };
    for (const char *path : paths) {
        FILE *f = std::fopen(path, "rb");
        if (!f) continue;
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            char *q = std::strstr(line, "ssid=\"");
            if (!q) continue;
            q += 6;
            char *e = std::strchr(q, '"');
            if (e) *e = 0;
            std::fclose(f);
            return q;
        }
        std::fclose(f);
    }
    return std::string();
}

std::string read_ip() {
    std::string ip;
    struct ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return ip;
    for (struct ifaddrs *a = ifs; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (std::strcmp(a->ifa_name, "wlan0")) continue;
        char b[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in *sin = (struct sockaddr_in *)a->ifa_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, b, sizeof(b))) ip = b;
    }
    freeifaddrs(ifs);
    return ip;
}

struct NetInfo {
    bool iface = false;
    std::string ssid, ip;
};

NetInfo read_net() {
    NetInfo n;
    n.iface = access("/sys/class/net/wlan0", F_OK) == 0;
    n.ssid = read_ssid();
    n.ip = read_ip();
    return n;
}

// ---------------------------------------------------------------- screen

struct Shade : ScreenView {
    bool wifi = true;
    int vol = -1;                 // 0..100, -1 = unknown
    int prev_vol = 60;
    int bright = -1;              // 0..255, -1 = unknown
    unsigned vol_set_at = 0;
    unsigned bright_at = 0;
    unsigned bright_written = 0;
    bool bright_pending = false;
    unsigned net_at = 0;
    int drag_dy = 0;
    NetInfo net;
    MpdClient *mpd = nullptr;

    Shade() {
        wifi = wifi_pref_on();
        mpd = &mpd_client();
        mpd->set_queue_watch(false);
        prev_vol = 60;
    }

    const char *id() const override { return "shade"; }
    bool overlay() const override { return true; }
    bool animated() const override { return true; }

    void on_show() override {
        wifi = wifi_pref_on();
        net = read_net();
        net_at = app.now ? app.now : 1;
        int v = mpd->status().volume;
        if (v >= 0) vol = v;
        bright = read_brightness();
        if (bright < 0) {
            int stored = cfg.geti("brightness", 150);
            bright = stored < 0 ? 0 : (stored > 255 ? 255 : stored);
        }
        bright_pending = false;
        drag_dy = 0;
    }

    void tick(unsigned now) override {
        mpd->tick(now);

        if (!mpd->connected()) vol = -1;
        if (mpd->connected() &&
            (vol_set_at == 0 || now - vol_set_at > 1500)) {
            int v = mpd->status().volume;
            if (v >= 0) vol = v;
        }

        if (net_at == 0 || now - net_at >= 10000) {
            net = read_net();
            net_at = now ? now : 1;
        }

        if (bright_pending && now - bright_at >= 400) {
            write_brightness(bright);
            save_key("brightness", std::to_string(bright).c_str());
            bright_pending = false;
        }
    }

    int bright_pct() const {
        if (bright < 0) return 0;
        return (bright * 100 + 127) / 255;
    }

    bool has_track() const { return mpd->connected() && mpd->has_song(); }
    bool has_wifi_card() const {
        return wifi && (net.iface || !net.ssid.empty() || !net.ip.empty());
    }

    // ------------------------------------------------------------ actions

    void toggle_wifi() {
        bool want = !wifi;
        if (access("/sys/class/net/wlan0", F_OK) != 0) {
            app.toast("Wi-Fi: адаптер wlan0 не найден");
            return;
        }
        int rc = std::system(want ? "ip link set wlan0 up 2>/dev/null"
                                  : "ip link set wlan0 down 2>/dev/null");
        if (rc != 0) {
            app.toast(want ? "Wi-Fi: не удалось включить"
                           : "Wi-Fi: не удалось выключить");
            return;                       // keep the old state
        }
        wifi = want;
        save_key("wifi", want ? "1" : "0");
        net_at = 0;                       // refresh the card on the next tick
    }

    void toggle_sound() {
        if (!mpd->connected()) {
            app.toast("MPD недоступен");
            return;
        }
        if (vol != 0) {
            if (vol > 0) prev_vol = vol;
            vol = 0;
        } else {
            vol = prev_vol > 0 ? prev_vol : 60;
        }
        vol_set_at = app.now;
        mpd->setvol(vol);
    }

    void set_volume(int x) {
        if (!mpd->connected()) {
            app.toast("MPD недоступен");
            return;
        }
        int pct = (x - SLX) * 100 / SLW;
        pct = std::max(0, std::min(100, pct));
        if (pct == vol) return;
        vol = pct;
        vol_set_at = app.now;
        if (pct > 0) prev_vol = pct;
        mpd->setvol(pct);
    }

    void set_brightness(int x) {
        int pct = (x - SLX) * 100 / SLW;
        pct = std::max(0, std::min(100, pct));
        int v = (pct * 255 + 50) / 100;
        v = std::max(0, std::min(255, v));
        if (v == bright) return;
        bright = v;
        bright_at = app.now;
        bright_pending = true;
        if (app.now - bright_written >= 80) {
            write_brightness(bright);
            bright_written = app.now;
        }
    }

    // ------------------------------------------------------------ drawing

    void draw_tile(Gfx &g, int i, const char *icn, const char *label, bool on) {
        int x = tx(i);
        g.rect(x, TY, TW, TH, on ? theme.accent : theme.surf);
        icon(g, icn, x + TW / 2 - 13, TY + 16, 26,
             on ? theme.onacc : theme.fg);
        g.text(x + TW / 2, TY + 58, label, 12,
               on ? theme.onacc : theme.dim, TF_CENTER);
    }

    void draw_card(Gfx &g, int y, const std::string &title,
                   const std::string &sub, const std::string &right) {
        g.rect(SLX, y, SLW, CARD_H, theme.surf);
        g.rect(SLX, y, 3, CARD_H, theme.accent);
        g.text(SLX + 16, y + 8, g.fit(title, 16, 300), 16, theme.fg, TF_BOLD);
        if (!right.empty())
            g.text(SLX + SLW, y + 11, g.fit(right, 11, 120), 11, theme.dim, TF_RIGHT);
        if (!sub.empty())
            g.text(SLX + 16, y + 34, g.fit(sub, 12, 380), 12, theme.dim);
    }

    // Same geometry as ui_slider, but the knob stays inside the track at 0 %
    // (the kit one starts 8 px to the left at the minimum).
    void draw_slider(Gfx &g, int y, int pct, const char *label,
                     const char *value) {
        pct = std::max(0, std::min(100, pct));
        g.text(SLX, y, label, 12, theme.dim);
        g.text(SLX + SLW, y, value, 12, theme.dim, TF_RIGHT);
        int ty = y + 30;
        g.rect(SLX, ty, SLW, 4, theme.track);
        int fw = SLW * pct / 100;
        if (fw > 0) g.rect(SLX, ty, fw, 4, theme.accent);
        int kx = SLX + fw - 8;
        kx = std::max(SLX, std::min(SLX + SLW - 16, kx));
        g.rect(kx, ty - 6, 16, 16, theme.accent);
    }

    void draw(Gfx &g) override {
        // the home screen below is drawn by App because overlay() is true
        for (int y = 0; y < 800; y++)
            for (int x = 0; x < 480; x++)
                g.cv.blend(x, y, 0, 150);

        g.rect(0, 0, 480, PANEL_H, theme.bg);
        for (int y = 12; y < PANEL_H; y += 24)
            for (int x = 0; x < 480; x++)
                g.cv.blend(x, y, theme.surf2, 20);

        // header: date and the settings icon
        g.text(240, 42, now_date(), 12, theme.dim, TF_CENTER);
        icon(g, "sliders", 434, 39, 22, theme.dim);

        // quick tiles: Wi-Fi, sound (MPD mute), player, power
        draw_tile(g, 0, "wifi", "Wi-Fi", wifi);
        draw_tile(g, 1, "volume", "Звук", mpd->connected() && vol > 0);
        draw_tile(g, 2, "music", "Плеер", false);
        draw_tile(g, 3, "power", "Питание", false);

        // sliders
        if (vol >= 0) {
            std::string v = std::to_string(vol) + "%";
            draw_slider(g, VOL_Y, vol, "ГРОМКОСТЬ", v.c_str());
        } else {
            // MPD is not reachable: an empty track, no knob, no fake value
            g.text(SLX, VOL_Y, "ГРОМКОСТЬ", 12, theme.dim);
            g.text(SLX + SLW, VOL_Y, "MPD недоступен", 12, theme.dim, TF_RIGHT);
            g.rect(SLX, VOL_Y + 30, SLW, 4, theme.track);
        }

        if (bright >= 0) {
            int pct = bright_pct();
            std::string v = std::to_string(pct) + "%";
            draw_slider(g, BRI_Y, pct, "ЯРКОСТЬ", v.c_str());
        } else {
            draw_slider(g, BRI_Y, 0, "ЯРКОСТЬ", "—");
        }

        // notification cards: the MPD track and the real wlan0 link
        int y = CARD_Y;
        bool any = false;

        if (has_track()) {
            const MpdSong &s = mpd->song();
            const MpdStatus &st = mpd->status();
            std::string sub = s.artist.empty() ? song_state(st)
                                               : s.artist + " · " + song_state(st);
            double dur = st.duration > 0 ? st.duration : s.time;
            double el = mpd->elapsed(app.now);
            std::string right = fmt_time((int)el);
            if (dur > 0) right += " / " + fmt_time((int)(dur + 0.5));
            draw_card(g, y, song_title(s), sub, right);
            y += CARD_H + CARD_GAP;
            any = true;
        }

        if (has_wifi_card()) {
            std::string title = net.ssid.empty() ? "Wi-Fi" : net.ssid;
            std::string right = net.ip.empty() ? "нет IP" : "подключено";
            std::string sub = net.ip.empty() ? "Wi-Fi · нет IP"
                                             : "Wi-Fi · " + net.ip;
            draw_card(g, y, title, sub, right);
            y += CARD_H + CARD_GAP;
            any = true;
        }

        if (!any) {
            g.text(240, CARD_Y + 24, "Нет уведомлений", 11, theme.dim,
                   TF_CENTER);
        }

        // grab handle
        g.rect(180, 452, 120, 4, theme.line);
    }

    // ------------------------------------------------------------ input

    bool tap(int x, int y) override {
        drag_dy = 0;

        if (y >= PANEL_H) { app.back(); return true; }
        if (hit(x, y, 428, 32, 44, 44)) { app.open("set-root"); return true; }

        for (int i = 0; i < 4; i++) {
            if (!hit(x, y, tx(i), TY, TW, TH)) continue;
            switch (i) {
            case 0: toggle_wifi(); break;
            case 1: toggle_sound(); break;
            case 2: app.open("player"); break;
            case 3: app.open("power"); break;
            }
            return true;
        }

        if (hit(x, y, 0, VOL_Y - 10, 480, 50)) {
            set_volume(x);
            return true;
        }
        if (hit(x, y, 0, BRI_Y - 8, 480, 50)) {
            set_brightness(x);
            return true;
        }

        int card_y = CARD_Y;
        if (has_track()) {
            if (hit(x, y, SLX, card_y, SLW, CARD_H)) {
                app.open("player");
                return true;
            }
            card_y += CARD_H + CARD_GAP;
        }
        if (has_wifi_card() && hit(x, y, SLX, card_y, SLW, CARD_H)) {
            app.open("set-wifi");
            return true;
        }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        bool horiz = std::abs(dx) >= std::abs(dy);

        if (horiz && y >= VOL_Y - 8 && y < VOL_Y + 40) {
            set_volume(x);
            return true;
        }
        if (horiz && y >= BRI_Y - 8 && y < BRI_Y + 40) {
            set_brightness(x);
            return true;
        }

        if (dy > 0) drag_dy = 0;
        else drag_dy += dy;
        if (drag_dy <= -80) {
            drag_dy = 0;
            app.back();
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_shade() { return new Shade(); }
