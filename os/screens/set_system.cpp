// S11 "Время и дата" — real RTC/zone facts and a real ntpd -q sync in tick.
#include "graphite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

unsigned now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string read_first_line(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    char line[256] = {0};
    char *r = std::fgets(line, sizeof(line), f);
    std::fclose(f);
    return r ? trim(line) : "";
}

std::string slurp(const char *path, size_t cap = 2048) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[256];
    size_t n;
    while (out.size() < cap && (n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return trim(out);
}

std::string upper_ascii(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return s;
}

std::string zone_offset() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[16] = {0};
    if (!std::strftime(buf, sizeof(buf), "%z", &t)) return "UTC";
    std::string z = buf;
    if (z.size() < 5) return "UTC";
    int sign = z[0] == '-' ? -1 : 1;
    int hh = (z[1] - '0') * 10 + (z[2] - '0');
    int mm = (z[3] - '0') * 10 + (z[4] - '0');
    char out[32];
    if (mm)
        std::snprintf(out, sizeof(out), "UTC%c%d:%02d", sign < 0 ? '-' : '+', hh, mm);
    else
        std::snprintf(out, sizeof(out), "UTC%c%d", sign < 0 ? '-' : '+', hh);
    return out;
}

std::string uptime_text() {
    FILE *f = std::fopen("/proc/uptime", "r");
    if (!f) return "нет данных";
    double sec = 0;
    if (std::fscanf(f, "%lf", &sec) != 1) sec = 0;
    std::fclose(f);
    long total = (long)sec;
    long d = total / 86400, h = (total % 86400) / 3600, m = (total % 3600) / 60;
    char buf[48];
    if (d) std::snprintf(buf, sizeof(buf), "%ld д %ld ч", d, h);
    else if (h) std::snprintf(buf, sizeof(buf), "%ld ч %ld мин", h, m);
    else std::snprintf(buf, sizeof(buf), "%ld мин", m);
    return buf;
}

std::string load_text() {
    FILE *f = std::fopen("/proc/loadavg", "r");
    if (!f) return "нет данных";
    char line[128] = {0};
    char *r = std::fgets(line, sizeof(line), f);
    std::fclose(f);
    if (!r) return "нет данных";
    std::string s = line;
    size_t p1 = s.find(' ');
    if (p1 == std::string::npos) return trim(s);
    size_t p2 = s.find(' ', p1 + 1);
    if (p2 == std::string::npos) return trim(s);
    size_t p3 = s.find(' ', p2 + 1);
    return trim(s.substr(0, p3 == std::string::npos ? s.size() : p3));
}

int note(Gfx &g, int y, const char *text) {
    g.text(24, y + 6, g.fit(text, 11, 432), 11, theme.dim);
    return y + 24;
}

int kv(Gfx &g, int y, const char *key, const std::string &val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val, 16, 272), 16, muted ? theme.dim : theme.fg);
    return y + 44;
}

int button(Gfx &g, int y, const char *label) {
    g.rect(24, y, 432, 48, theme.accent);
    g.text(240, y + 18, g.fit(label, 13, 408), 13, theme.onacc,
           TF_CENTER | TF_BOLD);
    return y + 48;
}

struct SetSystem : ScreenView {
    std::string clock = "14:35";
    std::string cap = "ПН, 24 НОЯ 2026 · EUROPE/MOSCOW";
    std::string human = "Пн, 24 ноября 2026";
    std::string tz = "Europe/Moscow";
    std::string off = "UTC+3";
    bool rtc = false;
    int sync = 0;                 // 0 idle, 1 running, 2 done
    unsigned sync_at = 0;
    unsigned check_at = 0;
    std::string sync_msg = "не запускалась";

    SetSystem() {
        rtc = access("/dev/rtc0", F_OK) == 0;
        tz = read_first_line("/etc/timezone");
        if (tz.empty()) {
            const char *e = std::getenv("TZ");
            tz = e && *e ? e : "UTC";
        }
        refresh();
    }

    void on_show() override { refresh(); }

