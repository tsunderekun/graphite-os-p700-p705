#include "mpd.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

// Embedded cover pictures only (JPEG/PNG). Kept in this TU alone so the rest
// of the shell does not pay for the decoder.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace {

const int kPort = 6600;
const unsigned kStatusPollMs = 1000;
const unsigned kConnectTimeoutUs = 300000;   // 300 ms, as specified
const unsigned kRecvTimeoutUs = 500000;
const size_t kCoverLimit = 100;              // cached covers on disk (~30 MB)
const size_t kCoverMaxBytes = 8u << 20;      // refuse absurd pictures
const int kCoverBig = 384;
const int kCoverSmall = 96;

unsigned mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

bool env_on(const char *name) {
    const char *v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

std::string quote_arg(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

void split_kv(const std::string &line, std::string &key, std::string &value) {
    size_t p = line.find(": ");
    if (p == std::string::npos) {
        p = line.find(':');
        if (p == std::string::npos) {
            key = line;
            value.clear();
            return;
        }
        key = line.substr(0, p);
        value = line.substr(p + 1);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        return;
    }
    key = line.substr(0, p);
    value = line.substr(p + 2);
}

int to_int(const std::string &s) { return std::atoi(s.c_str()); }
double to_num(const std::string &s) { return std::atof(s.c_str()); }

std::vector<MpdSong> parse_songs(const std::vector<std::string> &lines) {
    std::vector<MpdSong> out;
    MpdSong *cur = nullptr;
    for (const std::string &line : lines) {
        std::string k, v;
        split_kv(line, k, v);
        if (k == "file") {
            out.push_back(MpdSong());
            cur = &out.back();
            cur->file = v;
            continue;
        }
        if (!cur) continue;
        if (k == "Title") {
            if (cur->title.empty()) cur->title = v;
        } else if (k == "Artist") {
            if (cur->artist.empty()) cur->artist = v;
        } else if (k == "AlbumArtist") {
            if (cur->albumartist.empty()) cur->albumartist = v;
        } else if (k == "Album") {
            if (cur->album.empty()) cur->album = v;
        } else if (k == "Genre") {
            if (cur->genre.empty()) cur->genre = v;
        } else if (k == "Date") {
            if (cur->date.empty()) cur->date = v;
        } else if (k == "Format") {
            if (cur->format.empty()) cur->format = v;
        } else if (k == "Pos") {
            cur->pos = to_int(v);
        } else if (k == "Id") {
            cur->id = to_int(v);
        } else if (k == "Time") {
            if (cur->time <= 0) cur->time = to_int(v);
        } else if (k == "duration") {
            cur->time = (int)(to_num(v) + 0.5);
        } else if (k == "Bitrate") {
            cur->bitrate = to_int(v);
        }
    }
    return out;
}

// ---------------------------------------------------------------- paths

std::string home_dir() {
    const char *home = std::getenv("HOME");
    if (home && *home) return home;
    return "/home/user";
}

// mkdir -p for the two cache/config paths we own.
void mkdirs(const std::string &path) {
    if (path.empty()) return;
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if (path[i] == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(path.c_str(), 0755);
}

std::string hex_hash(const std::string &s) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

std::string lower_ascii(const std::string &s) {
    std::string out = s;
    for (char &c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

bool less_nocase(const std::string &a, const std::string &b) {
    std::string la = lower_ascii(a), lb = lower_ascii(b);
    if (la != lb) return la < lb;
    return a < b;
}

std::string year_of(const std::string &date) {
    std::string y;
    for (char c : date) {
        if (c >= '0' && c <= '9') {
            y += c;
            if (y.size() == 4) break;
        } else if (!y.empty()) {
            break;
        }
    }
    return y.size() == 4 ? y : std::string();
}

std::string base_name(const std::string &path) {
    size_t slash = path.find_last_of('/');
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n = n.substr(0, dot);
    return n;
}

// ---------------------------------------------------------------- covers

uint16_t rgb565(const uint8_t *p) {
    return (uint16_t)(((uint16_t)(p[0] & 0xF8) << 8) |
                      ((uint16_t)(p[1] & 0xFC) << 3) | (p[2] >> 3));
}

// Square cover-crop + box downscale into `target`×`target` RGB565.
void scale_cover(const uint8_t *rgb, int sw, int sh, int target,
                 CoverImage &out) {
    out.px.clear();
    out.w = out.h = 0;
    if (!rgb || sw <= 0 || sh <= 0 || target <= 0) return;
    out.w = out.h = target;
    out.px.assign((size_t)target * (size_t)target, 0);
    int s = std::min(sw, sh);
    int ox = (sw - s) / 2, oy = (sh - s) / 2;
    for (int y = 0; y < target; y++) {
        int y0 = oy + (int)((long long)y * s / target);
        int y1 = oy + (int)((long long)(y + 1) * s / target);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > oy + s) y1 = oy + s;
        for (int x = 0; x < target; x++) {
            int x0 = ox + (int)((long long)x * s / target);
            int x1 = ox + (int)((long long)(x + 1) * s / target);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > ox + s) x1 = ox + s;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t *row = rgb + ((size_t)yy * (size_t)sw + (size_t)x0) * 3;
                for (int xx = x0; xx < x1; xx++, row += 3) {
                    r += row[0];
                    g += row[1];
                    b += row[2];
                    n++;
                }
            }
            if (!n) continue;
            uint8_t px[3] = {(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n)};
            out.px[(size_t)y * (size_t)target + x] = rgb565(px);
        }
    }
}

// A stand-in cover for GS_DEMO previews only: no MPD, no decoder involved.
void demo_cover(const std::string &key, CoverImage &big, CoverImage &small) {
    auto fill = [&](CoverImage &c, int n) {
        c.w = c.h = n;
        c.px.resize((size_t)n * (size_t)n);
        uint32_t h = 2166136261u;
        for (unsigned char ch : key) {
            h ^= ch;
            h *= 16777619u;
        }
        int r0 = 26 + (int)(h & 0x2f), g0 = 28 + (int)((h >> 6) & 0x2f),
            b0 = 34 + (int)((h >> 12) & 0x2f);
        int r1 = 92 + (int)((h >> 3) & 0x6f), g1 = 96 + (int)((h >> 9) & 0x6f),
            b1 = 108 + (int)((h >> 15) & 0x6f);
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                int t = (x + y) * 255 / (2 * n);
                uint8_t px[3] = {
                    (uint8_t)((r0 * (255 - t) + r1 * t) / 255),
                    (uint8_t)((g0 * (255 - t) + g1 * t) / 255),
                    (uint8_t)((b0 * (255 - t) + b1 * t) / 255)};
                c.px[(size_t)y * (size_t)n + x] = rgb565(px);
            }
        }
    };
    fill(big, kCoverBig);
    fill(small, kCoverSmall);
}

