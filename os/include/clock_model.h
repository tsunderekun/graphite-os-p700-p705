#pragma once

#include <string>

namespace clock_model {

enum class State { Idle, Running, Finished };

struct Timer {
    State state = State::Idle;
    int remaining_seconds = 0;
    int paused_seconds = 0;
    unsigned deadline_ms = 0;
    unsigned last_second = 0;
    bool ringing = false;

    void start(int seconds, unsigned now_ms);
    void pause(unsigned now_ms);
    void resume(unsigned now_ms);
    void reset();
    // Updates the value and returns true exactly when the timer finishes.
    bool advance(unsigned now_ms);
    int remaining(unsigned now_ms) const;
    bool should_ring() const { return ringing; }
};

std::string format_clock(int hour, int minute); // HH:MM
std::string format_timer(int seconds);          // MM:SS or H:MM:SS

} // namespace clock_model
