#include "screens.h"

// Weak placeholders. A screen implementation in its own .cpp defines the same
// make_*() with default (strong) linkage and wins at link time, so agents never
// have to edit this file or the registry.
#define WEAK __attribute__((weak))

WEAK ScreenView *make_home() { return nullptr; }
WEAK ScreenView *make_player() { return nullptr; }
WEAK ScreenView *make_queue() { return nullptr; }
WEAK ScreenView *make_info() { return nullptr; }
WEAK ScreenView *make_library() { return nullptr; }
WEAK ScreenView *make_apps() { return nullptr; }
WEAK ScreenView *make_edit() { return nullptr; }
WEAK ScreenView *make_shade() { return nullptr; }
WEAK ScreenView *make_lock() { return nullptr; }
WEAK ScreenView *make_power() { return nullptr; }
WEAK ScreenView *make_menu() { return nullptr; }
WEAK ScreenView *make_files() { return nullptr; }
WEAK ScreenView *make_terminal() { return nullptr; }
WEAK ScreenView *make_notes() { return nullptr; }
WEAK ScreenView *make_browser() { return nullptr; }
WEAK ScreenView *make_reader() { return nullptr; }
WEAK ScreenView *make_video() { return nullptr; }
WEAK ScreenView *make_set_root() { return nullptr; }
WEAK ScreenView *make_set_display() { return nullptr; }
WEAK ScreenView *make_set_wifi() { return nullptr; }
WEAK ScreenView *make_set_keys() { return nullptr; }
WEAK ScreenView *make_set_sound() { return nullptr; }
WEAK ScreenView *make_set_power() { return nullptr; }
WEAK ScreenView *make_set_storage() { return nullptr; }
WEAK ScreenView *make_set_apps() { return nullptr; }
WEAK ScreenView *make_set_access() { return nullptr; }
WEAK ScreenView *make_set_system() { return nullptr; }
WEAK ScreenView *make_set_lab() { return nullptr; }
WEAK ScreenView *make_set_about() { return nullptr; }
WEAK ScreenView *make_set_launch() { return nullptr; }
