// notes — a real text editor bound to a .txt/.md file.
// Cursor (line, codepoint): tap places it, arrows/Home/End move it, Enter
// splits a line, Backspace joins. A separate "all notes" mode lists every note
// from the current directory and the known roots. The file picker is kept as
// "открыть…". Autosave is atomic (tmp + fsync + rename) with a 2 s debounce.
// All disk I/O happens in the constructor, on_show(), tick() or taps — never in
// draw().
#include "graphite.h"
#include "screens.h"
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

const size_t MAX_FILE = 2u * 1024u * 1024u;  // hard read limit
const size_t MAX_LINE = 1024;                // one huge line would kill draw()
const size_t MAX_LINES = 2000;
const int PICK_ROWS = 10;
const int PICK_Y = 152;

const int TEXT_X = 24;
const int TEXT_Y = 104;
const int TEXT_W = 440;
const int LH = 24;
const int TEXT_ROWS = 17;                    // 100..508, keyboard starts at 512

const int LIST_Y = 152;
const int LIST_ROW_H = 64;
const int LIST_ROWS = 9;

const int KB_Y = 512;
const int KB_PAD = 8;
const int KB_GAP = 4;
const int KB_ROW = 52;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void pop_cp(std::string &s) {
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

std::vector<std::string> cps(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (i + (size_t)len > s.size()) len = 1;
        out.push_back(s.substr(i, (size_t)len));
        i += (size_t)len;
    }
    return out;
}

int cp_len_at(const std::string &s, size_t i) {
    if (i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[i];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2
          : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (i + (size_t)n > s.size()) n = 1;
    return n;
}

int cp_count(const std::string &s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); i += (size_t)cp_len_at(s, i)) n++;
    return n;
}

size_t byte_at_col(const std::string &s, int col) {
    size_t b = 0;
    int c = 0;
    while (b < s.size() && c < col) {
        b += (size_t)cp_len_at(s, b);
        c++;
    }
    return b;
}

size_t prev_cp_start(const std::string &s, size_t b) {
    if (b == 0) return 0;
    b--;
    while (b > 0 && ((unsigned char)s[b] & 0xC0) == 0x80) b--;
    return b;
}

uint32_t cp_value(const std::string &c) {
    if (c.empty()) return 0;
    unsigned char b0 = (unsigned char)c[0];
    if (b0 < 0x80) return b0;
    if ((b0 & 0xE0) == 0xC0 && c.size() >= 2)
        return ((uint32_t)(b0 & 0x1F) << 6) | ((unsigned char)c[1] & 0x3F);
    if ((b0 & 0xF0) == 0xE0 && c.size() >= 3)
        return ((uint32_t)(b0 & 0x0F) << 12) |
               ((uint32_t)((unsigned char)c[1] & 0x3F) << 6) |
               ((unsigned char)c[2] & 0x3F);
    if ((b0 & 0xF8) == 0xF0 && c.size() >= 4)
        return ((uint32_t)(b0 & 0x07) << 18) |
               ((uint32_t)((unsigned char)c[1] & 0x3F) << 12) |
               ((uint32_t)((unsigned char)c[2] & 0x3F) << 6) |
               ((unsigned char)c[3] & 0x3F);
    return b0;
}

std::string upper_cp(const std::string &c) {
    if (c.size() == 1) {
        char ch = c[0];
        if (ch >= 'a' && ch <= 'z') return std::string(1, (char)(ch - 32));
        return c;
    }
    unsigned char b0 = (unsigned char)c[0];
    uint32_t cp = 0;
    int n = 0;
    if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; n = 1; }
    else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; n = 2; }
    else return c;
    for (int i = 1; i <= n && i < (int)c.size(); i++)
        cp = (cp << 6) | ((unsigned char)c[(size_t)i] & 0x3F);
    if (cp == 0x451) cp = 0x401;
    else if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
    else return c;
    std::string out;
    if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

std::string notes_count(int n) {
    char b[64];
    int m10 = n % 10, m100 = n % 100;
    const char *w = "заметок";
    if (m10 == 1 && m100 != 11) w = "заметка";
    else if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) w = "заметки";
    std::snprintf(b, sizeof b, "%d %s", n, w);
    return b;
}

// ---------------------------------------------------------------- paths

std::string home_dir() {
    const char *h = std::getenv("HOME");
    if (h && h[0] == '/') return h;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] == '/') return pw->pw_dir;
    return "/home/user";
}

std::string parent_dir(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t s = p.rfind('/');
    if (s == std::string::npos || s == 0) return "/";
    return p.substr(0, s);
}

std::string base_name(const std::string &p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string join(const std::string &dir, const std::string &name) {
    std::string out = dir;
    if (out.empty() || out.back() != '/') out += '/';
    return out + name;
}

std::string disp_path(const std::string &p) {
    std::string h = home_dir();
    if (p == h) return "~";
    if (p.size() > h.size() && p.compare(0, h.size(), h) == 0 && p[h.size()] == '/')
        return "~" + p.substr(h.size());
    return p;
}

bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool mtime_of(const std::string &p, time_t &t) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    t = st.st_mtime;
    return true;
}

