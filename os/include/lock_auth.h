#pragma once

#include <string>

namespace lock_auth {
enum class Result { Ok, InvalidPin, NotEnabled, WrongPin, IoError, InvalidFile };
bool enabled(const char *path = "/etc/graphite-pin");
Result set(const std::string &pin, const char *path = "/etc/graphite-pin");
Result verify(const std::string &pin, const char *path = "/etc/graphite-pin");
Result clear(const std::string &current, const char *path = "/etc/graphite-pin");
bool valid_pin(const std::string &pin);
struct AttemptLimiter {
    unsigned failures = 0;
    unsigned blocked_until = 0;
    bool blocked(unsigned now) const { return blocked_until && (int)(blocked_until - now) > 0; }
    bool reject(unsigned now, unsigned limit = 5, unsigned delay_ms = 30000) {
        if (blocked(now)) return true;
        if (++failures >= limit) { failures = 0; blocked_until = now + delay_ms; }
        return false;
    }
    void success() { failures = 0; }
};
}
