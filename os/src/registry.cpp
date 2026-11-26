#include "graphite.h"
#include "screens.h"
#include <cstring>

struct Entry { const char *id; ScreenView *(*fn)(); };

static const Entry table[] = {
    {"home", make_home},
    {"player", make_player},
    {"queue", make_queue},
    {"info", make_info},
    {"library", make_library},
    {"apps", make_apps},
    {"edit", make_edit},
    {"shade", make_shade},
    {"tasks", make_tasks},
    {"lock", make_lock},
    {"power", make_power},
    {"menu", make_menu},
    {"files", make_files},
    {"terminal", make_terminal},
    {"notes", make_notes},
    {"browser", make_browser},
    {"reader", make_reader},
    {"video", make_video},
    {"viewer", make_viewer},
    {"clock", make_clock},
    {"usb", make_usb},
    {"set-root", make_set_root},
    {"set-display", make_set_display},
    {"set-timers", make_set_timers},
    {"set-wifi", make_set_wifi},
    {"set-keys", make_set_keys},
    {"set-sound", make_set_sound},
    {"set-power", make_set_power},
    {"set-storage", make_set_storage},
    {"set-apps", make_set_apps},
    {"set-access", make_set_access},
    {"set-system", make_set_system},
    {"set-lab", make_set_lab},
    {"set-about", make_set_about},
    {"set-launch", make_set_launch},
};

ScreenView *screen_make(const char *id) {
    if (!id) return nullptr;
    for (const Entry &e : table)
        if (!std::strcmp(e.id, id)) return e.fn();
    return nullptr;
}
