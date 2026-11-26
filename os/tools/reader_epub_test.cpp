#include "reader.h"
#include <zip.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool put(zip_t*z,const char*n,const std::string&s){char*p=(char*)std::malloc(s.size());if(!p)return false;std::memcpy(p,s.data(),s.size());zip_source_t*x=zip_source_buffer(z,p,s.size(),1);if(!x){std::free(p);return false;}if(zip_file_add(z,n,x,ZIP_FL_OVERWRITE)<0){zip_source_free(x);return false;}return true;}
static bool make(const char*path,bool v3,bool encryption=false,bool bad_ncx=false,bool traversal=false){int e=0;zip_t*z=zip_open(path,ZIP_CREATE|ZIP_TRUNCATE,&e);if(!z)return false;
 std::string opf=std::string("<package><metadata xmlns:dc='http://purl.org/dc/elements/1.1/'><dc:title>Fixture</dc:title><dc:creator>Writer</dc:creator></metadata><manifest><item id='a' href='a.xhtml' media-type='application/xhtml+xml'/><item id='b' href='b.xhtml' media-type='application/xhtml+xml'/><item id='c' href='c.xhtml' media-type='application/xhtml+xml'/><item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>")+(v3?"<item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>":"")+"</manifest><spine toc='ncx'><itemref idref='a'/><itemref idref='b'/><itemref idref='c'/></spine></package>";
 std::string ncx=bad_ncx?"<ncx><navMap>":"<ncx><navMap><navPoint><navLabel><text>Chapter One</text></navLabel><content src='a.xhtml#one'/><navPoint><navLabel><text>Chapter Two</text></navLabel><content src='b.xhtml#two'/></navPoint></navPoint><navPoint><navLabel><text>Chapter Three</text></navLabel><content src='c.xhtml#three'/></navPoint></navMap></ncx>";
 std::string nav=std::string("<html xmlns:epub='http://www.idpf.org/2007/ops'><body><nav epub:type='toc'><ol><li><a href='a.xhtml#one'>Chapter One</a><ol><li><a href='b.xhtml#two'>Chapter Two</a></li></ol></li><li><a href='c.xhtml#three'>Chapter Three</a></li>")+(traversal?"<li><a href='../evil.xhtml'>Bad target</a></li>":"")+"</ol></nav></body></html>";
 bool ok=put(z,"META-INF/container.xml","<container><rootfiles><rootfile full-path='book.opf'/></rootfiles></container>")&&put(z,"book.opf",opf)&&put(z,"a.xhtml","<html xmlns='http://www.w3.org/1999/xhtml'><body><h1 id='one'>Chapter One</h1><p>First text.</p></body></html>")&&put(z,"b.xhtml","<html xmlns='http://www.w3.org/1999/xhtml'><body><h2 id='two'>Chapter Two</h2><p>Second text.</p></body></html>")&&put(z,"c.xhtml","<html xmlns='http://www.w3.org/1999/xhtml'><body><h3 id='three'>Chapter Three</h3><p>Third text.</p></body></html>")&&put(z,"toc.ncx",ncx)&&put(z,"nav.xhtml",nav);if(encryption)ok=ok&&put(z,"META-INF/encryption.xml","<encryption/>");if(zip_close(z)!=0)ok=false;return ok;}
static bool check(const char*n,bool actual){std::printf("%s: %s\n",n,actual?"PASS":"FAIL");return actual;}
int main(){bool all=true;reader::Book b;std::string err;
 make("epub2-test.epub",false);bool epub2=reader::load_epub("epub2-test.epub",b,err)&&b.title=="Fixture"&&b.author=="Writer"&&b.arena.find("First text.")<b.arena.find("Second text.");unsigned chapters=0;for(auto&x:b.blocks)if(x.flags&1)++chapters;reader::PageMap map;reader::paginate(b,reader::LayoutCfg{},reader::Advance{},map);bool targets_ok=chapters==3;for(size_t i=0;i<b.blocks.size();++i)if(b.blocks[i].flags&1)targets_ok&=reader::page_for_anchor(map,{(uint32_t)i,0})<map.pages.size();all&=check("EPUB2 NCX nested TOC (3 chapters, targets)",epub2&&targets_ok);
 make("epub3-test.epub",true);bool epub3=reader::load_epub("epub3-test.epub",b,err);chapters=0;for(auto&x:b.blocks)if(x.flags&1)++chapters;all&=check("EPUB3 nav nested TOC (3 chapters)",epub3&&chapters==3);
 FILE*f=std::fopen("epub-bad.epub","wb");if(f){std::fputs("bad zip",f);std::fclose(f);}all&=check("Broken ZIP rejected",!reader::load_epub("epub-bad.epub",b,err));
 make("epub-encrypted.epub",false,true);all&=check("Encryption rejected",!reader::load_epub("epub-encrypted.epub",b,err)&&err=="Книга защищена");
 make("epub-bad-ncx.epub",false,false,true);bool fallback=reader::load_epub("epub-bad-ncx.epub",b,err);chapters=0;for(auto&x:b.blocks)if(x.flags&1)++chapters;all&=check("Malformed NCX falls back to headings",fallback&&chapters==3);
 make("epub-nav-traversal.epub",true,false,false,true);bool safe_nav=reader::load_epub("epub-nav-traversal.epub",b,err);chapters=0;for(auto&x:b.blocks)if(x.flags&1)++chapters;all&=check("Nav ../ href ignored",safe_nav&&chapters==3);
 int be=0;zip_t*bz=zip_open("epub-large.epub",ZIP_CREATE|ZIP_TRUNCATE,&be);if(bz){std::string huge(8u*1024u*1024u+10,'x');put(bz,"META-INF/container.xml",huge);zip_close(bz);}all&=check("Oversized XML rejected",!reader::load_epub("epub-large.epub",b,err));
 // OPF traversal must be rejected before attempting to read a package entry.
 int e=0;zip_t*z=zip_open("epub-traversal.epub",ZIP_CREATE|ZIP_TRUNCATE,&e);if(z){put(z,"META-INF/container.xml","<container><rootfiles><rootfile full-path='../book.opf'/></rootfiles></container>");zip_close(z);}all&=check("Traversal rejected",!reader::load_epub("epub-traversal.epub",b,err));
 return all?0:1;
}
