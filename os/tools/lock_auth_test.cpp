#include "lock_auth.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main(){
 char dir[]="/tmp/graphite-pin-test-XXXXXX";assert(mkdtemp(dir));
 std::string path=std::string(dir)+"/pin";
 assert(!lock_auth::enabled(path.c_str()));
 assert(lock_auth::verify("1234",path.c_str())==lock_auth::Result::NotEnabled);
 assert(lock_auth::set("12x4",path.c_str())==lock_auth::Result::InvalidPin);
 assert(lock_auth::set("1234",path.c_str())==lock_auth::Result::Ok);
 assert(lock_auth::enabled(path.c_str()));
 struct stat st{};assert(stat(path.c_str(),&st)==0&&(st.st_mode&0777)==0600);
 assert(lock_auth::verify("1234",path.c_str())==lock_auth::Result::Ok);
 assert(lock_auth::verify("4321",path.c_str())==lock_auth::Result::WrongPin);
 assert(lock_auth::set("98765432",path.c_str())==lock_auth::Result::Ok);
 assert(lock_auth::verify("98765432",path.c_str())==lock_auth::Result::Ok);
 assert(lock_auth::clear("0000",path.c_str())==lock_auth::Result::WrongPin);
 assert(lock_auth::clear("98765432",path.c_str())==lock_auth::Result::Ok);
 assert(!lock_auth::enabled(path.c_str()));
 assert(lock_auth::set("1234",dir)==lock_auth::Result::IoError); // directory cannot be replaced
 std::string bad=std::string(dir)+"/bad";FILE*f=fopen(bad.c_str(),"w");assert(f);fputs("bad\n",f);fclose(f);chmod(bad.c_str(),0600);
 assert(!lock_auth::enabled(bad.c_str()));
 lock_auth::AttemptLimiter limiter;
 for(unsigned i=0;i<4;i++)assert(!limiter.reject(1000));
 assert(!limiter.blocked(1000));
 assert(!limiter.reject(1000));assert(limiter.blocked(1000));
 assert(limiter.blocked(30999));assert(!limiter.blocked(31000));
 unlink(bad.c_str());rmdir(dir);return 0;
}
