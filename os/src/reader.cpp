#include "reader.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace reader {
namespace {
struct Dec { uint32_t cp; size_t n; bool ok; };
Dec decode(const std::string &s, size_t i) {
    if (i >= s.size()) return {0, 0, false};
    const uint8_t a = (uint8_t)s[i];
    if (a < 0x80) return {a, 1, true};
    size_t n = a >= 0xF0 && a <= 0xF4 ? 4 : a >= 0xE0 ? 3 : a >= 0xC2 ? 2 : 0;
    if (!n || i + n > s.size()) return {0xfffd, 1, false};
    uint32_t cp = a & (n == 2 ? 0x1f : n == 3 ? 0x0f : 7);
    for (size_t j = 1; j < n; ++j) { uint8_t b=(uint8_t)s[i+j]; if ((b&0xc0)!=0x80) return {0xfffd,1,false}; cp=(cp<<6)|(b&63); }
    if ((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return {0xfffd,1,false};
    return {cp,n,true};
}
bool space(uint32_t c) { return c==0x20||c==0x09||c==0xa0||c==0x202f; }
int fallback_width(uint32_t c, int px) { return c==0x20 ? std::max(1,px/3) : (c>=0x300&&c<=0x36f ? 0 : std::max(1,px*3/5)); }
uint64_t hash_bytes(uint64_t h, const void *p, size_t n) { const uint8_t *b=(const uint8_t*)p; while(n--) { h^=*b++; h*=1099511628211ULL; } return h; }
template<class T> void put(std::vector<uint8_t>&v,T x) { const uint8_t*p=(const uint8_t*)&x; v.insert(v.end(),p,p+sizeof x); }
template<class T> bool get(const uint8_t*&p,const uint8_t*e,T&x) { if((size_t)(e-p)<sizeof x)return false; std::memcpy(&x,p,sizeof x);p+=sizeof x;return true; }
}

bool valid_utf8(const std::string&s) { for(size_t i=0;i<s.size();) { Dec d=decode(s,i);if(!d.ok)return false;i+=d.n; } return true; }
bool boundary(const std::string&s,size_t off) { return off<=s.size()&&(off==s.size()||(((uint8_t)s[off]&0xc0)!=0x80)); }
size_t next_boundary(const std::string&s,size_t off) { if(off>=s.size())return s.size(); ++off;while(off<s.size()&&(((uint8_t)s[off]&0xc0)==0x80))++off;return off; }
uint64_t layout_hash(const LayoutCfg&c) { uint64_t h=1469598103934665603ULL; const int a[]={c.width,c.line_height,c.lines_per_page,c.font_px,c.margin_px};h=hash_bytes(h,a,sizeof a);return hash_bytes(h,&c.font_id,sizeof c.font_id); }
bool add_block(Book&b,const std::string&s,uint16_t flags) { if(!valid_utf8(s)||b.arena.size()+s.size()>UINT32_MAX)return false;Block x;x.off=(uint32_t)b.arena.size();x.len=(uint32_t)s.size();x.first_run=(uint32_t)b.runs.size();x.flags=flags;b.arena+=s;b.blocks.push_back(x);return true; }
bool add_empty_block(Book&b,uint16_t flags) { (void)flags;return add_block(b,"",flags); }

namespace {
struct BrokenLine { uint32_t block, off, len; int width; };
// The single source of truth for break positions and measured widths.
bool break_block(const Book&b,uint32_t bi,const LayoutCfg&c,const Advance&adv,
                 const std::function<void(const BrokenLine&)>&emit) {
    const Block&bl=b.blocks[bi];
    if((uint64_t)bl.off+bl.len>b.arena.size()||!boundary(b.arena,bl.off)||!boundary(b.arena,bl.off+bl.len))return false;
    for(size_t pos=0;pos<bl.len;) {
        size_t start=pos,q=pos,last_space=std::string::npos;int width=0;
        while(q<bl.len) {
            Dec d=decode(b.arena,bl.off+q);if(!d.ok)return false;
            int w=adv?adv(d.cp):0;if(w<=0)w=fallback_width(d.cp,c.font_px);
            if(width+w>c.width&&q>start)break;
            width+=w;q+=d.n;if(space(d.cp))last_space=q-d.n;
            if(width>c.width)break;
        }
        if(q<bl.len&&last_space!=std::string::npos&&last_space>start) {
            q=last_space;width=0;
            for(size_t p=start;p<q;) { Dec d=decode(b.arena,bl.off+p);int w=adv?adv(d.cp):0;if(w<=0)w=fallback_width(d.cp,c.font_px);width+=w;p+=d.n; }
        }
        if(q<=pos) { q=next_boundary(b.arena,bl.off+pos)-bl.off;Dec d=decode(b.arena,bl.off+pos);int w=adv?adv(d.cp):0;width=w>0?w:fallback_width(d.cp,c.font_px); }
        emit(BrokenLine{bi,(uint32_t)start,(uint32_t)(q-start),width});pos=q;
    }
    return true;
}
}

bool paginate(const Book&b,const LayoutCfg&c,const Advance&adv,PageMap&out) {
    if(c.width<=0||c.line_height<=0||c.lines_per_page<=0)return false;
    out=PageMap{};out.layout_hash=layout_hash(c);out.pages.push_back(PageStart{});
    uint32_t global_line=0, chars=0;
    for(const auto&bl:b.blocks) {
        if((uint64_t)bl.off+bl.len>b.arena.size()||!boundary(b.arena,bl.off)||!boundary(b.arena,bl.off+bl.len))return false;
        for(size_t p=bl.off;p<(size_t)bl.off+bl.len;) { Dec d=decode(b.arena,p);if(!d.ok)return false;++chars;p+=d.n; }
        uint32_t bi=(uint32_t)(&bl-&b.blocks[0]);
        if(bl.len==0) { ++global_line; if(global_line%c.lines_per_page==0 && bi+1<b.blocks.size()) out.pages.push_back(PageStart{bi+1,0,0,0}); continue; }
        bool ok=break_block(b,bi,c,adv,[&](const BrokenLine&line) {
            if(global_line%c.lines_per_page==0 && (out.pages.empty()||out.pages.back().block!=line.block||out.pages.back().off!=line.off))
                out.pages.push_back(PageStart{line.block,line.off,(uint16_t)(global_line%c.lines_per_page),0});
            ++global_line;
        });
        if(!ok)return false;
    }
    // Empty book has one valid page; don't emit a spurious trailing page.
    out.total_chars=chars;out.complete=true;return true;
}
bool page_lines(const Book&b,const LayoutCfg&c,const Advance&adv,const PageMap&m,size_t page,std::vector<Line>&out) {
    out.clear();
    if(!m.complete||m.layout_hash!=layout_hash(c)||page>=m.pages.size()||c.width<=0||c.line_height<=0||c.lines_per_page<=0)return false;
    const PageStart&first=m.pages[page];
    const PageStart*next=page+1<m.pages.size()?&m.pages[page+1]:nullptr;
    uint32_t line_no=0;bool started=false,done=false,valid=true;
    for(uint32_t bi=first.block;bi<b.blocks.size()&&!done;bi++) {
        const Block&bl=b.blocks[bi];
        if((uint64_t)bl.off+bl.len>b.arena.size()) { valid=false;break; }
        if(bl.len==0) {
            if(bi<first.block||(bi==first.block&&first.off!=0))continue;
            if(next&&bi==next->block&&next->off==0) { done=true;break; }
            started=true;out.push_back(Line{bi,bl.off,0,0,c.margin_px,(int)line_no*c.line_height});++line_no;
            if(line_no>=(uint32_t)c.lines_per_page)break;
            continue;
        }
        if(!break_block(b,bi,c,adv,[&](const BrokenLine&l) {
            if(done||!valid)return;
            if(!started) { if(bi<first.block||(bi==first.block&&l.off<first.off))return;started=true; }
            if(next&&(bi>next->block||(bi==next->block&&l.off>=next->off))) { done=true;return; }
            if(line_no<(uint32_t)c.lines_per_page)out.push_back(Line{bi,bl.off+l.off,l.len,l.width,c.margin_px,(int)line_no*c.line_height});
            ++line_no;if(line_no>=(uint32_t)c.lines_per_page)done=true;
        })) { valid=false; }
        if(!valid)return false;
    }
    return valid&&started;
}
size_t page_for_anchor(const PageMap&m,Anchor a) { if(m.pages.empty())return 0;auto it=std::upper_bound(m.pages.begin(),m.pages.end(),a,[](Anchor x,const PageStart&y){return x.block<y.block||(x.block==y.block&&x.off<y.off);});return it==m.pages.begin()?0:(size_t)(it-m.pages.begin()-1); }
Anchor anchor_for_page(const PageMap&m,size_t p) { if(m.pages.empty())return {};const auto&s=m.pages[std::min(p,m.pages.size()-1)];return {s.block,s.off}; }
std::vector<uint8_t> serialize(const PageMap&m) { std::vector<uint8_t>v;v.insert(v.end(),{'R','P','G','1'});put<uint32_t>(v,1);put(v,m.layout_hash);put(v,m.total_chars);put<uint32_t>(v,(uint32_t)m.pages.size());for(auto&p:m.pages){put(v,p.block);put(v,p.off);put(v,p.line);put(v,p.chapter);}uint32_t crc=2166136261U;for(auto x:v){crc^=x;crc*=16777619U;}put(v,crc);return v; }
bool deserialize(const uint8_t*d,size_t n,PageMap&o) { if(!d||n<24||std::memcmp(d,"RPG1",4))return false;const uint8_t*p=d+4,*e=d+n;uint32_t ver,count,crc;if(!get(p,e,ver)||ver!=1||!get(p,e,o.layout_hash)||!get(p,e,o.total_chars)||!get(p,e,count)||count>50000||n!=4+4+8+4+4+(size_t)count*12+4)return false;o.pages.clear();for(uint32_t i=0;i<count;i++){PageStart s;if(!get(p,e,s.block)||!get(p,e,s.off)||!get(p,e,s.line)||!get(p,e,s.chapter))return false;o.pages.push_back(s);}if(!get(p,e,crc))return false;uint32_t c=2166136261U;for(size_t i=0;i<n-4;i++){c^=d[i];c*=16777619U;}if(c!=crc)return false;o.complete=true;return true; }
}
