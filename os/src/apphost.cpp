// apphost — runs one foreign X11 application at a time as uid 10000 and hands
// it the content area. Windows are filtered (pid, size, type, viewable) so
// dillo/netsurf/GTK helper windows do not steal the slot. Everything waits in
// apphost_tick(): draw() never blocks.
#include "graphite.h"
#include "apphost.h"
#include "osk.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XTest.h>

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <grp.h>
#include <string>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void alog(const char *fmt, ...) {
    static int dbg = -1;
    if (dbg < 0) dbg = std::getenv("GRAPHITE_APPHOST_DEBUG") ? 1 : 0;
    if (!dbg) return;
    FILE *f = std::fopen("/tmp/apphost.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

int on_xerror(Display *, XErrorEvent *e) {
    alog("X error code=%d req=%d", e->error_code, e->request_code);
    return 0;
}

const uid_t kUid = 10000;
const gid_t kGid = 10000;
const int kTop = 32;              // status bar
const int kContentH = 768;        // 800 - status bar
const int kKbContentH = 480;      // with the on-screen keyboard up
const unsigned kLaunchTimeout = 15000;
const unsigned kBackWindow = 2500; // two Backs inside this window = close
const unsigned kProbeEvery = 500;

pid_t g_pid = -1;
Window g_win = 0;
unsigned g_launched = 0;
bool g_closing = false;
bool g_term_sent = false, g_kill_sent = false;
unsigned g_close_at = 0;
unsigned g_last_probe = 0;

char g_cmd[256] = {0};
bool g_is_browser = false;
bool g_back_nav = false;
unsigned g_back_at = 0;

char g_dbus[256] = {0};
bool g_dbus_done = false;
pid_t g_helpers[4] = {-1, -1, -1, -1};
int g_nhelpers = 0;

int content_h() { return osk_visible() ? kKbContentH : kContentH; }

// ---------------------------------------------------------------- env

void ensure_runtime_dir() {
    ::mkdir("/run/user", 0755);
    if (::mkdir("/run/user/10000", 0700) != 0 && errno != EEXIST)
        alog("mkdir /run/user/10000 failed errno=%d", errno);
    if (::chown("/run/user/10000", kUid, kGid) != 0) { /* best effort */ }
    ::chmod("/run/user/10000", 0700);
}

bool read_dbus_file(char *out, size_t n) {
    FILE *f = std::fopen("/tmp/dbus-graphite.address", "rb");
    if (!f) return false;
    char b[512] = {0};
    bool got = std::fgets(b, sizeof b, f) != nullptr;
    std::fclose(f);
    if (!got) return false;
    size_t len = std::strlen(b);
    while (len && (b[len - 1] == '\n' || b[len - 1] == '\r' || b[len - 1] == ' '))
        b[--len] = 0;
    if (!len) return false;
    // A stale file must not be trusted: check the socket still exists.
    if (!std::strncmp(b, "unix:path=", 10)) {
        char path[256];
        const char *p = b + 10;
        size_t i = 0;
        while (p[i] && p[i] != ',' && i + 1 < sizeof path) { path[i] = p[i]; i++; }
        path[i] = 0;
        struct stat st;
        if (::stat(path, &st) != 0) return false;
    }
    std::snprintf(out, n, "%.*s", (int)(n ? n - 1 : 0), b);
    return true;
}

// One session bus for all GUI children. dbus-daemon must run as user, not root.
void start_dbus_session() {
    if (g_dbus_done) return;
    g_dbus_done = true;

    const char *env = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (env && *env) {
        std::snprintf(g_dbus, sizeof g_dbus, "%s", env);
        return;
    }
    if (read_dbus_file(g_dbus, sizeof g_dbus)) {
        alog("dbus cached %s", g_dbus);
        return;
    }

    ensure_runtime_dir();
    std::remove("/tmp/dbus-graphite.address");
    int fd[2];
    if (pipe(fd) != 0) { alog("dbus pipe failed"); return; }
    pid_t pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        alog("dbus fork failed");
        return;
    }
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        if (fd[1] != STDOUT_FILENO) close(fd[1]);
        setgroups(0, nullptr);
        if (setgid(kGid) != 0) { /* on the host we may already be unprivileged */ }
        if (setuid(kUid) != 0) { /* same: keep going, X access still fails cleanly */ }
        setenv("HOME", "/home/user", 1);
        setenv("XDG_RUNTIME_DIR", "/run/user/10000", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        setenv("LANG", "C.UTF-8", 1);
        execlp("dbus-daemon", "dbus-daemon", "--session", "--fork",
               "--print-address=1", (char *)nullptr);
        _exit(127);
    }

    close(fd[1]);
    char buf[512] = {0};
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(fd[0], &rs);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 700000;
    ssize_t got = 0;
    if (select(fd[0] + 1, &rs, nullptr, nullptr, &tv) > 0)
        got = read(fd[0], buf, sizeof buf - 1);
    close(fd[0]);

    if (got > 0) {
        buf[got] = 0;
        char *nl = std::strchr(buf, '\n');
        if (nl) *nl = 0;
        if (!std::strncmp(buf, "unix:", 5) || !std::strncmp(buf, "tcp:", 4)) {
            std::snprintf(g_dbus, sizeof g_dbus, "%.*s",
                          (int)sizeof g_dbus - 1, buf);
            FILE *f = std::fopen("/tmp/dbus-graphite.address", "w");
            if (f) {
                std::fprintf(f, "%s\n", g_dbus);
                std::fclose(f);
            }
            alog("dbus started %s", g_dbus);
        } else {
            alog("dbus: odd address '%s'", buf);
        }
    } else {
        alog("dbus: no address");
    }

    // --fork leaves the daemon behind and the launcher exits on its own; reap
    // it from tick() so nothing here can block the UI.
    if (pid > 0 && g_nhelpers < 4) g_helpers[g_nhelpers++] = pid;
}

