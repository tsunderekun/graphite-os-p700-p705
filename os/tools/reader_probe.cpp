#include "reader.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H

using namespace reader;

struct FontMetrics {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::string path;
    bool open() {
        if (FT_Init_FreeType(&library)) return false;
        const char *paths[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
                               "fonts/medeo/MedeoSans-Regular.ttf"};
        for (const char *candidate : paths) {
            if (!FT_New_Face(library,candidate,0,&face)) { path=candidate; break; }
        }
        return face && FT_Set_Pixel_Sizes(face,0,20)==0;
    }
    ~FontMetrics() { if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library); }
    int advance(uint32_t cp) {
        if(FT_Load_Char(face,cp,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP))return 0;
        return (int)((face->glyph->advance.x+32)>>6);
    }
};

static int synthetic_advance(uint32_t cp) { return cp==' '?7:(cp<0x80?10:12); }
static size_t cp_bytes(const std::string&s,size_t p) {
    unsigned char c=(unsigned char)s[p];return c<0x80?1:(c<0xe0?2:(c<0xf0?3:4));
}
static uint32_t cp_at(const std::string&s,size_t p) {
    const unsigned char*c=(const unsigned char*)s.data()+p;
    if(c[0]<0x80)return c[0];
    size_t n=cp_bytes(s,p);uint32_t cp=c[0]&((1u<<(7-n))-1);
    for(size_t i=1;i<n;i++)cp=(cp<<6)|(c[i]&63);
    return cp;
}
static int max_line_width(const Book&b,const Advance&measure,int limit) {
    int maximum=0;
    for(const Block&block:b.blocks) {
        size_t pos=0;
        while(pos<block.len) {
            size_t start=pos,q=pos,last_space=std::string::npos;int width=0;
            while(q<block.len) {
                uint32_t cp=cp_at(b.arena,block.off+q);size_t n=cp_bytes(b.arena,block.off+q);
                int w=measure(cp);if(w<=0)w=12;
                if(width+w>limit&&q>start)break;
                width+=w;q+=n;if(cp==' '||cp==0xa0||cp==0x202f)last_space=q-n;
                if(width>limit)break;
            }
            if(q<block.len&&last_space!=std::string::npos&&last_space>start)q=last_space;
            if(q<=pos)q+=cp_bytes(b.arena,block.off+q);
            int actual_width=0;
            for(size_t p=start;p<q;) {
                uint32_t cp=cp_at(b.arena,block.off+p);
                int w=measure(cp);actual_width+=w>0?w:12;p+=cp_bytes(b.arena,block.off+p);
            }
            maximum=std::max(maximum,actual_width);pos=q;
        }
    }
    return maximum;
}

static void tests() {
    Book b;
    assert(add_block(b,"Короткий абзац с ёлкой 😀 и URL https://example.invalid/"));
    assert(add_empty_block(b));
    assert(add_block(b,"длинноесловобезпробеловдлинноесловобезпробелов"));
    assert(add_block(b,"неразрывный\xc2\xa0пробел и ‑дефис"));
    assert(!boundary(b.arena,1));
    for(const auto&block:b.blocks) {
        assert(boundary(b.arena,block.off)&&boundary(b.arena,block.off+block.len));
        for(size_t p=block.off;p<block.off+block.len;p=next_boundary(b.arena,p))assert(boundary(b.arena,p));
    }
    LayoutCfg cfg;PageMap a,again;
    assert(paginate(b,cfg,synthetic_advance,a)&&paginate(b,cfg,synthetic_advance,again));
    assert(a.pages.size()==again.pages.size());
    for(size_t i=0;i<a.pages.size();i++)assert(a.pages[i].block==again.pages[i].block&&a.pages[i].off==again.pages[i].off);
    assert(page_for_anchor(a,anchor_for_page(a,0))==0);
    auto bytes=serialize(a);PageMap restored;assert(deserialize(bytes.data(),bytes.size(),restored));
    assert(restored.pages.size()==a.pages.size());bytes[8]^=1;assert(!deserialize(bytes.data(),bytes.size(),restored));
}

