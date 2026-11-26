#pragma once
#include <stdint.h>

enum NotifyPriority { N_LOW = 0, N_NORMAL = 1, N_HIGH = 2 };
enum NotifyFlag { NF_ONGOING = 1u };

struct NotifyItem {
    uint32_t id;
    char source[20];
    char key[32];
    char title[96];
    char text[240];
    unsigned time;
    int priority;
    bool ongoing;
    bool unread;
};

struct NotifySpec {
    const char *source;
    const char *key;
    const char *title;
    const char *text;
    unsigned time;
    int priority;
    bool ongoing;
};

uint32_t notify_push(const NotifySpec &spec);
void notify_clear();
bool notify_dismiss(uint32_t id);
void notify_remove(const char *source, const char *key);
void notify_mark_read(uint32_t id);
void notify_mark_all_read();
int notify_list(const NotifyItem **items);
int notify_unread();
bool notify_dnd();
void notify_set_dnd(bool enabled);
void notify_tick(unsigned now);
