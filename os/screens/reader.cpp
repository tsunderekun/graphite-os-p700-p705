#include "graphite.h"
#include "reader.h"
#include "reader_txt.h"
#include "reader_fb2.h"
#include "screens.h"

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace {
enum Mode { SHELF, READ, TOC, SETTINGS };
struct Item { std::string path, name; unsigned long long size=0; };

bool supported(const char *s) {
    size_t n=std::strlen(s);
    return (n>4 && (!strcasecmp(s+n-4,".txt")||!strcasecmp(s+n-4,".fb2"))) ||
            (n>8 && !strcasecmp(s+n-8,".fb2.zip")) || (n>5 && !strcasecmp(s+n-5,".epub"));
}
std::string basename(const std::string &p) {
    size_t n=p.find_last_of('/'); std::string s=n==p.npos?p:p.substr(n+1);
    if(s.size()>8&&!strcasecmp(s.c_str()+s.size()-8,".fb2.zip"))s.resize(s.size()-8);
    else if(s.size()>4)s.resize(s.size()-4);
    return s;
}
struct Reader : ScreenView {
    Mode mode=SHELF; std::vector<Item> books; reader::Book book; reader::PageMap map;
    reader::LayoutCfg cfg; std::vector<reader::Line> lines; size_t page=0,scroll=0; int size_index=0; bool sans=false;
    std::string error,title,opened; int drag_dx=0; bool moved=false,external=false; unsigned opened_at=0;
    const char *id() const override { return "reader"; }
    Reader() { scan(); const char *p=getenv("GRAPHITE_READER_OPEN"); if(p&&*p)open_book(p); }
    void scan() {
        books.clear(); std::vector<std::string> dirs;
        const char *e=getenv("GRAPHITE_BOOKS"); if(e&&*e)dirs.push_back(e);
        dirs.push_back("/home/user/Books"); dirs.push_back("/media/sd/Books");
        for(const auto &d:dirs) { DIR *h=opendir(d.c_str()); if(!h)continue; struct dirent *ent;
            while((ent=readdir(h))) if(supported(ent->d_name)) { std::string p=d+"/"+ent->d_name; struct stat st; if(stat(p.c_str(),&st)==0&&S_ISREG(st.st_mode)) books.push_back({p,ent->d_name,(unsigned long long)st.st_size}); }
            closedir(h);
        }
        std::sort(books.begin(),books.end(),[](const Item&a,const Item&b){return a.name<b.name;});
    }
    int advance(uint32_t cp) const {
        std::string s; if(cp<128)s+=(char)cp; else if(cp<2048){s+=(char)(0xc0|(cp>>6));s+=(char)(0x80|(cp&63));}else{s+=(char)(0xe0|(cp>>12));s+=(char)(0x80|((cp>>6)&63));s+=(char)(0x80|(cp&63));}
        return app.g ? std::max(1,app.g->textw(s,cfg.font_px)) : cfg.font_px/2;
    }
    bool layout(reader::Anchor anchor) {
        cfg.font_px=size_index==0?20:size_index==1?22:24; cfg.line_height=cfg.font_px==20?29:cfg.font_px==22?32:35;
        cfg.lines_per_page=704/cfg.line_height; cfg.width=432; cfg.margin_px=24; cfg.font_id=sans?2:1;
        if(!reader::paginate(book,cfg,[this](uint32_t c){return advance(c);},map)){error="Не удалось разметить книгу";return false;}
        page=reader::page_for_anchor(map,anchor); cache_page(); return true;
    }
    void cache_page() { lines.clear(); if(!reader::page_lines(book,cfg,[this](uint32_t c){return advance(c);},map,page,lines)) error="Не удалось прочитать страницу"; }
    void open_book(const std::string&p) {
        error.clear(); book=reader::Book{}; map=reader::PageMap{}; lines.clear(); opened.clear();
        std::string why; reader::Book b; bool ok=false;
        size_t n=p.size();
        if(n>8&&!strcasecmp(p.c_str()+n-8,".fb2.zip")) ok=reader::load_fb2_zip(p,b,why);
        else if(n>5&&!strcasecmp(p.c_str()+n-5,".epub")) ok=reader::load_epub(p,b,why);
        else {
            std::ifstream f(p,std::ios::binary); if(!f){error="Не удалось открыть файл";app.toast(error.c_str());mode=SHELF;return;}
            std::string bytes((std::istreambuf_iterator<char>(f)),{});
            if(n>4&&!strcasecmp(p.c_str()+n-4,".fb2"))ok=reader::load_fb2(bytes,b,why);else ok=reader::load_txt(bytes,b,why);
        }
        if(!ok){error=why;app.toast(why.c_str());mode=SHELF;return;}
        book=std::move(b); opened=p;title=book.title.empty()?basename(p):book.title; size_index=0;sans=false;
        reader::Anchor a{}; std::ifstream state("/home/user/.config/graphite/reader.state"); std::string old; unsigned block=0,off=0;
        if(state && std::getline(state,old) && (state>>block>>off) && old==p) a={block,off};
        mode=READ; layout(a); opened_at=app.now;
    }
    void save() {
        if(opened.empty()) return;
        reader::Anchor a=reader::anchor_for_page(map,page);
        ::mkdir("/home/user/.config",0755); ::mkdir("/home/user/.config/graphite",0755);
        std::ofstream f("/home/user/.config/graphite/reader.state.tmp"); if(!f)return; f<<opened<<"\n"<<a.block<<" "<<a.off<<"\n";f.close();rename("/home/user/.config/graphite/reader.state.tmp","/home/user/.config/graphite/reader.state");
    }
    void flip(int d) { if(mode!=READ)return; if((d<0&&page==0)||(d>0&&page+1>=map.pages.size()))return; page=(size_t)((int)page+d);cache_page();save(); }
    void draw(Gfx&g) override {
        if(mode==READ) { uint16_t bg=hex565("#F7F4EC"),fg=hex565("#1B1D1F"),dim=hex565("#6B7075"); g.rect(0,32,480,768,bg);
            icon(g,"back",18,36,18,fg);
            g.text(48,39,g.fit(title,15,366),15,fg,TF_BOLD); g.text(456,39,std::to_string(page+1)+" / "+std::to_string(map.pages.size()),11,dim,TF_RIGHT); g.hline(24,54,432,hex565("#D8D0C0"));
            for(const auto&l:lines) if(l.len&&l.off+l.len<=book.arena.size()) g.text(24,56+l.y,book.arena.substr(l.off,l.len),cfg.font_px,fg);
            g.hline(0,760,480,hex565("#D8D0C0")); int pct=map.pages.size()>1?(int)(page*100/(map.pages.size()-1)):100; g.text(24,770,std::to_string(page+1)+" / "+std::to_string(map.pages.size()),11,dim);g.text(456,770,std::to_string(pct)+" %",11,dim,TF_RIGHT);g.rect(24,794,432*pct/100,2,hex565("#177E85")); return;
        }
        ui_bg(g);
        if(mode==SHELF) { ui_header(g,"Библиотека",(std::to_string(books.size())+" книг").c_str(),true);
            if(external && !error.empty()) { g.text(240,300,g.fit(error,14,420),14,theme.fg,TF_CENTER); return; }
            if(books.empty()) {g.text(240,300,"Нет книг",16,theme.dim,TF_CENTER);g.text(240,332,"GRAPHITE_BOOKS или /media/sd/Books",11,theme.dim,TF_CENTER);return;}
            for(size_t i=scroll;i<books.size()&&i<scroll+12;i++){int y=96+(int)(i-scroll)*56;g.hline(0,y,480,theme.line2);icon(g,"note",24,y+16,24,theme.accent);g.text(60,y+7,g.fit(books[i].name,16,320),16,theme.fg);g.text(60,y+29,std::to_string(books[i].size/1024)+" KB",12,theme.dim);}
            return;
        }
        if(mode==TOC) {ui_header(g,"Оглавление","главы",true);int row=0;for(size_t i=0;i<book.blocks.size()&&row<12;i++){const reader::Block&b=book.blocks[i];if(!(b.flags&1))continue;std::string s=book.arena.substr(b.off,b.len);int indent=(int)b.level*16,y=96+row++*56;g.hline(0,y,480,theme.line2);g.text(24+indent,y+18,g.fit(s,15,390-indent),15,theme.fg);} if(!row)g.text(240,260,"Оглавление не найдено",13,theme.dim,TF_CENTER);return;}
        ui_header(g,"Настройки","Размер · гарнитура",true);
        const char* sizes[]={"20","22","24"};ui_section(g,112,"РАЗМЕР");ui_seg(g,24,144,432,sizes,3,size_index);
        const char* faces[]={"Serif","Sans"};ui_section(g,216,"ШРИФТ");ui_seg(g,24,248,432,faces,2,sans?1:0);
    }
    bool tap(int x,int y) override {
        if(mode==READ && opened_at && app.now-opened_at<400) return true;
        if(mode==SHELF){if(y<96)return false;size_t i=scroll+(y-96)/56;if(i<books.size()){open_book(books[i].path);return true;}return false;}
        if(mode==READ){if(y<56&&x<48){if(external)app.back();else {mode=SHELF;book=reader::Book{};}return true;}if(y>=32&&y<96&&x<150){mode=TOC;return true;}if(y>=32&&y<96&&x>300){mode=SETTINGS;return true;}if(x<120)flip(-1);else if(x>360)flip(1);else {mode=TOC;}return true;}
        if(y<96&&x<52){mode=READ;return true;}
        if(mode==TOC){int r=(y-96)/56;if(r>=0){int n=0;for(size_t i=0;i<book.blocks.size();i++){const auto&b=book.blocks[i];if(!(b.flags&1))continue;if(n++==r){page=reader::page_for_anchor(map,{(uint32_t)i,0});cache_page();mode=READ;save();break;}}}return true;}
        if(mode==SETTINGS){reader::Anchor a=reader::anchor_for_page(map,page);if(y>=144&&y<200)size_index=std::max(0,std::min(2,(x-24)/144));else if(y>=248&&y<304)sans=x>=240;layout(a);save();return true;}return false;
    }
    bool drag(int x,int y,int dx,int dy) override { (void)x;(void)y;(void)dy; drag_dx+=dx; if(std::abs(drag_dx)>=40){flip(drag_dx<0?1:-1);drag_dx=0;moved=true;}return true; }
    bool key(int k,bool down) override {if(!down)return false;if(k==XK_Escape||k==XK_BackSpace||k==XF86XK_Back){if(external){app.back();return true;}if(mode==SHELF)return false;mode=mode==READ?SHELF:READ;if(mode==SHELF)book=reader::Book{};return true;}if(mode==READ&&(k==XK_Left||k==XK_Right)){flip(k==XK_Left?-1:1);return true;}if(mode==SHELF&&k==XK_Up){if(scroll)scroll--;return true;}if(mode==SHELF&&k==XK_Down){if(scroll+12<books.size())scroll++;return true;}return false;}
    void on_show() override { if(!app.pending_file.empty()){std::string p=app.pending_file;app.pending_file.clear();external=true;open_book(p);return;} if(mode==SHELF) scan(); }
};
}
ScreenView *make_reader(){return new Reader();}