int tcp_open(const char *host, int port, unsigned timeout_us) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(fd);
        return -1;
    }
    int rc = ::connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    if (rc < 0) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (suseconds_t)timeout_us;
        int sel = ::select(fd + 1, nullptr, &wf, nullptr, &tv);
        if (sel <= 0) {
            ::close(fd);
            return -1;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            ::close(fd);
            return -1;
        }
    }
    return fd;   // still non-blocking: the idle socket never waits
}

} // namespace

// ---------------------------------------------------------------- cover key

std::string cover_key(const std::string &artist, const std::string &album,
                      const std::string &uri) {
    if (!album.empty()) {
        std::string a = lower_ascii(artist);
        if (a == "various artists" || a == "va") return album;
        return album + "\x1f" + artist;
    }
    size_t slash = uri.find_last_of('/');
    if (slash != std::string::npos && slash > 0)
        return "dir:" + uri.substr(0, slash);
    return uri.empty() ? std::string() : "file:" + uri;
}

std::string cover_key(const MpdSong &s) {
    std::string artist = !s.albumartist.empty() ? s.albumartist : s.artist;
    return cover_key(artist, s.album, s.file);
}

void CoverImage::blit(uint16_t *dst, int dstw, int x, int y, int size) const {
    if (!dst || !ok() || size <= 0 || dstw <= 0) return;
    for (int row = 0; row < size; row++) {
        int sy = (int)((long long)row * h / size);
        if (sy >= h) sy = h - 1;
        const uint16_t *src = px.data() + (size_t)sy * (size_t)w;
        uint16_t *d = dst + (size_t)(y + row) * (size_t)dstw + x;
        if (w == size) {
            std::memcpy(d, src, (size_t)size * sizeof(uint16_t));
            continue;
        }
        for (int col = 0; col < size; col++) {
            int sx = (int)((long long)col * w / size);
            if (sx >= w) sx = w - 1;
            d[col] = src[sx];
        }
    }
}

// ---------------------------------------------------------------- lifecycle

MpdClient::MpdClient() {
    demo_ = env_on("GS_DEMO");
    if (demo_) demo_fill();
}

MpdClient::~MpdClient() {
    idle_close();
    close_socket();
}

MpdClient &mpd_client() {
    static MpdClient client;
    return client;
}

// ---------------------------------------------------------------- connection

bool MpdClient::open_socket() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(fd);
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    int rc = ::connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return false;
    }
    if (rc < 0) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (suseconds_t)kConnectTimeoutUs;
        int sel = ::select(fd + 1, nullptr, &wf, nullptr, &tv);
        if (sel <= 0) {
            ::close(fd);
            return false;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            ::close(fd);
            return false;
        }
    }
    if (::fcntl(fd, F_SETFL, flags) < 0) {
        ::close(fd);
        return false;
    }

    struct timeval rtv;
    rtv.tv_sec = 0;
    rtv.tv_usec = (suseconds_t)kRecvTimeoutUs;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv)) < 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv)) < 0) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    rbuf_.clear();
    error_.clear();

    std::string hello;
    if (!read_line(hello) || hello.rfind("OK MPD", 0) != 0) {
        close_socket();
        return false;
    }
    connected_ = true;
    fail_count_ = 0;
    return true;
}

void MpdClient::close_socket() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    connected_ = false;
    rbuf_.clear();
}

void MpdClient::retry_later(unsigned now) {
    static const unsigned delays[4] = {1000, 2000, 5000, 10000};
    unsigned idx = (unsigned)std::min(fail_count_, 3);
    if (fail_count_ < 3) fail_count_++;
    next_connect_ = now + delays[idx];
}

void MpdClient::io_failed(unsigned now) {
    close_socket();
    pending_.clear();
    retry_later(now);
}

bool MpdClient::send_line(const std::string &line) {
    if (fd_ < 0) return false;
    std::string data = line;
    data += '\n';
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd_, data.data() + off, data.size() - off,
#ifdef MSG_NOSIGNAL
                           MSG_NOSIGNAL
#else
                           0
#endif
        );
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += (size_t)n;
    }
    return true;
}

bool MpdClient::read_line(std::string &out) {
    if (fd_ < 0) return false;
    for (;;) {
        size_t p = rbuf_.find('\n');
        if (p != std::string::npos) {
            out = rbuf_.substr(0, p);
            rbuf_.erase(0, p + 1);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return true;
        }
        char buf[1024];
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        rbuf_.append(buf, (size_t)n);
        if (rbuf_.size() > (1u << 20)) return false;
    }
}