    void refresh() {
        static const char *const wd_u[] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
        static const char *const wd_c[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
        static const char *const mon_u[] = {"ЯНВ", "ФЕВ", "МАР", "АПР", "МАЯ",
                                            "ИЮН", "ИЮЛ", "АВГ", "СЕН", "ОКТ",
                                            "НОЯ", "ДЕК"};
        static const char *const mon_g[] = {"января", "февраля", "марта", "апреля",
                                            "мая", "июня", "июля", "августа",
                                            "сентября", "октября", "ноября",
                                            "декабря"};
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        char c[16];
        std::snprintf(c, sizeof(c), "%02d:%02d", t.tm_hour, t.tm_min);
        clock = c;
        int wd = t.tm_wday;
        if (wd < 0 || wd > 6) wd = 0;
        int mo = t.tm_mon;
        if (mo < 0 || mo > 11) mo = 0;
        int year = t.tm_year + 1900;

        char h[128];
        std::snprintf(h, sizeof(h), "%s, %d %s %d", wd_c[wd], t.tm_mday,
                      mon_g[mo], year);
        human = h;

        char cc[192];
        std::snprintf(cc, sizeof(cc), "%s, %d %s %d · %s", wd_u[wd], t.tm_mday,
                      mon_u[mo], year, upper_ascii(tz).c_str());
        cap = cc;
        off = zone_offset();
    }

    void start_sync() {
        if (sync == 1) {
            app.toast("NTP: запрос уже идёт");
            return;
        }
        int rc = std::system("ntpd -q -p pool.ntp.org >/tmp/ntp.log 2>&1 &");
        (void)rc;
        sync = 1;
        sync_msg = "идёт · ntpd -q";
        sync_at = now_ms();
        check_at = 0;
        app.toast("NTP: запрос отправлен");
    }

    void finish_sync(bool exited) {
        std::string log = slurp("/tmp/ntp.log");
        if (!exited) {
            sync_msg = "нет ответа · ntpd";
            sync = 2;
            app.toast("NTP: нет ответа за 8 с");
            return;
        }
        bool net_err = log.find("timed out") != std::string::npos ||
                       log.find("unreachable") != std::string::npos ||
                       log.find("bad address") != std::string::npos ||
                       log.find("no server") != std::string::npos ||
                       log.find("Network is down") != std::string::npos;
        bool missing = log.find("not found") != std::string::npos ||
                       log.find("No such file") != std::string::npos;
        if (missing) {
            sync_msg = "ntpd не найден";
            app.toast("ntpd не найден на устройстве");
        } else if (net_err) {
            sync_msg = "нет сети";
            app.toast("NTP: нет сети · /tmp/ntp.log");
        } else if (log.empty() || log.find("reply from") != std::string::npos) {
            sync_msg = "время обновлено";
            app.toast("NTP: время обновлено");
        } else {
            std::string first = log;
            size_t e = first.find('\n');
            if (e != std::string::npos) first = first.substr(0, e);
            if (first.size() > 40) first = first.substr(0, 40) + "…";
            sync_msg = first;
            app.toast("NTP: /tmp/ntp.log");
        }
        sync = 2;
        refresh();
    }

    void tick(unsigned now) override {
        if (sync != 1) return;
        unsigned dt = now - sync_at;
        if (dt < 2500) return;
        if (check_at && now - check_at < 500) return;
        check_at = now;
        FILE *p = popen("pidof ntpd 2>/dev/null", "r");
        bool alive = false;
        if (p) {
            char b[64] = {0};
            if (std::fgets(b, sizeof(b), p) && trim(b).size()) alive = true;
            pclose(p);
        }
        if (alive && dt < 8000) return;
        finish_sync(!alive);
    }

    bool animated() const override { return sync == 1; }

    const char *id() const override { return "set-system"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Время и дата", ("СИСТЕМА · " + std::string(theme.code)).c_str(),
                  true);

        g.rect(24, 104, 432, 96, theme.surf);
        g.text(40, 112, clock, 40, theme.fg, TF_LIGHT);
        g.text(40, 168, g.fit(cap, 11, 400), 11, theme.dim);

        int y = 208;
        y = ui_section(g, y, "СИНХРОНИЗАЦИЯ");
        std::string tzv = tz + " · " + off;
        y = kv(g, y, "ЧАСОВОЙ ПОЯС", tzv);
        y = kv(g, y, "NTP СТАТУС", sync_msg, sync != 2);
        y = button(g, y + 8, sync == 1 ? "СИНХРОНИЗАЦИЯ…" : "СИНХРОНИЗИРОВАТЬ");
        y = note(g, y, "ntpd -q -p pool.ntp.org · лог /tmp/ntp.log");

        y = ui_section(g, y, "ФОРМАТ");
        y = kv(g, y, "ФОРМАТ", "24 ч · DD.MM.YYYY");
        y = kv(g, y, "СЕГОДНЯ", human);
        y = kv(g, y, "RTC", rtc ? "/dev/rtc0 · есть" : "/dev/rtc0 · нет");
        y = kv(g, y, "ПРИ ЗАГРУЗКЕ", "RTC, time.start");

        y = ui_section(g, y, "СОСТОЯНИЕ");
        y = kv(g, y, "UPTIME", uptime_text());
        y = kv(g, y, "LOADAVG", load_text());
        y = note(g, y, "NTP не подтверждён — время из RTC");

        g.text(24, 781, "СИСТЕМА · RTC /dev/rtc0", 11, theme.dim);
        g.text(456, 781, (tz + " · " + off).c_str(), 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 24, 332, 432, 48)) {
            start_sync();
            return true;
        }
        return false;
    }
};

} // namespace

ScreenView *make_set_system() { return new SetSystem(); }
