#include "graphite.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

struct Power : ScreenView {
    enum Action { NONE, POWER_OFF, REBOOT } action = NONE;
    enum State { IDLE, CONFIRM, STARTING, FAILED } state = IDLE;
    unsigned deadline = 0;
    pid_t helper = -1;
    int result_fd = -1;
    char error[96] = {};

    const char *id() const override { return "power"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        for (int y = 0; y < 800; y++)
            for (int x = 0; x < 480; x++) g.cv.blend(x, y, 0, 189);
        g.rect(32, 250, 416, 300, theme.surf);
        g.text(52, 272, state == STARTING ? "Завершение работы" : "Питание", 20, theme.fg, TF_LIGHT);
        g.text(52, 306, "Несохранённые данные могут быть потеряны.", 13, theme.dim);

        const char *power_label = state == CONFIRM && action == POWER_OFF ? "Подтвердить выключение" : "Выключить…";
        const char *reboot_label = state == CONFIRM && action == REBOOT ? "Подтвердить перезагрузку" : "Перезагрузить";
        g.rect(52, 344, 376, 56, theme.surf2);
        g.text(68, 363, power_label, 15, action == POWER_OFF && state == CONFIRM ? theme.accent : theme.fg);
        g.rect(52, 410, 376, 56, theme.surf2);
        g.text(68, 429, reboot_label, 15, action == REBOOT && state == CONFIRM ? theme.accent : theme.fg);
        g.rect(52, 476, 376, 56, theme.surf2);
        g.text(68, 495, state == FAILED ? "Закрыть сообщение" : "Отмена", 15, theme.dim);
        if (state == CONFIRM) g.text(52, 580, "Нажмите выбранное действие ещё раз", 13, theme.accent);
        if (state == STARTING) g.text(52, 580, "Останавливаем звук и синхронизируем…", 13, theme.accent);
        if (state == FAILED) g.text(52, 580, error, 13, theme.accent);
    }

    void launch() {
        int p[2];
        if (pipe2(p, O_CLOEXEC | O_NONBLOCK) != 0) { fail("Не удалось создать канал helper"); return; }
        helper = fork();
        if (helper == 0) {
            close(p[0]);
            dup2(p[1], STDOUT_FILENO);
            execl("/usr/local/bin/lg-reboot", "lg-reboot", action == POWER_OFF ? "poweroff" : "normal", (char *)nullptr);
            dprintf(p[1], "helper exec: %s", strerror(errno));
            _exit(127);
        }
        close(p[1]);
        if (helper < 0) { close(p[0]); helper = -1; fail("Не удалось запустить helper"); return; }
        result_fd = p[0];
        state = STARTING;
        deadline = app.now + 12000;
    }

    void fail(const char *msg) {
        state = FAILED;
        std::snprintf(error, sizeof(error), "%s", msg);
        action = NONE;
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 52, 344, 376, 56)) {
            if (state == STARTING) return true;
            if (state == CONFIRM && action == POWER_OFF) launch();
            else { state = CONFIRM; action = POWER_OFF; deadline = app.now + 3000; }
            return true;
        }
        if (hit(x, y, 52, 410, 376, 56)) {
            if (state == STARTING) return true;
            if (state == CONFIRM && action == REBOOT) launch();
            else { state = CONFIRM; action = REBOOT; deadline = app.now + 3000; }
            return true;
        }
        if (hit(x, y, 52, 476, 376, 56)) {
            if (state == FAILED) { state = IDLE; error[0] = 0; }
            else if (state != STARTING) { state = IDLE; action = NONE; }
            return true;
        }
        return false;
    }

    void tick(unsigned now) override {
        if (state == CONFIRM && now >= deadline) { state = IDLE; action = NONE; }
        if (state != STARTING) return;
        char buf[96];
        ssize_t n = read(result_fd, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; fail(buf); }
        int status = 0;
        pid_t r = waitpid(helper, &status, WNOHANG);
        if (r == helper) {
            helper = -1;
            if (result_fd >= 0) { close(result_fd); result_fd = -1; }
            if (state == STARTING) fail("Команда завершилась без reboot/poweroff");
        } else if (now >= deadline) {
            if (helper > 0) { kill(helper, SIGKILL); waitpid(helper, nullptr, 0); helper = -1; }
            if (result_fd >= 0) { close(result_fd); result_fd = -1; }
            fail("Таймаут: переход не выполнен");
        }
    }

    bool animated() const override { return state == CONFIRM || state == STARTING; }
    ~Power() override { if (result_fd >= 0) close(result_fd); }
};

} // namespace

ScreenView *make_power() { return new Power(); }