void reap_helpers() {
    for (int i = 0; i < g_nhelpers; i++) {
        if (g_helpers[i] <= 0) continue;
        int st = 0;
        pid_t w = waitpid(g_helpers[i], &st, WNOHANG);
        if (w == g_helpers[i] || w < 0) g_helpers[i] = -1;
    }
}

bool is_browser_command(const char *command) {
    const char *p = command;
    while (*p == ' ' || *p == '\t') p++;
    char prog[64];
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\t' && n < (int)sizeof prog - 1) {
        prog[n] = p[n];
        n++;
    }
    prog[n] = 0;
    const char *base = std::strrchr(prog, '/');
    base = base ? base + 1 : prog;
    return !std::strcmp(base, "dillo") || !std::strcmp(base, "netsurf") ||
           !std::strcmp(base, "netsurf-gtk3");
}

// ---------------------------------------------------------------- shell glue

void grab_keys(bool on) {
    if (!app.dpy) return;
    Window root = DefaultRootWindow(app.dpy);
    KeyCode codes[] = {
        XKeysymToKeycode(app.dpy, XF86XK_Back),
        XKeysymToKeycode(app.dpy, XK_Home),
        XKeysymToKeycode(app.dpy, XF86XK_PowerOff),
    };
    for (KeyCode kc : codes) {
        if (!kc) continue;
        alog("grab kc=%d on=%d", (int)kc, on ? 1 : 0);
        if (on)
            XGrabKey(app.dpy, kc, AnyModifier, root, True,
                     GrabModeAsync, GrabModeAsync);
        else
            XUngrabKey(app.dpy, kc, AnyModifier, root);
    }
    XSync(app.dpy, False);
}

void focus_shell() {
    if (!app.dpy) return;
    XSetInputFocus(app.dpy, app.win, RevertToPointerRoot, CurrentTime);
    XFlush(app.dpy);
    app.dirty = true;
}

