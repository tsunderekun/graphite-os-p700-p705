// terminal — real whitelisted console with an on-screen ЙЦУКЕН/EN keyboard.
// Commands never go through a shell: a fixed program name plus argv are run
// with execvp under a 300 ms budget. draw() only renders; commands run from
// Enter/tap/key events.
// Scrollback keeps the last ~500 lines; with the keyboard closed drag, ←↑↓→
// /PgUp/PgDn scroll, any new output jumps back to the prompt.
#include "graphite.h"
#include "screens.h"
#include "apphost.h"
#include <X11/keysym.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

const char *kPrompt = "user@l7:~$";
const int kPromptChars = 10;   // "user@l7:~$"
const int kAdvance = 7;        // DejaVu Sans Mono 12 px advance
const int kCols = 65;
const int kMaxBuf = 52;
const int kTop = 96;
const int kLineH = 18;
const int kRowsClosed = 36;
const int kRowsOpen = 23;
const int kMaxLines = 500;     // scrollback ceiling
const int kKeepLines = 400;    // trim back to this when exceeded
const int kKbY = 512;
const int kKbH = 288;
const int kFooterY = 744;

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

size_t cp_count(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n;
}

void pop_cp(std::string &s) {
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

std::string trunc_cp(const std::string &s, size_t n) {
    size_t count = 0, i = 0;
    while (i < s.size() && count < n) {
        unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        i += (size_t)len;
        if (i > s.size()) i = s.size();
        count++;
    }
    return s.substr(0, i);
}

std::vector<std::string> cps(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (i + (size_t)len > s.size()) len = 1;
        out.push_back(s.substr(i, (size_t)len));
        i += (size_t)len;
    }
    return out;
}

std::string upper_cp(const std::string &c) {
    if (c.size() == 1) {
        char ch = c[0];
        if (ch >= 'a' && ch <= 'z') return std::string(1, (char)(ch - 32));
        return c;
    }
    unsigned char b0 = (unsigned char)c[0];
    uint32_t cp = 0;
    int n = 0;
    if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; n = 1; }
    else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; n = 2; }
    else return c;
    for (int i = 1; i <= n && i < (int)c.size(); i++)
        cp = (cp << 6) | ((unsigned char)c[(size_t)i] & 0x3F);
    if (cp == 0x451) cp = 0x401;
    else if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
    else return c;
    std::string out;
    if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string ksym_char(int ks) {
    if (ks >= 0x20 && ks < 0x7f) return std::string(1, (char)ks);
    static const char *cyr[32] = {
        "а", "б", "в", "г", "д", "е", "ж", "з", "и", "й", "к", "л", "м", "н",
        "о", "п", "р", "с", "т", "у", "ф", "х", "ц", "ч", "ш", "щ", "ъ", "ы",
        "ь", "э", "ю", "я"};
    if (ks >= 0x6c0 && ks <= 0x6df) return cyr[ks - 0x6c0];
    if (ks >= 0x6e0 && ks <= 0x6ff) return upper_cp(cyr[ks - 0x6e0]);
    if (ks == XK_Cyrillic_io) return "ё";
    if (ks == XK_Cyrillic_IO) return "Ё";
    return std::string();
}

unsigned long long mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull +
           (unsigned long long)ts.tv_nsec / 1000000ull;
}

// A 65-column screen: collapse runs of blanks and wrap on words, so real
// `free`/`df`/`ip` output stays readable instead of breaking mid-word.
std::string squeeze(const std::string &s) {
    std::string o;
    bool sp = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') { sp = true; continue; }
        if (sp && !o.empty()) o += ' ';
        sp = false;
        o += c;
    }
    return o;
}

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> split_tokens(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Everything a shell could interpret is refused outright — there is no shell.
bool safe_line(const std::string &s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return false;
        switch (c) {
        case '|': case '>': case '<': case ';': case '$': case '`':
        case '&': case '(': case ')': case '"': case '\'': case '\\':
            return false;
        default:
            break;
        }
    }
    return true;
}

bool known_prog(const std::string &p) {
    return p == "uname" || p == "free" || p == "df" || p == "ip" || p == "mpc";
}