// Raw bytes for `readpicture`: no line framing, no 1 MB line cap.
bool MpdClient::read_exact(size_t n, std::vector<uint8_t> &out) {
    if (fd_ < 0) return false;
    size_t have = std::min(n, rbuf_.size());
    if (have) {
        out.insert(out.end(), (const uint8_t *)rbuf_.data(),
                   (const uint8_t *)rbuf_.data() + have);
        rbuf_.erase(0, have);
    }
    size_t need = n - have;
    while (need > 0) {
        char buf[4096];
        size_t want = std::min(need, sizeof(buf));
        ssize_t r = ::recv(fd_, buf, want, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;
        out.insert(out.end(), (const uint8_t *)buf, (const uint8_t *)buf + r);
        need -= (size_t)r;
        if (out.size() > kCoverMaxBytes) return false;
    }
    return true;
}

// 1 = OK, 0 = ACK (connection alive, command rejected), -1 = I/O failure.
int MpdClient::read_response(std::vector<std::string> &lines) {
    lines.clear();
    for (;;) {
        std::string line;
        if (!read_line(line)) return -1;
        if (line.rfind("OK", 0) == 0) return 1;
        if (line.rfind("ACK", 0) == 0) {
            error_ = line;
            return 0;
        }
        lines.push_back(line);
    }
}

// ---------------------------------------------------------------- fetching

bool MpdClient::fetch_status(unsigned now) {
    if (!send_line("status")) return false;
    std::vector<std::string> lines;
    int r = read_response(lines);
    if (r < 0) return false;
    if (r == 0) {
        need_status_ = false;
        last_status_ = now;
        return true;
    }

    MpdStatus s;
    for (const std::string &line : lines) {
        std::string k, v;
        split_kv(line, k, v);
        if (k == "state") s.state = v;
        else if (k == "elapsed") s.elapsed = to_num(v);
        else if (k == "duration") s.duration = to_num(v);
        else if (k == "volume") s.volume = to_int(v);
        else if (k == "random") s.random = to_int(v) != 0;
        else if (k == "repeat") s.repeat = to_int(v) != 0;
        else if (k == "single") s.single = to_int(v) != 0;
        else if (k == "song") s.song = to_int(v);
        else if (k == "songid") s.songid = to_int(v);
        else if (k == "playlistlength") s.playlistlength = to_int(v);
        else if (k == "playlist") s.playlist = to_int(v);
        else if (k == "bitrate") s.bitrate = to_int(v);
        else if (k == "audio") s.audio = v;
        else if (k == "error") s.error = v;
    }
    if (s.state.empty()) s.state = "stop";

    status_ = s;
    status_at_ = now;
    last_status_ = now;
    need_status_ = false;

    if (status_.playlistlength == 0) {
        queue_.clear();
        queue_loaded_ = true;
        queue_dirty_ = false;
        last_queue_version_ = status_.playlist;
    } else if (watch_queue_ &&
               (queue_dirty_ || !queue_loaded_ ||
                last_queue_version_ != status_.playlist ||
                queue_.size() != (size_t)status_.playlistlength)) {
        if (!fetch_queue()) return false;
    }
    return true;
}

bool MpdClient::fetch_current() {
    if (!send_line("currentsong")) return false;
    std::vector<std::string> lines;
    int r = read_response(lines);
    if (r < 0) return false;
    if (r == 0) return true;
    std::vector<MpdSong> songs = parse_songs(lines);
    song_ = songs.empty() ? MpdSong() : songs.front();
    return true;
}

bool MpdClient::fetch_queue() {
    if (!send_line("playlistinfo")) return false;
    std::vector<std::string> lines;
    int r = read_response(lines);
    if (r < 0) return false;
    if (r == 0) return true;
    queue_ = parse_songs(lines);
    queue_loaded_ = true;
    queue_dirty_ = false;
    last_queue_version_ = status_.playlist;
    return true;
}

bool MpdClient::run_pending(unsigned now) {
    for (int n = 0; n < 8 && !pending_.empty(); n++) {
        Pending p = pending_.front();
        pending_.pop_front();
        if (!send_line(p.line)) {
            io_failed(now);
            return false;
        }
        std::vector<std::string> lines;
        int r = read_response(lines);
        if (r < 0) {
            io_failed(now);
            return false;
        }
        if (r == 1 && p.kind == KIND_QUEUE) {
            queue_ = parse_songs(lines);
            queue_loaded_ = true;
            queue_dirty_ = false;
            last_queue_version_ = status_.playlist;
        } else if (r == 1 && p.kind == KIND_PLAYLIST) {
            pl_songs_ = parse_songs(lines);
        }
    }
    return true;
}

void MpdClient::tick(unsigned now) {
    now_ = now;
    if (demo_) {
        if (!demo_t0_) demo_t0_ = now;
        library_tick(now);
        state_tick(now);
        return;
    }

    if (fd_ < 0 && (int)(now - next_connect_) >= 0) {
        if (open_socket()) {
            need_status_ = true;
            last_status_ = 0;
            if (watch_queue_) queue_dirty_ = true;
            // Ask for whole cover pictures in one response (8 KB otherwise).
            if (send_line("binarylimit 1048576")) {
                std::vector<std::string> ignored;
                read_response(ignored);
            }
            next_idle_connect_ = now;   // idle socket follows the control link
        } else {
            retry_later(now);
        }
    }

    if (fd_ >= 0) {
        if (run_pending(now)) {
            if (need_status_ || (unsigned)(now - last_status_) >= kStatusPollMs) {
                if (!fetch_status(now)) {
                    io_failed(now);
                } else if (!fetch_current()) {
                    io_failed(now);
                } else {
                    last_status_ = now;
                }
            }
        }
    }

    idle_tick(now);
    cover_tick(now);
    library_tick(now);
    state_tick(now);
}

void MpdClient::set_queue_watch(bool on) {
    if (on == watch_queue_) return;
    watch_queue_ = on;
    if (on) {
        if (!queue_loaded_) queue_dirty_ = true;
        need_status_ = true;      // fetch the queue right away
    }
}

void MpdClient::retry_now() {
    fail_count_ = 0;
    next_connect_ = 0;
    next_idle_connect_ = 0;
    need_status_ = true;
    if (idle_fd_ >= 0) idle_close();
}

double MpdClient::elapsed(unsigned now) const {
    double el = status_.elapsed < 0 ? 0.0 : status_.elapsed;
    if (status_.state != "play" || status_.duration <= 0) return el;
    if (demo_) {
        unsigned t0 = demo_t0_ ? demo_t0_ : now;
        if (now < t0) return el;
        double t = el + (now - t0) * 0.001;
        while (t > status_.duration) t -= status_.duration;
        return t;
    }
    if (!connected_ || now < status_at_) return el;
    double t = el + (now - status_at_) * 0.001;
    if (t > status_.duration) t = status_.duration;
    return t;
}

// ---------------------------------------------------------------- idle

void MpdClient::idle_close() {
    if (idle_fd_ >= 0) ::close(idle_fd_);
    idle_fd_ = -1;
    idle_buf_.clear();
    idle_waiting_ = false;
}

bool MpdClient::idle_open(unsigned now) {
    if ((int)(now - next_idle_connect_) < 0) return false;
    next_idle_connect_ = now + 10000;
    int fd = tcp_open("127.0.0.1", kPort, kConnectTimeoutUs);
    if (fd < 0) return false;
    idle_fd_ = fd;
    idle_buf_.clear();
    idle_waiting_ = false;
    next_idle_connect_ = now + 3000;
    return true;
}

void MpdClient::idle_send(const std::string &line) {
    if (idle_fd_ < 0) return;
    std::string data = line;
    data += '\n';
    size_t off = 0;
    int waits = 0;
    while (off < data.size()) {
        ssize_t n = ::send(idle_fd_, data.data() + off, data.size() - off,
#ifdef MSG_NOSIGNAL
                           MSG_NOSIGNAL
#else
                           0
#endif
        );
        if (n < 0) {
            if (errno == EINTR) continue;
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && waits++ < 2) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(idle_fd_, &wf);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 20000;   // 20 ms: idle must never stall a tick
                if (::select(idle_fd_ + 1, nullptr, &wf, nullptr, &tv) > 0)
                    continue;
                break;
            }
            idle_close();
            return;
        }
        if (n == 0) {
            idle_close();
            return;
        }
        off += (size_t)n;
    }
    if (off < data.size()) idle_close();   // stuck socket: drop and reconnect
}

void MpdClient::idle_event(const std::string &name) {
    idle_events_++;
    if (name == "player" || name == "mixer" || name == "options" ||
        name == "playlist" || name == "output") {
        need_status_ = true;
    }
    if (name == "playlist") queue_dirty_ = true;
    if (name == "database" || name == "update") lib_need_stats_ = true;
}

