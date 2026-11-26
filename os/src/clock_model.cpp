#include "clock_model.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace clock_model {

std::string format_clock(int hour, int minute) {
    char out[8];
    std::snprintf(out, sizeof(out), "%02d:%02d", ((hour % 24) + 24) % 24,
                  ((minute % 60) + 60) % 60);
    return out;
}

std::string format_timer(int seconds) {
    seconds = std::max(0, seconds);
    const int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    char out[24];
    if (h) std::snprintf(out, sizeof(out), "%d:%02d:%02d", h, m, s);
    else std::snprintf(out, sizeof(out), "%02d:%02d", m, s);
    return out;
}

int Timer::remaining(unsigned now_ms) const {
    if (state == State::Running) {
        const int32_t delta = static_cast<int32_t>(deadline_ms - now_ms);
        if (delta <= 0) return 0;
        return (delta + 999) / 1000;
    }
    return state == State::Finished ? 0 : std::max(0, paused_seconds);
}

void Timer::start(int seconds, unsigned now_ms) {
    paused_seconds = std::max(0, seconds);
    remaining_seconds = paused_seconds;
    ringing = false;
    if (!paused_seconds) { state = State::Idle; return; }
    state = State::Running;
    deadline_ms = now_ms + static_cast<unsigned>(paused_seconds) * 1000u;
    last_second = now_ms / 1000u;
}

void Timer::pause(unsigned now_ms) {
    if (state != State::Running) return;
    paused_seconds = remaining(now_ms);
    remaining_seconds = paused_seconds;
    state = paused_seconds ? State::Idle : State::Finished;
    ringing = state == State::Finished;
}

void Timer::resume(unsigned now_ms) {
    if (state == State::Idle && paused_seconds > 0) start(paused_seconds, now_ms);
}

void Timer::reset() {
    state = State::Idle;
    remaining_seconds = paused_seconds = 0;
    deadline_ms = last_second = 0;
    ringing = false;
}

bool Timer::advance(unsigned now_ms) {
    if (state != State::Running) return false;
    const int32_t delta = static_cast<int32_t>(deadline_ms - now_ms);
    if (delta <= 0) {
        state = State::Finished;
        remaining_seconds = paused_seconds = 0;
        ringing = true;
        return true;
    }
    remaining_seconds = (delta + 999) / 1000;
    const unsigned sec = now_ms / 1000u;
    const bool changed = sec != last_second;
    last_second = sec;
    return changed;
}

} // namespace clock_model
