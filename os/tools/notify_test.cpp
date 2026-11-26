#include "notify.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    NotifySpec a{"test", "one", "First", "body", 1, N_NORMAL, false};
    uint32_t id = notify_push(a);
    assert(id && notify_unread() == 1);
    NotifySpec dup{"test", "one", "Updated", "new", 2, N_HIGH, false};
    assert(notify_push(dup) == id);
    const NotifyItem *items = nullptr;
    assert(notify_list(&items) == 1 && !std::strcmp(items[0].title, "Updated"));
    notify_mark_read(id);
    assert(notify_unread() == 0);
    NotifySpec ongoing{"system", "live", "Live", "state", 3, N_LOW, true};
    uint32_t ongoing_id = notify_push(ongoing);
    assert(!notify_dismiss(ongoing_id));
    for (int i = 0; i < 40; ++i) {
        char key[24]; std::snprintf(key, sizeof(key), "item-%d", i);
        NotifySpec s{"overflow", key, "Alert", "", (unsigned)i, N_NORMAL, false};
        assert(notify_push(s));
    }
    assert(notify_list(&items) == 32);
    bool kept_ongoing = false;
    for (int i = 0; i < 32; ++i) kept_ongoing |= items[i].id == ongoing_id;
    assert(kept_ongoing);
    assert(notify_dismiss(items[0].id));
    notify_set_dnd(true);
    assert(notify_dnd());
    notify_set_dnd(false);
    notify_clear();
    assert(notify_list(&items) == 1 && items[0].ongoing);
}