void MpdClient::idle_read(unsigned now) {
    (void)now;
    if (idle_fd_ < 0) return;
    for (int guard = 0; guard < 8; guard++) {
        char buf[2048];
        ssize_t n = ::recv(idle_fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            idle_buf_.append(buf, (size_t)n);
            continue;   // drain; non-blocking recv returns EAGAIN when done
        }
        if (n == 0) {
            idle_close();
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        idle_close();
        return;
    }

    for (;;) {
        size_t p = idle_buf_.find('\n');
        if (p == std::string::npos) break;
        std::string line = idle_buf_.substr(0, p);
        idle_buf_.erase(0, p + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("OK MPD", 0) == 0) {
            idle_send("idle player mixer options playlist database update");
            idle_waiting_ = true;
            continue;
        }
        if (line.rfind("changed: ", 0) == 0) {
            idle_event(line.substr(9));
            continue;
        }
        if (line == "OK") {
            // An idle cycle ended (event delivered or noidle): re-arm.
            idle_send("idle player mixer options playlist database update");
            idle_waiting_ = true;
            continue;
        }
        if (line.rfind("ACK", 0) == 0) {
            idle_close();
            return;
        }
    }
    if (idle_buf_.size() > 65536) idle_buf_.clear();
}

void MpdClient::idle_tick(unsigned now) {
    if (demo_) return;
    if (fd_ < 0) {
        if (idle_fd_ >= 0) idle_close();
        return;
    }
    if (idle_fd_ < 0) {
        idle_open(now);
        return;
    }
    idle_read(now);
}

// ---------------------------------------------------------------- covers

std::string MpdClient::cache_dir() const { return home_dir() + "/.cache/graphite"; }
std::string MpdClient::config_dir() const { return home_dir() + "/.config/graphite"; }

std::string MpdClient::cover_path(const std::string &key) const {
    return cache_dir() + "/covers/" + hex_hash(key) + ".565";
}

void MpdClient::want_cover(const std::string &key, const std::string &uri,
                           bool large) {
    if (key.empty()) return;
    for (CoverWant &w : cover_want_) {
        if (w.key != key) continue;
        w.at = now_;
        if (!uri.empty()) w.uri = uri;
        w.large = w.large || large;
        return;
    }
    if (cover_want_.size() >= 96) cover_want_.erase(cover_want_.begin());
    cover_want_.push_back(CoverWant{key, uri, large, now_});
}

const CoverImage *MpdClient::cover(const std::string &key, int size) const {
    if (key.empty()) return nullptr;
    if (size <= 128) {
        auto it = cover_small_.find(key);
        return it == cover_small_.end() ? nullptr : &it->second;
    }
    auto it = cover_big_.find(key);
    return it == cover_big_.end() ? nullptr : &it->second;
}

bool MpdClient::cover_missing(const std::string &key) const {
    return !key.empty() && cover_fail_.count(key) != 0;
}

bool MpdClient::cover_load_file(const std::string &key, CoverImage *big,
                                CoverImage *small) {
    std::string path = cover_path(key);
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4] = {0, 0, 0, 0};
    uint16_t bw = 0, sw = 0;
    bool ok = std::fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
              std::memcmp(magic, "GRC1", 4) == 0 &&
              std::fread(&bw, sizeof(bw), 1, f) == 1 &&
              std::fread(&sw, sizeof(sw), 1, f) == 1 && bw > 0 && bw <= 512 &&
              sw > 0 && sw <= 256;
    if (ok && big) {
        big->w = big->h = (int)bw;
        big->px.resize((size_t)bw * (size_t)bw);
        ok = std::fread(big->px.data(), sizeof(uint16_t), big->px.size(), f) ==
             big->px.size();
    } else if (ok) {
        ok = std::fseek(f, (long)((size_t)bw * (size_t)bw * sizeof(uint16_t)),
                        SEEK_CUR) == 0;
    }
    if (ok && small) {
        small->w = small->h = (int)sw;
        small->px.resize((size_t)sw * (size_t)sw);
        ok = std::fread(small->px.data(), sizeof(uint16_t), small->px.size(),
                        f) == small->px.size();
    }
    std::fclose(f);
    if (!ok) {
        if (big) {
            big->w = big->h = 0;
            big->px.clear();
        }
        if (small) {
            small->w = small->h = 0;
            small->px.clear();
        }
    }
    return ok;
}

void MpdClient::cover_save_file(const std::string &key, const CoverImage &big,
                                const CoverImage &small) {
    if (!big.ok() || !small.ok()) return;
    mkdirs(cache_dir() + "/covers");
    std::string path = cover_path(key);
    std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    uint16_t bw = (uint16_t)big.w, sw = (uint16_t)small.w;
    bool ok = std::fwrite("GRC1", 1, 4, f) == 4 &&
              std::fwrite(&bw, sizeof(bw), 1, f) == 1 &&
              std::fwrite(&sw, sizeof(sw), 1, f) == 1 &&
              std::fwrite(big.px.data(), sizeof(uint16_t), big.px.size(), f) ==
                  big.px.size() &&
              std::fwrite(small.px.data(), sizeof(uint16_t), small.px.size(),
                          f) == small.px.size();
    std::fclose(f);
    if (!ok) {
        ::unlink(tmp.c_str());
        return;
    }
    ::rename(tmp.c_str(), path.c_str());
}

void MpdClient::cover_prune(unsigned now) {
    std::string dir = cache_dir() + "/covers";
    DIR *d = ::opendir(dir.c_str());
    if (!d) return;
    struct Item {
        std::string path;
        time_t mtime;
    };
    std::vector<Item> items;
    while (struct dirent *e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 5 || n.compare(n.size() - 4, 4, ".565") != 0) continue;
        std::string p = dir + "/" + n;
        struct stat st;
        if (::stat(p.c_str(), &st) != 0) continue;
        items.push_back(Item{p, st.st_mtime});
    }
    ::closedir(d);
    if (items.size() <= kCoverLimit) return;
    std::sort(items.begin(), items.end(),
              [](const Item &a, const Item &b) { return a.mtime < b.mtime; });
    while (items.size() > kCoverLimit) {
        ::unlink(items.front().path.c_str());
        items.erase(items.begin());
    }
    cover_pruned_ = now;
}

void MpdClient::cover_remember(const std::string &key, const CoverImage &big,
                               const CoverImage &small, bool large) {
    if (small.ok()) {
        if (!cover_small_.count(key)) cover_small_lru_.push_back(key);
        cover_small_[key] = small;
        cover_fail_.erase(key);
        while (cover_small_.size() > 64 && !cover_small_lru_.empty()) {
            std::string k = cover_small_lru_.front();
            cover_small_lru_.pop_front();
            if (k == key) continue;
            cover_small_.erase(k);
        }
    }
    if (large && big.ok()) {
        if (!cover_big_.count(key)) cover_big_lru_.push_back(key);
        cover_big_[key] = big;
        while (cover_big_.size() > 4 && !cover_big_lru_.empty()) {
            std::string k = cover_big_lru_.front();
            cover_big_lru_.pop_front();
            if (k == key) continue;
            cover_big_.erase(k);
        }
    }
}

