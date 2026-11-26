// osk — on-screen keyboard for foreign X apps (xterm, netsurf, GTK).
// Separate override-redirect window at the bottom, does not take focus;
// keys are injected with XTest. Three layers: letters, digits/symbols and a
// terminal layer (Esc, Tab, Ctrl, Alt, arrows, Home/End, PgUp/PgDn).
// Ctrl/Alt are sticky for exactly one following key — tap CTRL, then C, and
// the app sees ^C. Nothing here blocks drawing: XTest runs on tap only.
#include "graphite.h"
#include "apphost.h"
#include "osk.h"

#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int W = 480, H = 288;
const int M = 8, GAP = 4, RH = 64;
const int TOP = 512;

enum { A_CHAR = 0, A_SHIFT, A_LAYER, A_FN, A_LANG, A_SPACE, A_DEL, A_ENTER,
       A_SPECIAL, A_MOD };
enum { MOD_CTRL = 1, MOD_ALT = 2 };

struct Key {
    int x, y, w, h;
    std::string label;
    uint32_t cp;     // character for A_CHAR, 1/2 for A_MOD
    int act;
    int sym;         // keysym for A_SPECIAL
    int mods;        // inherent modifiers for A_SPECIAL (^C etc.)
};

Display *dpy = nullptr;
Window win = 0;
Canvas *cv = nullptr;
Gfx *gfx = nullptr;
XImage *img = nullptr;
bool visible = false;
bool ru = true;
bool shift = false;
int layer = 0;             // 0 letters, 1 digits, 2 terminal
bool ctrl_armed = false;   // sticky for one key
bool alt_armed = false;    // sticky for one key
uint32_t mapped_cp = 0;    // what the scratch keycode is mapped to now
std::vector<Key> keys;

std::string u8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

uint32_t upper_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
    if (cp == 0x451) return 0x401;
    return cp;
}

uint32_t lower_cp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp == 0x401) return 0x451;
    return cp;
}

// Ctrl/Alt are keyed on a *Latin* keysym: xterm turns Ctrl into a control code
// only for ASCII. So a Russian letter is translated by physical position
// (ЙЦУКЕН -> QWERTY): Ctrl + С gives ^C, Ctrl + З gives ^P, exactly as on a
// real keyboard with the RU layout.
uint32_t latin_of(uint32_t cp) {
    cp = lower_cp(cp);
    if (cp < 0x80) return cp;
    static const struct { const char *ru, *en; } map[] = {
        {"йцукенгшщзхъ", "qwertyuiop[]"},
        {"фывапролджэ",  "asdfghjkl;'"},
        {"ячсмитьбю.",   "zxcvbnm,./"},
        {"ё",            "`"},
    };
    std::string s = u8(cp);
    for (const auto &m : map) {
        const char *p = std::strstr(m.ru, s.c_str());
        if (!p) continue;
        size_t ci = 0;
        for (const char *q = m.ru; q < p; q++)
            if (((unsigned char)*q & 0xC0) != 0x80) ci++;
        char e = m.en[ci];
        if (e) return (uint32_t)(unsigned char)e;
    }
    return cp;
}

void add_key(int x, int y, int w, const char *label, uint32_t cp, int act,
             int sym = 0, int mods = 0) {
    Key k;
    k.x = x; k.y = y; k.w = w; k.h = RH;
    k.label = label ? label : u8(cp);
    k.cp = cp; k.act = act; k.sym = sym; k.mods = mods;
    keys.push_back(k);
}

void add_row(const std::vector<uint32_t> &cps, int y, int x0, int width,
             const char *left_label = nullptr, int left_act = -1, int left_w = 0) {
    int n = (int)cps.size();
    int x = x0;
    if (left_label) {
        add_key(x, y, left_w, left_label, 0, left_act);
        x += left_w + GAP;
        width -= left_w + GAP;
    }
    int kw = (width - (n - 1) * GAP) / n;
    for (int i = 0; i < n; i++) {
        std::string lab = u8(cps[i]);
        if (shift) lab = u8(upper_cp(cps[i]));
        add_key(x, y, kw, lab.c_str(), cps[i], A_CHAR);
        x += kw + GAP;
    }
}

struct KDef { const char *label; int act; uint32_t cp; int sym; int mods; };

void add_defs(const KDef *d, int n, int y, int x0, int width) {
    int kw = (width - (n - 1) * GAP) / n;
    int extra = width - (kw * n + GAP * (n - 1));
    int x = x0;
    for (int i = 0; i < n; i++) {
        Key k;
        k.x = x; k.y = y; k.h = RH;
        k.w = kw + (i < extra ? 1 : 0);
        k.label = d[i].label;
        k.cp = d[i].cp; k.act = d[i].act;
        k.sym = d[i].sym; k.mods = d[i].mods;
        keys.push_back(k);
        x += k.w + GAP;
    }
}

