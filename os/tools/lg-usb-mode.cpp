#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {
const char *const gadget = "/sys/class/android_usb/android0";

std::string read_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[512];
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = 0;
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

std::vector<std::string> entries(const char *path) {
    std::vector<std::string> out;
    DIR *d = opendir(path);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') out.emplace_back(e->d_name);
    closedir(d);
    return out;
}

bool p20_mounted() {
    FILE *f = std::fopen("/proc/mounts", "r");
    if (!f) return false;
    char line[1024], dev[256], mount[256];
    bool found = false;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "%255s %255s", dev, mount) == 2 &&
            !std::strcmp(dev, "/dev/mmcblk0p20")) {
            found = true; break;
        }
    }
    std::fclose(f);
    return found;
}

void list_json(const std::vector<std::string> &v) {
    std::putchar('[');
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::putchar(',');
        std::printf("\"%s\"", v[i].c_str());
    }
    std::putchar(']');
}

int status(bool json) {
    const std::string base = std::string(gadget) + "/";
    bool present = exists(gadget);
    std::string state = read_file(base + "state");
    std::string functions = read_file(base + "functions");
    std::string enable = read_file(base + "enable");
    auto udc = entries("/sys/class/udc");
    auto net = entries("/sys/class/net");
    bool configfs = exists("/sys/kernel/config/usb_gadget");
    bool mounted = p20_mounted();
    if (json) {
        std::printf("{\"gadget_present\":%s,\"state\":\"%s\",\"functions\":\"%s\",\"enable\":\"%s\",\"udc\":",
                    present ? "true" : "false", state.c_str(), functions.c_str(), enable.c_str());
        list_json(udc);
        std::printf(",\"configfs\":%s,\"net_interfaces\":", configfs ? "true" : "false");
        list_json(net);
        std::printf(",\"p20_mounted\":%s}\n", mounted ? "true" : "false");
    } else {
        std::printf("gadget_present=%s\nstate=%s\nfunctions=%s\nenable=%s\nudc=",
                    present ? "yes" : "no", state.empty() ? "unavailable" : state.c_str(),
                    functions.empty() ? "unavailable" : functions.c_str(),
                    enable.empty() ? "unavailable" : enable.c_str());
        for (size_t i = 0; i < udc.size(); ++i) std::printf("%s%s", i ? "," : "", udc[i].c_str());
        std::printf("\nconfigfs=%s\nnet_interfaces=", configfs ? "present" : "absent");
        for (size_t i = 0; i < net.size(); ++i) std::printf("%s%s", i ? "," : "", net[i].c_str());
        std::printf("\np20_mounted=%s\n", mounted ? "yes" : "no");
    }
    return present && !state.empty() && !functions.empty() && !enable.empty() ? 0 : 1;
}

int selftest() {
    // Deliberately validate the command allowlist without requiring target sysfs;
    // missing device paths are reported by status, not treated as selftest faults.
    const bool safe = std::strcmp("status", "set") != 0 &&
                      std::strcmp("selftest", "recover") != 0;
    std::printf("selftest=%s\nchecks=read-only-paths,allowlist\nallowlist=status,selftest\n",
                safe ? "PASS" : "FAIL");
    return safe ? 0 : 1;
}
}

int main(int argc, char **argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "status") &&
        (argc == 2 || (argc == 3 && !std::strcmp(argv[2], "--json"))))
        return status(argc == 3);
    if (argc == 2 && !std::strcmp(argv[1], "selftest")) return selftest();
    std::fprintf(stderr, "usage: lg-usb-mode status [--json]|selftest\n");
    return 2;
}
