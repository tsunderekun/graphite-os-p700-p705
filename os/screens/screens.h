#pragma once
struct ScreenView;

// shell
ScreenView *make_home();
ScreenView *make_player();
ScreenView *make_queue();
ScreenView *make_info();
ScreenView *make_library();
ScreenView *make_apps();
ScreenView *make_edit();

// shell chrome
ScreenView *make_shade();
ScreenView *make_lock();
ScreenView *make_power();
ScreenView *make_menu();

// apps
ScreenView *make_files();
ScreenView *make_terminal();
ScreenView *make_notes();
ScreenView *make_browser();
ScreenView *make_reader();
ScreenView *make_video();

// device settings
ScreenView *make_set_root();
ScreenView *make_set_display();
ScreenView *make_set_wifi();
ScreenView *make_set_keys();
ScreenView *make_set_sound();
ScreenView *make_set_power();
ScreenView *make_set_storage();
ScreenView *make_set_apps();
ScreenView *make_set_access();
ScreenView *make_set_system();
ScreenView *make_set_lab();
ScreenView *make_set_about();
ScreenView *make_set_launch();
