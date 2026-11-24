#include "graphite.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

Settings cfg;

std::string Settings::get(const std::string &k, const std::string &def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second;
}

int Settings::geti(const std::string &k, int def) const {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    return std::atoi(it->second.c_str());
}

void Settings::set(const std::string &k, const std::string &v) { kv[k] = v; }

static void parse_into(const std::string &text, std::map<std::string, std::string> &out) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (!k.empty()) out[k] = v;
            }
        }
        pos = end + 1;
    }
}

bool Settings::load(const char *path) {
    kv.clear();
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    parse_into(text, kv);
    return true;
}

bool settings_save_key(const char *key, const char *value, const char *path) {
    int fd = ::open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX) != 0) { ::close(fd); return false; }

    std::string text;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) text.append(buf, n);

    std::map<std::string, std::string> kv;
    parse_into(text, kv);
    kv[key] = value;

    std::string out;
    for (auto &p : kv) { out += p.first; out += '='; out += p.second; out += '\n'; }

    std::string tmp = std::string(path) + ".tmp";
    int tfd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd >= 0) {
        size_t off = 0;
        bool ok = true;
        while (off < out.size()) {
            ssize_t w = ::write(tfd, out.data() + off, out.size() - off);
            if (w <= 0) { ok = false; break; }
            off += (size_t)w;
        }
        ::fsync(tfd);
        ::close(tfd);
        if (ok) ::rename(tmp.c_str(), path);
        else ::unlink(tmp.c_str());
    }

    ::flock(fd, LOCK_UN);
    ::close(fd);

    cfg.kv[key] = value;
    return true;
}
