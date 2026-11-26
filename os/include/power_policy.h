#pragma once

#include <functional>

enum class PowerState { Active, Dim, Locked };

struct PowerPolicy {
    using Writer = std::function<bool(int)>;

    PowerState state = PowerState::Active;
    unsigned last_input = 0;
    unsigned dim_after = 10000;
    unsigned lock_after = 30000;
    int brightness = 150;
    int dim_brightness = 40;
    Writer writer;

    explicit PowerPolicy(Writer write = Writer());
    void configure(unsigned dim_ms, unsigned lock_ms, int normal, int dim);
    bool input(unsigned now);
    bool tick(unsigned now);
    bool lock(unsigned now);
    bool wake(unsigned now);
    bool set_brightness(int value);
};

extern PowerPolicy power_policy;
