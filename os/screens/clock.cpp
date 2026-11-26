#include "graphite.h"
#include "clock_model.h"
#include "power_policy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {
bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string current_date() {
    static const char *months[] = {"января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char out[40];
    std::snprintf(out, sizeof(out), "%d %s %d", tmv.tm_mday, months[tmv.tm_mon], tmv.tm_year + 1900);
    return out;
}

struct Clock : ScreenView {
    clock_model::Timer timer;
    bool timer_tab = false;
    bool expired_overlay = false;
    int selection_seconds = 0;

    const char *id() const override { return "clock"; }
    bool animated() const override { return true; }

    void tick(unsigned now) override {
        const bool was_finished = timer.state == clock_model::State::Finished;
        const bool changed = timer.advance(now);
        if (changed) app.dirty = true;
        if (!was_finished && timer.state == clock_model::State::Finished) {
            expired_overlay = true;
            power_policy.wake(app.now);
            app.dirty = true;
        }
    }

    void on_show() override {
        const bool was_finished = timer.state == clock_model::State::Finished;
        if (const char *tab = std::getenv("GRAPHITE_CLOCK_TAB"))   // host preview only
            timer_tab = std::strcmp(tab, "timer") == 0;
        if (timer.advance(app.now) || timer.state == clock_model::State::Finished) app.dirty = true;
        if (!was_finished && timer.state == clock_model::State::Finished) {
            expired_overlay = true;
            power_policy.wake(app.now);
        }
    }

    void draw(Gfx &g) override {
        if (expired_overlay) {
            g.rect(0, 0, 480, 800, theme.bg);
            g.text(240, 286, "ВРЕМЯ ВЫШЛО", 24, theme.accent, TF_CENTER | TF_BOLD);
            g.text(240, 360, "00:00", 56, theme.fg, TF_CENTER | TF_LIGHT);
            g.rect(24, 712, 432, 48, theme.accent);
            g.text(240, 728, "ОК", 16, theme.onacc, TF_CENTER | TF_BOLD);
            return;
        }
        ui_bg(g);
        ui_header(g, "Часы", "время · таймер", true);
        g.rect(24, 104, 212, 44, timer_tab ? theme.surf : theme.accent);
        g.rect(244, 104, 212, 44, timer_tab ? theme.accent : theme.surf);
        g.text(130, 118, "ЧАСЫ", 13, timer_tab ? theme.fg : theme.onacc, TF_CENTER | TF_BOLD);
        g.text(350, 118, "ТАЙМЕР", 13, timer_tab ? theme.onacc : theme.fg, TF_CENTER | TF_BOLD);
        if (!timer_tab) {
            std::time_t t = std::time(nullptr);
            std::tm tmv{};
            localtime_r(&t, &tmv);
            g.text(240, 292, clock_model::format_clock(tmv.tm_hour, tmv.tm_min), 76, theme.fg, TF_CENTER | TF_LIGHT);
            const std::string date = current_date();
            g.text(240, 378, date, 14, theme.dim, TF_CENTER);
            return;
        }
        const int remain = timer.state == clock_model::State::Running ? timer.remaining(app.now)
            : (selection_seconds ? selection_seconds : timer.paused_seconds);
        g.text(240, 206, clock_model::format_timer(remain), 54, theme.fg, TF_CENTER | TF_LIGHT);
        const char *labels[] = {"1 мин", "5 мин", "10 мин", "15 мин"};
        for (int i = 0; i < 4; ++i) {
            const int x = 24 + i * 110;
            g.rect(x, 288, 102, 44, theme.surf);
            g.text(x + 51, 302, labels[i], 12, theme.fg, TF_CENTER);
        }
        g.rect(24, 352, 208, 44, theme.surf);
        g.rect(248, 352, 208, 44, theme.surf);
        g.text(128, 366, "+1 мин", 14, theme.fg, TF_CENTER);
        g.text(352, 366, "−1 мин", 14, theme.fg, TF_CENTER);
        const bool running = timer.state == clock_model::State::Running;
        g.rect(24, 424, 432, 48, theme.accent);
        const char *main = running ? "ПАУЗА" : (timer.paused_seconds ? "ПРОДОЛЖИТЬ" : "СТАРТ");
        g.text(240, 440, main, 15, theme.onacc, TF_CENTER | TF_BOLD);
        g.rect(24, 488, 432, 48, theme.surf);
        g.text(240, 504, "СБРОС", 15, theme.fg, TF_CENTER);
    }

    bool tap(int x, int y) override {
        if (expired_overlay) {
            if (hit(x, y, 24, 712, 432, 48)) {
                expired_overlay = false;
                timer.reset();
                selection_seconds = 0;
                app.dirty = true;
            }
            return true;
        }
        if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }
        if (hit(x, y, 24, 104, 212, 44)) { timer_tab = false; return true; }
        if (hit(x, y, 244, 104, 212, 44)) { timer_tab = true; return true; }
        if (!timer_tab) return true;
        if (hit(x, y, 24, 288, 432, 44)) {
            const int index = (x - 24) / 110;
            if (index >= 0 && index < 4) {
                const int p[] = {60, 300, 600, 900};
                selection_seconds = p[index];
                if (timer.state != clock_model::State::Running) timer.start(selection_seconds, app.now);
            }
            return true;
        }
        if (hit(x, y, 24, 352, 208, 44) || hit(x, y, 248, 352, 208, 44)) {
            const int base = timer.state == clock_model::State::Running ? timer.remaining(app.now)
                : (selection_seconds ? selection_seconds : timer.paused_seconds);
            const int adjust = hit(x, y, 24, 352, 208, 44) ? 60 : -60;
            selection_seconds = base + adjust < 0 ? 0 : base + adjust;
            if (timer.state != clock_model::State::Running) timer.start(selection_seconds, app.now);
            else timer.start(selection_seconds, app.now);
            return true;
        }
        if (hit(x, y, 24, 424, 432, 48)) {
            if (timer.state == clock_model::State::Running) timer.pause(app.now);
            else if (timer.remaining(app.now) > 0 || timer.paused_seconds > 0) timer.resume(app.now);
            else app.toast("Сначала выберите время");
            return true;
        }
        if (hit(x, y, 24, 488, 432, 48)) { timer.reset(); selection_seconds = 0; return true; }
        return true;
    }
};
} // namespace

ScreenView *make_clock() { return new Clock(); }
