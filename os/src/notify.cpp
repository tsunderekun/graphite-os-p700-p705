#include "notify.h"
#include "graphite.h"
#ifndef NOTIFY_MODEL_TEST
#include "mpd.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace {
constexpr int kCapacity = 32;
NotifyItem items[kCapacity]{};
int count = 0;
uint32_t next_id = 1;
bool is_dnd = false;
bool dnd_initialized = false;
bool shade_open = false;
unsigned next_sources = 0;
std::string last_track, last_state;
bool have_track_sample = false;
int last_battery_band = -1;

void copy_field(char *out, size_t cap, const char *s) {
    if (!cap) return;
    if (!s) s = "";
    size_t n = std::min(std::strlen(s), cap - 1);
    while (n && ((static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)) --n;
    std::memcpy(out, s, n);
    out[n] = 0;
}
void erase_at(int i) {
    for (int j = i; j + 1 < count; ++j) items[j] = items[j + 1];
    if (count) --count;
}
void source_push(const char *source, const char *key, const char *title,
                 const char *text, bool ongoing, int priority = N_NORMAL) {
    NotifySpec s{source, key, title, text, (unsigned)std::time(nullptr), priority, ongoing};
    notify_push(s);
}
}

uint32_t notify_push(const NotifySpec &s) {
#ifndef NOTIFY_MODEL_TEST
    if (!dnd_initialized) { is_dnd = cfg.geti("dnd", 0) != 0; dnd_initialized = true; }
#endif
    const char *source = s.source ? s.source : "system";
    const char *key = s.key ? s.key : "";
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(items[i].source, source) || std::strcmp(items[i].key, key)) continue;
        copy_field(items[i].title, sizeof(items[i].title), s.title);
        copy_field(items[i].text, sizeof(items[i].text), s.text);
        items[i].time = s.time;
        items[i].priority = std::max(0, std::min(2, s.priority));
        items[i].ongoing = s.ongoing;
        if (s.ongoing) items[i].unread = false;
        else if (!shade_open && !is_dnd) items[i].unread = true;
        NotifyItem updated = items[i];
        erase_at(i);
        for (int j = count; j > 0; --j) items[j] = items[j - 1];
        items[0] = updated;
        ++count;
        return updated.id;
    }
    if (count == kCapacity) {
        int victim = count - 1;
        while (victim >= 0 && items[victim].ongoing) --victim;
        if (victim < 0) return 0;
        erase_at(victim);
    }
    for (int j = count; j > 0; --j) items[j] = items[j - 1];
    NotifyItem &it = items[0];
    std::memset(&it, 0, sizeof(it));
    it.id = next_id++;
    copy_field(it.source, sizeof(it.source), source);
    copy_field(it.key, sizeof(it.key), key);
    copy_field(it.title, sizeof(it.title), s.title);
    copy_field(it.text, sizeof(it.text), s.text);
    it.time = s.time;
    it.priority = std::max(0, std::min(2, s.priority));
    it.ongoing = s.ongoing;
    it.unread = !s.ongoing && !shade_open && !is_dnd;
    ++count;
    return it.id;
}

void notify_clear() {
    for (int i = count - 1; i >= 0; --i) if (!items[i].ongoing) erase_at(i);
}
bool notify_dismiss(uint32_t id) {
    for (int i = 0; i < count; ++i) if (items[i].id == id) {
        if (items[i].ongoing) return false;
        erase_at(i);
        return true;
    }
    return false;
}
void notify_remove(const char *source, const char *key) {
    if (!source || !key) return;
    for (int i = count - 1; i >= 0; --i)
        if (!std::strcmp(items[i].source, source) && !std::strcmp(items[i].key, key)) erase_at(i);
}
void notify_mark_read(uint32_t id) {
    for (int i = 0; i < count; ++i) if (items[i].id == id) items[i].unread = false;
}
void notify_mark_all_read() { for (int i = 0; i < count; ++i) items[i].unread = false; }
int notify_list(const NotifyItem **out) { if (out) *out = items; return count; }
int notify_unread() { int n = 0; for (int i = 0; i < count; ++i) n += items[i].unread; return n; }
bool notify_dnd() { return is_dnd; }
void notify_set_dnd(bool enabled) {
    is_dnd = enabled;
    dnd_initialized = true;
    settings_save_key("dnd", enabled ? "1" : "0");
    cfg.set("dnd", enabled ? "1" : "0");
}

void notify_tick(unsigned now) {
#ifdef NOTIFY_MODEL_TEST
    (void)now;
    return;
#else
    shade_open = app.top() && std::strcmp(app.top()->id(), "shade") == 0;
    if (!dnd_initialized) {
        is_dnd = cfg.geti("dnd", 0) != 0;
        dnd_initialized = true;
    }
    if (next_sources && now - next_sources < 5000) return;
    next_sources = now ? now : 1;

    // MPD client owns its socket lifecycle; only sample its already-cached state.
    MpdClient &mpd = mpd_client();
    if (mpd.connected() && mpd.has_song()) {
        const MpdSong &song = mpd.song();
        const MpdStatus &status = mpd.status();
        const bool changed = !have_track_sample || song.file != last_track || status.state != last_state;
        if (changed) {
            std::string title = song.title.empty() ? song.file : song.title;
            source_push("mpd", "player", title.c_str(), song.artist.c_str(), true, N_LOW);
            last_track = song.file;
            last_state = status.state;
            have_track_sample = true;
        }
        if (!status.error.empty())
            source_push("mpd", "error", "Ошибка MPD", status.error.c_str(), false, N_HIGH);
    } else if (have_track_sample) {
        // Remove stale ongoing state when playback disappears.
        notify_remove("mpd", "player");
        have_track_sample = false;
        last_track.clear(); last_state.clear();
    }

    FILE *f = std::fopen("/sys/class/net/wlan0/operstate", "rb");
    if (f) {
        char state[32]{};
        bool up = std::fgets(state, sizeof(state), f) && std::strncmp(state, "up", 2) == 0;
        std::fclose(f);
        static int last_wifi = -1;
        if (last_wifi >= 0 && last_wifi != (int)up)
            source_push("wifi", up ? "connected" : "disconnected",
                        up ? "Wi-Fi подключён" : "Wi-Fi отключён", "wlan0", false, N_NORMAL);
        last_wifi = up;
    }

    f = std::fopen("/sys/class/power_supply/battery/capacity", "rb");
    int cap = -1;
    if (f) { if (std::fscanf(f, "%d", &cap) != 1) cap = -1; std::fclose(f); }
    int band = cap >= 0 && cap <= 10 ? 10 : cap >= 0 && cap <= 20 ? 20 : -1;
    if (band >= 0 && band != last_battery_band) {
        char body[40]; std::snprintf(body, sizeof(body), "Заряд батареи %d%%", cap);
        source_push("battery", band == 10 ? "low-10" : "low-20", "Низкий заряд", body, false,
                    band == 10 ? N_HIGH : N_NORMAL);
    }
    if (cap > 25) last_battery_band = -1;
    else if (band >= 0) last_battery_band = band;
#endif
}
