#pragma once

// Launcher for foreign X11 apps (Stage 1). The child drops to uid/gid 10000
// ("user") before exec: GUI must never run as root, even though the shell does.
// A session D-Bus daemon and XDG_RUNTIME_DIR are raised once, on the first
// launch, so GTK/NetSurf apps that expect them can actually open a window.
bool apphost_launch(const char *command);   // /bin/sh -c <command> as user
bool apphost_active();
bool apphost_launching();                   // pid up, no window yet, < 15 s
bool apphost_has_window();                  // a window was filtered in
const char *apphost_command();              // launch line, for the overlay
bool apphost_is_browser();                  // dillo / netsurf: Back navigates
bool apphost_back();                        // true = consumed (Alt+Left sent)
void apphost_close();
void apphost_tick(unsigned now);
void apphost_set_content_height(int h);
void apphost_focus_app();
bool apphost_on_map(unsigned long window);
void apphost_on_destroy(unsigned long window);
void apphost_note_key(int ksym, bool down);
