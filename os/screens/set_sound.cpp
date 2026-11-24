// S5 "Звук" — media volume through MPD, real output route and MPD status.
// Switches that only wrote a settings key are gone: nothing implemented them.
#include "graphite.h"
#include "mpd.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <ctime>
#include <algorithm>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string rd(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    char b[128];
    std::string out;
    if (std::fgets(b, sizeof(b), f)) out = b;
    std::fclose(f);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

int step_from_db(int db) {
    int s = ((db + 40) * 30 + 20) / 40;
    return std::max(0, std::min(30, s));
}

int db_from_step(int s) {
    return -40 + (s * 40 + 15) / 30;
}

std::string db_text(int db) {
    if (db < 0) return "−" + std::to_string(-db) + " дБ";
    return std::to_string(db) + " дБ";
}

int h2w_state() {
    std::string s = rd("/sys/class/switch/h2w/state");
    if (s.empty()) s = rd("/sys/devices/virtual/switch/h2w/state");
    if (s.empty()) return -1;
    return std::atoi(s.c_str());
}

std::string route_text() {
    int s = h2w_state();
    if (s < 0) return "не определён";
    return s == 0 ? "динамик · lgjack" : "наушники (с микрофоном) · lgjack";
}

std::string route_short() {
    int s = h2w_state();
    if (s < 0) return "НЕТ ДАННЫХ";
    return s == 0 ? "ДИНАМИК" : "НАУШНИКИ";
}

std::string base_name(const std::string &path) {
    size_t slash = path.find_last_of('/');
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n = n.substr(0, dot);
    return n;
}

std::string state_text(const MpdStatus &st) {
    if (st.state == "play") return "играет";
    if (st.state == "pause") return "пауза";
    if (st.state == "stop") return "остановлен";
    return "—";
}

std::string track_title(const MpdSong &s) {
    if (!s.title.empty()) return s.title;
    std::string n = base_name(s.file);
    return n.empty() ? "—" : n;
}

std::string audio_text(const std::string &a) {
    int sr = 0, bits = 0, ch = 0;
    if (std::sscanf(a.c_str(), "%d:%d:%d", &sr, &bits, &ch) == 3) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.1f", sr / 1000.0);
        std::string hz(b);
        for (char &c : hz) if (c == '.') c = ',';
        std::string chs = ch == 1 ? "моно" : (ch == 2 ? "стерео" : std::to_string(ch) + " кан.");
        return hz + " кГц · " + std::to_string(bits) + " бит · " + chs;
    }
    return a.empty() ? "—" : a;
}

std::string time_text(int sec) {
    if (sec <= 0) return "";
    int m = sec / 60, s = sec % 60;
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d", m, s);
    return b;
}

void section(Gfx &g, int y, const char *label) {
    ui_section(g, y, label);
}

void kv(Gfx &g, int y, const char *k, const std::string &v, bool muted, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(k, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(v, 16, 272), 16, muted ? theme.dim : theme.fg);
}

void mrow(Gfx &g, int y, const std::string &name, const std::string &sub,
          const std::string &val, bool first) {
    if (!first) g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 300), 16, theme.fg);
    g.text(24, y + 29, g.fit(sub, 12, 300), 12, theme.dim);
    if (!val.empty()) g.text(428, y + 19, g.fit(val, 12, 160), 12, theme.dim, TF_RIGHT);
}

void save_key(const char *k, const char *v) {
    if (!settings_save_key(k, v)) cfg.set(k, v);
}

struct Sound : ScreenView {
    int step = -1;                 // 0..30, -1 = unknown
    bool pending = false;
    unsigned pending_at = 0;
    unsigned vol_set_at = 0;

    Sound() {
        if (cfg.kv.find("vol_db") != cfg.kv.end())
            step = step_from_db(cfg.geti("vol_db", 0));
    }

    const char *id() const override { return "set-sound"; }

    int db() const { return db_from_step(step); }

