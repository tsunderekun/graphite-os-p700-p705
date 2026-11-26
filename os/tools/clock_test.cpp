#include "clock_model.h"

#include <cassert>
#include <iostream>

int main() {
    using clock_model::State;
    clock_model::Timer a, b;
    a.start(125, 1000);
    assert(a.state == State::Running && a.remaining(1000) == 125);
    assert(a.advance(61000) && a.remaining(61000) == 65);
    a.pause(61000);
    assert(a.state == State::Idle && a.paused_seconds == 65);
    a.resume(70000);
    assert(a.state == State::Running && a.remaining(70000) == 65);
    assert(a.advance(135000));
    assert(a.state == State::Finished && a.should_ring());
    assert(!a.advance(136000));
    assert(a.should_ring());
    a.reset();
    assert(a.state == State::Idle && !a.should_ring());
    assert(clock_model::format_clock(3, 7) == "03:07");
    assert(clock_model::format_timer(3599) == "59:59");
    assert(clock_model::format_timer(3601) == "1:00:01");
    assert(clock_model::format_timer(-9) == "00:00");
    b.start(3, 10);
    assert(a.state == State::Idle && b.remaining(10) == 3);
    std::cout << "clock model: all tests passed\n";
}