void build() {
    keys.clear();
    const int x0 = M, width = W - 2 * M;

    if (layer == 0) {
        if (ru) {
            add_row({0x439,0x446,0x443,0x43A,0x435,0x43D,0x433,0x448,0x449,0x437,0x445}, 8, x0, width);
            add_row({0x444,0x44B,0x432,0x430,0x43F,0x440,0x43E,0x43B,0x434,0x436,0x44D}, 76, x0, width);
            add_row({0x44F,0x447,0x441,0x43C,0x438,0x442,0x44C,0x431,0x44E,0x451}, 144, x0, width,
                    "SHIFT", A_SHIFT, 72);
        } else {
            add_row({'q','w','e','r','t','y','u','i','o','p'}, 8, x0, width);
            add_row({'a','s','d','f','g','h','j','k','l'}, 76, x0, width);
            add_row({'z','x','c','v','b','n','m'}, 144, x0, width,
                    "SHIFT", A_SHIFT, 72);
        }
    } else if (layer == 1) {
        add_row({'1','2','3','4','5','6','7','8','9','0'}, 8, x0, width);
        add_row({'!','@','#','$','%','^','&','*','(',')'}, 76, x0, width);
        add_row({'-','_','=','+','/',':',';',',','.','?'}, 144, x0, width,
                "SHIFT", A_SHIFT, 72);
    } else {
        // terminal layer: everything a shell needs that letters cannot make
        static const KDef r0[] = {
            {"ESC",  A_SPECIAL, 0, XK_Escape, 0},
            {"TAB",  A_SPECIAL, 0, XK_Tab,    0},
            {"CTRL", A_MOD,     1, 0,         0},
            {"ALT",  A_MOD,     2, 0,         0},
            {"←",    A_SPECIAL, 0, XK_Left,   0},
            {"↑",    A_SPECIAL, 0, XK_Up,     0},
            {"↓",    A_SPECIAL, 0, XK_Down,   0},
            {"→",    A_SPECIAL, 0, XK_Right,  0},
        };
        static const KDef r1[] = {
            {"HOME", A_SPECIAL, 0, XK_Home,     0},
            {"END",  A_SPECIAL, 0, XK_End,      0},
            {"PGUP", A_SPECIAL, 0, XK_Page_Up,  0},
            {"PGDN", A_SPECIAL, 0, XK_Page_Down,0},
            {"|",    A_CHAR,    '|', 0, 0},
            {"~",    A_CHAR,    '~', 0, 0},
            {"/",    A_CHAR,    '/', 0, 0},
            {"-",    A_CHAR,    '-', 0, 0},
        };
        static const KDef r2[] = {
            {"^C",   A_SPECIAL, 0, XK_c, MOD_CTRL},
            {"^D",   A_SPECIAL, 0, XK_d, MOD_CTRL},
            {"^Z",   A_SPECIAL, 0, XK_z, MOD_CTRL},
            {"^L",   A_SPECIAL, 0, XK_l, MOD_CTRL},
            {"DEL",  A_SPECIAL, 0, XK_Delete, 0},
            {"_",    A_CHAR,    '_', 0, 0},
            {"$",    A_CHAR,    '$', 0, 0},
            {"?",    A_CHAR,    '?', 0, 0},
        };
        add_defs(r0, 8, 8, x0, width);
        add_defs(r1, 8, 76, x0, width);
        add_defs(r2, 8, 144, x0, width);
    }

    // bottom row: layer | lang | space | DEL | FN | ENTER
    int y = 212, x = x0;
    const char *lay = layer == 1 ? "АБВ" : "123";
    add_key(x, y, 46, lay, 0, A_LAYER); x += 46 + GAP;
    add_key(x, y, 46, ru ? "ENG" : "РУС", 0, A_LANG); x += 46 + GAP;
    int spacew = (W - M) - x - (46 + GAP) - (46 + GAP) - (101 + GAP);
    add_key(x, y, spacew, "ПРОБЕЛ", 0x20, A_SPACE); x += spacew + GAP;
    add_key(x, y, 46, "DEL", 0, A_DEL); x += 46 + GAP;
    add_key(x, y, 46, layer == 2 ? "АБВ" : "FN", 0, A_FN); x += 46 + GAP;
    add_key(x, y, 101, "ВВОД", 0, A_ENTER);
}

void put_screen() {
    if (!img || !cv) return;
    if (img->bits_per_pixel == 16) {
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
                int gg = ((v >> 5) & 63) * 255 / 63;
                int b = (v & 31) * 255 / 31;
                dst[x] = ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
            }
        }
    }
    XPutImage(dpy, win, DefaultGC(dpy, DefaultScreen(dpy)), img, 0, 0, 0, 0, W, H);
    XFlush(dpy);
}