void cleanup() {
    pid_t pid = g_pid;
    bool orphan = g_win != 0;
    g_pid = -1;
    g_win = 0;
    g_closing = false;
    g_term_sent = g_kill_sent = false;
    g_cmd[0] = 0;
    g_is_browser = false;
    g_back_nav = false;
    // A wrapper shell may have forked the GUI and exited: finish the group so
    // no window is left on screen with no task behind it.
    if (orphan && pid > 0) {
        if (kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);
    }
    if (osk_visible()) osk_hide();   // g_win is already 0: resize is a no-op
    grab_keys(false);
    focus_shell();
    app.dirty = true;
}

void send_delete(Window w) {
    Atom prot = XInternAtom(app.dpy, "WM_PROTOCOLS", False);
    Atom del = XInternAtom(app.dpy, "WM_DELETE_WINDOW", False);
    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = prot;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)del;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(app.dpy, w, False, NoEventMask, &ev);
    XFlush(app.dpy);
}

// The child gets its own process group, so a shell wrapper cannot leave the
// real GUI process orphaned after SIGTERM.
void kill_app(int sig) {
    if (g_pid <= 0) return;
    if (kill(-g_pid, sig) != 0) kill(g_pid, sig);
}

// Alt+Left via XTest: dillo/netsurf navigate back in history like any page.
void send_alt_left() {
    if (!app.dpy || !g_win) return;
    KeyCode alt = XKeysymToKeycode(app.dpy, XK_Alt_L);
    KeyCode left = XKeysymToKeycode(app.dpy, XK_Left);
    if (!alt || !left) return;
    XSetInputFocus(app.dpy, g_win, RevertToPointerRoot, CurrentTime);
    XTestFakeKeyEvent(app.dpy, alt, True, CurrentTime);
    XTestFakeKeyEvent(app.dpy, left, True, CurrentTime);
    XTestFakeKeyEvent(app.dpy, left, False, CurrentTime);
    XTestFakeKeyEvent(app.dpy, alt, False, CurrentTime);
    XFlush(app.dpy);
    alog("back: Alt+Left win=0x%lx", (unsigned long)g_win);
}

// ---------------------------------------------------------------- filtering

// The shell owns the pid, but `/bin/sh -c <cmd>` is allowed to fork instead of
// exec (dash and busybox ash both do). A window belongs to the task when its
// _NET_WM_PID is the shell pid or one of its descendants.
bool pid_is_ours(pid_t p) {
    if (g_pid <= 0) return false;
    if (p == g_pid) return true;
    pid_t cur = p;
    for (int depth = 0; depth < 8 && cur > 1; depth++) {
        char path[64];
        std::snprintf(path, sizeof path, "/proc/%d/stat", (int)cur);
        FILE *f = std::fopen(path, "rb");
        if (!f) return false;
        char buf[512];
        bool got = std::fgets(buf, sizeof buf, f) != nullptr;
        std::fclose(f);
        if (!got) return false;
        char *rp = std::strrchr(buf, ')');
        if (!rp) return false;
        int ppid = 0;
        if (std::sscanf(rp + 2, "%*c %d", &ppid) != 1) return false;
        if (ppid == g_pid) return true;
        if (ppid <= 1 || ppid == cur) return false;
        cur = ppid;
    }
    return false;
}

bool pid_matches(Window w) {
    Atom a = XInternAtom(app.dpy, "_NET_WM_PID", True);
    if (!a) return false;
    Atom type;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;
    bool ok = false;
    if (XGetWindowProperty(app.dpy, w, a, 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        if (n >= 1 && fmt == 32)
            ok = pid_is_ours((pid_t)((long *)data)[0]);
        XFree(data);
    }
    return ok;
}

bool window_type_ok(Window w) {
    Atom a = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE", True);
    if (!a) return true;
    Atom type;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(app.dpy, w, a, 0, 8, False, XA_ATOM,
                           &type, &fmt, &n, &after, &data) != Success || !data)
        return true;
    bool ok = true;
    if (n >= 1 && fmt == 32) {
        Atom normal = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
        Atom dialog = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        ok = false;
        for (unsigned long i = 0; i < n; i++) {
            Atom t = ((Atom *)data)[i];
            if (t == normal || t == dialog) ok = true;
        }
    }
    XFree(data);
    return ok;
}

