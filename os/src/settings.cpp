#include "graphite.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <cerrno>
#include <string>
#include <vector>

#include <limits.h>

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

static bool valid_utf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { if (c == 0) return false; ++i; continue; }
        size_t n; unsigned int cp;
        if ((c & 0xe0) == 0xc0) { n=2; cp=c&0x1f; if (cp < 2) return false; }
        else if ((c & 0xf0) == 0xe0) { n=3; cp=c&0x0f; }
        else if ((c & 0xf8) == 0xf0) { n=4; cp=c&7; }
        else return false;
        if (i+n > s.size()) return false;
        for (size_t j=1; j<n; ++j) { unsigned char d=(unsigned char)s[i+j]; if ((d&0xc0)!=0x80) return false; cp=(cp<<6)|(d&0x3f); }
        if ((n==3 && cp<0x800) || (n==4 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
        i += n;
    }
    return true;
}

static bool valid_pair(const char *key, const char *value) {
    if (!key || !value) return false;
    std::string k(key), v(value);
    if (k.empty() || k.size()>128 || v.size()>4096 || !valid_utf8(k) || !valid_utf8(v)) return false;
    for (unsigned char c : k) if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
    return v.find('\n')==std::string::npos && v.find('\r')==std::string::npos;
}

static bool write_all(int fd, const char *data, size_t len) {
    size_t off=0;
    while (off<len) { ssize_t n=::write(fd,data+off,len-off); if (n<0 && errno==EINTR) continue; if (n<=0) return false; off+=(size_t)n; }
    return true;
}

bool Settings::load(const char *path) {
    kv.clear();
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) { if (text.size()+n > 65536) { std::fclose(f); return false; } text.append(buf, n); }
    bool read_ok = !std::ferror(f); std::fclose(f);
    if (!read_ok || !valid_utf8(text)) return false;
    parse_into(text, kv);
    return true;
}

bool settings_save_key(const char *key, const char *value, const char *path) {
    if (!path || !valid_pair(key,value)) return false;
    std::string target(path), lockpath=target+".lock";
    int fd = ::open(lockpath.c_str(), O_RDWR|O_CREAT, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX) != 0) { ::close(fd); return false; }

    std::string text;
    char buf[4096];
    int oldfd=::open(path,O_RDONLY|O_CLOEXEC); struct stat st; bool exists=oldfd>=0;
    if (exists && ::fstat(oldfd,&st)!=0) { ::close(oldfd); goto done; }
    if (!exists && errno!=ENOENT) goto done;
    if (exists) {
        for (;;) { ssize_t n=::read(oldfd,buf,sizeof(buf)); if (n<0 && errno==EINTR) continue; if (n<0 || text.size()+(size_t)(n>0?n:0)>65536) { ::close(oldfd); goto done; } if (!n) break; text.append(buf,(size_t)n); }
        ::close(oldfd);
        if (!valid_utf8(text)) goto done;
    }
    {
        std::string out; bool found=false; size_t pos=0;
        while (pos<text.size()) {
            size_t end=text.find('\n',pos); bool nl=end!=std::string::npos; if (!nl) end=text.size();
            std::string line=text.substr(pos,end-pos); std::string cmp=line; if (!cmp.empty()&&cmp.back()=='\r') cmp.pop_back();
            size_t eq=cmp.find('=');
            if (eq!=std::string::npos && cmp.substr(0,eq)==key) { if (!found) { out+=key; out+='='; out+=value; if(nl) out+='\n'; found=true; } }
            else { out+=line; if(nl) out+='\n'; }
            pos=nl?end+1:end;
        }
        if (!found) { if (!out.empty()&&out.back()!='\n') out+='\n'; out+=key; out+='='; out+=value; out+='\n'; }
        if (out.size()>65536) goto done;
        std::string dir="."; size_t slash=target.find_last_of('/'); if(slash!=std::string::npos) dir=slash==0?"/":target.substr(0,slash);
        std::string templ=dir+"/.lgsettings.XXXXXX"; std::vector<char> name(templ.begin(),templ.end()); name.push_back(0);
        int tfd=::mkstemp(name.data()); if(tfd<0) goto done;
        bool ok=true;
        if (exists && ::fchmod(tfd,st.st_mode&07777)!=0) ok=false;
        if (!exists && ::fchmod(tfd,0644)!=0) ok=false;
        if (ok && !write_all(tfd,out.data(),out.size())) ok=false;
        if (ok && ::fsync(tfd)!=0) ok=false;
        if (::close(tfd)!=0) ok=false;
        if (ok && ::rename(name.data(),path)!=0) ok=false;
        if (!ok) ::unlink(name.data());
        if (ok) { int dfd=::open(dir.c_str(),O_RDONLY|O_DIRECTORY); if(dfd>=0) { (void)::fsync(dfd); ::close(dfd); } }
        if (ok) cfg.kv[key]=value;
        ::flock(fd,LOCK_UN); ::close(fd); return ok;
    }
done:
    ::flock(fd,LOCK_UN); ::close(fd); return false;
}
