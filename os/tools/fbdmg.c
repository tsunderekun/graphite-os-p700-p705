#define _DEFAULT_SOURCE
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int fb = -1;
static unsigned long pushes;
static unsigned long damages;
static int verbose;
static int pan(void) {
    if (fb < 0) { pushes++; return 0; }   // -n dry-run: count only
    struct fb_var_screeninfo v;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &v) < 0) return -1;
    v.yoffset = 0;
    if (ioctl(fb, FBIOPAN_DISPLAY, &v) < 0) return -1;
    pushes++;
    return 0;
}

static int tracked(Display *d, Window w) {
    int n = 0; Window root, parent, *children = NULL;
    if (!XQueryTree(d, w, &root, &parent, &children, &n)) return 0;
    if (children) XFree(children);
    return parent == DefaultRootWindow(d);
}
static void watch(Display *d, Window w, int damage_event) {
    if (!w || w == DefaultRootWindow(d)) return;
    Damage dm = XDamageCreate(d, w, XDamageReportNonEmpty);
    XSync(d, False);   // without a flush the server never sees the subscription
    if (verbose) fprintf(stderr, "fbdmg: watch 0x%lx damage=%lu\n", (unsigned long)w, (unsigned long)dm);
    (void)damage_event;
}
static void scan(Display *d, int damage_event) {
    Window root = DefaultRootWindow(d), parent, *children = NULL; int n = 0;
    if (XQueryTree(d, root, &root, &parent, &children, &n)) {
        int watched = 0;
        for (int i = 0; i < n; ++i) { watch(d, children[i], damage_event); watched++; }
        if (verbose || getenv("FBDMG_DEBUG"))
            fprintf(stderr, "fbdmg: watching %d top-level windows\n", watched);
        if (children) XFree(children);
    }
}

int main(int argc, char **argv) {
    int hz = 60; const char *pidfile = NULL; int dry = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-q")) verbose = 0;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-n")) dry = 1;
        else if ((!strcmp(argv[i], "-r") || !strcmp(argv[i], "-p")) && i + 1 < argc) {
            if (!strcmp(argv[i], "-r")) hz = atoi(argv[++i]); else pidfile = argv[++i];
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            hz = atoi(argv[i]);   // bare HZ, like the old fbpan-ng
        } else { fprintf(stderr, "usage: fbdmg [-r HZ] [-n] [-q|-v] [-p PIDFILE]\n"); return 1; }
    }
    if (hz < 1) hz = 1;
    if (hz > 60) hz = 60;
    if (pidfile) { FILE *p = fopen(pidfile, "w"); if (!p) { perror("fbdmg pidfile"); return 1; } fprintf(p, "%ld\n", (long)getpid()); fclose(p); }
    int de, de_error;
    Display *d = NULL;
    for (int i = 0; i < 50 && !d; ++i) {   // X may still be accepting connections at boot
        d = XOpenDisplay(NULL);
        if (!d) d = XOpenDisplay(":0");
        if (!d) usleep(100000);
    }
    if (!d) { fprintf(stderr, "fbdmg: cannot open X display\n"); return 1; }
    if (!XDamageQueryExtension(d, &de, &de_error)) { fprintf(stderr, "fbdmg: XDamage extension unavailable\n"); XCloseDisplay(d); return 2; }
    Window root = DefaultRootWindow(d);
    XSelectInput(d, root, SubstructureNotifyMask);
    scan(d, de);
    if (dry) { fb = -1; }
    else {
        for (int i = 0; i < 30 && fb < 0; ++i) {
            fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
            if (fb < 0) usleep(100000);
        }
        if (fb < 0) { perror("fbdmg: /dev/fb0"); XCloseDisplay(d); return 1; }
    }
    signal(SIGTERM, stop); signal(SIGINT, stop);
    double interval = 1.0 / hz, next = now(), fallback = now() + 1.0, report = now() + 1.0;
    int dirty = 1;
    while (running) {
        double t = now();
        // When nothing changed there is nothing to pan until the 1 s fallback;
        // using `next` here made wait ≈ 0 and spun the loop at 100% CPU.
        double wake = dirty ? next : fallback;
        if (verbose && report < wake) wake = report;
        double wait = wake - t; if (wait < 0) wait = 0;
        struct timeval tv = {(long)wait, (long)((wait - (long)wait) * 1000000)};
        fd_set fds; FD_ZERO(&fds); FD_SET(ConnectionNumber(d), &fds);
        int r = select(ConnectionNumber(d) + 1, &fds, NULL, NULL, &tv);
        if (r > 0) while (XPending(d)) {
            XEvent e; XNextEvent(d, &e);
            if (getenv("FBDMG_DEBUG")) fprintf(stderr, "fbdmg: event type %d (damage base %d)\n", e.type, de);
            if (e.type == de + XDamageNotify) {
                XDamageNotifyEvent *x = (XDamageNotifyEvent *)&e;
                XDamageSubtract(d, x->damage, None, None); dirty = 1; ++damages;
            } else if (e.type == MapNotify || e.type == ConfigureNotify) {
                XConfigureEvent *x = (XConfigureEvent *)&e;
                if (tracked(d, x->window)) { watch(d, x->window, de); dirty = 1; }
            } else if (e.type == CreateNotify) {
                XCreateWindowEvent *x = (XCreateWindowEvent *)&e;
                if (x->parent == root) { watch(d, x->window, de); dirty = 1; }
            } else if (e.type == DestroyNotify) dirty = 1;
        }
        t = now();
        if (t >= fallback) { dirty = 1; fallback = t + 1.0; }
        if (dirty && t >= next) {
            if (pan() < 0) perror("fbdmg: FBIOPAN_DISPLAY");
            dirty = 0; next = t + interval;
        }
        if (verbose && t >= report) { fprintf(stderr, "fbdmg: pushes/interval=%lu damage/interval=%lu\n", pushes, damages); pushes = 0; damages = 0; report = t + 1.0; }
    }
    close(fb); XCloseDisplay(d); if (pidfile) unlink(pidfile); return 0;
}
