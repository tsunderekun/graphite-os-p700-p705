#pragma once

// Launcher for foreign X11 apps (Stage 1). The child drops to uid/gid 10000
// ("user") before exec: GUI must never run as root, even though the shell does.
// A session D-Bus daemon and XDG_RUNTIME_DIR are raised once, on the first
// launch, so GTK/NetSurf apps that expect them can actually open a window.
bool apphost_launch(const char *command);   // /bin/sh -c <command> as user
bool apphost_active();
int apphost_task_count();
bool apphost_task_info(int index, char *title, unsigned title_size,
                       char *command, unsigned command_size,
                       bool *foreground, bool *closing);
bool apphost_close_task(int index);
bool apphost_resume_task(int index);
bool apphost_any_task();
bool apphost_launching();                   // pid up, no window yet, < 15 s
bool apphost_has_window();                  // a window was filtered in
bool apphost_window_visible();              // adopted window is currently mapped
const char *apphost_command();              // launch line, for the overlay
bool apphost_is_browser();                  // dillo / netsurf: Back navigates
bool apphost_back();                        // true = consumed (Alt+Left sent)
void apphost_close();
void apphost_tick(unsigned now);
void apphost_set_content_height(int h);
void apphost_focus_app();
bool apphost_suspend();                     // unmap adopted window; keep process alive
bool apphost_resume();                      // map/raise adopted window and restore focus
bool apphost_on_map(unsigned long window);
void apphost_on_destroy(unsigned long window);
void apphost_on_unmap(unsigned long window);
void apphost_note_key(int ksym, bool down);
