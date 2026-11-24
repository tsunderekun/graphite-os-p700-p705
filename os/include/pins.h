#pragma once
#include <string>
#include <vector>

// Home-screen pins, stored one id per line in ~/.config/graphite/pins.
// Definitions live in screens/apps.cpp; apps, edit and home share the state.
namespace gpins {
const std::vector<std::string> &list();
bool has(const std::string &id);
bool toggle(const std::string &id);
void remove(const std::string &id);
} // namespace gpins