// <uri> <offset> -> "size: N", "type: image/jpeg", "binary: N", N bytes, OK
bool MpdClient::cover_fetch(const std::string &uri, std::vector<uint8_t> &jpeg) {
    jpeg.clear();
    if (fd_ < 0 || uri.empty()) return false;
    size_t offset = 0, total = 0;
    for (int guard = 0; guard < 64; guard++) {
        std::string cmd = "readpicture " + quote_arg(uri) + " " +
                          std::to_string((unsigned long)offset);
        if (!send_line(cmd)) return false;
        size_t size = 0, chunk = 0;
        bool got = false;
        for (int lines = 0; lines < 8; lines++) {
            std::string line;
            if (!read_line(line)) return false;
            if (line == "OK") return false;          // no embedded picture
            if (line.rfind("ACK", 0) == 0) {
                error_ = line;
                return false;
            }
            std::string k, v;
            split_kv(line, k, v);
            if (k == "size") {
                size = (size_t)to_int(v);
            } else if (k == "binary") {
                chunk = (size_t)to_int(v);
                got = true;
                break;
            }
        }
        if (!got || chunk == 0) return false;
        if (!read_exact(chunk, jpeg)) return false;
        for (int lines = 0; lines < 4; lines++) {
            std::string line;
            if (!read_line(line)) return false;
            if (line == "OK") break;
            if (line.rfind("ACK", 0) == 0) break;
        }
        offset += chunk;
        if (size > 0) total = size;
        if (total == 0 || offset >= total) break;
    }
    return !jpeg.empty() && jpeg.size() <= kCoverMaxBytes;
}

void MpdClient::cover_tick(unsigned now) {
    if (demo_ || cover_want_.empty()) return;

    // Drop requests no screen refreshed (screens re-register every tick).
    for (size_t i = 0; i < cover_want_.size();) {
        if ((int)(now - cover_want_[i].at) > 4000) cover_want_.erase(cover_want_.begin() + i);
        else i++;
    }
    if (cover_want_.empty()) return;

    // 1) disk cache first: cheap, works offline, no MPD traffic.
    for (const CoverWant &w : cover_want_) {
        bool have_small = cover_small_.count(w.key) != 0;
        bool have_big = !w.large || cover_big_.count(w.key) != 0;
        if (have_small && have_big) continue;
        auto t = cover_tried_.find(w.key);
        if (t != cover_tried_.end() && (int)(now - t->second) < 5000 &&
            !have_small)
            continue;
        cover_tried_[w.key] = now;
        CoverImage big, small;
        if (!cover_load_file(w.key, w.large ? &big : nullptr, &small)) continue;
        cover_remember(w.key, big, small, w.large);
    }

    // 2) at most one readpicture + decode per tick, so the UI stays responsive.
    for (const CoverWant &w : cover_want_) {
        if (cover_small_.count(w.key)) continue;
        auto f = cover_fail_.find(w.key);
        if (f != cover_fail_.end() && (int)(now - f->second) < 0) continue;
        if (fd_ < 0 || w.uri.empty()) continue;

        std::vector<uint8_t> jpeg;
        if (!cover_fetch(w.uri, jpeg)) {
            if (fd_ < 0) return;                    // socket died: retry later
            cover_fail_[w.key] = now + 300000;      // no picture: back off 5 min
            continue;
        }
        int iw = 0, ih = 0, ic = 0;
        unsigned char *rgb = stbi_load_from_memory(
            jpeg.data(), (int)jpeg.size(), &iw, &ih, &ic, 3);
        if (!rgb) {
            cover_fail_[w.key] = now + 300000;
            continue;
        }
        CoverImage big, small;
        scale_cover(rgb, iw, ih, kCoverBig, big);
        scale_cover(rgb, iw, ih, kCoverSmall, small);
        stbi_image_free(rgb);
        if (!big.ok() || !small.ok()) {
            cover_fail_[w.key] = now + 60000;
            continue;
        }
        cover_save_file(w.key, big, small);
        cover_remember(w.key, big, small, w.large);
        if (!cover_pruned_ || (int)(now - cover_pruned_) > 60000) cover_prune(now);
        return;                                     // one decode per tick
    }
}

// ---------------------------------------------------------------- library

void MpdClient::set_library_watch(bool on) {
    watch_library_ = on;
    if (on) lib_watch_at_ = now_;
    if (!on || lib_started_) return;
    lib_started_ = true;
    if (demo_) {
        lib_state_ = LIB_READY;
        return;
    }
    lib_state_ = LIB_LOADING;
    lib_error_.clear();
    if (!library_load_cache()) {
        lib_need_list_ = true;
        lib_cache_ver_ = 0;
    }
    lib_need_stats_ = true;
}

bool MpdClient::fetch_stats(unsigned now) {
    (void)now;
    if (fd_ < 0) return false;
    if (!send_line("stats")) return false;
    std::vector<std::string> lines;
    int r = read_response(lines);
    if (r < 0) return false;
    if (r == 0) return true;
    for (const std::string &line : lines) {
        std::string k, v;
        split_kv(line, k, v);
        if (k == "db_update") lib_check_ = (unsigned)to_int(v);
    }
    return true;
}

bool MpdClient::fetch_listallinfo(unsigned now) {
    (void)now;
    if (fd_ < 0) return false;
    if (!send_line("listallinfo")) return false;

    std::vector<LibraryAlbum> albums;
    std::map<std::string, size_t> idx;
    int songs = 0;
    MpdSong cur;
    bool have = false;
    bool io_ok = true;

    auto flush = [&]() {
        if (!have) return;
        have = false;
        std::string album = cur.album;
        std::string artist = !cur.albumartist.empty() ? cur.albumartist : cur.artist;
        if (album.empty()) {
            size_t slash = cur.file.find_last_of('/');
            album = (slash == std::string::npos || slash == 0)
                        ? std::string("Без альбома")
                        : cur.file.substr(0, slash);
            artist.clear();
        }
        std::string la = lower_ascii(artist);
        bool comp = artist.empty() || la == "various artists" || la == "va";
        std::string key = (comp ? std::string("\x01") : artist) + "\x1f" + album;
        auto it = idx.find(key);
        size_t i;
        if (it == idx.end()) {
            i = albums.size();
            LibraryAlbum a;
            a.album = album;
            a.artist = comp ? "Various Artists" : artist;
            a.year = year_of(cur.date);
            a.uri = cur.file;
            albums.push_back(a);
            idx[key] = i;
        } else {
            i = it->second;
        }
        LibraryAlbum &a = albums[i];
        if (a.year.empty()) a.year = year_of(cur.date);
        if (!cur.file.empty()) a.tracks.push_back(cur.file);
        if (cur.time > 0) a.duration += cur.time;
        songs++;
    };

    for (int guard = 0; guard < 400000; guard++) {
        std::string line;
        if (!read_line(line)) {
            io_ok = false;
            break;
        }
        if (line.rfind("OK", 0) == 0) break;
        if (line.rfind("ACK", 0) == 0) {
            error_ = line;
            io_ok = false;
            break;
        }
        std::string k, v;
        split_kv(line, k, v);
        if (k == "file") {
            flush();
            cur = MpdSong();
            cur.file = v;
            have = true;
        } else if (have) {
            if (k == "Artist") {
                if (cur.artist.empty()) cur.artist = v;
            } else if (k == "AlbumArtist") {
                if (cur.albumartist.empty()) cur.albumartist = v;
            } else if (k == "Album") {
                if (cur.album.empty()) cur.album = v;
            } else if (k == "Date") {
                if (cur.date.empty()) cur.date = v;
            } else if (k == "Time") {
                if (cur.time <= 0) cur.time = to_int(v);
            } else if (k == "duration") {
                cur.time = (int)(to_num(v) + 0.5);
            }
        }
    }
    flush();
    if (!io_ok) return false;

    if (albums.size() > 1) {
        std::sort(albums.begin(), albums.end(),
                  [](const LibraryAlbum &a, const LibraryAlbum &b) {
                      if (a.artist != b.artist) return less_nocase(a.artist, b.artist);
                      return less_nocase(a.album, b.album);
                  });
    }
    albums_ = albums;
    album_index_.clear();
    for (size_t i = 0; i < albums_.size(); i++)
        album_index_[albums_[i].artist + "\x1f" + albums_[i].album] = (int)i;
    lib_songs_ = songs;
    lib_cache_ver_ = lib_check_;
    lib_state_ = LIB_READY;
    lib_error_.clear();
    library_save_cache();
    return true;
}

