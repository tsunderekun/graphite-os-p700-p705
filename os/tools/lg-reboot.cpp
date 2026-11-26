#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
int run_mpc_stop() {
    pid_t pid = fork();
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDOUT_FILENO); dup2(nullfd, STDERR_FILENO); close(nullfd); }
        execlp("mpc", "mpc", "stop", (char *)nullptr);
        _exit(127);
    }
    if (pid < 0) return -1;
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        usleep(100000);
    }
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    return -1;
}
}

int main(int argc, char **argv) {
    if (getuid() != 0 || geteuid() != 0) { dprintf(STDOUT_FILENO, "lg-reboot: root required\n"); return 1; }
    if (argc != 2 || (strcmp(argv[1], "normal") && strcmp(argv[1], "poweroff"))) {
        dprintf(STDOUT_FILENO, "usage: lg-reboot normal|poweroff\n"); return 2;
    }
    // Stop MPD playback if available. An absent MPD is not fatal to shutdown.
    run_mpc_stop();
    sync();
    if (reboot(!strcmp(argv[1], "poweroff") ? RB_POWER_OFF : RB_AUTOBOOT) != 0) {
        dprintf(STDOUT_FILENO, "lg-reboot: reboot syscall: %s\n", strerror(errno));
        return 1;
    }
    dprintf(STDOUT_FILENO, "lg-reboot: syscall returned unexpectedly\n");
    return 1;
}