std::string ext_of(const std::string &n) {
    size_t d = n.find_last_of('.');
    if (d == std::string::npos) return "";
    std::string e = n.substr(d + 1);
    for (char &c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

bool note_ext_ok(const std::string &n) {
    std::string e = ext_of(n);
    return e == "txt" || e == "md";
}

std::string fmt_size(unsigned long long n) {
    char b[48];
    if (n < 1024) {
        std::snprintf(b, sizeof b, "%llu Б", n);
        return b;
    }
    double v = (double)n / 1024.0;
    const char *u = "КБ";
    if (v >= 1024.0) { v /= 1024.0; u = "МБ"; }
    std::snprintf(b, sizeof b, "%.1f %s", v, u);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

std::string fmt_date(time_t t) {
    static const char *mon[] = {"янв", "фев", "мар", "апр", "мая", "июн",
                                "июл", "авг", "сен", "окт", "ноя", "дек"};
    struct tm tmv;
    localtime_r(&t, &tmv);
    time_t n = time(nullptr);
    struct tm now;
    localtime_r(&n, &now);
    char b[48];
    int m = tmv.tm_mon;
    if (m < 0 || m > 11) m = 0;
    if (tmv.tm_year == now.tm_year)
        std::snprintf(b, sizeof b, "%d %s, %02d:%02d", tmv.tm_mday, mon[m],
                      tmv.tm_hour, tmv.tm_min);
    else
        std::snprintf(b, sizeof b, "%d %s %d", tmv.tm_mday, mon[m],
                      tmv.tm_year + 1900);
    return b;
}

// Drop a trailing incomplete UTF-8 sequence after a byte-limit cut.
void utf8_trim(std::string &s) {
    if (s.empty()) return;
    size_t i = s.size();
    size_t cont = 0;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) { i--; cont++; }
    if (i == 0) { s.clear(); return; }
    unsigned char b = (unsigned char)s[i - 1];
    int need = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2
             : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (need > 0 && cont + 1 < (size_t)need) s.erase(i - 1);
}

// First line of a file, short enough for a list preview.
std::string preview_of(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return "";
    char b[161];
    size_t n = std::fread(b, 1, sizeof b - 1, f);
    std::fclose(f);
    b[n] = 0;
    std::string s(b, n);
    size_t nl = s.find('\n');
    if (nl != std::string::npos) s.erase(nl);
    if (!s.empty() && s.back() == '\r') s.pop_back();
    for (char &c : s)
        if (c == '\t') c = ' ';
    size_t a = 0;
    while (a < s.size() && s[a] == ' ') a++;
    s.erase(0, a);
    if (s.size() > 120) {
        s.resize(120);
        utf8_trim(s);
    }
    return s;
}

struct PickEnt {
    std::string name;
    bool dir = false;
    unsigned long long size = 0;
};

struct NoteEnt {
    std::string path;
    std::string name;
    std::string dir;
    unsigned long long size = 0;
    time_t mtime = 0;
    std::string preview;
    bool dup = false;
};

enum { K_CHAR, K_SHIFT, K_LAYER, K_LANG, K_SPACE, K_DEL, K_ENTER,
       K_LEFT, K_RIGHT, K_HOME, K_END, K_UNDO, K_REDO };

struct Key {
    int x = 0, y = 0, w = 0, h = 0;
    std::string label;
    int act = K_CHAR;
    std::string text;
};

// One visual (wrapped) row: a codepoint range of one logical line.
struct Row {
    int line = 0;
    int c0 = 0;
    int c1 = 0;
};

// Snapshot of a small line range + cursor, enough to undo one edit.
struct Snap {
    int at = 0;
    std::vector<std::string> lines;
    int cl = 0;
    int cc = 0;
};

struct UndoOp {
    Snap before;
    Snap after;
};

enum { M_EDIT, M_LIST, M_ACTIONS, M_RENAME, M_DELETE };

struct Notes : ScreenView {
    std::string path;
    std::vector<std::string> body;
    int lang = 0;    // 0 — ЙЦУКЕН, 1 — EN
    int layer = 0;   // 0 — буквы, 1 — цифры
    bool shift = false;
    std::vector<Key> keys;

    // cursor / layout / scroll
    int cur_line = 0;
    int cur_col = 0;        // codepoints
    int want_x = -1;        // preferred pixel column for up/down
    int scroll_row = 0;
    std::vector<Row> rows;
    std::vector<int> line_row0;
    std::vector<int> line_nrows;

    // autosave + undo
    bool dirty = false;
    unsigned last_edit = 0;
    unsigned long last_save = 0;
    bool has_mod = false;
    time_t last_mod = 0;
    off_t last_size = 0;
    ino_t last_ino = 0;
    bool truncated = false;
    std::vector<UndoOp> undo;
    int undo_pos = 0;

    // modes
    int mode = M_EDIT;
    int drag_px = 0;

    // all-notes list
    std::vector<NoteEnt> notes;
    int notes_top = 0;
    int notes_drag = 0;
    int dlg_idx = -1;
    std::string rename_buf;

    // picker overlay
    bool picker_open = false;
    bool pick_from_list = false;
    bool env_seen = false;
    std::string pick_dir;          // "" — root list
    std::vector<PickEnt> pick;
    std::string pick_err;
    int pick_top = 0;
    int pick_drag = 0;

    // glyph advance cache for wrapping (same sum as Gfx::textw)
    std::map<uint32_t, int> adv_cache;
    int adv_ascii[128];

    Notes() {
        for (int &v : adv_ascii) v = -1;
        path = home_dir() + "/.config/graphite/notes.txt";
        std::vector<std::string> nx;
        bool cut = false;
        if (read_file(path, nx, cut)) {
            body.swap(nx);
            truncated = cut;
            struct stat st; if (stat(path.c_str(), &st) == 0) { has_mod=true; last_mod=st.st_mtime; last_size=st.st_size; last_ino=st.st_ino; }
        }
        if (body.empty()) body.push_back("");
        cur_line = (int)body.size() - 1;
        cur_col = cp_count(body[(size_t)cur_line]);
        build();
    }

    const char *id() const override { return "notes"; }

    // ------------------------------------------------------------ storage
    static void mkdirs(const std::string &dir) {
        std::string cur;
        for (size_t i = 0; i < dir.size(); i++) {
            cur += dir[i];
            if (dir[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
        }
        mkdir(dir.c_str(), 0755);
    }

    // Reads up to 2 MB; sets cut when the file or a line was truncated.
    static bool read_file(const std::string &p, std::vector<std::string> &out,
                          bool &cut) {
        cut = false;
        out.clear();
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) return false;
        FILE *f = std::fopen(p.c_str(), "rb");
        if (!f) return false;

        std::string cur;
        bool skip = false;      // skipping the rest of an over-long line
        size_t got = 0;
        int c;
        while ((c = std::fgetc(f)) != EOF) {
            if (++got > MAX_FILE) { cut = true; break; }
            if (c == '\n') {
                if (skip) utf8_trim(cur);
                out.push_back(cur);
                cur.clear();
                skip = false;
                if (out.size() >= MAX_LINES) { cut = true; break; }
                continue;
            }
            if (c == '\r') continue;
            if (skip) continue;
            if (cur.size() >= MAX_LINE) { cut = true; skip = true; continue; }
            cur.push_back((char)c);
        }
        std::fclose(f);
        if (!cur.empty()) {
            if (skip || cut) utf8_trim(cur);
            out.push_back(cur);
        }
        return true;
    }

    // tmp + fsync + rename + fsync(dir): a power cut never leaves half a note.
    bool save_note(bool quiet = false) {
        if (truncated) { app.toast("Усечённый файл: сохранение запрещено"); return false; }
        struct stat current;
        if (has_mod && (stat(path.c_str(), &current) != 0 || current.st_mtime != last_mod || current.st_size != last_size || current.st_ino != last_ino)) {
            app.toast("Конфликт: файл изменён извне"); return false;
        }
        std::string dir = parent_dir(path);
        if (!dir.empty()) mkdirs(dir);
        std::string tmp = path + ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (!f) {
            app.toast("Не удалось сохранить");
            return false;
        }
        bool ok = true;
        for (const std::string &l : body) {
            if (std::fwrite(l.data(), 1, l.size(), f) != l.size()) { ok = false; break; }
            if (std::fputc('\n', f) == EOF) { ok = false; break; }
        }
        if (ok && std::fflush(f) != 0) ok = false;
        if (ok && fsync(fileno(f)) != 0) ok = false;
        if (std::fclose(f) != 0) ok = false;
        if (!ok) {
            unlink(tmp.c_str());
            app.toast("Ошибка записи");
            return false;
        }
        if (rename(tmp.c_str(), path.c_str()) != 0) {
            unlink(tmp.c_str());
            app.toast("Ошибка записи");
            return false;
        }
        int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) {
            fsync(dfd);
            close(dfd);
        }
        dirty = false;
        has_mod = stat(path.c_str(), &current) == 0;
        if (has_mod) { last_mod=current.st_mtime; last_size=current.st_size; last_ino=current.st_ino; }
        last_save = now_ms();
        if (!quiet) app.toast("Сохранено");
        return true;
    }

    bool autosave_now() {
        if (!dirty) return true;
        return save_note(true);
    }

    static unsigned long now_ms() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (unsigned long)ts.tv_sec * 1000ul +
               (unsigned long)ts.tv_nsec / 1000000ul;
    }

    std::string mod_label() const {
        if (!has_mod) return "не сохранено";
        struct tm tmv;
        localtime_r(&last_mod, &tmv);
        char b[32];
        std::snprintf(b, sizeof b, "изменено %02d:%02d", tmv.tm_hour, tmv.tm_min);
        return b;
    }

    bool open_file(const std::string &p) {
        if (!autosave_now()) return false;
        std::vector<std::string> nx;
        bool cut = false;
        if (!read_file(p, nx, cut)) {
            app.toast("Не удалось открыть файл");
            return false;
        }
        path = p;
        body.swap(nx);
        truncated = cut;
        if (body.empty()) body.push_back("");
        struct stat st; has_mod=stat(p.c_str(), &st)==0 && S_ISREG(st.st_mode); if(has_mod){last_mod=st.st_mtime;last_size=st.st_size;last_ino=st.st_ino;}
        last_save = 0;
        dirty = false;
        cur_line = 0;
        cur_col = 0;
        want_x = -1;
        scroll_row = 0;
        undo.clear();
        undo_pos = 0;
        layout_all();
        ensure_cursor_visible();
        if (cut) app.toast("Файл обрезан: только чтение, сохраните копию");
        return true;
    }

    // ------------------------------------------------------------ wrapping
    int adv_at(const std::string &s, size_t b) {
        unsigned char c = (unsigned char)s[b];
        if (c < 0x80) {
            int &v = adv_ascii[c];
            if (v < 0) v = app.g ? app.g->textw(std::string(1, (char)c), 15) : 7;
            return v;
        }
        int n = cp_len_at(s, b);
        uint32_t key = cp_value(s.substr(b, (size_t)n));
        auto it = adv_cache.find(key);
        if (it != adv_cache.end()) return it->second;
        int w = app.g ? app.g->textw(s.substr(b, (size_t)n), 15) : 7;
        adv_cache[key] = w;
        return w;
    }

    void wrap_line(int li, std::vector<Row> &out) {
        const std::string &ln = body[(size_t)li];
        int n = cp_count(ln);
        if (n == 0) {
            out.push_back(Row{li, 0, 0});
            return;
        }
        std::vector<int> offs;
        std::vector<int> cw;
        offs.reserve((size_t)n + 1);
        cw.reserve((size_t)n);
        for (size_t b = 0; b < ln.size();) {
            offs.push_back((int)b);
            cw.push_back(adv_at(ln, b));
            b += (size_t)cp_len_at(ln, b);
        }
        offs.push_back((int)ln.size());

        int start = 0, w = 0, last_sp = -1;
        for (int i = 0; i < n; i++) {
            int cwi = cw[(size_t)i];
            if (i > start && w + cwi > TEXT_W) {
                int cut = (last_sp >= start) ? last_sp + 1 : i;
                if (cut <= start) cut = start + 1;
                out.push_back(Row{li, start, cut});
                start = cut;
                w = 0;
                last_sp = -1;
                for (int k = start; k <= i; k++) {
                    w += cw[(size_t)k];
                    if (ln[(size_t)offs[(size_t)k]] == ' ') last_sp = k;
                }
            } else {
                w += cwi;
                if (ln[(size_t)offs[(size_t)i]] == ' ') last_sp = i;
            }
        }
        out.push_back(Row{li, start, n});
    }

    void layout_all() {
        rows.clear();
        line_row0.assign(body.size(), 0);
        line_nrows.assign(body.size(), 1);
        std::vector<Row> tmp;
        for (int i = 0; i < (int)body.size(); i++) {
            tmp.clear();
            wrap_line(i, tmp);
            line_row0[(size_t)i] = (int)rows.size();
            line_nrows[(size_t)i] = (int)tmp.size();
            rows.insert(rows.end(), tmp.begin(), tmp.end());
        }
    }

    void rewrap_line(int li) {
        if (li < 0 || li >= (int)body.size()) return;
        if (line_row0.size() != body.size()) {
            layout_all();
            return;
        }
        std::vector<Row> tmp;
        wrap_line(li, tmp);
        int start = line_row0[(size_t)li];
        int oldn = line_nrows[(size_t)li];
        if (start + oldn > (int)rows.size()) {
            layout_all();
            return;
        }
        rows.erase(rows.begin() + start, rows.begin() + start + oldn);
        rows.insert(rows.begin() + start, tmp.begin(), tmp.end());
        int d = (int)tmp.size() - oldn;
        line_nrows[(size_t)li] = (int)tmp.size();
        for (int j = li + 1; j < (int)body.size(); j++)
            line_row0[(size_t)j] += d;
    }

    void relayout_from(int li) {
        if (li < 0) li = 0;
        if (li > (int)body.size()) li = (int)body.size();
        int cut = (li < (int)line_row0.size()) ? line_row0[(size_t)li]
                                               : (int)rows.size();
        if (cut < 0) cut = 0;
        if (cut > (int)rows.size()) cut = (int)rows.size();
        rows.resize((size_t)cut);
        line_row0.resize(body.size());
        line_nrows.resize(body.size());
        std::vector<Row> tmp;
        for (int j = li; j < (int)body.size(); j++) {
            tmp.clear();
            wrap_line(j, tmp);
            line_row0[(size_t)j] = (int)rows.size();
            line_nrows[(size_t)j] = (int)tmp.size();
            rows.insert(rows.end(), tmp.begin(), tmp.end());
        }
    }

    // ------------------------------------------------------------ cursor
    void clamp_cursor() {
        if (body.empty()) body.push_back("");
        if (cur_line < 0) cur_line = 0;
        if (cur_line >= (int)body.size()) cur_line = (int)body.size() - 1;
        int n = cp_count(body[(size_t)cur_line]);
        if (cur_col < 0) cur_col = 0;
        if (cur_col > n) cur_col = n;
    }

    int row_of(int line, int col) const {
        if (line < 0 || line >= (int)line_row0.size()) return 0;
        int first = line_row0[(size_t)line];
        int n = line_nrows[(size_t)line];
        for (int r = first; r < first + n && r < (int)rows.size(); r++) {
            const Row &w = rows[(size_t)r];
            if (col >= w.c0 && col < w.c1) return r;
        }
        int last = first + n - 1;
        if (last < 0) last = 0;
        if (last >= (int)rows.size()) last = (int)rows.size() - 1;
        return last < 0 ? 0 : last;
    }

    int row_prefix_w(const Row &r, int col) {
        if (col <= r.c0) return 0;
        if (col > r.c1) col = r.c1;
        const std::string &ln = body[(size_t)r.line];
        int w = 0;
        size_t b = byte_at_col(ln, r.c0);
        for (int c = r.c0; c < col; c++) {
            w += adv_at(ln, b);
            b += (size_t)cp_len_at(ln, b);
            if (b > ln.size()) break;
        }
        return w;
    }

    int col_of_row_x(int r, int x) {
        if (r < 0 || r >= (int)rows.size()) return 0;
        const Row &rw = rows[(size_t)r];
        const std::string &ln = body[(size_t)rw.line];
        size_t b = byte_at_col(ln, rw.c0);
        int w = 0;
        for (int c = rw.c0; c < rw.c1; c++) {
            int cw = adv_at(ln, b);
            if (x < TEXT_X + w + cw / 2) return c;
            w += cw;
            b += (size_t)cp_len_at(ln, b);
            if (b > ln.size()) break;
        }
        return rw.c1;
    }

    void ensure_cursor_visible() {
        clamp_cursor();
        if (rows.empty()) layout_all();
        int r = row_of(cur_line, cur_col);
        int maxs = (int)rows.size() - TEXT_ROWS;
        if (maxs < 0) maxs = 0;
        if (r < scroll_row) scroll_row = r;
        if (r >= scroll_row + TEXT_ROWS) scroll_row = r - TEXT_ROWS + 1;
        if (scroll_row > maxs) scroll_row = maxs;
        if (scroll_row < 0) scroll_row = 0;
    }

    void mark_dirty() {
        dirty = true;
        last_edit = app.now;
    }

    void insert_text(const std::string &t) {
        if (truncated) { app.toast("Файл усечён — только чтение"); return; }
        if (t.empty()) return;
        std::string &ln = body[(size_t)cur_line];
        if (ln.size() + t.size() > MAX_LINE) {
            app.toast("Строка слишком длинная");
            return;
        }
        size_t b = byte_at_col(ln, cur_col);
        Snap before{cur_line, {ln}, cur_line, cur_col};
        ln.insert(b, t);
        cur_col += cp_count(t);
        Snap after{cur_line, {ln}, cur_line, cur_col};
        push_undo(before, after);
        rewrap_line(cur_line);
        want_x = -1;
        mark_dirty();
        ensure_cursor_visible();
    }

    void insert_enter() {
        if (truncated) { app.toast("Файл усечён — только чтение"); return; }
        if (body.size() >= MAX_LINES) {
            app.toast("Слишком много строк");
            return;
        }
        int at = cur_line;
        std::string &ln = body[(size_t)at];
        size_t b = byte_at_col(ln, cur_col);
        Snap before{at, {ln}, at, cur_col};
        std::string left = ln.substr(0, b);
        std::string right = ln.substr(b);
        body[(size_t)at] = left;
        body.insert(body.begin() + at + 1, right);
        cur_line = at + 1;
        cur_col = 0;
        want_x = -1;
        Snap after{at, {left, right}, cur_line, cur_col};
        push_undo(before, after);
        relayout_from(at);
        mark_dirty();
        ensure_cursor_visible();
    }

    void backspace() {
        if (truncated) { app.toast("Файл усечён — только чтение"); return; }
        if (cur_col > 0) {
            int at = cur_line;
            std::string &ln = body[(size_t)at];
            size_t b = byte_at_col(ln, cur_col);
            size_t b0 = prev_cp_start(ln, b);
            Snap before{at, {ln}, at, cur_col};
            ln.erase(b0, b - b0);
            cur_col--;
            Snap after{at, {ln}, at, cur_col};
            push_undo(before, after);
            rewrap_line(at);
            want_x = -1;
            mark_dirty();
            ensure_cursor_visible();
            return;
        }
        if (cur_line > 0) {
            int at = cur_line - 1;
            std::string prev = body[(size_t)at];
            std::string cur = body[(size_t)cur_line];
            int pcol = cp_count(prev);
            Snap before{at, {prev, cur}, cur_line, cur_col};
            prev += cur;
            body[(size_t)at] = prev;
            body.erase(body.begin() + cur_line);
            cur_line = at;
            cur_col = pcol;
            want_x = -1;
            Snap after{at, {prev}, cur_line, cur_col};
            push_undo(before, after);
            relayout_from(at);
            mark_dirty();
            ensure_cursor_visible();
        }
    }

    void del_forward() {
        if (truncated) { app.toast("Файл усечён — только чтение"); return; }
        if (cur_col < cp_count(body[(size_t)cur_line])) {
            int at = cur_line;
            std::string &ln = body[(size_t)at];
            size_t b = byte_at_col(ln, cur_col);
            int n = cp_len_at(ln, b);
            Snap before{at, {ln}, at, cur_col};
            ln.erase(b, (size_t)n);
            Snap after{at, {ln}, at, cur_col};
            push_undo(before, after);
            rewrap_line(at);
            want_x = -1;
            mark_dirty();
            ensure_cursor_visible();
            return;
        }
        if (cur_line + 1 < (int)body.size()) {
            int at = cur_line;
            std::string a = body[(size_t)at];
            std::string b = body[(size_t)at + 1];
            Snap before{at, {a, b}, at, cur_col};
            a += b;
            body[(size_t)at] = a;
            body.erase(body.begin() + at + 1);
            Snap after{at, {a}, at, cur_col};
            push_undo(before, after);
            relayout_from(at);
            want_x = -1;
            mark_dirty();
            ensure_cursor_visible();
        }
    }

    void move_left() {
        want_x = -1;
        if (cur_col > 0) {
            cur_col--;
        } else if (cur_line > 0) {
            cur_line--;
            cur_col = cp_count(body[(size_t)cur_line]);
        }
        ensure_cursor_visible();
    }

    void move_right() {
        want_x = -1;
        if (cur_col < cp_count(body[(size_t)cur_line])) {
            cur_col++;
        } else if (cur_line + 1 < (int)body.size()) {
            cur_line++;
            cur_col = 0;
        }
        ensure_cursor_visible();
    }

    void move_vert(int dir) {
        if (rows.empty()) layout_all();
        int r = row_of(cur_line, cur_col);
        int target = r + dir;
        if (target < 0 || target >= (int)rows.size()) return;
        int x = want_x >= 0 ? want_x : row_prefix_w(rows[(size_t)r], cur_col);
        want_x = x;
        const Row &t = rows[(size_t)target];
        cur_line = t.line;
        cur_col = col_of_row_x(target, TEXT_X + x);
        ensure_cursor_visible();
    }

    void move_home() {
        cur_col = 0;
        want_x = -1;
        ensure_cursor_visible();
    }

    void move_end() {
        cur_col = cp_count(body[(size_t)cur_line]);
        want_x = -1;
        ensure_cursor_visible();
    }

    // ------------------------------------------------------------ undo
    void push_undo(const Snap &b, const Snap &a) {
        if (undo_pos < (int)undo.size())
            undo.erase(undo.begin() + undo_pos, undo.end());
        undo.push_back(UndoOp{b, a});
        if (undo.size() > 20) undo.erase(undo.begin());
        undo_pos = (int)undo.size();
    }

    void apply_snaps(const Snap &cur, const Snap &want) {
        int at = cur.at;
        if (at < 0) at = 0;
        if (at > (int)body.size()) at = (int)body.size();
        size_t n = cur.lines.size();
        if (at + (int)n > (int)body.size()) n = body.size() - (size_t)at;
        body.erase(body.begin() + at, body.begin() + at + (long)n);
        int iat = want.at;
        if (iat < 0) iat = 0;
        if (iat > (int)body.size()) iat = (int)body.size();
        body.insert(body.begin() + iat, want.lines.begin(), want.lines.end());
        if (body.empty()) body.push_back("");
    }

    void do_undo() {
        if (undo_pos <= 0) {
            app.toast("Отменить нечего");
            return;
        }
        UndoOp u = undo[(size_t)undo_pos - 1];
        apply_snaps(u.after, u.before);
        cur_line = u.before.cl;
        cur_col = u.before.cc;
        undo_pos--;
        int at = std::min(u.before.at, u.after.at);
        relayout_from(at);
        want_x = -1;
        clamp_cursor();
        mark_dirty();
        ensure_cursor_visible();
    }

    void do_redo() {
        if (undo_pos >= (int)undo.size()) {
            app.toast("Повторить нечего");
            return;
        }
        UndoOp u = undo[(size_t)undo_pos];
        apply_snaps(u.before, u.after);
        cur_line = u.after.cl;
        cur_col = u.after.cc;
        undo_pos++;
        int at = std::min(u.before.at, u.after.at);
        relayout_from(at);
        want_x = -1;
        clamp_cursor();
        mark_dirty();
        ensure_cursor_visible();
    }

    // ------------------------------------------------------------ notes list
    static std::vector<std::string> roots() {
        std::vector<std::string> r;
        const char *pre[] = {"/media/sd", "/media/sd/Notes", "/media/sd/Books",
                             "/home/user"};
        for (const char *p : pre)
            if (is_dir(p)) r.push_back(p);
        std::string cfgdir = home_dir() + "/.config/graphite";
        if (is_dir(cfgdir)) r.push_back(cfgdir);
        return r;
    }

    void scan_notes() {
        notes.clear();
        std::set<std::string> seen;
        auto add_dir = [&](const std::string &dir) {
            DIR *d = opendir(dir.c_str());
            if (!d) return;
            struct dirent *de;
            while ((de = readdir(d)) != nullptr) {
                const char *nm = de->d_name;
                if (!std::strcmp(nm, ".") || !std::strcmp(nm, "..")) continue;
                if (!note_ext_ok(nm)) continue;
                std::string full = join(dir, nm);
                struct stat st;
                if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                if (!seen.insert(full).second) continue;
                NoteEnt e;
                e.path = full;
                e.name = nm;
                e.dir = parent_dir(full);
                e.size = (unsigned long long)st.st_size;
                e.mtime = st.st_mtime;
                e.preview = preview_of(full);
                notes.push_back(e);
                if (notes.size() >= 200) break;
            }
            closedir(d);
        };
        add_dir(parent_dir(path));
        for (const std::string &r : roots()) {
            if (notes.size() >= 200) break;
            add_dir(r);
        }
        std::sort(notes.begin(), notes.end(), [](const NoteEnt &a, const NoteEnt &b) {
            if (a.mtime != b.mtime) return a.mtime > b.mtime;
            int c = strcasecmp(a.name.c_str(), b.name.c_str());
            if (c) return c < 0;
            return a.name < b.name;
        });
        // Same file name in two folders: show the folder in the meta line.
        std::map<std::string, int> count;
        for (const NoteEnt &e : notes) count[e.name]++;
        for (NoteEnt &e : notes) e.dup = count[e.name] > 1;
    }

    void open_list() {
        if (!autosave_now()) return;  // save failed; stay in the editor
        mode = M_LIST;
        dlg_idx = -1;
        notes_top = 0;
        notes_drag = 0;
        drag_px = 0;
        scan_notes();
        picker_open = false;
    }

    int list_max_top() const {
        int m = (int)notes.size() - LIST_ROWS;
        return m > 0 ? m : 0;
    }

    void open_note(const std::string &p) {
        if (p == path) {
            mode = M_EDIT;
            ensure_cursor_visible();
            return;
        }
        if (!open_file(p)) return;
        mode = M_EDIT;
        notes_top = 0;
        notes_drag = 0;
        ensure_cursor_visible();
    }

    void create_in(std::string dir) {
        if (!autosave_now()) return;
        if (dir.empty() || !is_dir(dir)) dir = home_dir() + "/.config/graphite";
        mkdirs(dir);
        std::string cand;
        for (unsigned long i = 1;; i++) {
            char b[64];
            if (i == 1) std::snprintf(b,sizeof b,"новая-заметка.txt");
            else std::snprintf(b, sizeof b, "новая-заметка-%lu.txt", i);
            cand = join(dir, b);
            int fd = open(cand.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0644);
            if(fd>=0){close(fd);break;}
            if(errno!=EEXIST){app.toast("Не удалось создать файл");return;}
        }
        path = cand;
        truncated = false;
        body.assign(1, "");
        struct stat st; has_mod=stat(path.c_str(), &st)==0; if(has_mod){last_mod=st.st_mtime;last_size=st.st_size;last_ino=st.st_ino;}
        last_save = 0;
        dirty = false;
        cur_line = 0;
        cur_col = 0;
        want_x = -1;
        scroll_row = 0;
        undo.clear();
        undo_pos = 0;
        layout_all();
        picker_open = false;
        mode = M_EDIT;
        app.toast(("Создан " + base_name(path)).c_str());
    }

    void start_rename() {
        if (dlg_idx < 0 || dlg_idx >= (int)notes.size()) {
            mode = M_LIST;
            return;
        }
        rename_buf = notes[(size_t)dlg_idx].name;
        layer = 0;
        shift = false;
        build();
        mode = M_RENAME;
    }

    void rename_confirm() {
        if (dlg_idx < 0 || dlg_idx >= (int)notes.size()) {
            mode = M_LIST;
            return;
        }
        std::string nn = trim(rename_buf);
        if (nn.empty() || nn == "." || nn == ".." || nn.find('/') != std::string::npos ||
            nn.size() > 96) {
            app.toast("Недопустимое имя");
            return;
        }
        std::string ext = ext_of(nn);
        if (ext != "txt" && ext != "md") nn += ".txt";
        std::string old = notes[(size_t)dlg_idx].path;
        std::string np = join(parent_dir(old), nn);
        if (np == old) {
            mode = M_LIST;
            return;
        }
        if (exists(np)) {
            app.toast("Такой файл уже есть");
            return;
        }
        if (rename(old.c_str(), np.c_str()) != 0) {
            app.toast("Не удалось переименовать");
            return;
        }
        if (path == old) {
            path = np;
            has_mod = mtime_of(path, last_mod);
        }
        mode = M_LIST;
        dlg_idx = -1;
        scan_notes();
        if (notes_top > list_max_top()) notes_top = list_max_top();
        app.toast("Переименовано");
    }

    void delete_confirm() {
        if (dlg_idx < 0 || dlg_idx >= (int)notes.size()) {
            mode = M_LIST;
            return;
        }
        std::string old = notes[(size_t)dlg_idx].path;
        if (unlink(old.c_str()) != 0) {
            app.toast("Не удалось удалить");
            mode = M_LIST;
            return;
        }
        if (path == old) {
            std::string def = home_dir() + "/.config/graphite/notes.txt";
            if (def != old && exists(def)) {
                open_file(def);
            } else {
                path = def;
                body.assign(1, "");
                cur_line = 0;
                cur_col = 0;
                scroll_row = 0;
                want_x = -1;
                undo.clear();
                undo_pos = 0;
                has_mod = false;
                last_mod = 0;
                dirty = false;
                layout_all();
                save_note(true);
            }
        }
        mode = M_LIST;
        dlg_idx = -1;
        scan_notes();
        if (notes_top > list_max_top()) notes_top = list_max_top();
        app.toast("Удалено");
    }

    // ------------------------------------------------------------ picker
    int pick_total() const {
        return pick_dir.empty() ? (int)pick.size() : (int)pick.size() + 1;
    }

    void load_list() {
        pick.clear();
        pick_err.clear();
        pick_top = 0;
        pick_drag = 0;

        if (pick_dir.empty()) {
            for (const std::string &p : roots()) pick.push_back(PickEnt{p, true, 0});
            if (pick.empty() && is_dir(home_dir()))
                pick.push_back(PickEnt{home_dir(), true, 0});
            if (pick.empty()) pick_err = "Нет доступных папок";
            return;
        }

        DIR *d = opendir(pick_dir.c_str());
        if (!d) {
            pick_err = "Нет доступа";
            return;
        }
        std::vector<PickEnt> out;
        struct dirent *de;
        while ((de = readdir(d)) != nullptr) {
            const char *nm = de->d_name;
            if (!std::strcmp(nm, ".") || !std::strcmp(nm, "..")) continue;
            std::string full = join(pick_dir, nm);
            struct stat st;
            if (stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                out.push_back(PickEnt{nm, true, 0});
            } else if (S_ISREG(st.st_mode) && note_ext_ok(nm)) {
                out.push_back(PickEnt{nm, false, (unsigned long long)st.st_size});
            }
            if (out.size() >= 400) break;
        }
        closedir(d);
        std::sort(out.begin(), out.end(), [](const PickEnt &a, const PickEnt &b) {
            if (a.dir != b.dir) return a.dir;
            int c = strcasecmp(a.name.c_str(), b.name.c_str());
            if (c) return c < 0;
            return a.name < b.name;
        });
        pick.swap(out);
        if (pick.empty()) pick_err = "Нет файлов .txt/.md";
    }

    void open_picker(bool from_list = false) {
        if (!autosave_now()) return;
        pick_from_list = from_list;
        picker_open = true;
        mode = M_EDIT;
        pick_dir = parent_dir(path);
        load_list();
    }

    void close_picker() {
        picker_open = false;
        if (pick_from_list) {
            mode = M_LIST;
            scan_notes();
        } else {
            mode = M_EDIT;
        }
        pick_from_list = false;
    }

    void pick_up() {
        if (pick_dir.empty()) return;
        std::vector<std::string> r = roots();
        if (std::find(r.begin(), r.end(), pick_dir) != r.end()) {
            pick_dir.clear();
        } else {
            std::string p = parent_dir(pick_dir);
            pick_dir = (p == pick_dir) ? "" : p;
        }
        load_list();
    }

    // ------------------------------------------------------------ keyboard
    void build() {
        keys.clear();
        const int pad = KB_PAD, gap = KB_GAP, rowh = KB_ROW;
        const int inner = 480 - pad * 2;

        auto lay = [&](const std::vector<std::string> &ch, int x0, int width, int y) {
            int n = (int)ch.size();
            if (!n) return;
            int base = (width - gap * (n - 1)) / n;
            int extra = width - (base * n + gap * (n - 1));
            int x = x0;
            for (int i = 0; i < n; i++) {
                Key k;
                k.x = x;
                k.y = y;
                k.w = base + (i < extra ? 1 : 0);
                k.h = rowh;
                k.act = K_CHAR;
                k.text = shift ? upper_cp(ch[(size_t)i]) : ch[(size_t)i];
                k.label = k.text;
                keys.push_back(k);
                x += k.w + gap;
            }
        };

        int y0 = KB_Y + pad;

        // navigation row: cursor + undo/redo
        {
            const char *lab[6] = {"←", "→", "HOME", "END", "UNDO", "REDO"};
            int acts[6] = {K_LEFT, K_RIGHT, K_HOME, K_END, K_UNDO, K_REDO};
            int n = 6;
            int base = (inner - gap * (n - 1)) / n;
            int extra = inner - (base * n + gap * (n - 1));
            int x = pad;
            for (int i = 0; i < n; i++) {
                Key k;
                k.x = x;
                k.y = y0;
                k.w = base + (i < extra ? 1 : 0);
                k.h = rowh;
                k.act = acts[i];
                k.label = lab[i];
                keys.push_back(k);
                x += k.w + gap;
            }
        }

        std::vector<std::string> r0, r1, r2;
        if (layer == 0) {
            if (lang == 0) {
                r0 = cps("йцукенгшщзх");
                r1 = cps("фывапролджэ");
                r2 = cps("ячсмитьбюъ");
            } else {
                r0 = cps("qwertyuiop");
                r1 = cps("asdfghjkl;");
                r2 = cps("zxcvbnm,./");
            }
        } else {
            r0 = cps("1234567890");
            r1 = cps("-/:;()&\"'@");
            r2 = cps(".,?!_+=*%#~");
        }

        int yl = y0 + rowh + gap;
        lay(r0, pad, inner, yl);
        lay(r1, pad, inner, yl + (rowh + gap));

        int y2 = yl + 2 * (rowh + gap);
        if (layer == 0) {
            int sw = 52;
            int x = pad;
            Key sh;
            sh.x = x;
            sh.y = y2;
            sh.w = sw;
            sh.h = rowh;
            sh.act = K_SHIFT;
            sh.label = "SHIFT";
            keys.push_back(sh);
            lay(r2, pad + sw + gap, inner - sw - gap, y2);
        } else {
            lay(r2, pad, inner, y2);
        }

        // bottom row: 123/АБВ · EN/RU · ПРОБЕЛ · DEL · ВВОД
        int y3 = yl + 3 * (rowh + gap);
        const double units[5] = {1.2, 1.0, 4.0, 1.0, 2.2};
        int avail = inner - gap * 4;
        double total = 0;
        for (double u : units) total += u;
        int x = pad;
        int acts[5] = {K_LAYER, K_LANG, K_SPACE, K_DEL, K_ENTER};
        const char *labels[5] = {layer ? "АБВ" : "123", lang ? "РУС" : "ENG",
                                 "ПРОБЕЛ", "DEL", "ВВОД"};
        for (int i = 0; i < 5; i++) {
            Key k;
            k.x = x;
            k.y = y3;
            k.w = (i == 4) ? (pad + inner - x) : (int)(avail * units[i] / total);
            k.h = rowh;
            k.act = acts[i];
            k.label = labels[i];
            keys.push_back(k);
            x += k.w + gap;
        }
    }

    // ------------------------------------------------------------ drawing
    void draw(Gfx &g) override {
        if (picker_open) {
            draw_picker(g);
            return;
        }
        if (mode == M_RENAME) {
            draw_rename(g);
            return;
        }
        if (mode == M_DELETE) {
            draw_delete(g);
            return;
        }
        if (mode == M_LIST || mode == M_ACTIONS) {
            draw_list(g);
            if (mode == M_ACTIONS) draw_actions(g);
            return;
        }
        draw_editor(g);
    }

    void draw_keys(Gfx &g) {
        g.rect(0, KB_Y, g.cv.w, 800 - KB_Y, theme.bar);
        g.hline(0, KB_Y, g.cv.w, theme.line2);
        for (const Key &k : keys) {
            bool enter = k.act == K_ENTER;
            bool on = (k.act == K_SHIFT && shift) || (k.act == K_LAYER && layer);
            bool dead = (k.act == K_UNDO && undo_pos <= 0) ||
                        (k.act == K_REDO && undo_pos >= (int)undo.size());
            uint16_t fill = enter ? theme.accent : (on ? theme.sel : theme.surf);
            uint16_t col = enter ? theme.onacc
                                 : (k.act == K_CHAR ? theme.fg
                                                    : (on ? theme.accent : theme.dim));
            if (dead) col = theme.dim;
            int px = enter ? 14 : (k.act == K_CHAR ? 16 : 12);
            int flags = TF_CENTER | (enter ? TF_BOLD : 0);
            g.rect(k.x, k.y, k.w, k.h, fill);
            g.text(k.x + k.w / 2, k.y + (k.h - px) / 2 - 1, k.label, px, col, flags);
        }
    }

    void draw_editor(Gfx &g) {
        ui_bg(g);

        icon(g, "back", 15, 53, 22, theme.fg);
        g.text(60, 42, "Заметки", 24, theme.fg, TF_LIGHT);

        std::string sub = base_name(path);
        sub += dirty ? " · не сохранено" : " · " + mod_label();
        g.text(44, 66, g.fit(sub, 11, 282), 11, theme.dim);

        icon(g, "note", 344, 53, 20, theme.fg);
        g.text(470, 44, "СОХРАНИТЬ", 11, theme.accent, TF_RIGHT);
        g.hline(0, 96, g.cv.w, theme.line2);

        if (rows.empty()) layout_all();
        int first = scroll_row;
        int last = std::min((int)rows.size(), first + TEXT_ROWS);
        for (int r = first; r < last; r++) {
            const Row &rw = rows[(size_t)r];
            const std::string &ln = body[(size_t)rw.line];
            size_t b0 = byte_at_col(ln, rw.c0);
            size_t b1 = byte_at_col(ln, rw.c1);
            int y = TEXT_Y + (r - first) * LH;
            if (b1 > b0)
                g.text(TEXT_X, y, ln.substr(b0, b1 - b0), 15, theme.fg);
        }

        // scrollbar, only when the note is longer than the viewport
        if ((int)rows.size() > TEXT_ROWS) {
            int track_h = TEXT_ROWS * LH;
            int maxs = (int)rows.size() - TEXT_ROWS;
            int th = track_h * TEXT_ROWS / (int)rows.size();
            if (th < 24) th = 24;
            int ty = TEXT_Y + (maxs > 0 ? (track_h - th) * scroll_row / maxs : 0);
            g.rect(470, TEXT_Y, 2, track_h, theme.line2);
            g.rect(470, ty, 2, th, theme.dim);
        }

        // cursor: a vertical bar at the glyph boundary
        int cr = row_of(cur_line, cur_col);
        if (cr >= first && cr < first + TEXT_ROWS) {
            int x = TEXT_X + row_prefix_w(rows[(size_t)cr], cur_col);
            int y = TEXT_Y + (cr - first) * LH;
            g.rect(x, y + 3, 2, 18, theme.accent);
        }

        draw_keys(g);
    }

    void draw_picker(Gfx &g) {
        ui_bg(g);

        g.text(24, 42, "Открыть файл", 24, theme.fg, TF_LIGHT);
        g.text(24, 72, pick_dir.empty() ? "ПАПКИ"
                                        : g.fit(disp_path(pick_dir), 11, 380),
               11, theme.dim);
        icon(g, "close", 432, 53, 20, theme.fg);
        g.hline(0, 96, g.cv.w, theme.line2);

        g.rect(24, 104, 206, 44, theme.surf);
        g.text(36, 110, "+ Новый", 14, theme.fg);
        g.rect(238, 104, 218, 44, theme.surf);
        g.text(254, 110, "ВСЕ ЗАМЕТКИ", 14, theme.accent);
        g.hline(0, 148, g.cv.w, theme.line2);

        int top = pick_top;
        int total = pick_total();
        for (int i = 0; i < PICK_ROWS; i++) {
            int idx = top + i;
            if (idx >= total) break;
            int y = PICK_Y + i * 56;
            g.hline(0, y + 55, g.cv.w, theme.line2);

            if (pick_dir.empty()) {
                const PickEnt &e = pick[(size_t)idx];
                icon(g, "folder", 20, y + 16, 24, theme.accent);
                g.text(56, y + 18, g.fit(disp_path(e.name), 16, 300), 16, theme.fg);
                g.text(456, y + 22, "папка", 11, theme.dim, TF_RIGHT);
            } else if (idx == 0) {
                icon(g, "back", 20, y + 16, 24, theme.dim);
                g.text(56, y + 18, "..", 16, theme.fg);
                g.text(456, y + 22, "Наверх", 11, theme.dim, TF_RIGHT);
            } else {
                const PickEnt &e = pick[(size_t)idx - 1];
                if (e.dir) {
                    icon(g, "folder", 20, y + 16, 24, theme.accent);
                    g.text(56, y + 18, g.fit(e.name, 16, 300), 16, theme.fg);
                    g.text(456, y + 22, "папка", 11, theme.dim, TF_RIGHT);
                } else {
                    icon(g, "note", 20, y + 16, 24, theme.dim);
                    g.text(56, y + 18, g.fit(e.name, 16, 300), 16, theme.fg);
                    g.text(456, y + 22, fmt_size(e.size), 11, theme.dim, TF_RIGHT);
                }
            }
        }

        if (total == 0 && !pick_err.empty())
            g.text(240, 300, pick_err.c_str(), 15, theme.dim, TF_CENTER);

        g.text(24, 772, "«..» — наверх · тап по пустому — закрыть", 11, theme.dim);
    }

    void draw_list(Gfx &g) {
        ui_bg(g);

        g.text(24, 42, "Все заметки", 24, theme.fg, TF_LIGHT);
        std::string sub = notes_count((int)notes.size()) + " · по времени";
        g.text(24, 72, sub, 11, theme.dim);
        icon(g, "close", 432, 53, 20, theme.fg);
        g.hline(0, 96, g.cv.w, theme.line2);

        g.rect(24, 104, 206, 44, theme.surf);
        g.text(36, 110, "+ Новый", 14, theme.fg);
        g.rect(238, 104, 218, 44, theme.surf);
        g.text(254, 110, "Открыть…", 14, theme.accent);
        g.hline(0, 148, g.cv.w, theme.line2);

        for (int i = 0; i < LIST_ROWS; i++) {
            int idx = notes_top + i;
            if (idx >= (int)notes.size()) break;
            const NoteEnt &e = notes[(size_t)idx];
            int y = LIST_Y + i * LIST_ROW_H;
            bool cur = e.path == path;
            if (cur) g.rect(0, y, 480, LIST_ROW_H, theme.sel);
            if (cur) g.rect(0, y + 14, 3, 36, theme.accent);
            icon(g, "note", 18, y + 18, 24, cur ? theme.accent : theme.dim);
            g.text(54, y + 6, g.fit(e.name, 16, 350), 16,
                   cur ? theme.accent : theme.fg);
            std::string meta = fmt_size(e.size) + " · " + fmt_date(e.mtime);
            if (e.dup) meta += " · " + disp_path(e.dir);
            g.text(54, y + 27, g.fit(meta, 11, 350), 11, theme.dim);
            if (!e.preview.empty())
                g.text(54, y + 43, g.fit(e.preview, 12, 356), 12, theme.dim);
            for (int d = 0; d < 3; d++)
                g.rect(452, y + 24 + d * 6, 3, 3, theme.dim);
            g.hline(0, y + LIST_ROW_H - 1, g.cv.w, theme.line2);
        }

        if (notes.empty()) {
            g.text(240, 300, "Заметок нет", 18, theme.dim, TF_CENTER);
            g.text(240, 328, "«+ Новый» создаст первую", 12, theme.dim, TF_CENTER);
        }

        g.text(24, 746, g.fit("тап — открыть · точки — переименовать/удалить", 11, 440),
               11, theme.dim);
    }

    void draw_actions(Gfx &g) {
        g.rect(10, 246, 460, 236, theme.bar);
        g.frame(10, 246, 460, 236, theme.line2);
        if (dlg_idx >= 0 && dlg_idx < (int)notes.size()) {
            g.text(30, 266, g.fit(notes[(size_t)dlg_idx].name, 15, 400), 15,
                   theme.fg);
        }
        g.rect(20, 312, 440, 48, theme.surf);
        g.text(240, 326, "Переименовать", 15, theme.fg, TF_CENTER);
        g.rect(20, 368, 440, 48, theme.surf);
        g.text(240, 382, "Удалить", 15, theme.accent, TF_CENTER);
        g.rect(20, 432, 440, 44, theme.surf);
        g.text(240, 445, "Отмена", 14, theme.dim, TF_CENTER);
    }

    void draw_rename(Gfx &g) {
        ui_bg(g);
        g.text(24, 42, "Переименовать", 24, theme.fg, TF_LIGHT);
        std::string dir;
        if (dlg_idx >= 0 && dlg_idx < (int)notes.size())
            dir = disp_path(parent_dir(notes[(size_t)dlg_idx].path));
        g.text(24, 72, g.fit(dir, 11, 400), 11, theme.dim);
        icon(g, "close", 432, 53, 20, theme.fg);
        g.hline(0, 96, g.cv.w, theme.line2);

        g.rect(20, 100, 440, 52, theme.surf);
        std::string shown = rename_buf.empty() ? "имя.txt" : rename_buf;
        uint16_t col = rename_buf.empty() ? theme.dim : theme.fg;
        std::string vis = g.fit(shown, 16, 408);
        g.text(32, 116, vis, 16, col);
        int cx = 32 + g.textw(vis, 16);
        if (cx > 452) cx = 452;
        g.rect(cx + 2, 118, 2, 20, theme.accent);

        g.rect(20, 164, 206, 44, theme.surf);
        g.text(123, 177, "ОК", 15, theme.accent, TF_CENTER);
        g.rect(238, 164, 222, 44, theme.surf);
        g.text(349, 177, "ОТМЕНА", 15, theme.fg, TF_CENTER);

        g.text(20, 476, "ВВОД — переименовать", 11, theme.dim);
        draw_keys(g);
    }

    void draw_delete(Gfx &g) {
        ui_bg(g);
        g.text(24, 42, "Удалить заметку?", 24, theme.fg, TF_LIGHT);
        g.text(24, 72, "Файл будет удалён без корзины", 11, theme.dim);
        icon(g, "close", 432, 53, 20, theme.fg);
        g.hline(0, 96, g.cv.w, theme.line2);

        if (dlg_idx >= 0 && dlg_idx < (int)notes.size()) {
            const NoteEnt &e = notes[(size_t)dlg_idx];
            g.text(20, 130, g.fit(e.name, 18, 440), 18, theme.fg);
            g.text(20, 162, g.fit(disp_path(e.path), 11, 440), 11, theme.dim);
        }
        g.rect(20, 236, 440, 52, theme.surf);
        g.text(240, 252, "УДАЛИТЬ", 15, theme.accent, TF_CENTER);
        g.rect(20, 296, 440, 44, theme.surf);
        g.text(240, 309, "ОТМЕНА", 14, theme.dim, TF_CENTER);
    }

    // ------------------------------------------------------------ keys
    void press(const Key &k) {
        switch (k.act) {
        case K_CHAR:
            insert_text(k.text);
            if (shift) {
                shift = false;
                build();
            }
            break;
        case K_SHIFT:
            shift = !shift;
            build();
            break;
        case K_LAYER:
            layer = layer ? 0 : 1;
            shift = false;
            build();
            break;
        case K_LANG:
            lang = lang ? 0 : 1;
            shift = false;
            build();
            break;
        case K_SPACE:
            insert_text(" ");
            if (shift) {
                shift = false;
                build();
            }
            break;
        case K_DEL:
            backspace();
            break;
        case K_ENTER:
            insert_enter();
            break;
        case K_LEFT:
            move_left();
            break;
        case K_RIGHT:
            move_right();
            break;
        case K_HOME:
            move_home();
            break;
        case K_END:
            move_end();
            break;
        case K_UNDO:
            do_undo();
            break;
        case K_REDO:
            do_redo();
            break;
        }
    }

    void press_rename(const Key &k) {
        switch (k.act) {
        case K_CHAR:
            if (rename_buf.size() + k.text.size() <= 96) rename_buf += k.text;
            if (shift) { shift = false; build(); }
            break;
        case K_SHIFT:
            shift = !shift;
            build();
            break;
        case K_LAYER:
            layer = layer ? 0 : 1;
            shift = false;
            build();
            break;
        case K_LANG:
            lang = lang ? 0 : 1;
            shift = false;
            build();
            break;
        case K_SPACE:
            if (rename_buf.size() < 96) rename_buf += " ";
            if (shift) { shift = false; build(); }
            break;
        case K_DEL:
            pop_cp(rename_buf);
            break;
        case K_ENTER:
            rename_confirm();
            break;
        default:
            break;
        }
    }

    void tap_text(int x, int y) {
        if (rows.empty()) layout_all();
        int r = scroll_row + (y - TEXT_Y) / LH;
        if (r < 0) r = 0;
        if (r >= (int)rows.size()) r = (int)rows.size() - 1;
        if (r < 0) return;
        const Row &rw = rows[(size_t)r];
        cur_line = rw.line;
        cur_col = col_of_row_x(r, x);
        want_x = x - TEXT_X;
        if (want_x < 0) want_x = 0;
        ensure_cursor_visible();
    }

    void leave_editor() {
        if (dirty && !save_note(true)) return;
        app.back();
    }

    // ------------------------------------------------------------ input
    bool picker_tap(int x, int y) {
        if (hit(x, y, 424, 32, 56, 56)) {  // ×
            close_picker();
            return true;
        }
        if (hit(x, y, 24, 104, 206, 44)) {  // + Новый
            create_in(pick_dir.empty() ? parent_dir(path) : pick_dir);
            return true;
        }
        if (hit(x, y, 238, 104, 218, 44)) {  // все заметки
            picker_open = false;
            pick_from_list = false;
            open_list();
            return true;
        }
        if (y >= PICK_Y && y < PICK_Y + PICK_ROWS * 56) {
            int i = (y - PICK_Y) / 56;
            int idx = pick_top + i;
            int total = pick_total();
            if (idx < total) {
                if (pick_dir.empty()) {
                    pick_dir = pick[(size_t)idx].name;
                    load_list();
                } else if (idx == 0) {
                    pick_up();
                } else {
                    const PickEnt &e = pick[(size_t)idx - 1];
                    if (e.dir) {
                        pick_dir = join(pick_dir, e.name);
                        load_list();
                    } else {
                        std::string p = join(pick_dir, e.name);
                        picker_open = false;
                        pick_from_list = false;
                        open_file(p);
                        mode = M_EDIT;
                    }
                }
                return true;
            }
        }
        close_picker();
        return true;
    }

    bool list_tap(int x, int y) {
        if (hit(x, y, 424, 32, 56, 56)) {  // ×
            mode = M_EDIT;
            return true;
        }
        if (y >= 104 && y < 148) {
            if (x < 234) {
                create_in(parent_dir(path));
                return true;
            }
            if (x < 464) {
                open_picker(true);
                return true;
            }
        }
        if (y >= LIST_Y && y < LIST_Y + LIST_ROWS * LIST_ROW_H) {
            int i = (y - LIST_Y) / LIST_ROW_H;
            int idx = notes_top + i;
            if (idx >= 0 && idx < (int)notes.size()) {
                if (x >= 424) {
                    dlg_idx = idx;
                    mode = M_ACTIONS;
                } else {
                    open_note(notes[(size_t)idx].path);
                }
                return true;
            }
        }
        return false;
    }

    bool actions_tap(int x, int y) {
        if (hit(x, y, 20, 312, 440, 48)) {
            start_rename();
            return true;
        }
        if (hit(x, y, 20, 368, 440, 48)) {
            mode = M_DELETE;
            return true;
        }
        if (hit(x, y, 20, 432, 440, 44)) {
            mode = M_LIST;
            dlg_idx = -1;
            return true;
        }
        mode = M_LIST;
        dlg_idx = -1;
        return true;
    }

    bool rename_tap(int x, int y) {
        if (hit(x, y, 424, 32, 56, 56)) {
            mode = M_LIST;
            return true;
        }
        if (hit(x, y, 20, 164, 206, 44)) {
            rename_confirm();
            return true;
        }
        if (hit(x, y, 238, 164, 222, 44)) {
            mode = M_LIST;
            return true;
        }
        for (const Key &k : keys) {
            if (!hit(x, y, k.x, k.y, k.w, k.h)) continue;
            press_rename(k);
            return true;
        }
        return false;
    }

    bool delete_tap(int x, int y) {
        if (hit(x, y, 424, 32, 56, 56) || hit(x, y, 20, 296, 440, 44)) {
            mode = M_LIST;
            return true;
        }
        if (hit(x, y, 20, 236, 440, 52)) {
            delete_confirm();
            return true;
        }
        return false;
    }

    bool editor_tap(int x, int y) {
        if (hit(x, y, 0, 32, 64, 64)) {
            leave_editor();
            return true;
        }
        if (hit(x, y, 380, 32, 96, 64)) {
            save_note();
            return true;
        }
        if (hit(x, y, 336, 32, 48, 64)) {
            open_list();
            return true;
        }
        if (y >= TEXT_Y - 4 && y < TEXT_Y + TEXT_ROWS * LH) {
            tap_text(x, y);
            return true;
        }
        for (const Key &k : keys) {
            if (!hit(x, y, k.x, k.y, k.w, k.h)) continue;
            press(k);
            return true;
        }
        return false;
    }

    bool tap(int x, int y) override {
        if (picker_open) return picker_tap(x, y);
        switch (mode) {
        case M_LIST: return list_tap(x, y);
        case M_ACTIONS: return actions_tap(x, y);
        case M_RENAME: return rename_tap(x, y);
        case M_DELETE: return delete_tap(x, y);
        default: return editor_tap(x, y);
        }
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x;
        (void)dx;
        if (dy == 0) return false;

        // A gesture can leave the screen (edge-back, shade) without App
        // notifying us, so flush the debounced autosave before any drag.
        if (mode == M_EDIT && !picker_open && dirty) save_note(true);

        if (picker_open) {
            pick_drag += dy;
            int steps = pick_drag / 56;
            if (steps == 0) return false;
            pick_drag -= steps * 56;
            int want = pick_top - steps;
            if (want < 0) want = 0;
            int max = pick_total() - PICK_ROWS;
            if (max < 0) max = 0;
            if (want > max) want = max;
            if (want == pick_top) return false;
            pick_top = want;
            return true;
        }

        if (mode == M_LIST) {
            if (y < LIST_Y || y >= LIST_Y + LIST_ROWS * LIST_ROW_H) return false;
            notes_drag += dy;
            int steps = notes_drag / LIST_ROW_H;
            if (steps == 0) return false;
            notes_drag -= steps * LIST_ROW_H;
            int want = notes_top - steps;
            if (want < 0) want = 0;
            if (want > list_max_top()) want = list_max_top();
            if (want == notes_top) return false;
            notes_top = want;
            return true;
        }

        if (mode != M_EDIT || y >= KB_Y) return false;
        if ((int)rows.size() <= TEXT_ROWS) return false;
        drag_px += dy;
        int steps = drag_px / LH;
        if (steps == 0) return false;
        drag_px -= steps * LH;
        int maxs = (int)rows.size() - TEXT_ROWS;
        int want = scroll_row - steps;
        if (want < 0) want = 0;
        if (want > maxs) want = maxs;
        if (want == scroll_row) return false;
        scroll_row = want;
        return true;
    }

    bool key(int ksym, bool down) override {
        if (!down) return false;

        // Home and Power leave the screen through App; flush first but let the
        // global handler do its job.
        if (ksym == XK_Home || ksym == XF86XK_HomePage ||
            ksym == XF86XK_PowerOff) {
            autosave_now();
            return false;
        }

        if (picker_open) {
            if (ksym == XK_Escape || ksym == XF86XK_Back || ksym == XK_BackSpace) {
                close_picker();
                return true;
            }
            return true;
        }

        if (mode != M_EDIT) {
            if (ksym == XK_Escape || ksym == XF86XK_Back) {
                mode = (mode == M_LIST) ? M_EDIT : M_LIST;
                if (mode == M_EDIT) dlg_idx = -1;
                return true;
            }
            if (mode == M_LIST) {
                if (ksym == XK_Up || ksym == XK_Page_Up) {
                    if (notes_top > 0) notes_top--;
                    return true;
                }
                if (ksym == XK_Down || ksym == XK_Page_Down) {
                    if (notes_top < list_max_top()) notes_top++;
                    return true;
                }
            }
            if (mode == M_RENAME) {
                if (ksym == XK_Return || ksym == XK_KP_Enter) {
                    rename_confirm();
                    return true;
                }
                if (ksym == XK_BackSpace) {
                    pop_cp(rename_buf);
                    return true;
                }
            }
            return false;
        }

        switch (ksym) {
        case XK_Left: move_left(); return true;
        case XK_Right: move_right(); return true;
        case XK_Up: move_vert(-1); return true;
        case XK_Down: move_vert(1); return true;
        case XK_Page_Up: move_vert(-TEXT_ROWS); return true;
        case XK_Page_Down: move_vert(TEXT_ROWS); return true;
        case XK_End: move_end(); return true;
        case XK_Return:
        case XK_KP_Enter: insert_enter(); return true;
        case XK_BackSpace: backspace(); return true;
        case XK_Delete: del_forward(); return true;
        case XF86XK_Back:
            leave_editor();
            return true;
        default:
            break;
        }
        return false;
    }

    // One tick per second while dirty lets autosave fire after the 2 s debounce.
    bool animated() const override { return dirty; }

    void tick(unsigned now) override {
        if (dirty && (unsigned)(now - last_edit) >= 2000) save_note(true);
    }

    void on_show() override {
        if (dirty) save_note(true);
        if (!app.open_path.empty()) {
            std::string p = app.open_path;
            app.open_path.clear();
            picker_open = false;
            mode = M_EDIT;
            open_file(p);
        }
        if (!env_seen) {
            env_seen = true;
            const char *f = std::getenv("GS_NOTES_FILE");
            if (f && f[0] == '/') {
                mode = M_EDIT;
                open_file(f);
            }
            const char *e = std::getenv("GS_NOTES_LIST");
            if (e && e[0] == '1') {
                open_list();
            } else {
                e = std::getenv("GS_NOTES_PICKER");
                if (e && e[0] == '1') open_picker(false);
            }
        }
        if (rows.empty() || line_row0.size() != body.size()) layout_all();
        clamp_cursor();
        ensure_cursor_visible();
    }
};

} // namespace

ScreenView *make_notes() { return new Notes(); }