void MpdClient::library_tick(unsigned now) {
    if (!watch_library_) return;
    if (demo_) {
        if (!albums_.empty()) lib_state_ = LIB_READY;
        return;
    }
    // Only do the heavy work while the library screen is actually in front:
    // a 1-2 MB listallinfo must never stall the player's tick.
    if ((int)(now - lib_watch_at_) > 3000) return;
    if (!connected_) {
        lib_error_ = "MPD недоступен";
        if (albums_.empty()) lib_state_ = LIB_ERROR;
        return;
    }
    if (lib_need_stats_) {
        lib_need_stats_ = false;
        if (!fetch_stats(now)) {
            io_failed(now);
            return;
        }
        bool same = lib_check_ != 0 && lib_check_ == lib_cache_ver_;
        if (!same) lib_need_list_ = true;
        else if (lib_state_ == LIB_LOADING) lib_state_ = LIB_READY;
    }
    if (lib_need_list_) {
        lib_need_list_ = false;
        lib_state_ = LIB_LOADING;
        lib_error_.clear();
        if (!fetch_listallinfo(now)) {
            if (fd_ < 0) io_failed(now);
            lib_state_ = albums_.empty() ? LIB_ERROR : LIB_READY;
            lib_error_ = error_.empty() ? "Не удалось прочитать библиотеку"
                                        : error_;
        }
    }
}

void MpdClient::library_rebuild() {
    lib_started_ = true;
    lib_need_stats_ = false;
    lib_need_list_ = true;
    lib_state_ = LIB_LOADING;
    lib_error_.clear();
}

void MpdClient::library_check() { lib_need_stats_ = true; }

bool MpdClient::library_load_cache() {
    std::string path = cache_dir() + "/library.tsv";
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    char *buf = nullptr;
    size_t cap = 0;
    ssize_t n = 0;
    std::vector<LibraryAlbum> albums;
    int songs = 0;
    unsigned ver = 0;
    bool header_ok = false;

    while ((n = ::getline(&buf, &cap, f)) > 0) {
        std::string line(buf, (size_t)n);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty()) continue;
        if (!header_ok) {
            header_ok = line.rfind("#v1\t", 0) == 0;
            if (!header_ok) break;
            std::vector<std::string> f2;
            size_t p = 4;
            while (p <= line.size()) {
                size_t e = line.find('\t', p);
                if (e == std::string::npos) e = line.size();
                f2.push_back(line.substr(p, e - p));
                p = e + 1;
            }
            if (f2.size() >= 1) ver = (unsigned)to_int(f2[0]);
            if (f2.size() >= 3) songs = to_int(f2[2]);
            continue;
        }
        std::vector<std::string> f2;
        size_t p = 0;
        while (p <= line.size()) {
            size_t e = line.find('\t', p);
            if (e == std::string::npos) e = line.size();
            f2.push_back(line.substr(p, e - p));
            p = e + 1;
        }
        if (f2.size() < 4) continue;
        LibraryAlbum a;
        a.album = f2[0];
        a.artist = f2[1];
        a.year = f2[2];
        a.uri = f2[3];
        // columns: album, artist, year, uri, tracks, duration, uris
        if (f2.size() > 5) a.duration = to_int(f2[5]);
        if (f2.size() > 6) {
            const std::string &all = f2[6];
            size_t s = 0;
            while (s <= all.size()) {
                size_t e = all.find('\x1f', s);
                if (e == std::string::npos) e = all.size();
                if (e > s) a.tracks.push_back(all.substr(s, e - s));
                s = e + 1;
            }
        }
        albums.push_back(a);
        if (albums.size() > 20000) break;
    }
    if (buf) std::free(buf);
    std::fclose(f);
    if (!header_ok) return false;

    albums_ = albums;
    album_index_.clear();
    for (size_t i = 0; i < albums_.size(); i++)
        album_index_[albums_[i].artist + "\x1f" + albums_[i].album] = (int)i;
    if (songs <= 0)
        for (const LibraryAlbum &a : albums_) songs += (int)a.tracks.size();
    lib_songs_ = songs;
    lib_cache_ver_ = ver;
    lib_state_ = LIB_READY;
    return true;
}

void MpdClient::library_save_cache() {
    mkdirs(cache_dir());
    std::string path = cache_dir() + "/library.tsv";
    std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "#v1\t%u\t%u\t%d\n", lib_cache_ver_, (unsigned)albums_.size(),
                 lib_songs_);
    for (const LibraryAlbum &a : albums_) {
        std::string tracks;
        for (size_t i = 0; i < a.tracks.size(); i++) {
            if (i) tracks += '\x1f';
            tracks += a.tracks[i];
        }
        std::fprintf(f, "%s\t%s\t%s\t%s\t%d\t%d\t%s\n", a.album.c_str(),
                     a.artist.c_str(), a.year.c_str(), a.uri.c_str(),
                     (int)a.tracks.size(), a.duration, tracks.c_str());
    }
    bool ok = std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        ::unlink(tmp.c_str());
        return;
    }
    ::rename(tmp.c_str(), path.c_str());
}