void draw() {
    if (!cv || !gfx) return;
    cv->clear(theme.bg);
    build();
    for (const Key &k : keys) {
        bool enter = k.act == A_ENTER;
        bool on = (k.act == A_SHIFT && shift) ||
                  (k.act == A_LAYER && layer == 1) ||
                  (k.act == A_FN && layer == 2) ||
                  (k.act == A_MOD && ((k.cp == 1 && ctrl_armed) ||
                                      (k.cp == 2 && alt_armed)));
        gfx->rect(k.x, k.y, k.w, k.h, enter ? theme.accent : (on ? theme.sel : theme.surf));
        uint16_t c = enter ? theme.onacc : (on ? theme.accent : theme.fg);
        int px = (k.act == A_CHAR || k.w > 60) ? 16 : 13;
        if (k.w < 45) px = 15;
        gfx->text(k.x + k.w / 2, k.y + (k.h - px) / 2 - 2, k.label, px, c, TF_CENTER);
    }
    put_screen();
}

int scratch_keycode() {
    static int kc = -1;
    if (kc > 0) return kc;
    int min = 8, max = 255;
    XDisplayKeycodes(dpy, &min, &max);
    kc = max;
    return kc;
}

void scratch_sym(KeySym sym) {
    int kc = scratch_keycode();
    XChangeKeyboardMapping(dpy, kc, 1, &sym, 1);
    XSync(dpy, False);
    mapped_cp = 0;
}

void tap_keycode(KeyCode kc) {
    XTestFakeKeyEvent(dpy, kc, True, 0);
    XTestFakeKeyEvent(dpy, kc, False, 0);
}

// One keypress with real Ctrl/Alt held around it. Modifiers are pressed
// before and released after the key, so xterm turns Ctrl+C into 0x03.
void send_sym(KeySym sym, int mods) {
    if (!dpy) return;
    KeyCode ck = (mods & MOD_CTRL) ? XKeysymToKeycode(dpy, XK_Control_L) : 0;
    KeyCode ak = (mods & MOD_ALT) ? XKeysymToKeycode(dpy, XK_Alt_L) : 0;
    KeyCode kc = XKeysymToKeycode(dpy, sym);
    if (!kc) {
        // Some servers trim rarely used keysyms from the map.
        scratch_sym(sym);
        kc = scratch_keycode();
    }
    if (ck) XTestFakeKeyEvent(dpy, ck, True, 0);
    if (ak) XTestFakeKeyEvent(dpy, ak, True, 0);
    tap_keycode(kc);
    if (ak) XTestFakeKeyEvent(dpy, ak, False, 0);
    if (ck) XTestFakeKeyEvent(dpy, ck, False, 0);
    XFlush(dpy);
    apphost_focus_app();
}

void type_cp(uint32_t cp, int mods) {
    if (!cp) return;
    if (mods) {
        uint32_t l = latin_of(cp);
        KeySym sym = l < 0x100 ? (KeySym)l : (KeySym)(0x01000000u | l);
        send_sym(sym, mods);
        return;
    }
    int kc = scratch_keycode();
    if (mapped_cp != cp) {
        KeySym sym = cp < 0x100 ? (KeySym)cp : (KeySym)(0x01000000u | cp);
        XChangeKeyboardMapping(dpy, kc, 1, &sym, 1);
        XSync(dpy, False);
        mapped_cp = cp;
    }
    tap_keycode(kc);
    XFlush(dpy);
    apphost_focus_app();
}

int sticky() {
    return (ctrl_armed ? MOD_CTRL : 0) | (alt_armed ? MOD_ALT : 0);
}

void clear_sticky() { ctrl_armed = alt_armed = false; }

void press(const Key &k) {
    switch (k.act) {
    case A_CHAR: {
        uint32_t cp = shift ? upper_cp(k.cp) : k.cp;
        type_cp(cp, sticky());
        clear_sticky();
        shift = false;
        break;
    }
    case A_SPECIAL:
        send_sym((KeySym)k.sym, sticky() | k.mods);
        clear_sticky();
        break;
    case A_MOD:
        if (k.cp == 1) ctrl_armed = !ctrl_armed;
        else alt_armed = !alt_armed;
        break;
    case A_SHIFT: shift = !shift; break;
    case A_LAYER: layer = layer == 1 ? 0 : 1; shift = false; break;
    case A_FN: layer = layer == 2 ? 0 : 2; shift = false; break;
    case A_LANG: ru = !ru; shift = false; break;
    case A_SPACE: type_cp(0x20, sticky()); clear_sticky(); break;
    case A_DEL: send_sym(XK_BackSpace, sticky()); clear_sticky(); break;
    case A_ENTER: send_sym(XK_Return, sticky()); clear_sticky(); break;
    default: break;
    }
    draw();
}

