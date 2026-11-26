#include "graphite.h"
#include "mpd.h"
#include "apphost.h"
#include "osk.h"
#include "power_policy.h"
#include "lock_auth.h"
#include "notify.h"
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

App app;
extern unsigned long gfx_text_us, gfx_textw_us;
static bool write_backlight(int value) {
    int fd = ::open("/sys/class/leds/lcd-backlight/brightness", O_WRONLY);
    if (fd < 0) return false;
    char b[8]; int n = std::snprintf(b, sizeof(b), "%d\n", value);
    ssize_t written = ::write(fd, b, (size_t)n);
    int rc = ::close(fd);
    return written == n && rc == 0;
}
PowerPolicy power_policy(write_backlight);

// "Запуск…" overlay. Shown while the child has no window yet — the shell is
// still the visible window then, so this is what the user sees. Geometry is
// shared by draw and tap.
static const int kOvX = 140, kOvY = 450, kOvW = 200, kOvH = 56;

static bool ov_hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void draw_launch_overlay(Gfx &g) {
    if (!apphost_launching()) return;
    // Opaque scrim over the content: the screen behind must not peek out in
    // half-cut lines while the child is starting.
    g.rect(0, 32, g.cv.w, 768, theme.bg);
    const int px = 20, py = 300, pw = 440, ph = 232;
    g.rect(px, py, pw, ph, theme.surf);
    g.frame(px, py, pw, ph, theme.line2);
    g.text(px + 20, py + 24, "Запуск…", 24, theme.fg, TF_LIGHT);
    g.text(px + 20, py + 62, g.fit(apphost_command(), 13, pw - 40), 13, theme.accent);
    g.text(px + 20, py + 90, "Ждём окно приложения — до 15 секунд", 12, theme.dim);
    g.rect(kOvX, kOvY, kOvW, kOvH, theme.accent);
    g.text(kOvX + kOvW / 2, kOvY + 19, "ОТМЕНА", 15, theme.onacc,
           TF_CENTER | TF_BOLD);
}

static unsigned ms_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

bool App::init() {
    cfg.load("/etc/lgtest-settings");
    power_policy.configure((unsigned)std::max(0, cfg.geti("dim_after", 10)) * 1000u,
                           (unsigned)std::max(0, cfg.geti("lock_after", 30)) * 1000u,
                           cfg.geti("brightness", 150), cfg.geti("dim_brightness", 40));
    power_policy.last_input = ms_now();
    if (!power_policy.set_brightness(power_policy.brightness))
        std::fprintf(stderr, "graphite: cannot set initial backlight\n");
    std::string th = cfg.get("theme", "graphite");
    std::string ac = cfg.get("accent", "");
    theme_set(th.c_str(), ac.empty() ? nullptr : ac.c_str());
    if (!gfx_init()) {
        std::fprintf(stderr, "graphite: no DejaVu fonts\n");
        return false;
    }

    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::fprintf(stderr, "graphite: cannot open display\n");
        return false;
    }
    int scr = DefaultScreen(dpy);
    depth = DefaultDepth(dpy, scr);
    Visual *vis = DefaultVisual(dpy, scr);

    XSetWindowAttributes at;
    std::memset(&at, 0, sizeof(at));
    at.override_redirect = True;
    at.background_pixel = BlackPixel(dpy, scr);
    at.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                    PointerMotionMask | KeyPressMask | KeyReleaseMask |
                    StructureNotifyMask | FocusChangeMask;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0,
                        CopyFromParent, InputOutput, vis,
                        CWOverrideRedirect | CWBackPixel | CWEventMask, &at);
    XStoreName(dpy, win, "graphite shell");
    XSelectInput(dpy, RootWindow(dpy, scr), SubstructureNotifyMask);
    XMapRaised(dpy, win);
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
    Bool ar = False;
    XkbSetDetectableAutoRepeat(dpy, True, &ar);

    // invisible cursor: the phone has no mouse
    char data = 0;
    Pixmap pm = XCreateBitmapFromData(dpy, win, &data, 1, 1);
    XColor dummy;
    std::memset(&dummy, 0, sizeof(dummy));
    Cursor blank = XCreatePixmapCursor(dpy, pm, pm, &dummy, &dummy, 0, 0);
    XDefineCursor(dpy, win, blank);
    XFreePixmap(dpy, pm);

    gc = XCreateGC(dpy, win, 0, nullptr);

    img = XCreateImage(dpy, vis, depth, ZPixmap, 0, nullptr, W, H, 32, 0);
    if (!img) return false;
    size_t bytes = (size_t)img->bytes_per_line * H;
    img->data = (char *)std::calloc(bytes, 1);
    if (!img->data) return false;

    // MIT-SHM: put the frame through a shared segment instead of copying
    // 768 KB into the X socket on every redraw. Falls back to XPutImage.
    if (XShmQueryExtension(dpy)) {
        XImage *sim = XShmCreateImage(dpy, vis, depth, ZPixmap, nullptr, &shm, W, H);
        if (sim) {
            shm.shmid = shmget(IPC_PRIVATE, (size_t)sim->bytes_per_line * sim->height,
                               IPC_CREAT | 0600);
            if (shm.shmid >= 0) {
                shm.shmaddr = sim->data = (char *)shmat(shm.shmid, nullptr, 0);
                if (shm.shmaddr != (char *)-1) {
                    shm.readOnly = False;
                    if (XShmAttach(dpy, &shm)) {
                        XSync(dpy, False);
                        shmctl(shm.shmid, IPC_RMID, nullptr);   // attached copy lives on
                        std::free(img->data);
                        img->data = nullptr;
                        XDestroyImage(img);
                        img = sim;
                        shm_ok = true;
                    }
                }
            }
        }
    }

    cv = new Canvas(W, H);
    g = new Gfx(*cv);
    now = ms_now();
    return true;
}

