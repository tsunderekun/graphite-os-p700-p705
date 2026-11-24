#pragma once
#include <X11/Xlib.h>

bool osk_visible();
void osk_toggle();
void osk_hide();
bool osk_event(XEvent &ev);