std::vector<std::string> split_lines(const std::string &raw) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= raw.size()) {
        size_t j = raw.find('\n', i);
        std::string ln = raw.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        out.push_back(ln);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

struct RunOut {
    std::vector<std::string> lines;
    bool timed_out = false;
};

struct Job {
    int fd = -1;
    pid_t pid = -1;
    std::string raw;
    bool timeout = false;
};

// Run several argv commands at once, no shell, total wall clock <= ~300 ms.
std::vector<RunOut> run_many(const std::vector<std::vector<std::string>> &cmds) {
    std::vector<RunOut> outs(cmds.size());
    std::vector<Job> jobs(cmds.size());
    unsigned long long start = mono_ms();

    for (size_t i = 0; i < cmds.size(); i++) {
        if (cmds[i].empty()) continue;
        int fd[2];
        if (pipe(fd) != 0) continue;
        fcntl(fd[0], F_SETFD, FD_CLOEXEC);
        fcntl(fd[1], F_SETFD, FD_CLOEXEC);
        pid_t pid = fork();
        if (pid < 0) {
            close(fd[0]);
            close(fd[1]);
            continue;
        }
        if (pid == 0) {
            dup2(fd[1], STDOUT_FILENO);
            dup2(fd[1], STDERR_FILENO);
            std::vector<char *> av;
            for (const std::string &t : cmds[i]) av.push_back(const_cast<char *>(t.c_str()));
            av.push_back(nullptr);
            execvp(av[0], av.data());
            std::string msg = "command not found: " + cmds[i][0] + "\n";
            ssize_t ignored = write(STDERR_FILENO, msg.data(), msg.size());
            (void)ignored;
            _exit(127);
        }
        close(fd[1]);
        jobs[i].fd = fd[0];
        jobs[i].pid = pid;
    }

    for (;;) {
        int left = 300 - (int)(mono_ms() - start);
        if (left <= 0) break;
        fd_set rs;
        FD_ZERO(&rs);
        int maxfd = -1;
        int open = 0;
        for (const Job &j : jobs) {
            if (j.fd < 0) continue;
            FD_SET(j.fd, &rs);
            if (j.fd > maxfd) maxfd = j.fd;
            open++;
        }
        if (!open) break;
        struct timeval tv;
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        int r = select(maxfd + 1, &rs, nullptr, nullptr, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        for (Job &j : jobs) {
            if (j.fd < 0 || !FD_ISSET(j.fd, &rs)) continue;
            char tmp[2048];
            ssize_t n = read(j.fd, tmp, sizeof tmp);
            if (n > 0) {
                if (j.raw.size() < 16384) j.raw.append(tmp, (size_t)n);
            } else if (n == 0) {
                close(j.fd);
                j.fd = -1;
            } else if (errno != EINTR && errno != EAGAIN) {
                close(j.fd);
                j.fd = -1;
            }
        }
    }

    for (Job &j : jobs) {
        if (j.fd >= 0) {
            close(j.fd);
            j.fd = -1;
            j.timeout = true;
        }
    }

    for (size_t i = 0; i < jobs.size(); i++) {
        Job &j = jobs[i];
        if (j.pid <= 0) continue;
        int st = 0;
        pid_t w = 0;
        for (;;) {
            w = waitpid(j.pid, &st, WNOHANG);
            if (w != 0) break;
            if (mono_ms() - start > 290) break;
            usleep(1000);
        }
        if (w == 0) {
            kill(j.pid, SIGKILL);
            waitpid(j.pid, &st, 0);
        }
        outs[i].timed_out = j.timeout;
        outs[i].lines = split_lines(j.raw);
    }
    return outs;
}

enum { K_CHAR, K_SHIFT, K_LAYER, K_LANG, K_SPACE, K_DEL, K_ENTER };

struct Key {
    int x = 0, y = 0, w = 0, h = 0;
    std::string label;
    int act = K_CHAR;
    std::string text;
};

struct Terminal : ScreenView {
    std::vector<std::string> lines;
    std::vector<std::string> hist;
    std::string buf;
    int hidx = -1;

    int lang = 0;    // 0 — ЙЦУКЕН, 1 — EN
    int layer = 0;   // 0 — буквы, 1 — цифры
    bool shift = false;
    bool kb_open = false;
    int scroll = 0;    // lines back from the prompt; 0 = follow output
    int drag_acc = 0;
    std::vector<Key> keys;

    Terminal() {
        const char *env = std::getenv("GS_TERM_KB");
        kb_open = env && env[0] && env[0] != '0';
        build();
        boot();
    }

    const char *id() const override { return "terminal"; }

    // ------------------------------------------------------------ storage
    void out(const std::string &text) {
        std::string s = squeeze(text);
        if (s.empty()) {
            lines.push_back("");
        } else {
            std::string cur;
            size_t i = 0;
            while (i < s.size()) {
                size_t j = s.find(' ', i);
                std::string w = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
                size_t need = cp_count(cur) + (cur.empty() ? 0 : 1) + cp_count(w);
                if (need <= (size_t)kCols) {
                    if (!cur.empty()) cur += ' ';
                    cur += w;
                } else {
                    if (!cur.empty()) {
                        lines.push_back(cur);
                        cur.clear();
                    }
                    while (cp_count(w) > (size_t)kCols) {
                        std::string head = trunc_cp(w, (size_t)kCols);
                        lines.push_back(head);
                        w = w.substr(head.size());
                    }
                    cur = w;
                }
                if (j == std::string::npos) break;
                i = j + 1;
            }
            if (!cur.empty()) lines.push_back(cur);
        }
        if (lines.size() > (size_t)kMaxLines) {
            size_t cut = lines.size() - (size_t)kKeepLines;
            lines.erase(lines.begin(), lines.begin() + (long)cut);
            scroll -= (int)cut;
            if (scroll < 0) scroll = 0;
        }
        scroll = 0;   // new output always lands on the prompt
        drag_acc = 0;
    }

    void say(const std::string &text) {
        out(std::string(kPrompt) + " " + text);
    }

    // Real output at screen creation: uname/free/df/ip/mpc run concurrently,
    // so the whole batch stays inside the same 300 ms budget.
    void boot() {
        const char *cmds[] = {"uname -a", "free -m", "df -h /", "ip -4 addr", "mpc status"};
        std::vector<std::vector<std::string>> jobs;
        for (const char *c : cmds) jobs.push_back(split_tokens(c));
        std::vector<RunOut> res = run_many(jobs);
        for (size_t i = 0; i < jobs.size(); i++) {
            say(cmds[i]);
            for (const std::string &ln : res[i].lines) out(ln);
            if (res[i].timed_out) out("(нет ответа)");
        }
    }

    void help() {
        out("help — список команд");
        out("clear — очистить экран");
        out("uname [-a] — ядро и архитектура");
        out("free [-m] — оперативная память");
        out("df [-h] [/] — файловые системы");
        out("ip [-4] addr — сетевые адреса");
        out("mpc status — состояние MPD");
        out("XTERM в шапке — настоящий терминал от user");
        out("Up / Down — история команд");
        out("Прокрутка: тяни, PgUp/PgDn, ↑↓, End");
        out("shell, | > ; $ ` — отключены");
    }

    void submit() {
        std::string cmd = trim(buf);
        if (cmd.empty()) {
            lines.push_back(kPrompt);
        } else {
            say(cmd);
        }
        if (!cmd.empty() && (hist.empty() || hist.back() != cmd)) {
            hist.push_back(cmd);
            if (hist.size() > 50) hist.erase(hist.begin());
        }
        buf.clear();
        hidx = -1;
        scroll = 0;
        drag_acc = 0;
        if (cmd.empty()) return;

        if (!safe_line(cmd)) {
            out("command not found: " + cmd);
            return;
        }
        std::vector<std::string> argv = split_tokens(cmd);
        if (argv.empty()) return;
        const std::string prog = argv[0];
        if (prog == "help") { help(); return; }
        if (prog == "clear") { lines.clear(); return; }
        if (!known_prog(prog)) {
            out("command not found: " + prog);
            return;
        }
        std::vector<std::vector<std::string>> jobs;
        jobs.push_back(argv);
        std::vector<RunOut> res = run_many(jobs);
        for (const std::string &ln : res[0].lines) out(ln);
        if (res[0].timed_out) out("(нет ответа)");
    }

    // ------------------------------------------------------------ keyboard
    void build() {
        keys.clear();
        const int pad = 6, gap = 3, rowh = 66;
        const int inner = 480 - pad * 2;

        auto lay = [&](const std::vector<std::string> &ch, int x0, int width, int y) {
            int n = (int)ch.size();
            if (!n) return;
            int base = (width - gap * (n - 1)) / n;
            int extra = width - (base * n + gap * (n - 1));
            int x = x0;
            for (int i = 0; i < n; i++) {
                Key k;
                k.x = x;
                k.y = y;
                k.w = base + (i < extra ? 1 : 0);
                k.h = rowh;
                k.act = K_CHAR;
                k.text = shift ? upper_cp(ch[(size_t)i]) : ch[(size_t)i];
                k.label = k.text;
                keys.push_back(k);
                x += k.w + gap;
            }
        };

        std::vector<std::string> r0, r1, r2;
        if (layer == 0) {
            if (lang == 0) {
                r0 = cps("йцукенгшщзх");
                r1 = cps("фывапролджэ");
                r2 = cps("ячсмитьбюъ");
            } else {
                r0 = cps("qwertyuiop");
                r1 = cps("asdfghjkl;");
                r2 = cps("zxcvbnm,./");
            }
        } else {
            r0 = cps("1234567890");
            r1 = cps("-/.:@_+=,%");
            r2 = cps("?!~#*[]{}^");
        }

        int y0 = kKbY + pad;
        lay(r0, pad, inner, y0);
        lay(r1, pad, inner, y0 + rowh + gap);

        int y2 = y0 + 2 * (rowh + gap);
        if (layer == 0) {
            int sw = 56;
            Key sh;
            sh.x = pad;
            sh.y = y2;
            sh.w = sw;
            sh.h = rowh;
            sh.act = K_SHIFT;
            sh.label = "SHIFT";
            keys.push_back(sh);
            lay(r2, pad + sw + gap, inner - sw - gap, y2);
        } else {
            lay(r2, pad, inner, y2);
        }

        int y3 = y0 + 3 * (rowh + gap);
        const double units[5] = {1.2, 1.0, 4.0, 1.0, 2.2};
        int avail = inner - gap * 4;
        double total = 0;
        for (double u : units) total += u;
        int x = pad;
        int acts[5] = {K_LAYER, K_LANG, K_SPACE, K_DEL, K_ENTER};
        const char *labels[5] = {layer ? "АБВ" : "123", lang ? "РУС" : "ENG",
                                 "ПРОБЕЛ", "DEL", "ВВОД"};
        for (int i = 0; i < 5; i++) {
            Key k;
            k.x = x;
            k.y = y3;
            k.w = (i == 4) ? (pad + inner - x) : (int)(avail * units[i] / total);
            k.h = rowh;
            k.act = acts[i];
            k.label = labels[i];
            keys.push_back(k);
            x += k.w + gap;
        }
    }

    void press(const Key &k) {
        switch (k.act) {
        case K_CHAR:
            if (cp_count(buf) < (size_t)kMaxBuf) buf += k.text;
            if (shift) { shift = false; build(); }
            break;
        case K_SHIFT:
            shift = !shift;
            build();
            break;
        case K_LAYER:
            layer = layer ? 0 : 1;
            shift = false;
            build();
            break;
        case K_LANG:
            lang = lang ? 0 : 1;
            shift = false;
            build();
            break;
        case K_SPACE:
            if (cp_count(buf) < (size_t)kMaxBuf) buf += " ";
            if (shift) { shift = false; build(); }
            break;
        case K_DEL:
            pop_cp(buf);
            break;
        case K_ENTER:
            submit();
            break;
        }
    }

    // ------------------------------------------------------------ events
    bool key(int ksym, bool down) override {
        if (!down) return false;
        if (!kb_open) {
            // keyboard hidden: the arrows/page keys belong to the scrollback
            int page = kRowsClosed - 2;
            if (ksym == XK_Page_Up) { scroll_by(page); return true; }
            if (ksym == XK_Page_Down) { scroll_by(-page); return true; }
            // Home stays the system key (back to the shell), so only End is
            // taken here: it jumps straight to the prompt.
            if (ksym == XK_End) { scroll_by(-max_scroll()); return true; }
            if ((ksym == XK_Up || ksym == XK_Down) && max_scroll() > 0) {
                scroll_by(ksym == XK_Up ? 1 : -1);
                return true;
            }
        }
        if (ksym == XK_Return || ksym == XK_KP_Enter) {
            submit();
            return true;
        }
        if (ksym == XK_BackSpace) {
            if (cp_count(buf)) pop_cp(buf);
            return true;
        }
        if (ksym == XK_Up && !hist.empty()) {
            if (hidx < 0) hidx = (int)hist.size() - 1;
            else if (hidx > 0) hidx--;
            buf = hist[(size_t)hidx];
            return true;
        }
        if (ksym == XK_Down && hidx >= 0) {
            hidx++;
            if (hidx >= (int)hist.size()) {
                hidx = -1;
                buf.clear();
            } else {
                buf = hist[(size_t)hidx];
            }
            return true;
        }
        std::string c = ksym_char(ksym);
        if (c.empty()) return false;
        if (cp_count(buf) < (size_t)kMaxBuf) buf += c;
        return true;
    }

    bool drag(int x, int y, int dx, int dy) override {
        (void)x; (void)y; (void)dx;
        if (kb_open) return false;   // inside the keyboard drags do nothing
        drag_acc += dy;
        bool moved = false;
        while (drag_acc >= kLineH) { scroll_by(1); drag_acc -= kLineH; moved = true; }
        while (drag_acc <= -kLineH) { scroll_by(-1); drag_acc += kLineH; moved = true; }
        return moved;
    }

    int rows_visible() const { return kb_open ? kRowsOpen : kRowsClosed; }

    int max_scroll() const {
        int m = (int)lines.size() - rows_visible();
        return m > 0 ? m : 0;
    }

    // Rows of command output in the viewport: the input line takes one row
    // only while we follow the prompt.
    int body_rows() const { return scroll > 0 ? rows_visible() : rows_visible() - 1; }

    void scroll_by(int n) {
        scroll += n;
        int ms = max_scroll();
        if (scroll > ms) scroll = ms;
        if (scroll < 0) scroll = 0;
    }

    size_t first_line() const {
        int body = body_rows();
        size_t total = lines.size();
        if (scroll > 0) {
            int sc = scroll;
            int ms = max_scroll();
            if (sc > ms) sc = ms;
            int skip = body + sc;
            return total > (size_t)skip ? total - (size_t)skip : 0;
        }
        size_t first = total > (size_t)body ? total - (size_t)body : 0;
        // If the viewport would start in the middle of a command block and a
        // prompt is only a few lines below, start at that prompt instead.
        if (first > 0) {
            size_t f = first;
            while (f < lines.size() && f - first <= 6 &&
                   lines[f].compare(0, (size_t)kPromptChars, kPrompt) != 0)
                f++;
            if (f < lines.size() && f - first <= 6) first = f;
        }
        return first;
    }

    int input_row() const { return (int)(lines.size() - first_line()); }

    int input_y() const { return kTop + input_row() * kLineH; }

    bool tap(int x, int y) override {
        if (y < 96 && x > 360) {
            apphost_launch("xterm -title graphite -fa 'DejaVu Sans Mono' -fs 11 "
                           "-bg '#101419' -fg '#efebef' -cr '#3fc8d0' -ls");
            return true;
        }
        if (hit(x, y, 4, 42, 44, 44)) {
            app.open("home");
            return true;
        }
        if (!kb_open) {
            if (y >= kFooterY) {
                if (scroll > 0) { scroll = 0; return true; }
                kb_open = true;
                return true;
            }
            if (scroll > 0) { scroll = 0; return true; }   // tap → back to prompt
            int iy = input_y();
            if (hit(x, y, 0, iy - 12, 480, 44)) {
                kb_open = true;
                return true;
            }
            return false;
        }
        for (const Key &k : keys) {
            if (!hit(x, y, k.x, k.y, k.w, k.h)) continue;
            press(k);
            return true;
        }
        int iy = input_y();
        if (hit(x, y, 0, iy - 12, 480, 44)) {
            kb_open = false;
            return true;
        }
        return false;
    }

    // Long XTERM does not launch: it explains how to get Ctrl/arrows in xterm
    // (the OSK opens its terminal layer by itself when xterm has focus).
    bool longtap(int x, int y) override {
        if (y < 96 && x > 360) {
            app.toast("xterm: в клавиатуре FN — Esc, Ctrl, Alt, стрелки", 2800);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------ draw
    void draw(Gfx &g) override {
        ui_bg(g);
        icon(g, "back", 15, 53, 22, theme.fg);
        g.text(60, 42, "Терминал", 24, theme.fg, TF_LIGHT);
        std::string sub = "TTY1 · 65×";
        sub += std::to_string(kb_open ? kRowsOpen : kRowsClosed);
        if (scroll > 0) {
            sub += " · ↑";
            sub += std::to_string(scroll);
        }
        g.text(60, 72, sub, 11, theme.dim);
        g.text(456, 42, "XTERM", 12, theme.accent, TF_RIGHT);
        g.text(456, 66, "FN · CTRL · СТРЕЛКИ", 11, theme.dim, TF_RIGHT);

        int body = body_rows();
        size_t first = first_line();
        int row = 0;
        for (size_t i = first; i < lines.size() && row < body; i++, row++) {
            int y = kTop + row * kLineH + 1;
            const std::string &ln = lines[i];
            if (ln.compare(0, (size_t)kPromptChars, kPrompt) == 0) {
                g.text(12, y, kPrompt, 12, theme.accent, TF_MONO);
                g.text(12 + kPromptChars * kAdvance, y,
                       ln.substr((size_t)kPromptChars), 12, theme.fg, TF_MONO);
            } else {
                g.text(12, y, ln, 12, theme.fg, TF_MONO);
            }
        }

        if (scroll == 0) {
            int iy = kTop + row * kLineH + 1;
            g.text(12, iy, kPrompt, 12, theme.accent, TF_MONO);
            g.text(12 + kPromptChars * kAdvance, iy, " " + buf, 12, theme.fg, TF_MONO);
            int cx = 12 + (kPromptChars + 1 + (int)cp_count(buf)) * kAdvance;
            g.rect(cx, iy + 13, 7, 2, theme.accent);
        }

        // static scrollbar: thumb at the bottom while following the prompt
        int ms = max_scroll();
        if (ms > 0) {
            int total = (int)lines.size();
            int trackH = body * kLineH;
            int th = trackH * body / total;
            if (th < 12) th = 12;
            int sc = scroll > ms ? ms : (scroll < 0 ? 0 : scroll);
            int num = total - body - sc;
            int den = total - body;
            int ty = kTop + (trackH - th) * num / (den > 0 ? den : 1);
            g.rect(473, kTop, 3, trackH, theme.surf2);
            g.rect(473, ty, 3, th, theme.dim);
        }

        if (kb_open) {
            g.rect(0, kKbY, g.cv.w, kKbH, theme.bar);
            g.hline(0, kKbY, g.cv.w, theme.line2);
            for (const Key &k : keys) {
                bool enter = k.act == K_ENTER;
                bool on = (k.act == K_SHIFT && shift) || (k.act == K_LAYER && layer);
                uint16_t fill = enter ? theme.accent : (on ? theme.sel : theme.surf);
                uint16_t col = enter ? theme.onacc
                                     : (k.act == K_CHAR ? theme.fg
                                                        : (on ? theme.accent : theme.dim));
                int px = enter ? 14 : (k.act == K_CHAR ? 16 : 12);
                int flags = TF_CENTER | (enter ? TF_BOLD : 0);
                g.rect(k.x, k.y, k.w, k.h, fill);
                g.text(k.x + k.w / 2, k.y + (k.h - px) / 2 - 1, k.label, px, col, flags);
            }
        } else {
            g.rect(0, kFooterY, g.cv.w, 800 - kFooterY, theme.bar);
            g.hline(0, kFooterY, g.cv.w, theme.line2);
            icon(g, "keyboard", 24, 760, 22, theme.dim);
            g.text(56, 762, "КЛАВИАТУРА", 13, theme.dim);
            if (scroll > 0) g.text(456, 763, "↓ К ПРОМПТУ", 11, theme.accent, TF_RIGHT);
            else g.text(456, 763, std::to_string(kRowsClosed) + " СТРОК",
                        11, theme.dim, TF_RIGHT);
        }
    }
};

} // namespace

ScreenView *make_terminal() { return new Terminal(); }