ScreenView *App::top() { return stack.empty() ? nullptr : stack.back(); }

bool App::open_file(const std::string &path) {
    std::string name = path.substr(path.find_last_of('/') == std::string::npos
                                       ? 0 : path.find_last_of('/') + 1);
    for (char &c : name) c = (char)std::tolower((unsigned char)c);
    const char *target = nullptr;
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".txt") == 0) target = "reader";
    else if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".fb2") == 0) target = "reader";
    else if (name.size() >= 8 && name.compare(name.size() - 8, 8, ".fb2.zip") == 0) target = "reader";
    else if ((name.size() >= 4 && name.compare(name.size() - 4, 4, ".png") == 0) ||
             (name.size() >= 4 && name.compare(name.size() - 4, 4, ".jpg") == 0) ||
             (name.size() >= 5 && name.compare(name.size() - 5, 5, ".jpeg") == 0)) target = "viewer";
    if (!target) { toast("Нет приложения для этого типа"); return false; }
    pending_file = path;
    open(target);
    return true;
}

ScreenView *App::open(const char *id) {
    auto it = cache.find(id);
    if (it == cache.end()) {
        ScreenView *s = screen_make(id);
        if (!s) {
            toast("Экран в разработке");
            return top();
        }
        it = cache.emplace(id, s).first;
    }
    ScreenView *s = it->second;
    if (!std::strcmp(id, "home")) {
        stack.clear();
        stack.push_back(s);
        s->scroll_reset();
        s->on_show();
        dirty = true;
        return s;
    }
    while (stack.size() > 1 && stack.back()->overlay()) stack.pop_back();
    if (!stack.empty() && stack.back() == s) {
        s->on_show();
        dirty = true;
        return s;
    }
    stack.push_back(s);
    s->scroll_reset();   // a freshly opened screen starts at the top
    s->on_show();
    dirty = true;
    return s;
}

ScreenView *App::replace(const char *id) {
    auto it = cache.find(id);
    if (it == cache.end()) {
        ScreenView *s = screen_make(id);
        if (!s) { toast("Экран в разработке"); return top(); }
        it = cache.emplace(id, s).first;
    }
    while (stack.size() > 1 && stack.back()->overlay()) stack.pop_back();
    if (stack.empty()) stack.push_back(it->second);
    else stack.back() = it->second;
    it->second->scroll_reset();
    it->second->on_show();
    dirty = true;
    return it->second;
}