void MpdClient::play_album(int index) {
    if (index < 0 || index >= (int)albums_.size()) return;
    const LibraryAlbum &a = albums_[index];
    if (demo_) {
        // Rebuild the demo queue from the album's uris.
        queue_.clear();
        int pos = 0;
        for (const std::string &u : a.tracks) {
            MpdSong s;
            s.file = u;
            s.title = base_name(u);
            s.artist = a.artist;
            s.album = a.album;
            s.date = a.year;
            s.id = pos + 1;
            s.pos = pos;
            s.time = 180 + (pos * 17) % 120;
            queue_.push_back(s);
            pos++;
        }
        status_.playlistlength = (int)queue_.size();
        status_.playlist++;
        status_.song = 0;
        status_.songid = queue_.empty() ? -1 : queue_[0].id;
        song_ = queue_.empty() ? MpdSong() : queue_[0];
        status_.duration = song_.time;
        status_.elapsed = 0;
        status_.state = "play";
        demo_t0_ = now_;
        queue_loaded_ = true;
        return;
    }

    std::vector<std::string> cmds;
    cmds.push_back("clear");
    std::string filter;
    if (!a.tracks.empty()) {
        for (const std::string &u : a.tracks) cmds.push_back("add " + quote_arg(u));
    } else {
        // Cache written by an older build: exact-match filter as a fallback.
        filter = "(Album == " + quote_arg(a.album) + ")";
        if (a.artist != "Various Artists" && !a.artist.empty())
            filter = "(Album == " + quote_arg(a.album) +
                     " AND AlbumArtist == " + quote_arg(a.artist) + ")";
        cmds.push_back("findadd " + filter);
    }
    cmds.push_back("play");
    enqueue_list(cmds);
    queue_loaded_ = false;
    queue_dirty_ = true;
    status_.state = "play";
    status_.elapsed = 0;
    if (!a.tracks.empty()) {
        status_.song = 0;
        status_.playlistlength = (int)a.tracks.size();
    }
    state_dirty_ = true;
}

void MpdClient::play_all() {
    if (albums_.empty()) return;
    if (demo_) {
        play_album(0);
        return;
    }
    std::vector<std::string> cmds;
    cmds.push_back("clear");
    size_t count = 0;
    for (const LibraryAlbum &a : albums_) {
        for (const std::string &u : a.tracks) {
            if (count >= 5000) break;
            cmds.push_back("add " + quote_arg(u));
            count++;
        }
        if (count >= 5000) break;
    }
    if (count == 0) return;
    cmds.push_back("play");

    // One MPD round-trip per 380 lines keeps a huge list from stalling a tick.
    const size_t kChunk = 380;
    std::vector<std::string> block;
    for (size_t i = 0; i < cmds.size(); i++) {
        block.push_back(cmds[i]);
        if (block.size() >= kChunk || i + 1 == cmds.size()) {
            enqueue_list(block);
            block.clear();
        }
    }
    queue_loaded_ = false;
    queue_dirty_ = true;
    status_.state = "play";
    status_.elapsed = 0;
    status_.song = 0;
    status_.playlistlength = (int)count;
    state_dirty_ = true;
}

// ---------------------------------------------------------------- resume

void MpdClient::state_tick(unsigned now) {
    if (demo_) return;
    if (!state_loaded_) {
        state_loaded_ = true;
        std::string path = config_dir() + "/player.state";
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f) {
            char buf[2048];
            if (std::fgets(buf, sizeof(buf), f)) {
                std::string uri = buf;
                while (!uri.empty() && (uri.back() == '\n' || uri.back() == '\r'))
                    uri.pop_back();
                if (std::fgets(buf, sizeof(buf), f)) state_elapsed_ = std::atof(buf);
                state_uri_ = uri;
            }
            std::fclose(f);
        }
    }

    if (!resume_done_) {
        if (connected_ && status_at_ != 0 && !song_.file.empty()) {
            bool match = !state_uri_.empty() && song_.file == state_uri_;
            bool fresh = status_.state == "play" &&
                         std::fabs(status_.elapsed - state_elapsed_) < 15.0;
            resume_done_ = true;
            if (match && !fresh && state_elapsed_ >= 5.0 &&
                status_.state != "play") {
                char cmd[64];
                std::snprintf(cmd, sizeof(cmd), "seekcur %.0f", state_elapsed_);
                enqueue(cmd);
                status_.elapsed = state_elapsed_;
            }
        }
    }

    if (!song_.file.empty() && song_.file != last_uri_) {
        last_uri_ = song_.file;
        state_dirty_ = true;
    }
    if (status_.state == "play" && (unsigned)(now - state_saved_) >= 15000)
        state_dirty_ = true;
    if (state_dirty_ && connected_) {
        state_dirty_ = false;
        state_saved_ = now;
        state_save();
    }
}

void MpdClient::state_save() {
    if (demo_ || song_.file.empty()) return;
    mkdirs(config_dir());
    std::string path = config_dir() + "/player.state";
    std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    double el = elapsed(now_);
    bool ok = std::fprintf(f, "%s\n%.1f\n", song_.file.c_str(), el) > 0 &&
              std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        ::unlink(tmp.c_str());
        return;
    }
    ::rename(tmp.c_str(), path.c_str());
}

// ---------------------------------------------------------------- commands

void MpdClient::enqueue(const std::string &line, Kind kind) {
    pending_.push_back(Pending{line, kind});
    need_status_ = true;
}

void MpdClient::enqueue_list(const std::vector<std::string> &cmds) {
    if (cmds.empty()) return;
    if (demo_) return;
    std::string block = "command_list_ok_begin\n";
    for (const std::string &c : cmds) {
        block += c;
        block += '\n';
    }
    block += "command_list_end";
    pending_.push_back(Pending{block, KIND_SIMPLE});
    need_status_ = true;
}

void MpdClient::play(int pos) {
    if (demo_) {
        if (pos >= 0 && pos < (int)queue_.size()) {
            status_.song = pos;
            status_.songid = queue_[pos].id;
            song_ = queue_[pos];
            status_.duration = song_.time;
        }
        status_.state = "play";
        status_.elapsed = 0;
        demo_t0_ = now_;
        state_dirty_ = true;
        return;
    }
    if (pos >= 0) {
        char cmd[32];
        std::snprintf(cmd, sizeof(cmd), "play %d", pos);
        enqueue(cmd);
        status_.song = pos;
    } else {
        enqueue("play");
    }
    status_.state = "play";
    state_dirty_ = true;
}

void MpdClient::pause(bool on) {
    double cur = elapsed(now_);
    status_.elapsed = cur;
    status_.state = on ? "pause" : "play";
    if (!on) {
        if (demo_) demo_t0_ = now_;
        else status_at_ = now_;
    }
    if (on) state_dirty_ = true;
    if (!demo_) enqueue(on ? "pause 1" : "pause 0");
}