bool is_toplevel(Window w) {
    Window root_ret = 0, parent = 0, *kids = nullptr;
    unsigned n = 0;
    bool ok = false;
    if (XQueryTree(app.dpy, w, &root_ret, &parent, &kids, &n)) {
        ok = parent == DefaultRootWindow(app.dpy);
        if (kids) XFree(kids);
    }
    return ok;
}

// The one gate every candidate window has to pass. Helper windows (1x1, 2x2),
// toolbars, splash screens and other processes' windows never get here.
bool try_adopt(Window w) {
    if (g_pid <= 0 || g_win || g_closing || !app.dpy) return false;
    if (w == 0 || w == app.win) return false;

    XWindowAttributes at;
    if (!XGetWindowAttributes(app.dpy, w, &at)) return false;
    if (at.override_redirect || at.map_state != IsViewable) return false;
    if (at.c_class != InputOutput) return false;
    if (at.width < 200 || at.height < 150) return false;
    if (!is_toplevel(w)) return false;
    if (!window_type_ok(w)) return false;
    if (!pid_matches(w)) return false;

    g_win = w;
    alog("adopt win=0x%lx %dx%d", (unsigned long)w, at.width, at.height);
    XMoveResizeWindow(app.dpy, w, 0, kTop, app.W, content_h());
    XRaiseWindow(app.dpy, w);
    XSync(app.dpy, False);
    XSetInputFocus(app.dpy, w, RevertToPointerRoot, CurrentTime);
    XFlush(app.dpy);
    app.dirty = true;
    return true;
}

// Late helper resize (a client mapping 1x1 and then asking for its real size)
// and windows mapped before the shell saw MapNotify: walk root children.
void probe_windows() {
    if (!app.dpy || g_win) return;
    Window root_ret = 0, parent = 0, *kids = nullptr;
    unsigned n = 0;
    Window root = DefaultRootWindow(app.dpy);
    if (!XQueryTree(app.dpy, root, &root_ret, &parent, &kids, &n)) return;
    for (unsigned i = 0; i < n && !g_win; i++) {
        if (kids[i] == app.win) continue;
        try_adopt(kids[i]);
    }
    if (kids) XFree(kids);
}

} // namespace

// ---------------------------------------------------------------- public