void App::push(const char *id) {
    auto it = cache.find(id);
    if (it == cache.end()) {
        ScreenView *s = screen_make(id);
        if (!s) { toast("Экран в разработке"); return; }
        it = cache.emplace(id, s).first;
    }
    stack.push_back(it->second);
    it->second->scroll_reset();
    it->second->on_show();
    dirty = true;
}

void App::back() {
    if (stack.size() > 1) {
        stack.pop_back();
        dirty = true;
        return;
    }
    if (!stack.empty() && std::strcmp(stack.back()->id(), "home")) {
        open("home");
    }
}

void App::toast(const char *text, unsigned ms) {
    toast_text = text ? text : "";
    toast_until = now + ms;
    dirty = true;
}

void App::render() {
    if (!cv || stack.empty()) return;
    const bool stats = std::getenv("GRAPHITE_RENDER_STATS") != nullptr;
    unsigned t0 = stats ? ms_now() : 0;
    if (stack.size() > 1 && stack.back()->overlay())
        stack[stack.size() - 2]->draw(*g);
    ScreenView *screen = stack.back();
    int st = screen->scroll_top(), sb = screen->scroll_bottom();
    int vh = sb - st;
    int maxs = screen->scrollable() ? std::max(0, screen->content_height(vh) - vh) : 0;
    screen->scroll_y = std::max(0, std::min(screen->scroll_y, maxs));
    int preview = 0;
    if (const char *p = std::getenv("GRAPHITE_SCROLL_PREVIEW")) preview = std::max(0, std::atoi(p));
    int draw_scroll = screen->scrollable() && preview ? std::min(preview, maxs) : screen->scroll_y;
    if (screen->scrollable()) g->begin_body(st, sb, draw_scroll);
    ui_header_back = false;   // only the top screen's header counts
    screen->draw(*g);
    if (screen->scrollable()) {
        g->end_body();
        if (maxs > 0) {
            int thumb = std::max(16, vh * vh / screen->content_height(vh));
            int ty = st + (vh - thumb) * draw_scroll / maxs;
            g->rect(W - 3, ty, 2, thumb, theme.line);
        }
    }
    ui_status(*g);
    draw_launch_overlay(*g);
    if (!toast_text.empty() && (int)(toast_until - now) > 0) ui_toast(*g, toast_text.c_str());

    unsigned t1 = stats ? ms_now() : 0;

    int bpp = img->bits_per_pixel;
    if (depth == 16 && bpp == 16) {
        for (int y = 0; y < H; y++) {
            uint16_t *dst = (uint16_t *)(img->data + (size_t)y * img->bytes_per_line);
            std::memcpy(dst, cv->px.data() + (size_t)y * W, (size_t)W * 2);
        }
    } else {
        for (int y = 0; y < H; y++) {
            uint32_t *dst = (uint32_t *)(img->data + (size_t)y * img->bytes_per_line);
            for (int x = 0; x < W; x++) {
                uint16_t v = cv->px[(size_t)y * W + x];
                int r = ((v >> 11) & 31) * 255 / 31;
                int g2 = ((v >> 5) & 63) * 255 / 63;
                int b = (v & 31) * 255 / 31;
                dst[x] = ((uint32_t)r << 16) | ((uint32_t)g2 << 8) | (uint32_t)b;
            }
        }
    }
    unsigned t1b = stats ? ms_now() : 0;
    if (shm_ok) {
        XShmPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H, False);
        XSync(dpy, False);   // do not rewrite the segment while X reads it
    } else {
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
        XFlush(dpy);
    }
    if (stats) {
        unsigned t2 = ms_now();
        std::fprintf(stderr, "render draw=%u copy=%u put=%u text=%.0f textw=%.0f\n",
                     t1 - t0, t1b - t1, t2 - t1b, gfx_text_us / 1000.0, gfx_textw_us / 1000.0);
        gfx_text_us = gfx_textw_us = 0;
    }
    dirty = false;
}

