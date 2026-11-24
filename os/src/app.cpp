#include "graphite.h"
#include "mpd.h"
#include "apphost.h"
#include "osk.h"
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/select.h>

App app;

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

    cv = new Canvas(W, H);
    g = new Gfx(*cv);
    now = ms_now();
    return true;
}

ScreenView *App::top() { return stack.empty() ? nullptr : stack.back(); }

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
    if (stack.size() > 1 && stack.back()->overlay())
        stack[stack.size() - 2]->draw(*g);
    stack.back()->draw(*g);
    ui_status(*g);
    draw_launch_overlay(*g);
    if (!toast_text.empty() && (int)(toast_until - now) > 0) ui_toast(*g, toast_text.c_str());

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
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
    XFlush(dpy);
    dirty = false;
}

void App::handle_event(XEvent &ev) {
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
            if (top()->drag(x, y, x - cur_x, y - cur_y)) dirty = true;
        }
        cur_x = x; cur_y = y;
        break;
    }
    case ButtonRelease:
        if (ev.xbutton.button == Button1 && down) {
            down = false;
            int x = ev.xbutton.x, y = ev.xbutton.y;
            if (!long_fired && std::abs(x - down_x) < 14 && std::abs(y - down_y) < 14) {
                if (apphost_launching()) {
                    if (ov_hit(x, y, kOvX, kOvY, kOvW, kOvH)) apphost_close();
                    break;
                }
                if (apphost_active() && y < 40) {
                    if (x >= 292 && x <= 344) osk_toggle();
                    break;
                }
                if (top() && top()->tap(x, y)) dirty = true;
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
        if (kdbg && (ks == XK_Home || ks == XF86XK_PowerOff)) {
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
        bool duplicable = ks == XK_Home || ks == XF86XK_Back ||
                          ks == XF86XK_MenuKB || ks == XF86XK_PowerOff;
        if (press && duplicable && dup_sym == (int)ks && now - dup_at < 120)
            break;
        if (press && duplicable) { dup_at = now; dup_sym = (int)ks; }

        bool handled = false;
        if (apphost_active()) {
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
                    if (osk_visible()) osk_hide();
                    apphost_close();
                    open("home");
                    break;
                }
                if (ks == XF86XK_PowerOff) {
                    if (osk_visible()) osk_hide();
                    apphost_close();
                    open("lock");
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

        // release of Home / Power
        if (key_sym && (int)ks == key_sym) {
            int sym = key_sym;
            key_sym = 0;
            if (!key_long_fired) {
                if (sym == XF86XK_PowerOff) {
                    if (top() && !std::strcmp(top()->id(), "lock")) open("home");
                    else open("lock");
                } else if (sym == XK_Home || sym == XF86XK_HomePage) {
                    open("home");
                }
            }
            key_long_fired = false;
        }
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
        if (ev.xunmap.window != win) apphost_on_destroy(ev.xunmap.window);
        break;
    default:
        break;
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

        if (down && !long_fired && now - down_at >= 650) {
            long_fired = true;
            if (top() && top()->longtap(down_x, down_y)) dirty = true;
        }
        if (key_sym && !key_long_fired && now - key_at >= 700) {
            key_long_fired = true;
            if (std::getenv("GRAPHITE_KEYDEBUG")) {
                FILE *f = std::fopen("/tmp/keys.log", "a");
                if (f) { std::fprintf(f, "%u long sym=%d\n", now, key_sym); std::fclose(f); }
            }
            if (key_sym == XF86XK_PowerOff) open("power");
            else if (key_sym == XK_Home || key_sym == XF86XK_HomePage) push("shade");
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