bool apphost_launch(const char *command) {
    if (!app.dpy || !command || !*command) return false;
    if (g_pid > 0) { app.toast("Уже открыто приложение"); return false; }

    ensure_runtime_dir();
    start_dbus_session();
    std::snprintf(g_cmd, sizeof g_cmd, "%s", command);
    g_is_browser = is_browser_command(command);
    g_back_nav = false;

    pid_t pid = fork();
    if (pid < 0) { app.toast("Не удалось запустить"); return false; }
    if (pid == 0) {
        setpgid(0, 0);
        setgroups(0, nullptr);
        if (setgid(kGid) != 0) { /* host preview: keep the current gid */ }
        if (setuid(kUid) != 0) { /* host preview: dillo will not start */ }

        const char *disp = DisplayString(app.dpy);
        setenv("DISPLAY", disp && *disp ? disp : ":0", 1);
        setenv("HOME", "/home/user", 1);
        setenv("USER", "user", 1);
        setenv("LOGNAME", "user", 1);
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
        setenv("XDG_RUNTIME_DIR", "/run/user/10000", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("GTK_CSD", "0", 1);
        setenv("NO_AT_BRIDGE", "1", 1);
        setenv("XMODIFIERS", "@im=none", 1);
        if (g_dbus[0]) setenv("DBUS_SESSION_BUS_ADDRESS", g_dbus, 1);
        else unsetenv("DBUS_SESSION_BUS_ADDRESS");

        execl("/bin/sh", "sh", "-c", command, (char *)nullptr);
        _exit(127);
    }

    g_pid = pid;
    setpgid(pid, pid);   // race-free: the child does this too
    g_win = 0;
    g_launched = app.now;
    g_last_probe = app.now;
    g_closing = false;
    g_term_sent = g_kill_sent = false;
    XSetErrorHandler(on_xerror);
    alog("launch pid=%d cmd='%s'", (int)pid, g_cmd);
    grab_keys(true);
    app.dirty = true;
    return true;
}

bool apphost_active() { return g_pid > 0; }

bool apphost_has_window() { return g_win != 0; }

bool apphost_launching() {
    if (g_pid <= 0 || g_win || g_closing) return false;
    return app.now - g_launched <= kLaunchTimeout;
}

const char *apphost_command() { return g_cmd; }

bool apphost_is_browser() { return g_pid > 0 && g_is_browser; }

// Back policy: a browser gets one Alt+Left. A second Back within a couple of
// seconds falls through to apphost_close(), which the caller does.
bool apphost_back() {
    if (g_pid <= 0 || !g_is_browser || !g_win) return false;
    if (g_back_nav && app.now - g_back_at <= kBackWindow) {
        g_back_nav = false;
        return false;
    }
    send_alt_left();
    g_back_nav = true;
    g_back_at = app.now;
    return true;
}

void apphost_note_key(int ksym, bool down) {
    alog("key ks=0x%x %s active=%d", ksym, down ? "down" : "up", g_pid > 0 ? 1 : 0);
}

bool apphost_on_map(unsigned long window) {
    if (g_pid <= 0 || g_win || !app.dpy) return false;
    return try_adopt((Window)window);
}

void apphost_on_destroy(unsigned long window) {
    if (!g_win || (Window)window != g_win) return;
    g_win = 0;
    focus_shell();
    if (!g_closing) {
        g_closing = true;
        g_close_at = app.now;
    }
    app.dirty = true;
}

void apphost_set_content_height(int h) {
    if (g_win && app.dpy)
        XMoveResizeWindow(app.dpy, g_win, 0, kTop, app.W, h);
}

void apphost_focus_app() {
    if (g_win && app.dpy) {
        XRaiseWindow(app.dpy, g_win);
        XSetInputFocus(app.dpy, g_win, RevertToPointerRoot, CurrentTime);
        XFlush(app.dpy);
    }
}

void apphost_close() {
    if (g_pid <= 0) return;
    alog("close pid=%d win=0x%lx", (int)g_pid, (unsigned long)g_win);
    g_closing = true;
    g_close_at = app.now;
    g_back_nav = false;
    if (g_win) send_delete(g_win);
    app.dirty = true;   // drop the launch overlay / clearing state at once
}

void apphost_tick(unsigned now) {
    reap_helpers();
    if (g_pid <= 0) return;

    int st = 0;
    pid_t r = waitpid(g_pid, &st, WNOHANG);
    if (r == g_pid) { alog("exited pid=%d st=%d", (int)g_pid, st); cleanup(); return; }

    // Keep looking for a suitable window: the first MapNotify is often a
    // 1x1 helper, and some clients only get their real size in a later
    // ConfigureNotify.
    if (!g_win && !g_closing && now - g_launched < kLaunchTimeout &&
        now - g_last_probe >= kProbeEvery) {
        g_last_probe = now;
        probe_windows();
    }

    if (!g_win && now - g_launched > kLaunchTimeout && !g_closing) {
        app.toast("Приложение не открылось");
        g_closing = true;
        g_close_at = now;
    }

    if (g_closing) {
        if (!g_term_sent && now - g_close_at > 800) {
            g_term_sent = true;
            alog("SIGTERM pid=%d", (int)g_pid);
            kill_app(SIGTERM);
        }
        if (!g_kill_sent && now - g_close_at > 2500) {
            g_kill_sent = true;
            alog("SIGKILL pid=%d", (int)g_pid);
            kill_app(SIGKILL);
        }
    }
}