void App::handle_event(XEvent &ev) {
    bool lock_top = top() && !std::strcmp(top()->id(), "lock");
    const bool press_like = ev.type == ButtonPress || ev.type == KeyPress;
    // Every real interaction is activity: taps, released touches, drags and
    // keys. Before this, only KeyPress reached power_policy.input(), so the
    // screen dimmed 10 s after the last key even while the user kept tapping.
    const bool activity = press_like || ev.type == ButtonRelease ||
                          (ev.type == MotionNotify && down);
    if (lock_top && lock_auth::enabled() && press_like) {
        if (power_policy.state == PowerState::Locked) {
            if (!power_policy.wake(now)) { toast("Ошибка записи подсветки"); return; }
            dirty = true;
        }
        // The PIN screen owns input. Keys may wake the backlight but must
        // never reach Home/shade; taps fall through to the PIN keypad.
        if (ev.type == KeyPress) return;
    } else if (power_policy.state == PowerState::Locked) {
        if (!press_like) {
            // A release/drag of an old gesture must not wake the screen.
        } else if (!power_policy.wake(now)) {
            toast("Ошибка записи подсветки");
            return;
        } else {
            dirty = true;
            if (lock_top) open("home");
            return;
        }
    } else if (activity && !power_policy.input(now)) {
        toast("Ошибка записи подсветки");
    }
    if (osk_event(ev)) return;
    switch (ev.type) {
    case Expose:
        if (ev.xexpose.count == 0) dirty = true;
        break;
    case ButtonPress:
        if (ev.xbutton.button == Button1) {
            down = true;
            long_fired = false;
            pulled = false;
            scrolling = false;
            down_x = cur_x = ev.xbutton.x;
            down_y = cur_y = ev.xbutton.y;
            down_at = now;
        }
        break;
    case MotionNotify: {
        int x = ev.xmotion.x, y = ev.xmotion.y;
        if (apphost_launching()) break;   // the launch overlay owns gestures
        if (down && !pulled && down_x < 20 && x - down_x > 60 &&
            std::abs(y - down_y) < 50) {
            pulled = true;
            back();
            dirty = true;
            cur_x = x; cur_y = y;
            break;
        }
        if (down && !pulled && down_y < 40 && y - down_y > 60) {
            pulled = true;
            push("shade");
            dirty = true;
            cur_x = x; cur_y = y;
            break;
        }
        if (down && top() && !pulled) {
            ScreenView *s = top();
            int dx = x - cur_x, dy = y - cur_y;
            bool body = y >= s->scroll_top() && y < s->scroll_bottom();
            if (s->scrollable() && down_x >= 20 && body &&
                !s->scroll_gesture_excluded(down_x, down_y) &&
                (scrolling || std::abs(y - down_y) > std::abs(x - down_x))) {
                scrolling = true;
                int vh = s->scroll_bottom() - s->scroll_top();
                int mx = std::max(0, s->content_height(vh) - vh);
                int next = std::max(0, std::min(mx, s->scroll_y - dy));
                if (next != s->scroll_y) { s->scroll_y = next; dirty = true; }
            } else if (s->drag(x, y, dx, dy)) dirty = true;
        }
        cur_x = x; cur_y = y;
        break;
    }
    case ButtonRelease:
        if (ev.xbutton.button == Button1 && down) {
            down = false;
            scrolling = false;
            int x = ev.xbutton.x, y = ev.xbutton.y;
            if (!long_fired && std::abs(x - down_x) < 14 && std::abs(y - down_y) < 14) {
                if (apphost_launching()) {
                    if (ov_hit(x, y, kOvX, kOvY, kOvW, kOvH)) apphost_close();
                    break;
                }
                if (apphost_window_visible() && y < 40) {
                    if (x >= 292 && x <= 344) osk_toggle();
                    break;
                }
                // The header back arrow is chrome, not body: handle it here so
                // every screen with ui_header(..., true) gets a working arrow.
                if (ui_header_back && x < 64 && y >= 32 && y < 96) {
                    back();
                    dirty = true;
                    break;
                }
                if (top()) {
                    ScreenView *s = top();
                    int ty = y;
                    if (s->scrollable() && y >= s->scroll_top() && y < s->scroll_bottom()) ty += s->scroll_y;
                    if (s->tap(x, ty)) dirty = true;
                }
            }
        }
        break;
    case KeyPress:
    case KeyRelease: {
        if (ev.type == KeyRelease) {
            // drop auto-repeat release pairs so screens see clean presses
            if (XPending(dpy)) {
                XEvent next;
                XPeekEvent(dpy, &next);
                if (next.type == KeyPress && next.xkey.time == ev.xkey.time &&
                    next.xkey.keycode == ev.xkey.keycode)
                    break;
            }
        }
        KeySym ks = XLookupKeysym(&ev.xkey, 0);
        bool press = ev.type == KeyPress;

        static bool kdbg = std::getenv("GRAPHITE_KEYDEBUG") != nullptr;
        if (kdbg && (ks == XK_Home || ks == XF86XK_HomePage ||
                     ks == XF86XK_PowerOff)) {
            FILE *f = std::fopen("/tmp/keys.log", "a");
            if (f) {
                std::fprintf(f, "%u %s ks=0x%lx held=%d long=%d\n", now,
                             press ? "down" : "up", (unsigned long)ks,
                             key_sym, (int)key_long_fired);
                std::fclose(f);
            }
        }

        // event0 (touch panel) and event1 (keypad) both report the Home key
        static unsigned dup_at = 0;
        static int dup_sym = 0;
        bool duplicable = ks == XK_Home || ks == XF86XK_HomePage || ks == XF86XK_Back ||
                          ks == XF86XK_MenuKB || ks == XF86XK_PowerOff;
        if (press && duplicable && dup_sym == (int)ks && now - dup_at < 120)
            break;
        if (press && duplicable) { dup_at = now; dup_sym = (int)ks; }

        bool handled = false;
        // Release of a tracked Home/Power key. This has to run wherever the key
        // was pressed: over a foreign window, on a shell screen or on the tasks
        // screen. The old check required a visible app window, so the release
        // was dropped on shell screens: key_sym stayed set and every later Home
        // press looked like autorepeat (long press re-opened tasks forever).
        if (ev.type == KeyRelease && key_sym && (int)ks == key_sym) {
            int sym = key_sym;
            key_sym = 0;
            // The hold time must come from the X timestamps, not from when the
            // loop happened to run the events: a busy shell can process the
            // whole press/release pair in one batch and lose the long press.
            unsigned hold = (key_time && ev.xkey.time > key_time)
                                ? ev.xkey.time - key_time : 0;
            bool fired_tick = key_long_fired;
            key_long_fired = false;
            key_time = 0;
            if (lock_top && lock_auth::enabled()) break;   // the PIN screen owns it
            if (fired_tick) break;                         // tick already ran it
            if (hold >= 650) {                             // a hold the loop missed
                key_long_action(sym);
                break;
            }
            if (sym == XK_Home || sym == XF86XK_HomePage) {
                if (apphost_active()) apphost_suspend();
                open("home");
            } else if (sym == XF86XK_PowerOff) {
                if (apphost_active()) apphost_suspend();
                if (power_policy.state == PowerState::Locked) {
                    power_policy.wake(now);
                    open(lock_auth::enabled() ? "lock" : "home");
                } else if (power_policy.lock(now)) {
                    open("lock");
                } else {
                    toast("Ошибка записи подсветки");
                }
            }
            break;
        }
        if (apphost_window_visible()) {
            apphost_note_key((int)ks, press);
            if (press) {
                if (ks == XF86XK_Back) {
                    if (osk_visible()) { osk_hide(); break; }
                    if (apphost_back()) break;   // dillo/netsurf: page back
                    apphost_close();
                    break;
                }
                if (ks == XK_Escape) {
                    if (osk_visible()) { osk_hide(); break; }
                    apphost_close();
                    break;
                }
                if (ks == XK_Home || ks == XF86XK_HomePage) {
                    if (key_sym == (int)ks) break;
                    key_sym = (int)ks;
                    key_at = now;
                    key_time = ev.xkey.time;
                    key_long_fired = false;
                    break;
                }
                if (ks == XF86XK_PowerOff) {
                    if (key_sym == (int)ks) break;
                    key_sym = (int)ks;
                    key_at = now;
                    key_time = ev.xkey.time;
                    key_long_fired = false;
                    break;
                }
            }
            break;   // never feed app keystrokes to the shell screen below
        }
        if (top()) handled = top()->key((int)ks, press);
        if (handled) { dirty = true; break; }

        if (press) {
            if (ks == XK_Escape || ks == XK_BackSpace ||
                ks == XF86XK_Back || ks == XF86XK_Forward) {
                back();
                break;
            }
            if (ks == XF86XK_MenuKB || ks == XK_Menu) {
                if (top() && !std::strcmp(top()->id(), "menu")) back();
                else push("menu");
                break;
            }
            if (ks == XF86XK_Search) { open("apps"); break; }

            if (ks == XK_Home || ks == XF86XK_HomePage ||
                ks == XF86XK_PowerOff) {
                if (key_sym == (int)ks) break;   // autorepeat, keep the hold time
                key_sym = (int)ks;
                key_at = now;
                key_time = ev.xkey.time;
                key_long_fired = false;
                break;
            }

            auto &m = mpd_client();
            if (ks == XF86XK_AudioRaiseVolume || ks == XF86XK_AudioLowerVolume ||
                ks == XF86XK_AudioMute) {
                if (m.connected()) {
                    static int prev_vol = 60;
                    int v = m.status().volume;
                    if (ks == XF86XK_AudioMute) {
                        if (v > 0) { prev_vol = v; v = 0; }
                        else v = prev_vol > 0 ? prev_vol : 60;
                    } else {
                        v += (ks == XF86XK_AudioRaiseVolume) ? 3 : -3;
                        if (v < 0) v = 0;
                        if (v > 100) v = 100;
                    }
                    m.setvol(v);
                    char b[32];
                    std::snprintf(b, sizeof(b), "VOL %d%%", v);
                    toast(b, 1200);
                }
                break;
            }
            break;
        }

        // release of Home / Power is handled once, above, for every screen
        break;
    }
    case MapNotify:
        if (apphost_on_map(ev.xmap.window)) dirty = true;
        break;
    case DestroyNotify:
        if (ev.xdestroywindow.window == win) running = false;
        else {
            apphost_on_destroy(ev.xdestroywindow.window);
            if (osk_visible()) osk_hide();
        }
        break;
    case UnmapNotify:
        if (ev.xunmap.window != win) apphost_on_unmap(ev.xunmap.window);
        break;
    default:
        break;
    }
}

