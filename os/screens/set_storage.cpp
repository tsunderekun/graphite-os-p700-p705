// S3 "Хранилище" — real df/du, cache wipe and SD unmount.
#include "graphite.h"
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ftw.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string comma(std::string s) {
    for (char &c : s)
        if (c == '.') c = ',';
    return s;
}

std::string hsize(unsigned long long b) {
    char buf[48];
    if (b >= 1000000000ULL)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", (double)b / 1e9);
    else if (b >= 1000000ULL)
        std::snprintf(buf, sizeof(buf), "%.0f МБ", (double)b / 1e6);
    else
        std::snprintf(buf, sizeof(buf), "%.0f КБ", (double)b / 1e3);
    return comma(buf);
}

std::string mb_text(unsigned long long b) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f МБ", (double)b / 1e6);
    return comma(buf);
}

struct Disk {
    bool ok = false;
    unsigned long long total = 0, freeb = 0, used = 0;
    int pct = 0;
};

Disk read_disk(const char *mp) {
    Disk d;
    struct statvfs v;
    if (statvfs(mp, &v) != 0) return d;
    unsigned long long fr = v.f_frsize ? v.f_frsize : v.f_bsize;
    d.ok = true;
    d.total = (unsigned long long)v.f_blocks * fr;
    d.freeb = (unsigned long long)v.f_bavail * fr;
    if (d.freeb > d.total) d.freeb = d.total;
    d.used = d.total - d.freeb;
    d.pct = d.total ? (int)(d.used * 100 / d.total) : 0;
    if (d.pct > 100) d.pct = 100;
    return d;
}

std::string fs_of(const char *mp) {
    FILE *f = std::fopen("/proc/mounts", "r");
    if (!f) return "";
    char line[512], dev[160], point[160], type[64];
    std::string out;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "%159s %159s %63s", dev, point, type) != 3) continue;
        if (!std::strcmp(point, mp)) { out = type; break; }
    }
    std::fclose(f);
    return out;
}

unsigned long long du_sum = 0;
int du_cb(const char *path, const struct stat *st, int type, struct FTW *) {
    (void)path;
    if (type == FTW_F) {
        du_sum += (unsigned long long)st->st_size;
        if (du_sum > 96ULL * 1024 * 1024) return 1;
    }
    return 0;
}

unsigned long long du(const char *path) {
    if (access(path, F_OK) != 0) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return (unsigned long long)st.st_size;
    du_sum = 0;
    nftw(path, du_cb, 16, FTW_PHYS);
    return du_sum;
}

int rm_cb(const char *path, const struct stat *, int type, struct FTW *) {
    if (type == FTW_DP) ::rmdir(path);
    else ::unlink(path);
    return 0;
}

void rm_tree(const char *path) {
    if (access(path, F_OK) != 0) return;
    nftw(path, rm_cb, 16, FTW_DEPTH | FTW_PHYS);
}

std::string swap_text() {
    FILE *f = std::fopen("/proc/swaps", "r");
    if (!f) return "нет";
    char line[512];
    if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return "нет"; }
    unsigned long long kb = 0;
    std::string last;
    while (std::fgets(line, sizeof(line), f)) {
        char name[160], type[64];
        unsigned long long size = 0, used = 0;
        int prio = 0;
        if (std::sscanf(line, "%159s %63s %llu %llu %d", name, type, &size, &used, &prio) >= 3) {
            kb += size;
            last = name;
        }
    }
    std::fclose(f);
    if (!kb) return "нет";
    size_t slash = last.find_last_of('/');
    std::string nm = slash == std::string::npos ? last : last.substr(slash + 1);
    return hsize(kb * 1024ULL) + " · " + nm;
}

int kv(Gfx &g, int y, const char *key, const std::string &val, bool muted = false) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, g.fit(key, 12, 150), 12, theme.dim);
    g.text(184, y + 5, g.fit(val, 16, 272), 16, muted ? theme.dim : theme.fg);
    return y + 44;
}

int diskrow(Gfx &g, int y, const char *name, const std::string &total,
            const std::string &meta, const Disk &d) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 8, name, 16, theme.fg);
    g.text(456, y + 9, total, 12, theme.dim, TF_RIGHT);
    g.rect(24, y + 31, 432, 4, theme.track);
    if (d.ok && d.pct > 0) {
        int w = 432 * d.pct / 100;
        if (w < 1) w = 1;
        g.rect(24, y + 31, w, 4, theme.accent);
    }
    g.text(24, y + 41, g.fit(meta, 12, 432), 12, theme.dim);
    return y + 64;
}

int row(Gfx &g, int y, const char *name, const char *sub, const char *value,
        bool chev, bool disabled) {
    g.hline(0, y, g.cv.w, theme.line2);
    g.text(24, y + 7, g.fit(name, 16, 300), 16, disabled ? theme.dim : theme.fg);
    if (sub && *sub) g.text(24, y + 29, g.fit(sub, 12, 330), 12, theme.dim);
    if (value && *value)
        g.text(428, y + 19, g.fit(value, 12, 170), 12, theme.dim, TF_RIGHT);
    if (chev) icon(g, "chevron", 438, y + 20, 16, theme.dim);
    return y + 56;
}

int button(Gfx &g, int y, const std::string &label, bool active = true) {
    g.rect(24, y, 432, 48, active ? theme.accent : theme.surf);
    g.text(240, y + 18, g.fit(label, 13, 408), 13,
           active ? theme.onacc : theme.dim, TF_CENTER | TF_BOLD);
    return y + 48;
}

