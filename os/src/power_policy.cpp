#include "power_policy.h"

#include <algorithm>

PowerPolicy::PowerPolicy(Writer write) : writer(std::move(write)) {}

void PowerPolicy::configure(unsigned dim_ms, unsigned lock_ms, int normal, int dim) {
    dim_after = dim_ms;
    lock_after = std::max(dim_ms, lock_ms);
    brightness = std::max(0, std::min(255, normal));
    dim_brightness = std::max(0, std::min(255, dim));
}

bool PowerPolicy::set_brightness(int value) {
    value = std::max(0, std::min(255, value));
    if (writer && !writer(value)) return false;
    brightness = value;
    return true;
}

bool PowerPolicy::input(unsigned now) {
    if (state == PowerState::Locked) return wake(now);
    last_input = now;
    if (state == PowerState::Dim) {
        if (writer && !writer(brightness)) return false;
        state = PowerState::Active;
    }
    return true;
}

bool PowerPolicy::lock(unsigned now) {
    if (state == PowerState::Locked) return true;
    last_input = now;
    if (writer && !writer(0)) return false;
    state = PowerState::Locked;
    return true;
}

bool PowerPolicy::wake(unsigned now) {
    if (state != PowerState::Locked) return input(now);
    if (writer && !writer(brightness)) return false;
    state = PowerState::Active;
    last_input = now;
    return true;
}

bool PowerPolicy::tick(unsigned now) {
    if (state == PowerState::Locked) return false;
    unsigned elapsed = now - last_input;
    if (elapsed >= lock_after) return lock(now);
    if (elapsed >= dim_after && state == PowerState::Active) {
        if (writer && !writer(dim_brightness)) return false;
        state = PowerState::Dim;
        return true;
    }
    return false;
}
