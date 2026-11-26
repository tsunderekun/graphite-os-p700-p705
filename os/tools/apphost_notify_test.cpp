#include "notify.h"
#include <cassert>
#include <cstring>

int main() {
    NotifySpec first{"apphost", "launch:0:1", "Не удалось запустить", "нет окна за 15 с", 1, N_HIGH, false};
    uint32_t id = notify_push(first);
    assert(id);
    NotifySpec duplicate{"apphost", "launch:0:1", "Не удалось запустить", "обновлено", 2, N_HIGH, false};
    assert(notify_push(duplicate) == id);
    const NotifyItem *items = nullptr;
    assert(notify_list(&items) == 1);
    assert(!std::strcmp(items[0].source, "apphost"));
    assert(!std::strcmp(items[0].text, "обновлено"));

    NotifySpec next_episode{"apphost", "launch:0:2", "Не удалось запустить", "нет окна за 15 с", 3, N_HIGH, false};
    assert(notify_push(next_episode) != id);
    assert(notify_list(&items) == 2);
}
