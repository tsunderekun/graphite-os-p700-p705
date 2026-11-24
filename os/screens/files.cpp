// files — file manager for the graphite shell.
//
// v1 P0: multi-select (long-press), in-memory clipboard (copy/move), paste,
// delete with confirmation, rename, new folder, sort (name/size/date),
// hidden-file toggle, properties + free space, breadcrumbs, no 500-row cap.
//
// Rules of the screen:
//   * every disk read/write happens in the constructor, on_show(), tap() or
//     tick() — never in draw();
//   * destructive operations are limited to /media, /mnt, /sdcard, /home,
//     /tmp; the rest of the root filesystem is read-only from this screen;
//   * copy/move runs as a small state machine, a slice per call, so the UI
//     stays responsive and can be cancelled;
//   * folders are copied recursively, symlinks are skipped, existing
//     destinations are never overwritten.
#include "graphite.h"
#include "screens.h"

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#ifndef MSDOS_SUPER_MAGIC
#define MSDOS_SUPER_MAGIC 0x4d44
#endif

namespace {

// ---------------------------------------------------------------- geometry
const int LIST_Y = 96;
const int ROW_H = 56;
const int ROWS_NORMAL = 12;    // 88 .. 760, footer 760 .. 800
const int ROWS_SELECT = 10;    // 96 .. 656, action bars 656 .. 780
const int MAX_ENTRIES = 10000; // safety cap, reported honestly in the footer
const size_t COPY_BUF = 128 * 1024;
const unsigned long long SLICE_TAP = 2ull * 1024 * 1024;
const unsigned long long SLICE_TICK = 4ull * 1024 * 1024;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// ---------------------------------------------------------------- text
void append_utf8(std::string &s, unsigned cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

void pop_utf8(std::string &s) {
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

std::string trim_spaces(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

// Physical X keyboard -> UTF-8. ASCII and Latin-1 are direct, the Cyrillic
// legacy keysym block is translated through its Unicode order, and OSK-style
// Unicode keysyms (0x01000000|cp) pass straight through.
unsigned keysym_cp(unsigned long ks) {
    if (ks >= 0x20 && ks <= 0x7E) return (unsigned)ks;
    if (ks >= 0xA0 && ks <= 0xFF) return (unsigned)ks;
    if ((ks & 0xFF000000ul) == 0x01000000ul) return (unsigned)(ks & 0xFFFFFFul);
    if (ks == 0x06A3) return 0x0451;   // ё
    if (ks == 0x06B3) return 0x0401;   // Ё
    static const unsigned low[32] = {
        0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
        0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E,
        0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
        0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A
    };
    if (ks >= 0x06C0 && ks <= 0x06DF) return low[ks - 0x06C0];
    if (ks >= 0x06E0 && ks <= 0x06FF) return low[ks - 0x06E0] - 0x20;
    return 0;
}

// app.cpp reports the base keysym only (XLookupKeysym index 0), so Shift is
// lost for screens. Ask the server for the keyboard state and translate the
// key's level-1 keysym. OSK Unicode keysyms are already cased and are left
// untouched.
unsigned shifted_cp(unsigned long ks, unsigned cp, bool shift_hint) {
    if (!app.dpy || (ks & 0xFF000000ul) == 0x01000000ul) return cp;
    bool shift = shift_hint;
    if (!shift) {
        char keys[32];
        if (XQueryKeymap(app.dpy, keys)) {
            KeyCode sl = XKeysymToKeycode(app.dpy, XK_Shift_L);
            KeyCode sr = XKeysymToKeycode(app.dpy, XK_Shift_R);
            if (sl > 0 && (keys[sl / 8] & (1 << (sl % 8)))) shift = true;
            if (sr > 0 && (keys[sr / 8] & (1 << (sr % 8)))) shift = true;
        }
    }
    if (!shift) return cp;
    KeyCode kc = XKeysymToKeycode(app.dpy, (KeySym)ks);
    if (!kc) return cp;
    KeySym s = XkbKeycodeToKeysym(app.dpy, kc, 0, 1);
    unsigned c2 = keysym_cp((unsigned long)s);
    return c2 ? c2 : cp;
}

// ---------------------------------------------------------------- names/paths
bool is_text_name(const std::string &n) {
    size_t d = n.find_last_of('.');
    if (d == std::string::npos || d + 1 >= n.size()) return false;
    std::string e;
    for (size_t i = d + 1; i < n.size(); i++)
        e += (char)std::tolower((unsigned char)n[i]);
    return e == "txt" || e == "md" || e == "log" || e == "conf";
}

bool is_dot_name(const std::string &n) { return !n.empty() && n[0] == '.'; }

std::string norm_dir(std::string p) {
    if (p.empty()) return "/";
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string join_path(const std::string &dir, const std::string &name) {
    if (dir == "/") return "/" + name;
    return dir + "/" + name;
}

std::string parent_of(std::string p) {
    p = norm_dir(p);
    size_t s = p.rfind('/');
    if (s == std::string::npos || s == 0) return "/";
    return p.substr(0, s);
}

std::string base_of(const std::string &p) {
    std::string q = norm_dir(p);
    size_t s = q.rfind('/');
    if (s == std::string::npos) return q;
    return q.substr(s + 1);
}

bool under(const std::string &p, const char *root) {
    std::string r(root);
    if (p == r) return true;
    return p.size() > r.size() && p.compare(0, r.size(), r) == 0 &&
           p[r.size()] == '/';
}

// The shell runs as root; only user media and home are writable here.
bool can_write(const std::string &p) {
    static const char *roots[] = {"/media", "/mnt", "/sdcard", "/home",
                                  "/tmp", "/var/tmp"};
    for (const char *r : roots)
        if (under(p, r)) return true;
    return false;
}

// Mount points and home itself are writable containers, not objects to
// delete or rename from this screen.
bool protected_path(const std::string &p) {
    static const char *roots[] = {"/home", "/home/user", "/media", "/media/sd",
                                  "/mnt",  "/mnt/sd",    "/sdcard", "/tmp",
                                  "/var/tmp"};
    for (const char *r : roots)
        if (p == r) return true;
    return false;
}

// ---------------------------------------------------------------- formatting
std::string fmt_size(unsigned long long n) {
    char b[48];
    if (n < 1024) {
        std::snprintf(b, sizeof b, "%llu Б", n);
        return b;
    }
    double v = (double)n / 1024.0;
    const char *u = "КБ";
    if (v >= 1024.0) { v /= 1024.0; u = "МБ"; }
    if (v >= 1024.0) { v /= 1024.0; u = "ГБ"; }
    std::snprintf(b, sizeof b, "%.1f %s", v, u);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

std::string fmt_date(long long t) {
    time_t tt = (time_t)t;
    struct tm tmv;
    char b[40];
    if (!localtime_r(&tt, &tmv)) return "—";
    if (!std::strftime(b, sizeof b, "%d.%m.%Y %H:%M", &tmv)) return "—";
    return b;
}

std::string plural_n(int n, const char *one, const char *few, const char *many) {
    int m = n % 100;
    if (m >= 11 && m <= 14) return std::to_string(n) + " " + many;
    switch (n % 10) {
    case 1: return std::to_string(n) + " " + one;
    case 2:
    case 3:
    case 4: return std::to_string(n) + " " + few;
    default: return std::to_string(n) + " " + many;
    }
}

std::string plural_obj(int n) {
    return plural_n(n, "объект", "объекта", "объектов");
}

std::string mode_text(mode_t m) {
    char b[11];
    b[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        b[1 + i] = (m & (mode_t)(1u << (8 - i))) ? rwx[i] : '-';
    b[10] = 0;
    return b;
}

std::string free_at(const std::string &p) {
    struct statvfs v;
    if (statvfs(p.c_str(), &v) != 0) return "—";
    unsigned long long fr = v.f_frsize ? v.f_frsize : v.f_bsize;
    return fmt_size((unsigned long long)v.f_bavail * fr);
}

bool on_vfat(const std::string &p) {
    struct statfs s;
    if (statfs(p.c_str(), &s) != 0) return false;
    return (unsigned long)s.f_type == (unsigned long)MSDOS_SUPER_MAGIC;
}

// ---------------------------------------------------------------- entries
struct Ent {
    std::string name;
    bool dir = false;
    bool link = false;
    unsigned long long size = 0;
    long long mtime = 0;
    mode_t mode = 0;
};

enum SortMode { SORT_NAME = 0, SORT_SIZE = 1, SORT_DATE = 2 };

bool less_ent(const Ent &a, const Ent &b, int mode) {
    if (a.dir != b.dir) return a.dir;
    if (mode == SORT_SIZE && a.size != b.size) return a.size > b.size;
    if (mode == SORT_DATE && a.mtime != b.mtime) return a.mtime > b.mtime;
    int c = strcasecmp(a.name.c_str(), b.name.c_str());
    if (c) return c < 0;
    return a.name < b.name;
}

const char *sort_label(int mode) {
    return mode == SORT_SIZE ? "РАЗМ" : mode == SORT_DATE ? "ДАТА" : "ИМЯ";
}

const char *sort_short(int mode) {
    return mode == SORT_SIZE ? "размер" : mode == SORT_DATE ? "дата" : "имя";
}

// ---------------------------------------------------------------- job/clipboard
enum OpKind { OP_FILE = 0, OP_MKDIR, OP_RMDIR, OP_UNLINK, OP_RENAME };

struct Op {
    int kind = OP_FILE;
    std::string src, dst;
    unsigned long long size = 0;
};

struct Clip {
    bool on = false;
    bool move = false;
    std::vector<std::string> paths;
};

struct Job {
    bool active = false;
    int type = 0;  // 0 copy, 1 move, 2 delete
    bool move_src = false;
    bool from_clip = false;
    bool cancel = false;

    std::vector<Op> ops;
    size_t op = 0;

    int fd_in = -1, fd_out = -1;
    std::string cur, cur_src, cur_dst;
    unsigned long long file_done = 0, file_size = 0;
    unsigned long long total_bytes = 0, done_bytes = 0;
    size_t ok = 0, files = 0, dirs = 0, errors = 0, skipped = 0;
    std::string err1;
    std::vector<char> buf;
};

enum JobType { J_COPY = 0, J_MOVE = 1, J_DELETE = 2 };
enum Modal { M_NONE = 0, M_RENAME, M_MKDIR, M_CONFIRM, M_PROPS, M_BUFFER };

bool write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (w == 0) return false;
        off += (size_t)w;
    }
    return true;
}

struct Files : ScreenView {
    std::string path;
    std::string err;
    std::string free_str;
    std::vector<std::string> hist;
    std::vector<Ent> ents;
    std::set<std::string> sel;
    int scroll = 0;
    int drag_px = 0;
    int drag_total = 0;
    unsigned gesture_at = 0;
    int sort_mode = SORT_NAME;
    bool show_hidden = false;
    bool selecting = false;
    bool menu_open = false;
    bool preview_select = false;
    bool shift_held = false;
    bool cut = false;

    Clip clip;
    Job job;

    int modal = M_NONE;
    int dlg_idx = -1;
    std::string dlg_title;
    std::string dlg_value;
    std::string dlg_hint;

    // confirm-delete snapshot
    int del_count = 0;
    std::string del_size;
    bool del_has_dir = false;
    std::vector<std::string> del_names;

    // properties snapshot
    std::string props_path, props_name, props_type, props_size;
    std::string props_date, props_mode, props_free, props_open;
    bool props_folder = false;
    bool props_text = false;

    // clipboard dialog snapshot
    std::string buf_mode;
    std::vector<std::string> buf_names;
    std::string preview_dialog;

    // menu sheet: 444 .. 800, header 44, six 52 px rows
    static const int MENU_Y = 444;

    Files() {
        static const char *cands[] = {"/media/sd", "/mnt/sd", "/sdcard", "/"};
        const char *envdir = std::getenv("GS_FILES_DIR");
        bool ok = false;
        if (envdir && *envdir) {
            ok = load(norm_dir(envdir));
            if (ok) path = norm_dir(envdir);
        }
        if (!ok) {
            for (const char *c : cands) {
                if (load(c)) {
                    path = norm_dir(c);
                    ok = true;
                    break;
                }
            }
        }
        if (!ok) {
            path = envdir && *envdir ? norm_dir(envdir) : "/media/sd";
            err = "Нет доступа";
        }

        // Preview-only helpers, inherited from preview.sh.
        const char *ds = std::getenv("GS_FILES_SELECT");
        if (ds && *ds && ds[0] != '0' && !ents.empty()) preview_select = true;
        const char *dm = std::getenv("GS_FILES_MENU");
        if (dm && *dm && dm[0] != '0') menu_open = true;
        const char *dd = std::getenv("GS_FILES_DIALOG");
        if (dd && *dd && dd[0] != '0') {
            preview_dialog = dd;
            preview_select = true;
        }
    }

    void maybe_preview_select() {
        if (!preview_select) return;
        preview_select = false;
        if (ents.empty()) return;
        bool single = preview_dialog == "rename" || preview_dialog == "props";
        selecting = true;
        int n = (int)ents.size() < (single ? 1 : 2) ? (int)ents.size()
                                                    : (single ? 1 : 2);
        for (int i = 0; i < n; i++) sel.insert(ents[(size_t)i].name);
        clamp_scroll();
        if (preview_dialog == "rename") open_rename();
        else if (preview_dialog == "delete") open_delete();
        else if (preview_dialog == "mkdir") open_mkdir();
        else if (preview_dialog == "props") open_props(0);
        else if (preview_dialog == "buffer") {
            clip.on = true;
            clip.move = false;
            clip.paths.clear();
            for (int i = 0; i < n; i++)
                clip.paths.push_back(join_path(path, ents[(size_t)i].name));
            open_buffer();
        }
    }

    const char *id() const override { return "files"; }

    void on_show() override {
        sync_gesture();
        shift_held = false;
        if (!app.open_path.empty()) {
            std::string p = app.open_path;
            app.open_path.clear();
            std::string dir = p;
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) dir = parent_of(p);
            if (!load(dir)) {
                app.toast("Нет доступа к папке");
                return;
            }
            path = norm_dir(dir);
            hist.clear();
            scroll = 0;
            selecting = false;
            sel.clear();
            drag_px = 0;
            drag_total = 0;
            for (size_t i = 0; i < ents.size(); i++) {
                if (base_of(p) == ents[i].name) {
                    ensure_visible((int)i + 1);
                    break;
                }
            }
            maybe_preview_select();
            return;
        }
        // Returning to the screen: refresh without losing the current folder.
        if (!load(path)) app.toast("Нет доступа к папке");
        selecting = false;
        sel.clear();
        drag_px = 0;
        drag_total = 0;
        maybe_preview_select();
    }

    // ------------------------------------------------------------ loading
    bool load(const std::string &p) {
        DIR *d = opendir(p.c_str());
        if (!d) return false;
        std::vector<Ent> out;
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            const char *nm = e->d_name;
            if (!std::strcmp(nm, ".") || !std::strcmp(nm, "..")) continue;
            if (!show_hidden && nm[0] == '.') continue;
            std::string full = join_path(p, nm);
            struct stat lst;
            if (lstat(full.c_str(), &lst) != 0) continue;
            struct stat st = lst;
            if (S_ISLNK(lst.st_mode)) {
                struct stat tgt;
                if (stat(full.c_str(), &tgt) == 0) st = tgt;
            }
            Ent en;
            en.name = nm;
            en.link = S_ISLNK(lst.st_mode);
            en.dir = S_ISDIR(st.st_mode);
            en.size = (unsigned long long)st.st_size;
            en.mtime = (long long)st.st_mtime;
            en.mode = st.st_mode;
            out.push_back(en);
            if (out.size() >= (size_t)MAX_ENTRIES) {
                cut = true;
                break;
            }
        }
        closedir(d);
        if (out.size() < (size_t)MAX_ENTRIES) cut = false;
        std::sort(out.begin(), out.end(), [this](const Ent &a, const Ent &b) {
            return less_ent(a, b, sort_mode);
        });
        ents.swap(out);
        sel.clear();
        err.clear();
        free_str = free_at(p);
        clamp_scroll();
        return true;
    }

    void refresh() {
        if (!load(path)) {
            app.toast("Нет доступа");
            return;
        }
        clamp_scroll();
    }

    void resort() {
        std::sort(ents.begin(), ents.end(), [this](const Ent &a, const Ent &b) {
            return less_ent(a, b, sort_mode);
        });
    }

    int rows_visible() const { return selecting ? ROWS_SELECT : ROWS_NORMAL; }
    int total_rows() const { return (int)ents.size() + 1; }  // + ".."

    int max_top() const {
        int m = total_rows() - rows_visible();
        return m > 0 ? m : 0;
    }

    int top_row() const {
        int m = max_top();
        return scroll < 0 ? 0 : (scroll > m ? m : scroll);
    }

    void clamp_scroll() {
        int m = max_top();
        if (scroll < 0) scroll = 0;
        if (scroll > m) scroll = m;
    }

    void ensure_visible(int idx) {
        int vis = rows_visible();
        if (idx < scroll) scroll = idx;
        if (idx >= scroll + vis) scroll = idx - vis + 1;
        clamp_scroll();
    }

    void enter(const std::string &name) {
        std::string old = path;
        std::string np = join_path(path, name);
        if (!load(np)) {
            app.toast("Нет доступа");
            return;
        }
        path = norm_dir(np);
        hist.push_back(old);
        scroll = 0;
        selecting = false;
        sel.clear();
        drag_px = 0;
        drag_total = 0;
    }

    void up() {
        std::string old = path;
        std::string np;
        if (!hist.empty()) {
            np = hist.back();
            hist.pop_back();
        } else {
            np = parent_of(path);
        }
        if (!load(np)) {
            app.toast("Нет доступа");
            return;
        }
        path = norm_dir(np);
        (void)old;
        scroll = 0;
        selecting = false;
        sel.clear();
        drag_px = 0;
        drag_total = 0;
    }

    std::string crumb_path() const {
        if (path == "/") return "/";
        std::vector<std::string> parts;
        size_t i = 0;
        while (i < path.size()) {
            while (i < path.size() && path[i] == '/') i++;
            size_t j = i;
            while (j < path.size() && path[j] != '/') j++;
            if (j > i) parts.push_back(path.substr(i, j - i));
            i = j;
        }
        size_t start = parts.size() > 3 ? parts.size() - 3 : 0;
        std::string out = start > 0 ? "…/" : "/";
        for (size_t k = start; k < parts.size(); k++) {
            if (k > start) out += "/";
            out += parts[k];
        }
        return out;
    }

    // ------------------------------------------------------------ selection
    int selected_count() const { return (int)sel.size(); }

    unsigned long long selected_bytes() const {
        unsigned long long n = 0;
        for (const Ent &e : ents)
            if (!e.dir && sel.count(e.name)) n += e.size;
        return n;
    }

    std::vector<int> selected_indices() const {
        std::vector<int> out;
        for (size_t i = 0; i < ents.size(); i++)
            if (sel.count(ents[i].name)) out.push_back((int)i);
        return out;
    }

    std::vector<std::string> selected_paths() const {
        std::vector<std::string> out;
        for (const Ent &e : ents)
            if (sel.count(e.name)) out.push_back(join_path(path, e.name));
        return out;
    }

    void toggle_sel(const std::string &name) {
        if (sel.count(name)) sel.erase(name);
        else sel.insert(name);
    }

    void select_all_toggle() {
        if (ents.empty()) return;
        if (sel.size() >= ents.size()) {
            sel.clear();
            return;
        }
        for (const Ent &e : ents) sel.insert(e.name);
    }

    void exit_select() {
        selecting = false;
        sel.clear();
        clamp_scroll();
    }

    // ------------------------------------------------------------ gestures
    void sync_gesture() {
        if (gesture_at != app.down_at) {
            gesture_at = app.down_at;
            drag_total = 0;
            drag_px = 0;
        }
    }

    int idx_at(int y) const {
        if (y < LIST_Y || y >= LIST_Y + rows_visible() * ROW_H) return -1;
        int i = (y - LIST_Y) / ROW_H;
        int idx = top_row() + i;
        if (idx >= total_rows()) return -1;
        return idx;
    }

    // ------------------------------------------------------------ clipboard
    void set_clip(const std::vector<int> &idx, bool move) {
        if (idx.empty()) {
            app.toast("Ничего не выбрано");
            return;
        }
        clip.paths.clear();
        for (int i : idx) clip.paths.push_back(join_path(path, ents[(size_t)i].name));
        clip.on = !clip.paths.empty();
        clip.move = move;
        char b[96];
        std::snprintf(b, sizeof b, "%s: %d · вставка в «···»",
                      move ? "Буфер (переместить)" : "Буфер (копировать)",
                      (int)clip.paths.size());
        app.toast(b, 2600);
        exit_select();
    }

    void open_buffer() {
        buf_mode = clip.on
                       ? (clip.move ? "Переместить · " : "Копировать · ") +
                             plural_obj((int)clip.paths.size())
                       : "Буфер пуст";
        buf_names.clear();
        for (size_t i = 0; i < clip.paths.size() && i < 6; i++)
            buf_names.push_back(base_of(clip.paths[i]));
        if (clip.paths.size() > 6)
            buf_names.push_back("… ещё " + std::to_string(clip.paths.size() - 6));
        modal = M_BUFFER;
    }

    // ------------------------------------------------------------ planning
    bool plan_copy(const std::string &src, const std::string &dst,
                   bool move_src, std::vector<Op> &out, int depth,
                   size_t &skipped) {
        if (out.size() >= 4000 || depth > 12) {
            skipped++;
            return false;
        }
        struct stat st;
        if (lstat(src.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) {
            skipped++;
            return false;
        }
        if (S_ISDIR(st.st_mode)) {
            Op d;
            d.kind = OP_MKDIR;
            d.src = src;
            d.dst = dst;
            out.push_back(d);
            DIR *dir = opendir(src.c_str());
            if (!dir) {
                out.pop_back();
                skipped++;
                return false;
            }
            struct dirent *e;
            int n = 0;
            bool complete = true;
            while ((e = readdir(dir)) != nullptr) {
                if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
                    continue;
                if (++n > 2000) {
                    skipped++;
                    complete = false;
                    break;
                }
                if (!plan_copy(join_path(src, e->d_name), join_path(dst, e->d_name),
                               move_src, out, depth + 1, skipped))
                    complete = false;
            }
            closedir(dir);
            if (move_src && complete) {
                Op r;
                r.kind = OP_RMDIR;
                r.src = src;
                out.push_back(r);
            }
            return complete;
        }
        if (!S_ISREG(st.st_mode)) {
            skipped++;
            return false;
        }
        Op f;
        f.kind = OP_FILE;
        f.src = src;
        f.dst = dst;
        f.size = (unsigned long long)st.st_size;
        out.push_back(f);
        return true;
    }

    bool plan_delete(const std::string &src, std::vector<Op> &out, int depth,
                     size_t &skipped) {
        if (out.size() >= 4000 || depth > 12) {
            skipped++;
            return false;
        }
        struct stat st;
        if (lstat(src.c_str(), &st) != 0) {
            skipped++;
            return false;
        }
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            DIR *dir = opendir(src.c_str());
            if (!dir) {
                skipped++;
                return false;
            }
            struct dirent *e;
            int n = 0;
            bool complete = true;
            while ((e = readdir(dir)) != nullptr) {
                if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
                    continue;
                if (++n > 2000) {
                    skipped++;
                    complete = false;
                    break;
                }
                if (!plan_delete(join_path(src, e->d_name), out, depth + 1, skipped)) {
                    complete = false;
                    break;
                }
            }
            closedir(dir);
            if (!complete) return false;
            Op r;
            r.kind = OP_RMDIR;
            r.src = src;
            out.push_back(r);
            return true;
        }
        Op u;
        u.kind = OP_UNLINK;
        u.src = src;
        out.push_back(u);
        return true;
    }

    // ------------------------------------------------------------ job lifecycle
    void begin_job(Job &j, int type) {
        j.active = true;
        j.type = type;
        j.op = 0;
        j.fd_in = j.fd_out = -1;
        job = std::move(j);
        run_job(SLICE_TAP);
    }

    void start_copy(const std::vector<std::string> &srcs, bool move, bool from_clip) {
        if (!can_write(path)) {
            app.toast("Запись только на SD и в /home");
            return;
        }
        Job j;
        int exists = 0, self = 0, inside = 0;
        for (const std::string &src : srcs) {
            struct stat st;
            if (lstat(src.c_str(), &st) != 0) {
                j.skipped++;
                continue;
            }
            if (move && protected_path(src)) {
                j.skipped++;
                continue;
            }
            if (move && parent_of(src) == norm_dir(path)) {
                self++;
                continue;
            }
            if (under(path, src.c_str())) {
                inside++;
                continue;
            }
            std::string dst = join_path(path, base_of(src));
            struct stat dst_st;
            if (lstat(dst.c_str(), &dst_st) == 0) {
                exists++;
                continue;
            }
            if (move) {
                Op r;
                r.kind = OP_RENAME;
                r.src = src;
                r.dst = dst;
                j.ops.push_back(r);
            } else {
                plan_copy(src, dst, false, j.ops, 0, j.skipped);
            }
        }
        if (j.ops.empty()) {
            if (self) app.toast("Уже в этой папке");
            else if (inside) app.toast("Нельзя копировать папку в себя");
            else if (exists) app.toast("Такие файлы уже есть");
            else app.toast("Нечего копировать");
            return;
        }
        for (const Op &o : j.ops)
            if (o.kind == OP_FILE) j.total_bytes += o.size;
        j.move_src = move;
        j.from_clip = from_clip;
        if (!move) j.buf.assign(COPY_BUF, 0);
        begin_job(j, move ? J_MOVE : J_COPY);
    }

    void start_paste() {
        if (!clip.on || clip.paths.empty()) {
            app.toast("Буфер пуст");
            return;
        }
        if (!can_write(path)) {
            app.toast("Запись только на SD и в /home");
            return;
        }
        // Drop vanished sources instead of failing the whole paste.
        std::vector<std::string> srcs;
        for (const std::string &p : clip.paths) {
            struct stat st;
            if (lstat(p.c_str(), &st) == 0) srcs.push_back(p);
        }
        if (srcs.empty()) {
            clip.on = false;
            clip.paths.clear();
            app.toast("Исходные файлы пропали");
            return;
        }
        start_copy(srcs, clip.move, true);
    }

    void start_delete(const std::vector<std::string> &srcs) {
        if (!can_write(path)) {
            app.toast("Удаление только с SD и из /home");
            return;
        }
        Job j;
        for (const std::string &src : srcs) {
            if (src == path || under(path, src.c_str())) {
                j.skipped++;
                continue;
            }
            if (protected_path(src)) {
                j.skipped++;
                continue;
            }
            std::vector<Op> one;
            size_t local_skipped = 0;
            if (plan_delete(src, one, 0, local_skipped)) {
                j.ops.insert(j.ops.end(), one.begin(), one.end());
            } else {
                j.skipped++;
            }
        }
        if (j.ops.empty()) {
            app.toast("Нечего удалять");
            return;
        }
        begin_job(j, J_DELETE);
    }

    void start_file(const Op &o) {
        job.cur = base_of(o.dst);
        job.cur_src = o.src;
        job.cur_dst = o.dst;
        job.file_done = 0;
        job.file_size = o.size;
        job.fd_in = ::open(o.src.c_str(), O_RDONLY);
        if (job.fd_in < 0) {
            job.skipped++;
            job.op++;
            return;
        }
        job.fd_out = ::open(o.dst.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (job.fd_out < 0) {
            ::close(job.fd_in);
            job.fd_in = -1;
            job.errors++;
            if (job.err1.empty()) job.err1 = base_of(o.src);
            job.op++;
        }
    }

    void finish_file() {
        int e = 0;
        if (fsync(job.fd_out) != 0) e = errno;
        if (::close(job.fd_out) != 0 && !e) e = errno;
        ::close(job.fd_in);
        job.fd_out = job.fd_in = -1;
        if (e) {
            unlink(job.cur_dst.c_str());
            job.errors++;
            if (job.err1.empty()) job.err1 = job.cur;
        } else {
            job.files++;
            job.ok++;
            if (job.move_src && unlink(job.cur_src.c_str()) != 0) job.errors++;
        }
        job.cur.clear();
        job.op++;
    }

    void fail_file() {
        int e = errno;
        if (job.fd_out >= 0) ::close(job.fd_out);
        if (job.fd_in >= 0) ::close(job.fd_in);
        job.fd_out = job.fd_in = -1;
        if (!job.cur_dst.empty()) unlink(job.cur_dst.c_str());
        job.errors++;
        if (job.err1.empty()) job.err1 = job.cur;
        (void)e;
        job.cur.clear();
        job.op++;
    }

    void abort_job() {
        if (job.fd_out >= 0) {
            ::close(job.fd_out);
            if (!job.cur_dst.empty()) unlink(job.cur_dst.c_str());
        }
        if (job.fd_in >= 0) ::close(job.fd_in);
        job.fd_out = job.fd_in = -1;
        job.cancel = false;
        finish_job(true);
    }

    void finish_job(bool cancelled) {
        int type = job.type;
        size_t ok = job.ok, files = job.files, dirs = job.dirs;
        size_t errors = job.errors, skipped = job.skipped;
        unsigned long long bytes = job.done_bytes;
        bool from_clip = job.from_clip;

        std::string t;
        if (cancelled) {
            t = "Отменено";
            if (bytes) t += " · " + fmt_size(bytes);
        } else if (type == J_COPY) {
            if (!files && dirs) {
                t = "Копия: " + plural_n((int)dirs, "папка", "папки", "папок");
            } else {
                t = "Копия: " + plural_n((int)files, "файл", "файла", "файлов") +
                    " · " + fmt_size(bytes);
                if (dirs) t += " · папок " + std::to_string(dirs);
            }
        } else if (type == J_MOVE) {
            t = "Перенос: " + std::to_string(ok);
        } else {
            t = "Удалено: " + std::to_string(ok);
        }
        if (errors) t += " · ошибок " + std::to_string(errors);
        if (skipped) t += " · проп. " + std::to_string(skipped);

        job.active = false;
        job.ops.clear();
        job.buf.clear();
        if (type == J_MOVE && from_clip && !cancelled && !errors && !skipped) {
            clip.on = false;
            clip.paths.clear();
        }
        selecting = false;
        sel.clear();
        load(path);
        clamp_scroll();
        app.toast(t.c_str(), 3200);
    }

    void run_job(unsigned long long budget) {
        if (!job.active) return;
        if (job.cancel) {
            abort_job();
            return;
        }
        int steps = 0;
        while (job.active) {
            if (job.cancel) {
                abort_job();
                return;
            }
            if (++steps > 600) break;
            if (job.fd_in >= 0) {
                if (budget == 0) break;
                size_t cap = job.buf.size();
                if (cap == 0) {
                    job.buf.assign(COPY_BUF, 0);
                    cap = job.buf.size();
                }
                size_t want = (size_t)std::min<unsigned long long>(budget, cap);
                ssize_t n = ::read(job.fd_in, job.buf.data(), want);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    fail_file();
                    continue;
                }
                if (n == 0) {
                    finish_file();
                    if (budget == 0) break;
                    continue;
                }
                if (!write_all(job.fd_out, job.buf.data(), (size_t)n)) {
                    fail_file();
                    continue;
                }
                job.done_bytes += (unsigned long long)n;
                job.file_done += (unsigned long long)n;
                budget -= (unsigned long long)n;
                continue;
            }
            if (job.op >= job.ops.size()) {
                finish_job(false);
                return;
            }
            Op &o = job.ops[job.op];
            switch (o.kind) {
            case OP_FILE:
                start_file(o);
                break;
            case OP_MKDIR:
                if (mkdir(o.dst.c_str(), 0777) == 0) {
                    job.ok++;
                    job.dirs++;
                } else if (errno == EEXIST) {
                    job.ok++;
                } else {
                    job.errors++;
                    if (job.err1.empty()) job.err1 = base_of(o.dst);
                }
                job.op++;
                break;
            case OP_RMDIR:
                if (rmdir(o.src.c_str()) == 0) job.ok++;
                else job.errors++;
                job.op++;
                break;
            case OP_UNLINK:
                if (unlink(o.src.c_str()) == 0) job.ok++;
                else job.errors++;
                job.op++;
                break;
            case OP_RENAME: {
                if (rename(o.src.c_str(), o.dst.c_str()) == 0) {
                    job.ok++;
                    job.op++;
                } else if (errno == EXDEV) {
                    std::vector<Op> plan;
                    plan_copy(o.src, o.dst, true, plan, 0, job.skipped);
                    if (plan.empty()) {
                        job.errors++;
                        if (job.err1.empty()) job.err1 = base_of(o.src);
                        job.op++;
                    } else {
                        job.ops.erase(job.ops.begin() + (long)job.op);
                        job.ops.insert(job.ops.begin() + (long)job.op,
                                       plan.begin(), plan.end());
                    }
                } else {
                    job.errors++;
                    if (job.err1.empty()) job.err1 = base_of(o.src);
                    job.op++;
                }
                break;
            }
            default:
                job.op++;
                break;
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    void open_rename() {
        std::vector<int> idx = selected_indices();
        if (idx.size() != 1) {
            app.toast("Выберите один объект");
            return;
        }
        if (!can_write(path)) {
            app.toast("Изменение только на SD и в /home");
            return;
        }
        dlg_idx = idx[0];
        if (protected_path(join_path(path, ents[(size_t)dlg_idx].name))) {
            app.toast("Системный путь защищён");
            return;
        }
        dlg_title = "Переименовать";
        dlg_value = ents[(size_t)dlg_idx].name;
        dlg_hint = "Ввод с клавиатуры · Enter — ОК · Esc — отмена";
        modal = M_RENAME;
    }

    void open_mkdir() {
        if (!can_write(path)) {
            app.toast("Создание только на SD и в /home");
            return;
        }
        dlg_idx = -1;
        dlg_title = "Новая папка";
        dlg_value.clear();
        dlg_hint = "Ввод с клавиатуры · Enter — создать · Esc — отмена";
        modal = M_MKDIR;
    }

    void open_delete() {
        std::vector<int> idx = selected_indices();
        if (idx.empty()) {
            app.toast("Ничего не выбрано");
            return;
        }
        if (!can_write(path)) {
            app.toast("Удаление только с SD и из /home");
            return;
        }
        for (int i : idx) {
            if (protected_path(join_path(path, ents[(size_t)i].name))) {
                app.toast("Системный путь защищён");
                return;
            }
        }
        del_count = (int)idx.size();
        unsigned long long bytes = 0;
        del_has_dir = false;
        del_names.clear();
        for (int i : idx) {
            const Ent &e = ents[(size_t)i];
            if (e.dir) del_has_dir = true;
            else bytes += e.size;
            if (del_names.size() < 3) del_names.push_back(e.name);
        }
        del_size = bytes ? fmt_size(bytes) : "";
        modal = M_CONFIRM;
    }

    void open_props(int idx) {
        struct stat st;
        props_folder = idx < 0;
        props_text = false;
        props_name = props_folder ? path : ents[(size_t)idx].name;
        props_path = props_folder ? path : join_path(path, ents[(size_t)idx].name);
        if (lstat(props_path.c_str(), &st) != 0) {
            app.toast("Нет доступа");
            return;
        }
        props_type = props_folder || S_ISDIR(st.st_mode)
                         ? "Папка"
                         : S_ISLNK(st.st_mode) ? "Ссылка" : "Файл";
        props_size = (props_folder || S_ISDIR(st.st_mode)) ? "—" : fmt_size((unsigned long long)st.st_size);
        props_date = fmt_date((long long)st.st_mtime);
        props_mode = on_vfat(props_path) ? "—" : mode_text(st.st_mode);
        props_free = free_str.empty() ? free_at(path) : free_str;
        if (props_folder || S_ISDIR(st.st_mode)) {
            props_open = "—";
        } else if (is_text_name(props_path)) {
            props_open = "Заметки";
            props_text = true;
        } else {
            props_open = "Нет приложения";
        }
        modal = M_PROPS;
    }

    void commit_text() {
        std::string name = trim_spaces(dlg_value);
        if (name.empty()) {
            app.toast("Имя не может быть пустым");
            return;
        }
        if (name == "." || name == ".." || name.find('/') != std::string::npos) {
            app.toast("Недопустимое имя");
            return;
        }
        if (name.size() > 240) {
            app.toast("Слишком длинное имя");
            return;
        }
        std::string dst = join_path(path, name);
        if (modal == M_MKDIR) {
            if (mkdir(dst.c_str(), 0777) != 0) {
                app.toast(errno == EEXIST ? "Такая папка уже есть"
                                           : "Не удалось создать папку");
                return;
            }
            modal = M_NONE;
            load(path);
            app.toast("Папка создана");
            return;
        }
        if (modal == M_RENAME) {
            if (dlg_idx < 0 || (size_t)dlg_idx >= ents.size()) {
                modal = M_NONE;
                return;
            }
            std::string old = ents[(size_t)dlg_idx].name;
            if (old == name) {
                modal = M_NONE;
                return;
            }
            struct stat st;
            if (lstat(dst.c_str(), &st) == 0) {
                app.toast("Такое имя уже есть");
                return;
            }
            std::string src = join_path(path, old);
            if (rename(src.c_str(), dst.c_str()) != 0) {
                app.toast("Не удалось переименовать");
                return;
            }
            modal = M_NONE;
            selecting = false;
            sel.clear();
            load(path);
            app.toast("Переименовано");
        }
    }

    // ------------------------------------------------------------ drawing
    void bar_button(Gfx &g, int x, int y, int w, int h, const char *label,
                    bool enabled, bool primary = false) {
        uint16_t fill = !enabled ? theme.bg : primary ? theme.accent : theme.surf;
        uint16_t text = !enabled ? theme.dim : primary ? theme.onacc : theme.fg;
        g.rect(x, y, w, h, fill);
        if (!primary) g.frame(x, y, w, h, enabled ? theme.line2 : theme.line);
        g.text(x + w / 2, y + (h - 12) / 2 - 1, label, 12, text, TF_CENTER);
    }

    void draw_header(Gfx &g) {
        const int hy = 32;
        if (selecting) {
            bar_button(g, 8, hy, 96, 64, "ОТМЕНА", true);
            std::string t = std::to_string(selected_count()) + " выбрано";
            g.text(112, hy + 10, t, 22, theme.fg, TF_LIGHT);
            bool all = !ents.empty() && sel.size() >= ents.size();
            bar_button(g, 400, hy, 56, 64, all ? "СНЯТЬ" : "ВСЕ", true);
            unsigned long long bytes = selected_bytes();
            std::string sub = crumb_path();
            if (bytes) sub += " · " + fmt_size(bytes);
            g.text(112, hy + 42, g.fit(sub, 11, 282), 11, theme.dim);
            g.hline(0, LIST_Y - 1, 480, theme.line2);
            return;
        }
        icon(g, "back", 15, hy + 21, 22, theme.fg);
        g.text(60, hy + 10, "Файлы", 24, theme.fg, TF_LIGHT);
        g.text(60, hy + 40, g.fit(crumb_path(), 11, 250), 11, theme.dim);

        g.text(340, hy + 24, sort_label(sort_mode), 11, theme.dim, TF_CENTER);
        g.text(398, hy + 24, "СКР", 11, show_hidden ? theme.accent : theme.dim,
               TF_CENTER);
        g.text(446, hy + 18, "···", 20, theme.dim, TF_CENTER);
        g.hline(0, LIST_Y - 1, 480, theme.line2);
    }

    void draw_rows(Gfx &g) {
        int top = top_row();
        int total = total_rows();
        int vis = rows_visible();
        for (int i = 0; i < vis; i++) {
            int idx = top + i;
            if (idx >= total) break;
            int y = LIST_Y + i * ROW_H;
            g.hline(0, y + ROW_H - 1, 480, theme.line2);

            if (idx == 0) {
                int ix = selecting ? 60 : 24;
                icon(g, "back", ix, y + 16, 24, theme.dim);
                g.text(ix + 36, y + 18, "..", 16, theme.fg);
                g.text(456, y + 22, "Наверх", 12, theme.dim, TF_RIGHT);
                continue;
            }

            const Ent &e = ents[(size_t)idx - 1];
            bool picked = sel.count(e.name) != 0;
            if (picked) {
                g.rect(0, y, 480, ROW_H - 1, theme.sel);
                g.rect(0, y, 3, ROW_H - 1, theme.accent);
            }

            int icon_x = selecting ? 60 : 24;
            int text_x = selecting ? 96 : 60;
            int text_w = selecting ? 288 : 300;

            if (selecting) {
                if (picked) {
                    g.rect(24, y + 16, 24, 24, theme.accent);
                    icon(g, "check", 28, y + 20, 16, theme.onacc);
                } else {
                    g.frame(24, y + 16, 24, 24, theme.track);
                }
            }

            uint16_t name_c = is_dot_name(e.name) ? theme.dim : theme.fg;
            if (e.dir) {
                icon(g, "folder", icon_x, y + 16, 24, theme.accent);
                g.text(text_x, y + 18, g.fit(e.name, 16, text_w), 16, name_c);
                g.text(456, y + 22, "папка", 12, theme.dim, TF_RIGHT);
            } else {
                icon(g, "note", icon_x, y + 16, 24, theme.dim);
                g.text(text_x, y + 18, g.fit(e.name, 16, text_w), 16, name_c);
                g.text(456, y + 22, fmt_size(e.size), 12, theme.dim, TF_RIGHT);
            }
        }

        if (total == 1 && modal == M_NONE) {
            const char *t = err.empty() ? "Папка пуста" : err.c_str();
            g.text(240, 240, t, 15, theme.dim, TF_CENTER);
        }
    }

    void draw_footer(Gfx &g) {
        if (selecting) return;
        int y = LIST_Y + ROWS_NORMAL * ROW_H;
        g.hline(0, y, 480, theme.line2);
        g.text(24, y + 12, ("Свободно " + (free_str.empty() ? "—" : free_str)).c_str(),
               11, theme.dim);
        std::string cnt = plural_obj((int)ents.size());
        if (cut) cnt = std::to_string(MAX_ENTRIES) + "+ · не все";
        g.text(456, y + 12, cnt.c_str(), 11, theme.dim, TF_RIGHT);
    }

    void draw_select_bar(Gfx &g) {
        // row B: operations that are not about the selection itself
        bar_button(g, 24, 656, 138, 44, "ВСТАВИТЬ", clip.on, clip.on);
        bar_button(g, 170, 656, 138, 44, "СВОЙСТВА", true);
        bar_button(g, 316, 656, 138, 44, "БУФЕР", true);
        // row A: actions on selected objects
        bar_button(g, 24, 708, 102, 48, "КОПИЯ", true);
        bar_button(g, 134, 708, 102, 48, "ПЕРЕМЕСТ.", true);
        bar_button(g, 244, 708, 102, 48, "УДАЛИТЬ", true);
        bar_button(g, 354, 708, 102, 48, "ИМЯ", true);
    }

    void draw_menu(Gfx &g) {
        g.rect(0, MENU_Y, 480, 800 - MENU_Y, theme.surf);
        g.hline(0, MENU_Y, 480, theme.accent);
        g.text(20, MENU_Y + 15, "ДЕЙСТВИЯ", 13, theme.fg, TF_BOLD);
        icon(g, "close", 440, MENU_Y + 13, 18, theme.dim);

        const int rows = 6;
        for (int i = 0; i < rows; i++) {
            int y = MENU_Y + 44 + i * 52;
            if (i) g.hline(0, y, 480, theme.line2);
            const char *ico = i == 0 ? "plus" : i == 1 ? "folder" : i == 2 ? "check"
                              : i == 3 ? "sliders" : i == 4 ? "info" : "restart";
            const char *lab = i == 0 ? "Вставить сюда" : i == 1 ? "Новая папка"
                              : i == 2 ? "Скрытые файлы" : i == 3 ? "Сортировка"
                              : i == 4 ? "Свойства папки" : "Обновить";
            icon(g, ico, 24, y + 14, 24, i == 0 && !clip.on ? theme.dim : theme.fg);
            g.text(60, y + 9, lab, 16,
                   i == 0 && !clip.on ? theme.dim : theme.fg);
            if (i == 2) {
                ui_switch(g, 412, y + 14, show_hidden);
            } else if (i == 0) {
                std::string v = clip.on
                                    ? std::to_string((int)clip.paths.size()) + " шт."
                                    : "буфер пуст";
                g.text(456, y + 19, v.c_str(), 12, theme.dim, TF_RIGHT);
            } else if (i == 3) {
                g.text(456, y + 19, sort_short(sort_mode), 12, theme.dim, TF_RIGHT);
            }
        }
    }

    void draw_job(Gfx &g) {
        int x = 12, y = 592, w = 456, h = 164;
        g.rect(x, y, w, h, theme.bg);
        g.frame(x, y, w, h, theme.line);
        const char *title = job.type == J_COPY ? "КОПИРОВАНИЕ"
                            : job.type == J_MOVE ? "ПЕРЕМЕЩЕНИЕ" : "УДАЛЕНИЕ";
        g.text(28, y + 14, title, 13, theme.accent, TF_BOLD);

        int pct = 0;
        if (job.type == J_COPY) {
            if (job.total_bytes)
                pct = (int)(job.done_bytes * 100 / job.total_bytes);
            else if (!job.ops.empty())
                pct = (int)(job.op * 100 / job.ops.size());
        } else if (!job.ops.empty()) {
            pct = (int)(job.op * 100 / job.ops.size());
        }
        if (pct > 100) pct = 100;
        g.rect(28, y + 40, 424, 6, theme.track);
        if (pct > 0) g.rect(28, y + 40, 424 * pct / 100, 6, theme.accent);

        g.text(28, y + 58, g.fit(job.cur.empty() ? "…" : job.cur, 12, 300),
               12, theme.fg);
        g.text(460, y + 58,
               (std::to_string(job.op) + " из " + std::to_string(job.ops.size())).c_str(),
               11, theme.dim, TF_RIGHT);

        if (job.type == J_COPY)
            g.text(28, y + 80, (fmt_size(job.done_bytes) + " / " +
                                fmt_size(job.total_bytes)).c_str(),
                   11, theme.dim);
        else
            g.text(28, y + 80, "Esc — отмена", 11, theme.dim);

        bar_button(g, 320, y + 108, 136, 44, "ОТМЕНА", true);
    }

    void card(Gfx &g, int x, int y, int w, int h) {
        g.rect(x, y, w, h, theme.bg);
        g.frame(x, y, w, h, theme.line);
    }

    void draw_modal(Gfx &g) {
        if (modal == M_RENAME || modal == M_MKDIR) {
            card(g, 20, 232, 440, 292);
            g.text(40, 252, dlg_title.c_str(), 20, theme.fg, TF_BOLD);
            g.rect(40, 288, 400, 52, theme.surf2);
            g.frame(40, 288, 400, 52, theme.line2);
            std::string shown = g.fit(dlg_value, 16, 370);
            g.text(52, 306, shown, 16, theme.fg);
            int cx = 52 + g.textw(shown, 16);
            if (cx < 428) g.rect(cx + 1, 302, 2, 22, theme.accent);
            g.text(40, 352, g.fit(dlg_hint, 11, 400), 11, theme.dim);
            bar_button(g, 40, 448, 120, 48, "ОЧИСТИТЬ", !dlg_value.empty());
            bar_button(g, 176, 448, 130, 48, "ОТМЕНА", true);
            bar_button(g, 322, 448, 118, 48, "ОК", true, true);
            return;
        }
        if (modal == M_CONFIRM) {
            card(g, 20, 216, 440, 328);
            g.text(40, 240, "Удалить?", 20, theme.fg, TF_BOLD);
            std::string l1 = plural_obj(del_count);
            if (!del_size.empty()) l1 += " · " + del_size;
            g.text(40, 276, l1.c_str(), 14, theme.fg);
            g.text(40, 302, "Без корзины. Восстановить нельзя.", 12, theme.dim);
            if (del_has_dir)
                g.text(40, 324, "Папки удаляются со всем содержимым.", 12, theme.dim);
            int ny = 352;
            for (const std::string &n : del_names) {
                g.text(40, ny, g.fit("· " + n, 12, 390), 12, theme.dim);
                ny += 20;
            }
            bar_button(g, 40, 456, 180, 52, "ОТМЕНА", true);
            bar_button(g, 260, 456, 180, 52, "УДАЛИТЬ", true, true);
            return;
        }
        if (modal == M_PROPS) {
            card(g, 20, 140, 440, 492);
            g.text(40, 164, "Свойства", 20, theme.fg, TF_BOLD);
            g.text(40, 198, g.fit(props_name, 15, 400), 15, theme.fg, TF_BOLD);
            const char *labels[6] = {"ТИП", "РАЗМЕР", "ДАТА", "ПРАВА",
                                     "СВОБОДНО", "ОТКРЫТЬ С ПОМОЩЬЮ"};
            const std::string *vals[6] = {&props_type, &props_size, &props_date,
                                          &props_mode, &props_free, &props_open};
            int y = 236;
            for (int i = 0; i < 6; i++) {
                g.text(40, y, labels[i], 11, theme.dim);
                g.text(460, y - 2, g.fit(*vals[i], 13, 300), 13, theme.fg,
                       TF_RIGHT);
                y += 36;
            }
            g.text(40, 456,
                   props_mode == "—" ? "Права на vfat недоступны." : "",
                   11, theme.dim);
            bar_button(g, 40, 560, 180, 48, "ЗАКРЫТЬ", true);
            bar_button(g, 260, 560, 180, 48, "ОТКРЫТЬ", props_text, true);
            return;
        }
        if (modal == M_BUFFER) {
            card(g, 20, 236, 440, 304);
            g.text(40, 260, "Буфер", 20, theme.fg, TF_BOLD);
            g.text(40, 294, buf_mode.c_str(), 14, theme.fg);
            int y = 322;
            for (const std::string &n : buf_names) {
                g.text(40, y, g.fit("· " + n, 12, 390), 12, theme.dim);
                y += 22;
            }
            bar_button(g, 40, 460, 180, 48, "ОЧИСТИТЬ", clip.on);
            bar_button(g, 260, 460, 180, 48, "ЗАКРЫТЬ", true);
        }
    }

    void draw(Gfx &g) override {
        ui_bg(g);
        draw_header(g);
        draw_rows(g);
        draw_footer(g);
        if (selecting) draw_select_bar(g);
        if (menu_open) draw_menu(g);
        if (job.active) draw_job(g);
        if (modal != M_NONE) draw_modal(g);
    }

    // ------------------------------------------------------------ input
    void ui_tap_modal(int x, int y) {
        if (modal == M_RENAME || modal == M_MKDIR) {
            if (hit(x, y, 40, 448, 120, 48)) {
                dlg_value.clear();
                return;
            }
            if (hit(x, y, 176, 448, 130, 48)) {
                modal = M_NONE;
                return;
            }
            if (hit(x, y, 322, 448, 118, 48)) {
                commit_text();
                return;
            }
            return;
        }
        if (modal == M_CONFIRM) {
            if (hit(x, y, 40, 456, 180, 52)) {
                modal = M_NONE;
                return;
            }
            if (hit(x, y, 260, 456, 180, 52)) {
                std::vector<std::string> paths = selected_paths();
                modal = M_NONE;
                if (!paths.empty()) start_delete(paths);
                return;
            }
            return;
        }
        if (modal == M_PROPS) {
            if (hit(x, y, 40, 560, 180, 48)) {
                modal = M_NONE;
                return;
            }
            if (props_text && hit(x, y, 260, 560, 180, 48)) {
                std::string p = props_path;
                modal = M_NONE;
                app.open_path = p;
                app.open("notes");
                return;
            }
            return;
        }
        if (modal == M_BUFFER) {
            if (hit(x, y, 40, 460, 180, 48)) {
                clip.on = false;
                clip.move = false;
                clip.paths.clear();
                modal = M_NONE;
                app.toast("Буфер очищен");
                return;
            }
            if (hit(x, y, 260, 460, 180, 48)) modal = M_NONE;
            return;
        }
    }

    void ui_tap_menu(int x, int y) {
        if (y < MENU_Y) {
            menu_open = false;
            return;
        }
        if (y < MENU_Y + 44) {
            if (x >= 428) menu_open = false;
            return;
        }
        int row = (y - (MENU_Y + 44)) / 52;
        if (row < 0 || row >= 6) return;
        switch (row) {
        case 0:
            menu_open = false;
            start_paste();
            break;
        case 1:
            menu_open = false;
            open_mkdir();
            break;
        case 2:
            menu_open = false;
            show_hidden = !show_hidden;
            load(path);
            break;
        case 3:
            menu_open = false;
            sort_mode = (sort_mode + 1) % 3;
            resort();
            app.toast((std::string("Сортировка: ") + sort_short(sort_mode)).c_str(),
                      1400);
            break;
        case 4:
            menu_open = false;
            open_props(-1);
            break;
        case 5:
            menu_open = false;
            refresh();
            break;
        }
    }

    void ui_tap_header(int x, int y) {
        const int hy = 32;
        if (y < hy || y >= hy + 64) return;
        if (selecting) {
            if (x < 104) exit_select();
            else if (x >= 400) select_all_toggle();
            return;
        }
        if (x < 64) {
            app.open("home");
        } else if (x >= 300 && x < 370) {
            sort_mode = (sort_mode + 1) % 3;
            resort();
            app.toast((std::string("Сортировка: ") + sort_short(sort_mode)).c_str(),
                      1400);
        } else if (x >= 376 && x < 424) {
            show_hidden = !show_hidden;
            load(path);
            app.toast(show_hidden ? "Скрытые показаны" : "Скрытые убраны", 1400);
        } else if (x >= 428) {
            menu_open = true;
        }
    }

    bool tap(int x, int y) override {
        sync_gesture();
        if (job.active) {
            if (hit(x, y, 320, 700, 136, 44)) {
                job.cancel = true;
                run_job(0);
            }
            return true;
        }
        if (modal != M_NONE) {
            ui_tap_modal(x, y);
            return true;
        }
        if (menu_open) {
            ui_tap_menu(x, y);
            return true;
        }
        if (y >= 32 && y < 96) {
            ui_tap_header(x, y);
            return true;
        }
        if (selecting) {
            if (y >= 656 && y < 700) {
                if (hit(x, y, 24, 656, 138, 44)) start_paste();
                else if (hit(x, y, 170, 656, 138, 44)) {
                    std::vector<int> si = selected_indices();
                    if (si.size() == 1) open_props(si[0]);
                    else if (si.empty()) open_props(-1);
                    else app.toast("Выберите один объект");
                } else if (hit(x, y, 316, 656, 138, 44)) open_buffer();
                return true;
            }
            if (y >= 708 && y < 756) {
                std::vector<int> si = selected_indices();
                if (hit(x, y, 24, 708, 102, 48)) set_clip(si, false);
                else if (hit(x, y, 134, 708, 102, 48)) set_clip(si, true);
                else if (hit(x, y, 244, 708, 102, 48)) open_delete();
                else if (hit(x, y, 354, 708, 102, 48)) open_rename();
                return true;
            }
        }
        int idx = idx_at(y);
        if (idx < 0) return false;
        if (selecting) {
            if (idx == 0) {
                exit_select();
                up();
                return true;
            }
            toggle_sel(ents[(size_t)idx - 1].name);
            return true;
        }
        if (idx == 0) {
            up();
            return true;
        }
        const Ent &e = ents[(size_t)idx - 1];
        if (e.dir) {
            enter(e.name);
        } else if (is_text_name(e.name)) {
            app.open_path = join_path(path, e.name);
            app.open("notes");
        } else {
            app.toast("Нет приложения для этого типа");
        }
        return true;
    }

    bool longtap(int x, int y) override {
        (void)x;
        sync_gesture();
        if (drag_total > 14) return false;  // that was a scroll, not a hold
        if (modal != M_NONE || menu_open || job.active) return false;
        int idx = idx_at(y);
        if (idx <= 0) return false;
        if (!selecting) {
            selecting = true;
            sel.clear();
        }
        sel.insert(ents[(size_t)idx - 1].name);
        ensure_visible(idx);
        app.invalidate();
        return true;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x;
        (void)y;
        sync_gesture();
        if (modal != M_NONE || menu_open || job.active) return false;
        drag_total += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (dy == 0) return false;
        drag_px += dy;
        int rows = drag_px / ROW_H;
        if (rows == 0) return false;
        drag_px -= rows * ROW_H;
        int want = scroll - rows;
        int m = max_top();
        if (want < 0) want = 0;
        if (want > m) want = m;
        if (want == scroll) return false;
        scroll = want;
        return true;
    }

    bool key(int ksym, bool down) override {
        unsigned long k = (unsigned long)(unsigned)ksym;
        if (k == XK_Shift_L || k == XK_Shift_R) {
            shift_held = down;
            return false;
        }
        if (!down) return false;

        if (modal == M_RENAME || modal == M_MKDIR) {
            if (k == XK_Escape || k == XF86XK_Back) {
                modal = M_NONE;
                return true;
            }
            if (k == XK_Return || k == XK_KP_Enter) {
                commit_text();
                return true;
            }
            if (k == XK_BackSpace || k == XK_Delete) {
                pop_utf8(dlg_value);
                return true;
            }
            unsigned cp = shifted_cp(k, keysym_cp(k), shift_held);
            if (cp >= 0x20 && cp != 0x7F && cp != '/' && dlg_value.size() < 240)
                append_utf8(dlg_value, cp);
            return true;  // modal text field: swallow the rest
        }
        if (modal == M_CONFIRM) {
            if (k == XK_Escape || k == XF86XK_Back) modal = M_NONE;
            return true;
        }
        if (modal == M_PROPS || modal == M_BUFFER) {
            if (k == XK_Escape || k == XK_Return || k == XF86XK_Back)
                modal = M_NONE;
            return true;
        }
        if (menu_open) {
            if (k == XK_Escape || k == XF86XK_Back) {
                menu_open = false;
                return true;
            }
            return false;
        }
        if (job.active) {
            if (k == XK_Escape || k == XF86XK_Back) {
                job.cancel = true;
                run_job(0);
            }
            return true;
        }
        if (selecting) {
            if (k == XK_Escape || k == XK_BackSpace || k == XF86XK_Back) {
                exit_select();
                return true;
            }
            return false;
        }
        return false;
    }

    void tick(unsigned now) override {
        (void)now;
        if (job.active) run_job(SLICE_TICK);
    }

    bool animated() const override { return job.active; }
};

} // namespace

ScreenView *make_files() { return new Files(); }
