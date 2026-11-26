#include "lock_auth.h"

#include <array>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace lock_auth {
namespace {
// Compact SHA-256 implementation; PIN verifier is SHA256(salt || PIN).
uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
std::array<unsigned char, 32> sha256(const std::string &s) {
 static const uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc2b2ae3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint64_t bits=(uint64_t)s.size()*8; std::string m=s; m.push_back(char(0x80)); while(m.size()%64!=56)m.push_back(0); for(int i=7;i>=0;--i)m.push_back(char(bits>>(i*8)));
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(size_t off=0;off<m.size();off+=64){uint32_t w[64]; for(int i=0;i<16;i++)w[i]=(uint32_t)(unsigned char)m[off+4*i]<<24|(uint32_t)(unsigned char)m[off+4*i+1]<<16|(uint32_t)(unsigned char)m[off+4*i+2]<<8|(unsigned char)m[off+4*i+3]; for(int i=16;i<64;i++){uint32_t a=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),b=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+a+w[i-7]+b;} uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7]; for(int i=0;i<64;i++){uint32_t t1=z+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i],t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;}
 std::array<unsigned char,32> out{};for(int i=0;i<8;i++)for(int j=0;j<4;j++)out[i*4+j]=(unsigned char)(h[i]>>(24-8*j));return out;
}
std::string hex(const unsigned char *p,size_t n){static const char*d="0123456789abcdef";std::string s;for(size_t i=0;i<n;i++){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
bool load(std::string &salt,std::string &hash,const char*path){struct stat st{};if(stat(path,&st)||!S_ISREG(st.st_mode)||(st.st_mode&0777)!=0600)return false;FILE*f=fopen(path,"rb");if(!f)return false;char a[128]{},b[128]{},extra[2]{};bool ok=fgets(a,sizeof a,f)&&fgets(b,sizeof b,f)&&!fgets(extra,sizeof extra,f);fclose(f);salt=a;hash=b;if(!ok)return false;if(!salt.empty()&&salt.back()=='\n')salt.pop_back();if(!hash.empty()&&hash.back()=='\n')hash.pop_back();return salt.size()==32&&hash.size()==64;}
}
bool valid_pin(const std::string&p){if(p.size()<4||p.size()>8)return false;for(char c:p)if(c<'0'||c>'9')return false;return true;}
bool enabled(const char*path){std::string a,b;return load(a,b,path);}
Result set(const std::string&p,const char*path){if(!valid_pin(p))return Result::InvalidPin;int r=open("/dev/urandom",O_RDONLY);if(r<0)return Result::IoError;unsigned char salt[16];size_t n=0;while(n<sizeof salt){ssize_t z=read(r,salt+n,sizeof salt-n);if(z<=0){close(r);return Result::IoError;}n+=z;}close(r);std::string s=hex(salt,sizeof salt),h=hex(sha256(s+p).data(),32),tmp=std::string(path)+".tmp";int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return Result::IoError;std::string data=s+"\n"+h+"\n";bool ok=write(fd,data.data(),data.size())==(ssize_t)data.size()&&fsync(fd)==0&&fchmod(fd,0600)==0;int e=errno;close(fd);if(!ok||rename(tmp.c_str(),path)){unlink(tmp.c_str());errno=e;return Result::IoError;}return Result::Ok;}
Result verify(const std::string&p,const char*path){std::string s,h;if(!load(s,h,path))return Result::NotEnabled;auto got=hex(sha256(s+p).data(),32);unsigned char diff=0;for(size_t i=0;i<64;i++)diff|=(unsigned char)(got[i]^h[i]);return diff?Result::WrongPin:Result::Ok;}
Result clear(const std::string&p,const char*path){Result r=verify(p,path);if(r!=Result::Ok)return r;return unlink(path)==0?Result::Ok:Result::IoError;}
}