    void on_show() override {
        pending = false;
        vol_set_at = 0;
        MpdClient &m = mpd_client();
        if (m.connected() && m.status().volume >= 0) {
            step = (m.status().volume * 30 + 50) / 100;
        } else if (cfg.kv.find("vol_db") != cfg.kv.end()) {
            step = step_from_db(cfg.geti("vol_db", 0));
        }
    }

    void set_vol(int x) {
        int p = (x - 24) * 30 / 432;
        p = std::max(0, std::min(30, p));
        if (p == step) return;
        step = p;
        pending = true;
        pending_at = app.now;
        vol_set_at = app.now;
    }

    void tick(unsigned now) override {
        MpdClient &m = mpd_client();
        m.set_queue_watch(false);
        m.tick(now);

        if (!pending && (vol_set_at == 0 || now - vol_set_at > 2000) &&
            m.connected() && m.status().volume >= 0)
            step = (m.status().volume * 30 + 50) / 100;

        if (pending && now - pending_at >= 300) {
            m.setvol(step * 100 / 30);
            save_key("vol_db", std::to_string(db()).c_str());
            pending = false;
        }
    }

    bool animated() const override { return true; }

    void draw(Gfx &g) override {
        MpdClient &m = mpd_client();
        const MpdStatus &st = m.status();
        const bool conn = m.connected();
        const std::string dbv = step < 0 ? "—" : db_text(db());

        ui_bg(g);
        ui_header(g, "Звук", (std::string("MPD · ") + route_short()).c_str(), true);

        section(g, 96, "ГРОМКОСТЬ");
        g.text(24, 128, dbv, 30, theme.fg, TF_LIGHT);
        const std::string sval = step < 0 ? "—" : (std::to_string(step) + " / 30");
        ui_slider(g, 24, 184, 432, step < 0 ? 0 : step * 100 / 30, "МЕДИА",
                  sval.c_str());
        g.text(24, 228, "0…30 шагов · −40…0 дБ · команда MPD setvol", 11, theme.dim);

        section(g, 252, "MPD");
        std::string stxt = !conn ? "не подключён" : state_text(st);
        mrow(g, 280, "Состояние", conn ? "TCP 6600 · связь есть" : "TCP 6600 · нет связи",
             stxt, true);

        std::string title = "Трек";
        std::string artist = conn ? "нет текущего трека" : "MPD не подключён";
        std::string tval;
        if (m.has_song()) {
            const MpdSong &s = m.song();
            title = track_title(s);
            artist = s.artist.empty() ? "—" : s.artist;
            tval = time_text(s.time);
        }
        mrow(g, 336, title, artist, tval, false);

        std::string fmt = conn ? audio_text(st.audio) : "нет данных от MPD";
        std::string br = (conn && st.bitrate > 0) ? std::to_string(st.bitrate) + " кбит/с" : "—";
        mrow(g, 392, "Формат", fmt, br, false);

        std::string vol = (conn && st.volume >= 0) ? std::to_string(st.volume) + " %" : "—";
        mrow(g, 448, "Выход", route_text(), vol, false);

        std::string q = conn ? std::to_string(st.playlistlength) : "—";
        mrow(g, 504, "Очередь", "плейлист MPD", q, false);

        section(g, 566, "НЕДОСТУПНО");
        kv(g, 594, "НЕ БЕСПОКОИТЬ", "нет системы уведомлений", true, true);
        kv(g, 638, "МЕЛОДИЯ ЗВОНКА", "нет телефонии", true, false);

        g.text(24, 696,
               "Звук в системе один — MPD; системного микшера нет", 11, theme.dim);

        g.text(24, 781, "ЗВУК · MPD", 11, theme.dim);
        std::string right = conn ? state_text(st) : "нет связи";
        if (conn && st.volume >= 0) right += " · " + std::to_string(st.volume) + " %";
        g.text(456, 781, right, 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }
        if (hit(x, y, 0, 176, 480, 44)) { set_vol(x); return true; }
        return false;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)dx;
        (void)dy;
        if (y < 176 || y >= 226) return false;
        set_vol(x);
        return true;
    }
};

} // namespace

ScreenView *make_set_sound() { return new Sound(); }