int main() {
    tests();FontMetrics font;if(!font.open()){std::cerr<<"FAIL FreeType font load\n";return 2;}
    std::string text;const std::string paragraph="В начале было слово, и слово было у Бога. Это русский текст для проверки стабильной раскладки. ";
    while(text.size()<20*1024)text+=paragraph;
    if(text.size()>20*1024)text.resize(20*1024);
    while(!valid_utf8(text))text.pop_back();
    Book book;size_t start=0;
    while(start<text.size()) {
        size_t end=text.find(". ",start);if(end==std::string::npos)end=text.size();else end+=2;
        if(end<=start)end=text.size();
        assert(add_block(book,text.substr(start,end-start)));
        start=end;
    }
    std::string rebuilt;for(const Block&b:book.blocks)rebuilt.append(book.arena,b.off,b.len);assert(rebuilt==text);
    LayoutCfg cfg;PageMap map;Advance measure=[&font](uint32_t cp){return font.advance(cp);};
    assert(paginate(book,cfg,measure,map));
    auto t0=std::chrono::steady_clock::now();PageMap second;assert(paginate(book,cfg,measure,second));auto t1=std::chrono::steady_clock::now();
    assert(map.pages.size()==second.pages.size());
    for(size_t i=0;i<map.pages.size();i++)assert(map.pages[i].block==second.pages[i].block&&map.pages[i].off==second.pages[i].off);
    int maxw=max_line_width(book,measure,cfg.width);assert(maxw<=cfg.width);
    std::vector<uint32_t> consumed(book.blocks.size(),0);
    auto lines_t0=std::chrono::steady_clock::now();
    for(size_t page=0;page<map.pages.size();page++) {
        std::vector<Line> lines,repeat;
        assert(page_lines(book,cfg,measure,map,page,lines));
        assert(page_lines(book,cfg,measure,map,page,repeat));
        assert(lines.size()==repeat.size());
        for(size_t i=0;i<lines.size();i++) {
            const Line&a=lines[i];const Line&z=repeat[i];
            assert(a.block==z.block&&a.off==z.off&&a.len==z.len&&a.width==z.width&&a.y==z.y);
            assert(a.width<=cfg.width);
            if(a.len) {
                assert(a.block<book.blocks.size());
                const Block&b=book.blocks[a.block];
                assert(a.off==b.off+consumed[a.block]);
                assert(boundary(book.arena,a.off)&&boundary(book.arena,a.off+a.len));
                consumed[a.block]+=a.len;
            }
        }
    }
    auto lines_t1=std::chrono::steady_clock::now();
    for(size_t i=0;i<book.blocks.size();i++)assert(consumed[i]==book.blocks[i].len);
    std::vector<Line> invalid_page;assert(!page_lines(book,cfg,measure,map,map.pages.size(),invalid_page)&&invalid_page.empty());
    double page_lines_ms=std::chrono::duration<double,std::milli>(lines_t1-lines_t0).count()/map.pages.size();
    volatile uint32_t sink=0;auto f0=std::chrono::steady_clock::now();
    for(size_t i=0;i<20;i++)sink+=anchor_for_page(map,i%map.pages.size()).off;
    auto f1=std::chrono::steady_clock::now();(void)sink;
    double total=std::chrono::duration<double,std::milli>(t1-t0).count();
    double flip=std::chrono::duration<double,std::milli>(f1-f0).count()/20.0;
    std::cout<<"PASS font="<<font.path<<" real_ft=yes fixture_bytes="<<text.size()<<" blocks="<<book.blocks.size()
             <<" pages="<<map.pages.size()<<" geometry=480x800 text=432x704 lh=29 rows=24"
             <<" layout_total_ms="<<total<<" layout_ms_per_page="<<total/map.pages.size()
             <<" page_lines=ok page_lines_ms_per_page="<<page_lines_ms
             <<" flip_ms="<<flip<<" max_width="<<maxw<<" lossless=yes deterministic=yes\n";
    return flip<150.0?0:1;
}
