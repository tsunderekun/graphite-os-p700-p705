#include "graphite.h"
#include "screens.h"
#include "image_loader.h"

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace {
const int kPhotoTop = 72;   // below the status bar and the 40 px title strip
struct Picture { std::string path, name; unsigned long long size = 0; };
bool supported(const char *s) {
    std::string n(s);
    auto dot = n.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = n.substr(dot);
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}
std::string size_text(unsigned long long n) {
    char b[32];
    if (n < 1024) std::snprintf(b, sizeof b, "%llu Б", n);
    else if (n < 1024 * 1024) std::snprintf(b, sizeof b, "%llu КБ", n / 1024);
    else std::snprintf(b, sizeof b, "%.1f МиБ", n / 1048576.0);
    return b;
}
bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

struct Viewer : ScreenView {
    std::vector<Picture> files;
    size_t selected = 0, scroll = 0;
    bool viewing = false, failed = false, external = false;
    Image image;
    std::string error;
    ~Viewer() override { image_free(image); }
    const char *id() const override { return "viewer"; }
    void scan() {
        files.clear();
        const char *dirs[] = {"/home/user/Pictures", "/media/sd/Pictures"};
        for (const char *dir : dirs) {
            DIR *d = opendir(dir);
            if (!d) continue;
            while (dirent *e = readdir(d)) {
                if (!supported(e->d_name)) continue;
                std::string p = std::string(dir) + "/" + e->d_name;
                struct stat st;
                if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                    files.push_back({p, e->d_name, (unsigned long long)st.st_size});
            }
            closedir(d);
        }
        std::sort(files.begin(), files.end(), [](const Picture &a, const Picture &b) {
            return a.name < b.name;
        });
        if (selected >= files.size()) selected = files.empty() ? 0 : files.size() - 1;
    }
    void open(size_t i) {
        if (i >= files.size()) return;
        selected = i;
        image_free(image);
        failed = !image_load(files[i].path, image, error);
        viewing = true;
    }
    void on_show() override {
        if (!app.pending_file.empty()) {
            std::string p = app.pending_file; app.pending_file.clear();
            external = true; files.clear(); files.push_back({p, p.substr(p.find_last_of('/') == std::string::npos ? 0 : p.find_last_of('/') + 1), 0});
            open(0); return;
        }
        if (!viewing) scan();
        // Host-only preview hook: renders the photo view for QA without a tap.
        if (const char *pv = std::getenv("GRAPHITE_VIEWER_PREVIEW")) {
            if (!viewing && *pv) open((size_t)std::strtoul(pv, nullptr, 10));
        }
    }
    void back_to_shelf() { if (external) { external = false; app.back(); return; } viewing = false; failed = false; image_free(image); }
    void draw(Gfx &g) override {
        if (!viewing) {
            ui_bg(g);
            ui_header(g, "Изображения", "Pictures · SD", true);
            if (files.empty()) {
                g.text(240, 310, "Нет изображений", 16, theme.dim, TF_CENTER);
                g.text(240, 342, "Положите файлы в Pictures", 12, theme.dim, TF_CENTER);
                return;
            }
            for (size_t i = scroll; i < files.size() && i < scroll + 12; ++i) {
                int y = 96 + (int)(i - scroll) * 56;
                g.hline(24, y, 432, theme.line2);
                icon(g, "grid", 24, y + 16, 24, theme.accent);
                g.text(60, y + 8, g.fit(files[i].name, 15, 310), 15, theme.fg);
                g.text(60, y + 31, size_text(files[i].size), 11, theme.dim);
            }
            return;
        }
        g.rect(0, 0, 480, 800, theme.bg);
        if (failed) {
            g.text(240, 360, g.fit(error, 14, 420), 14, theme.fg, TF_CENTER);
            g.rect(168, 420, 144, 48, theme.surf);
            g.text(240, 438, "К списку", 14, theme.fg, TF_CENTER);
            return;
        }
        // Wide photos are shown rotated: the user turns the phone and the
        // picture fills 800x480 instead of a thin strip. Bars are hidden in
        // this mode; taps are mapped back into the rotated coordinates.
        if (image.width > image.height) {
            ImageFit f;
            if (image_fit(image.width, image.height, 800, 480, f)) {
                for (int u = 0; u < 800; ++u) {
                    int su = u - (800 - f.width) / 2;
                    if (su < 0 || su >= f.width) continue;
                    int sx = (int)((long long)su * image.width / f.width);
                    for (int v = 0; v < 480; ++v) {
                        int sv = v - (480 - f.height) / 2;
                        if (sv < 0 || sv >= f.height) continue;
                        int sy = (int)((long long)sv * image.height / f.height);
                        const unsigned char *p = image.pixels + ((size_t)sy * image.width + sx) * 3;
                        g.cv.px[(size_t)(799 - u) * g.cv.w + v] =
                            (uint16_t)(((p[0] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[2] >> 3));
                    }
                }
            }
            return;
        }
        ImageFit fit;
        if (image_fit(image.width, image.height, 480, 688, fit)) {
            for (int y = 0; y < fit.height; ++y) {
                int sy = (int)((long long)y * image.height / fit.height);
                for (int x = 0; x < fit.width; ++x) {
                    int sx = (int)((long long)x * image.width / fit.width);
                    const unsigned char *p = image.pixels + ((size_t)sy * image.width + sx) * 3;
                    g.cv.px[(size_t)(kPhotoTop + fit.y + y) * g.cv.w + fit.x + x] =
                        (uint16_t)(((p[0] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[2] >> 3));
                }
            }
        }
        g.rect(0, 32, 480, 40, theme.bar);
        icon(g, "back", 12, 39, 18, theme.fg);
        g.text(40, 45, g.fit(files[selected].name, 12, 296), 12, theme.fg);
        g.text(464, 46, std::to_string(selected + 1) + "/" + std::to_string(files.size()), 11, theme.dim, TF_RIGHT);
        g.rect(0, 760, 480, 40, theme.bar);
        g.text(240, 774, "← → листать · Back к списку", 11, theme.dim, TF_CENTER);
    }
    void step(int d) {
        if (files.empty()) { back_to_shelf(); return; }
        size_t next = (selected + files.size() + d) % files.size();
        open(next);
    }
    bool tap(int x, int y) override {
        if (!viewing) {
            if (hit(x, y, 0, 32, 64, 64)) { app.back(); return true; }
            if (y >= 96) { size_t i = scroll + (size_t)((y - 96) / 56); if (i < files.size()) open(i); }
            return true;
        }
        if (failed) { back_to_shelf(); return true; }
        // Rotated view: u runs along the phone's long side (0..800), v is the
        // short side. Left third — previous, right third — next, middle — back.
        if (image.width > image.height) {
            int u = 799 - y;
            if (u < 266) step(-1);
            else if (u > 533) step(1);
            else back_to_shelf();
            return true;
        }
        if (y >= 32 && y < 72) { back_to_shelf(); return true; }
        if (y >= kPhotoTop && y < 760) { if (x < 160) step(-1); else if (x >= 320) step(1); }
        return true;
    }
    bool key(int k, bool down) override {
        if (!down) return false;
        if (k == XK_Escape || k == XK_BackSpace || k == XF86XK_Back) {
            if (viewing) { back_to_shelf(); return true; }
            app.back(); return true;
        }
        if (viewing && (k == XK_Left || k == XK_Right)) { step(k == XK_Left ? -1 : 1); return true; }
        if (!viewing && k == XK_Down && scroll + 12 < files.size()) { ++scroll; return true; }
        if (!viewing && k == XK_Up && scroll) { --scroll; return true; }
        return false;
    }
};
}
ScreenView *make_viewer() { return new Viewer(); }