struct SetStorage : ScreenView {
    Disk root, sd;
    unsigned long long covers = 0, reader = 0, mpdc = 0;
    std::string sdfs;
    bool sd_ok = false;
    std::string swap;
    bool extract_armed = false;

    SetStorage() { refresh(); }

    void on_show() override { refresh(); }

    static constexpr const char *kCovers[] = {
        "/home/user/.cache/graphite/covers", "/root/.cache/graphite/covers"};
    static constexpr const char *kReader[] = {
        "/home/user/.cache/graphite/reader", "/root/.cache/graphite/reader"};
    static constexpr const char *kMpd = "/var/cache/mpd";

    unsigned long long sum_paths(const char *const *v, int n) {
        unsigned long long s = 0;
        for (int i = 0; i < n; i++) s += du(v[i]);
        return s;
    }

    unsigned long long cache_total() const { return covers + reader + mpdc; }

    void refresh() {
        root = read_disk("/");
        sd = read_disk("/media/sd");
        sd_ok = sd.ok;
        sdfs.clear();
        if (sd_ok) sdfs = fs_of("/media/sd");
        covers = sum_paths(kCovers, 2);
        reader = sum_paths(kReader, 2);
        mpdc = du(kMpd);
        swap = swap_text();
    }

    void clear_cache() {
        unsigned long long before = cache_total();
        for (const char *p : kCovers) rm_tree(p);
        for (const char *p : kReader) rm_tree(p);
        rm_tree(kMpd);
        sync();
        refresh();
        if (before == 0) {
            app.toast("Кэш пуст");
            return;
        }
        unsigned long long freed = before > cache_total() ? before - cache_total() : 0;
        std::string t = "Кэш очищен · освобождено " + mb_text(freed);
        app.toast(t.c_str());
    }

    void extract_sd() {
        if (!sd_ok) {
            app.toast("Карта не смонтирована");
            return;
        }
        // MPD keeps the card's files open; stop playback before unmount.
        int rc = std::system("mpc stop >/dev/null 2>&1");
        (void)rc;
        sync();
        if (::umount("/media/sd") != 0) {
            std::string t = std::string("Не вышло: ") + std::strerror(errno);
            app.toast(t.c_str());
            return;
        }
        sd = Disk();
        sd_ok = false;
        sdfs.clear();
        root = read_disk("/");
        app.toast("Карта отмонтирована · можно вынуть");
    }

    const char *id() const override { return "set-storage"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        ui_header(g, "Хранилище", "ROOT + SD", true);
        int y = 96;
        y = ui_section(g, y, "НАКОПИТЕЛИ");

        std::string rmeta = "занято " + hsize(root.used) + " · свободно " + hsize(root.freeb);
        y = diskrow(g, y, "Система", root.ok ? hsize(root.total) : "—", rmeta, root);

        std::string sdmeta;
        std::string sdname = "SD-карта";
        if (sd_ok) {
            sdmeta = "занято " + hsize(sd.used) + " · свободно " + hsize(sd.freeb);
            if (!sdfs.empty()) sdname = "SD · " + sdfs;
        } else {
            sdmeta = "не смонтирована · вставьте карту";
        }
        y = diskrow(g, y, sdname.c_str(), sd_ok ? hsize(sd.total) : "—", sdmeta, sd);

        y = row(g, y, extract_armed ? "Подтвердить извлечение" : "Извлечь карту",
                sd_ok ? (extract_armed ? "ещё один тап · MPD остановится"
                                       : "umount /media/sd · второй тап")
                      : "карта не смонтирована",
                "", sd_ok, !sd_ok);

        y = ui_section(g, y, "КЭШ");
        y = kv(g, y, "КЭШ ОБЛОЖЕК", mb_text(covers));
        y = kv(g, y, "КЭШ ЧИТАЛКИ", mb_text(reader));
        y = kv(g, y, "КЭШ MPD", mb_text(mpdc));

        unsigned long long cache = cache_total();
        std::string bl = "ОЧИСТИТЬ КЭШ · " + mb_text(cache);
        y = button(g, y + 8, bl, cache > 0);

        y = ui_section(g, y, "СЕРВИС");
        y = kv(g, y, "МУЗЫКА", "MPD · /media/sd/Music");
        y = kv(g, y, "SWAP", swap);
        y = row(g, y, "Открыть Файлы", "/home/user · pcmanfm", "", true, false);

        g.text(24, y + 6, g.fit("очистка не трогает музыку, заметки и индекс MPD",
                                11, 432), 11, theme.dim);

        g.text(24, 781, "ХРАНИЛИЩЕ · df /", 11, theme.dim);
        g.text(456, 781, "eMMC + SD", 11, theme.dim, TF_RIGHT);
    }

    bool tap(int x, int y) override {
        if (hit(x, y, 0, 252, 480, 56)) {
            if (!sd_ok) {
                extract_armed = false;
                app.toast("Карта не смонтирована");
                return true;
            }
            if (!extract_armed) {
                extract_armed = true;
                app.toast("Ещё один тап — отмонтировать карту");
            } else {
                extract_armed = false;
                extract_sd();
            }
            return true;
        }
        if (hit(x, y, 24, 476, 432, 48)) {
            extract_armed = false;
            clear_cache();
            return true;
        }
        if (hit(x, y, 0, 640, 480, 56)) {
            extract_armed = false;
            app.open("files");
            return true;
        }
        extract_armed = false;
        return false;
    }
};

} // namespace

ScreenView *make_set_storage() { return new SetStorage(); }