void create() {
    if (win) return;
    dpy = app.dpy;
    int scr = DefaultScreen(dpy);
    XSetWindowAttributes at;
    std::memset(&at, 0, sizeof(at));
    at.override_redirect = True;
    at.background_pixel = BlackPixel(dpy, scr);
    at.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, TOP, W, H, 0,
                        CopyFromParent, InputOutput, DefaultVisual(dpy, scr),
                        CWOverrideRedirect | CWBackPixel | CWEventMask, &at);
    img = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr),
                       ZPixmap, 0, nullptr, W, H, 32, 0);
    img->data = (char *)std::calloc((size_t)img->bytes_per_line * H, 1);
    cv = new Canvas(W, H);
    gfx = new Gfx(*cv);
}

void focus_back() { apphost_focus_app(); }

bool str_has(const char *s, const char *needle) {
    if (!s) return false;
    size_t n = std::strlen(needle);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               std::tolower((unsigned char)p[i]) ==
               std::tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

// xterm keeps the input focus on its VT100 child window, so walk up.
bool class_has(Window w, const char *needle) {
    for (int depth = 0; w && depth < 8; depth++) {
        XClassHint ch;
        std::memset(&ch, 0, sizeof(ch));
        bool hit = false;
        if (XGetClassHint(dpy, w, &ch)) {
            hit = str_has(ch.res_name, needle) || str_has(ch.res_class, needle);
            if (ch.res_name) XFree(ch.res_name);
            if (ch.res_class) XFree(ch.res_class);
        }
        if (hit) return true;
        Window root = 0, parent = 0, *kids = nullptr;
        unsigned n = 0;
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &n)) break;
        if (kids) XFree(kids);
        if (!parent || parent == root) break;
        w = parent;
    }
    return false;
}

bool focused_is_xterm() {
    if (!dpy) return false;
    Window w = 0;
    int rev = 0;
    XGetInputFocus(dpy, &w, &rev);
    if (!w || w == PointerRoot || w == None) return false;
    return class_has(w, "xterm");
}

// Debug-only preview: GS_OSK_PREVIEW=1 draws the letters layer, =123 the
// digits layer, =term the terminal layer. Used by preview.sh on the host;
// a no-op on the phone.
void preview() {
    static bool done = false;
    if (done) return;
    done = true;
    const char *e = std::getenv("GS_OSK_PREVIEW");
    if (!e || !*e || e[0] == '0') return;
    if (visible || apphost_active() || !app.dpy) return;
    if (!std::strcmp(e, "term") || !std::strcmp(e, "fn")) layer = 2;
    else if (!std::strcmp(e, "123") || !std::strcmp(e, "digits")) layer = 1;
    create();
    visible = true;
    XMapRaised(dpy, win);
    draw();
    app.dirty = true;
}

} // namespace

bool osk_visible() { return visible; }

void osk_hide() {
    if (!visible) return;
    visible = false;
    clear_sticky();   // a hidden keyboard must not keep Ctrl armed
    shift = false;
    if (win) XUnmapWindow(dpy, win);
    apphost_set_content_height(768);
    focus_back();
    app.dirty = true;
}

void osk_toggle() {
    if (!apphost_window_visible()) {
        app.toast("Клавиатура только для внешних приложений");
        return;
    }
    if (visible) { osk_hide(); return; }
    create();
    // xterm gets the terminal layer straight away: Esc, Tab, Ctrl+letters,
    // arrows — no extra taps.
    if (focused_is_xterm()) { layer = 2; shift = false; }
    visible = true;
    XMapRaised(dpy, win);
    apphost_set_content_height(480);
    draw();
    focus_back();
    app.dirty = true;
}

bool osk_event(XEvent &ev) {
    preview();
    if (!win || !visible) return false;
    if (ev.type == Expose && ev.xexpose.window == win) { draw(); return true; }
    if (ev.xany.window != win) return false;
    if (ev.type == ButtonPress) return true;
    if (ev.type == ButtonRelease) {
        int x = ev.xbutton.x, y = ev.xbutton.y;
        for (const Key &k : keys)
            // Expand only the hit rectangle into half of each 4 px gutter;
            // adjacent rectangles meet at a shared boundary without overlap.
            if (x >= k.x - GAP / 2 && x < k.x + k.w + (GAP - GAP / 2) &&
                y >= k.y && y < k.y + k.h) {
                press(k);
                break;
            }
        return true;
    }
    return false;
}
