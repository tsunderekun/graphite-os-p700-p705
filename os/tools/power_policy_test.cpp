#include "power_policy.h"
#include <cassert>
#include <vector>

int main() {
    std::vector<int> writes;
    bool fail = false;
    PowerPolicy p([&](int n) { writes.push_back(n); return !fail; });
    p.configure(100, 300, 150, 40);
    p.last_input = 1000;
    assert(!p.tick(1099));
    assert(p.tick(1100) && p.state == PowerState::Dim && writes.back() == 40);
    assert(p.input(1200) && p.state == PowerState::Active && writes.back() == 150);
    assert(p.tick(1500) && p.state == PowerState::Locked && writes.back() == 0);
    assert(p.wake(1510) && p.state == PowerState::Active && writes.back() == 150);
    fail = true;
    assert(!p.tick(1610));
    assert(p.state == PowerState::Active); // failed sysfs write does not commit state
}