void App::key_long_action(int sym) {
    if (sym == XF86XK_PowerOff) {
        if (apphost_active()) apphost_suspend();
        open("power");
    } else if (sym == XK_Home || sym == XF86XK_HomePage) {
        if (apphost_window_visible()) {
            apphost_suspend();
            open("home");
        }
        push("tasks");
    }
}

void App::run() {
    int fd = ConnectionNumber(dpy);
    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            now = ms_now();
            handle_event(ev);
        }
        now = ms_now();
        apphost_tick(now);
        int unread_before = notify_unread();
        notify_tick(now);
        if (notify_unread() != unread_before) dirty = true;
        if (power_policy.tick(now)) {
            if (power_policy.state == PowerState::Locked) open("lock");
            dirty = true;
        }

        if (down && !long_fired && now - down_at >= 650) {
            long_fired = true;
            if (top() && top()->longtap(down_x, down_y)) dirty = true;
        }
        if (key_sym && !key_long_fired && now - key_at >= 650) {
            key_long_fired = true;
            if (std::getenv("GRAPHITE_KEYDEBUG")) {
                FILE *f = std::fopen("/tmp/keys.log", "a");
                if (f) { std::fprintf(f, "%u long sym=%d\n", now, key_sym); std::fclose(f); }
            }
            bool pin_screen = top() && !std::strcmp(top()->id(), "lock") &&
                              lock_auth::enabled();
            if (pin_screen) {
                // A held key must not put the tasks screen over the PIN pad.
                key_sym = 0;
                key_long_fired = false;
            } else {
                key_long_action(key_sym);
            }
        }
        if (!toast_text.empty() && (int)(toast_until - now) <= 0) {
            toast_text.clear();
            dirty = true;
        }
        if (now - last_clock >= 10000) {
            last_clock = now;
            dirty = true;
        }
        if (top() && top()->animated() && now - last_tick >= 1000) {
            top()->tick(now);
            last_tick = now;
            dirty = true;
        }
        if (dirty) render();

        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(fd, &fs);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        select(fd + 1, &fs, nullptr, nullptr, &tv);
    }
}
