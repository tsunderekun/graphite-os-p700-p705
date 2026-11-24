#include "graphite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    if (!app.init()) return 1;
    std::fprintf(stderr, "graphite: %dx%d depth %d, theme %s\n",
                 app.W, app.H, app.depth, theme.code);
    const char *start = std::getenv("GRAPHITE_SCREEN");
    if (start && !std::strcmp(start, "shade")) {
        app.open("home");
        app.push("shade");
    } else {
        app.open(start && *start ? start : "home");
    }
    app.run();
    return 0;
}
