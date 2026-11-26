// graphite shell — shared API for the Xlib shell.
// Frozen: screens may use everything here; core files are owned by the core.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>

// ---------------------------------------------------------------- colors
uint16_t rgb565(int r, int g, int b);
uint16_t hex565(const char *hex);

enum Family { FAM_DARK = 0, FAM_LIGHT = 1, FAM_GREY = 2 };

struct Theme {
    const char *id;      // graphite
    const char *name;    // Графит
    const char *code;    // GRAPHITE
    int family;          // Family
    uint16_t bg, bar, surf, surf2, line, line2, art;
    uint16_t fg, dim, accent, accent2, track, sel, num, note, onacc;
};
extern Theme theme;

bool theme_init();
bool theme_set(const char *id, const char *accent);   // nullptr = keep
const char *const *theme_ids(int *n);
const char *const *accent_ids(int *n);

// ---------------------------------------------------------------- canvas
struct Canvas {
    int w, h;
    std::vector<uint16_t> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_) {}
    void clear(uint16_t c);
    void rect(int x, int y, int w, int h, uint16_t c);
    void frame(int x, int y, int w, int h, uint16_t c);
    void hline(int x, int y, int w, uint16_t c);
    void vline(int x, int y, int h, uint16_t c);
    void blend(int x, int y, uint16_t c, int a);
    void disc(int cx, int cy, int r, uint16_t c);
    void ring(int cx, int cy, int r, int thickness, uint16_t c);
    void triangle(int cx, int cy, int size, bool right, uint16_t c);
    void copy_from(const Canvas &src);
};

enum TextFlag {
    TF_CENTER = 1,
    TF_RIGHT  = 2,
    TF_MONO   = 4,
    TF_BOLD   = 8,
    TF_LIGHT  = 16
};

struct Gfx {
    Canvas &cv;
    int offset_y = 0, clip_top = 0, clip_bottom = 0;
    explicit Gfx(Canvas &c) : cv(c) {}
    void rect(int x, int y, int w, int h, uint16_t c);
    void frame(int x, int y, int w, int h, uint16_t c);
    void hline(int x, int y, int w, uint16_t c);
    void vline(int x, int y, int h, uint16_t c);
    void begin_body(int top, int bottom, int scroll);
    void end_body();
    int  text(int x, int y, const std::string &s, int px, uint16_t c, int flags = 0);
    int  textw(const std::string &s, int px, int tracking = 0);
    std::string fit(const std::string &s, int px, int maxw);
};

bool gfx_init(const char *font_dir = "/usr/share/fonts/dejavu");
void icon(Gfx &g, const char *name, int x, int y, int size, uint16_t color);

// ---------------------------------------------------------------- settings
struct Settings {
    std::map<std::string, std::string> kv;
    std::string get(const std::string &k, const std::string &def = "") const;
    int geti(const std::string &k, int def = 0) const;
    void set(const std::string &k, const std::string &v);
    bool load(const char *path = "/etc/lgtest-settings");
};
extern Settings cfg;
bool settings_save_key(const char *key, const char *value,
                       const char *path = "/etc/lgtest-settings");

// ---------------------------------------------------------------- screens
struct ScreenView {
    int scroll_y = 0;
    void scroll_reset() { scroll_y = 0; }
    virtual ~ScreenView() {}
    virtual const char *id() const = 0;
    virtual void draw(Gfx &g) = 0;
    virtual bool tap(int x, int y) { (void)x; (void)y; return false; }
    virtual bool longtap(int x, int y) { (void)x; (void)y; return false; }
    virtual bool drag(int x, int y, int dx, int dy) { (void)x; (void)y; (void)dx; (void)dy; return false; }
    virtual bool key(int ksym, bool down) { (void)ksym; (void)down; return false; }
    virtual void tick(unsigned now) { (void)now; }
    virtual bool animated() const { return false; }
    virtual bool overlay() const { return false; }
    virtual bool scrollable() const { return false; }
    virtual int content_height(int viewport_h) const { return viewport_h; }
    virtual int scroll_top() const { return 96; }
    virtual int scroll_bottom() const { return 760; }
    virtual bool scroll_gesture_excluded(int x, int y) const { (void)x; (void)y; return false; }
    virtual void on_show() {}
};

struct App {
    Display *dpy = nullptr;
    Window win = 0;
    GC gc = 0;
    XImage *img = nullptr;
    int W = 480, H = 800;
    int depth = 16;
    Canvas *cv = nullptr;
    Gfx *g = nullptr;
    XShmSegmentInfo shm{};
    bool shm_ok = false;
    std::vector<ScreenView *> stack;
    std::map<std::string, ScreenView *> cache;
    bool dirty = true;
    bool running = true;
    unsigned now = 0;
    unsigned last_tick = 0;
    unsigned last_clock = 0;
    int down_x = 0, down_y = 0, cur_x = 0, cur_y = 0;
    bool down = false;
    unsigned down_at = 0;
    bool long_fired = false;
    bool pulled = false;
    bool scrolling = false;
    unsigned key_at = 0;
    unsigned key_time = 0;   // X server timestamp of the press, for a true hold time
    int key_sym = 0;
    bool key_long_fired = false;
    std::string toast_text;
    unsigned toast_until = 0;
    std::string open_path;   // pending file to open in notes (set by files/info)
    std::string pending_file; // explicit file handoff to reader/viewer

    bool init();
    ScreenView *open(const char *id);   // push (home resets the stack)
    bool open_file(const std::string &path);
    ScreenView *replace(const char *id);// replace the current page (pivots)
    void push(const char *id);
    void back();
    ScreenView *top();
    void invalidate() { dirty = true; }
    void toast(const char *text, unsigned ms = 2200);
    void render();
    void handle_event(XEvent &ev);
    void key_long_action(int sym);
    void run();
};
extern App app;

ScreenView *screen_make(const char *id);

// ---------------------------------------------------------------- ui kit
void ui_bg(Gfx &g);
void ui_status(Gfx &g);
int  ui_header(Gfx &g, const char *title, const char *sub, bool back);
extern bool ui_header_back;   // set by ui_header(..., back=true) during draw
int  ui_section(Gfx &g, int y, const char *label);
int  ui_row(Gfx &g, int y, const char *name, const char *sub,
            const char *value, bool chevron, bool accent);
int  ui_row_h();
void ui_switch(Gfx &g, int x, int y, bool on);
void ui_slider(Gfx &g, int x, int y, int w, int pct,
               const char *label, const char *value);
void ui_seg(Gfx &g, int x, int y, int w, const char *const *items, int n, int cur);
void ui_chevron(Gfx &g, int x, int y, uint16_t c);
void ui_toast(Gfx &g, const char *text);
void ui_tile(Gfx &g, int x, int y, int w, int h, const char *icon_name,
             const char *label, const char *meta, bool live);