// MPD has no "toggle" command: it is either `pause 1` / `pause 0` or a bare
// `pause`, which flips the server-side state. Always send the explicit form so
// the optimistic local state below is the one MPD actually applies; otherwise
// the next status poll reverts the UI while playback keeps going.
void MpdClient::toggle() {
    if (status_.state == "play") {
        pause(true);
        return;
    }
    if (status_.state == "pause") {
        pause(false);
        return;
    }

    // "stop" (or no status yet): `pause 0` is a no-op there in MPD 0.24, so
    // start the current song instead of pretending to unpause it.
    status_.elapsed = elapsed(now_);
    status_.state = "play";
    if (demo_) demo_t0_ = now_;
    else status_at_ = now_;
    state_dirty_ = true;
    if (!demo_) enqueue("play");
}

void MpdClient::next() {
    if (demo_) {
        demo_step(1);
        state_dirty_ = true;
        return;
    }
    enqueue("next");
}

void MpdClient::previous() {
    if (demo_) {
        demo_step(-1);
        state_dirty_ = true;
        return;
    }
    enqueue("previous");
}

void MpdClient::seek(double seconds) {
    if (seconds < 0) seconds = 0;
    if (status_.duration > 0 && seconds > status_.duration)
        seconds = status_.duration;
    status_.elapsed = seconds;
    state_dirty_ = true;
    if (demo_) {
        demo_t0_ = now_;
    } else {
        status_at_ = now_;
        char cmd[48];
        std::snprintf(cmd, sizeof(cmd), "seekcur %.1f", seconds);
        enqueue(cmd);
    }
}

void MpdClient::setvol(int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    status_.volume = value;
    if (demo_) return;
    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "setvol %d", value);
    enqueue(cmd);
}

void MpdClient::random(bool on) {
    status_.random = on;
    if (!demo_) enqueue(on ? "random 1" : "random 0");
}

void MpdClient::repeat(bool on) {
    status_.repeat = on;
    if (!demo_) enqueue(on ? "repeat 1" : "repeat 0");
}

void MpdClient::single(bool on) {
    status_.single = on;
    if (!demo_) enqueue(on ? "single 1" : "single 0");
}

void MpdClient::clear() {
    queue_.clear();
    queue_loaded_ = true;
    queue_dirty_ = false;
    status_.playlistlength = 0;
    status_.song = -1;
    status_.songid = -1;
    song_ = MpdSong();
    status_.state = "stop";
    status_.elapsed = 0;
    status_.duration = 0;
    if (demo_) return;
    enqueue("clear");
}

void MpdClient::load(const std::string &name) {
    if (demo_) return;
    enqueue("load " + quote_arg(name));
    queue_dirty_ = true;
}

void MpdClient::save(const std::string &name) {
    if (demo_) return;
    enqueue("save " + quote_arg(name));
}

void MpdClient::list_playlist(const std::string &name) {
    if (demo_) {
        pl_songs_ = queue_;
        return;
    }
    enqueue("listplaylistinfo " + quote_arg(name), KIND_PLAYLIST);
}

// ---------------------------------------------------------------- demo

void MpdClient::demo_step(int delta) {
    if (queue_.empty()) return;
    int n = (int)queue_.size();
    int p = status_.song;
    if (p < 0) p = 0;
    p = (p + delta + n) % n;
    status_.song = p;
    status_.songid = queue_[p].id;
    song_ = queue_[p];
    status_.duration = song_.time;
    status_.elapsed = 0;
    status_.state = "play";
    demo_t0_ = now_;
}

void MpdClient::demo_fill() {
    struct DemoRow {
        const char *album;
        const char *year;
        const char *title;
        int sec;
    };
    static const DemoRow rows[] = {
        {"Группа крови", "1988", "Группа крови", 285},
        {"Группа крови", "1988", "Закрой за мной дверь", 218},
        {"Группа крови", "1988", "Война", 262},
        {"Группа крови", "1988", "Спокойная ночь", 367},
        {"Группа крови", "1988", "Мама, мы все тяжело больны", 271},
        {"Звезда по имени Солнце", "1989", "Звезда по имени Солнце", 228},
        {"Звезда по имени Солнце", "1989", "Печаль", 302},
        {"Звезда по имени Солнце", "1989", "Перемен", 296},
        {"Звезда по имени Солнце", "1989", "Легенда", 336},
        {"Чёрный альбом", "1990", "Красно-жёлтые дни", 301},
        {"Чёрный альбом", "1990", "Кукушка", 373},
        {"Чёрный альбом", "1990", "Когда твоя девушка больна", 264},
    };

    demo_ = true;
    connected_ = true;
    queue_loaded_ = true;
    queue_dirty_ = false;

    queue_.clear();
    albums_.clear();
    album_index_.clear();
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    std::map<std::string, size_t> idx;
    for (int i = 0; i < n; i++) {
        MpdSong s;
        char num[16];
        std::snprintf(num, sizeof(num), "%02d", i + 1);
        s.file = std::string("Кино/") + rows[i].album + "/" + num + " " +
                 rows[i].title + ".mp3";
        s.title = rows[i].title;
        s.artist = "Кино";
        s.album = rows[i].album;
        s.genre = "Рок";
        s.date = rows[i].year;
        s.format = "44100:16:2";
        s.bitrate = 320;
        s.id = i + 1;
        s.pos = i;
        s.time = rows[i].sec;
        queue_.push_back(s);

        std::string key = std::string(rows[i].album) + "\x1fКино";
        auto it = idx.find(key);
        size_t ai;
        if (it == idx.end()) {
            ai = albums_.size();
            LibraryAlbum a;
            a.album = rows[i].album;
            a.artist = "Кино";
            a.year = rows[i].year;
            a.uri = s.file;
            albums_.push_back(a);
            idx[key] = ai;
        } else {
            ai = it->second;
        }
        albums_[ai].tracks.push_back(s.file);
        albums_[ai].duration += s.time;
    }

    status_.state = "play";
    status_.elapsed = 72;
    status_.duration = queue_.empty() ? 0 : queue_[0].time;
    status_.volume = 60;
    status_.random = true;
    status_.repeat = true;
    status_.single = false;
    status_.song = 0;
    status_.songid = queue_.empty() ? -1 : queue_[0].id;
    status_.playlistlength = n;
    status_.playlist = 1;
    status_.bitrate = 320;
    status_.audio = "44100:16:2";
    status_.error.clear();

    song_ = queue_.empty() ? MpdSong() : queue_[0];
    last_queue_version_ = 1;
    demo_t0_ = mono_ms();

    lib_songs_ = n;
    lib_cache_ver_ = 1;
    lib_state_ = LIB_READY;

    // Preview-only stand-in covers so the art area is exercised without MPD.
    for (const LibraryAlbum &a : albums_) {
        std::string key = cover_key(a.artist, a.album, a.uri);
        CoverImage big, small;
        demo_cover(key, big, small);
        cover_small_[key] = small;
        cover_big_[key] = big;
        cover_small_lru_.push_back(key);
        cover_big_lru_.push_back(key);
    }
}
