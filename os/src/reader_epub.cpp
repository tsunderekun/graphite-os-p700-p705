#include "reader.h"

#include <libxml/parser.h>
#include <zip.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <functional>
#include <tuple>

namespace reader { namespace {
const size_t LIMIT=64u*1024u*1024u, XML_LIMIT=8u*1024u*1024u, TEXT_LIMIT=16u*1024u*1024u;
bool path_ok(const std::string &p) {
    if(p.empty()||p[0]=='/'||p[0]=='\\'||p.find('\\')!=p.npos||p.find(':')!=p.npos||p.find('%')!=p.npos)return false;
    size_t i=0; while(i<p.size()){size_t j=p.find('/',i);if(j==p.npos)j=p.size();std::string c=p.substr(i,j-i);if(c.empty()||c==".."||c==".")return false;i=j+1;} return true;
}
std::string join(const std::string &base,const std::string &p) { if(!path_ok(p))return {};size_t n=base.find_last_of('/');std::string r=n==base.npos?p:base.substr(0,n+1)+p;return path_ok(r)?r:std::string(); }
bool entry(zip_t*z,const std::string&p,std::string&out,size_t&budget,size_t cap=XML_LIMIT){zip_stat_t s;zip_stat_init(&s);if(!path_ok(p)||zip_stat(z,p.c_str(),0,&s)||!(s.valid&ZIP_STAT_SIZE)||s.size>cap||s.size>LIMIT-budget)return false;zip_file_t*f=zip_fopen(z,p.c_str(),0);if(!f)return false;out.clear();char b[16384];bool ok=true;for(;;){zip_int64_t n=zip_fread(f,b,sizeof b);if(n<0){ok=false;break;}if(!n)break;if((size_t)n>cap-out.size()||(size_t)n>LIMIT-budget-out.size()){ok=false;break;}out.append(b,(size_t)n);}zip_fclose(f);if(!ok)return false;budget+=out.size();return true;}
xmlDocPtr parse(const std::string&s){if(s.empty()||s.size()>XML_LIMIT)return nullptr;xmlDocPtr d=xmlReadMemory(s.data(),(int)s.size(),nullptr,nullptr,XML_PARSE_NONET|XML_PARSE_COMPACT|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);if(d&&(d->intSubset||d->extSubset)){xmlFreeDoc(d);return nullptr;}return d;}
std::string prop(xmlNodePtr n,const char*k){xmlChar*v=xmlGetProp(n,BAD_CAST k);if(!v)return {};std::string s((char*)v);xmlFree(v);return s;}
std::string content(xmlNodePtr n){xmlChar*v=xmlNodeGetContent(n);if(!v)return {};std::string s((char*)v);xmlFree(v);return s;}
bool elem(xmlNodePtr n,const char*s){return n&&n->type==XML_ELEMENT_NODE&&!xmlStrcasecmp(n->name,BAD_CAST s);}
struct Item{std::string href,type,properties;};
using Anchors=std::map<std::string,uint32_t>;
void walk_text(xmlNodePtr n,Book&b,std::string&para,bool&skip,bool&bad,Anchors&anchors){
 for(;n;n=n->next){if(n->type!=XML_ELEMENT_NODE){if(n->type==XML_TEXT_NODE&&!skip){xmlChar*v=xmlNodeGetContent(n);if(v){std::string t((char*)v);xmlFree(v);for(char&c:t)if(c=='\n'||c=='\r'||c=='\t')c=' ';if(!para.empty()&&!t.empty()&&!std::isspace((unsigned char)para.back())&&!std::isspace((unsigned char)t.front()))para+=' ';para+=t;if(para.size()>XML_LIMIT)bad=true;}}continue;}
  std::string id=prop(n,"id");bool hidden=skip||elem(n,"head")||elem(n,"script")||elem(n,"style")||elem(n,"nav");bool block=elem(n,"p")||elem(n,"div")||elem(n,"li")||elem(n,"h1")||elem(n,"h2")||elem(n,"h3")||elem(n,"h4")||elem(n,"h5")||elem(n,"h6")||elem(n,"br");bool heading=elem(n,"h1")||elem(n,"h2")||elem(n,"h3")||elem(n,"h4")||elem(n,"h5")||elem(n,"h6");
 if(elem(n,"img")){if(!para.empty())para+=' ';continue;}
 if(elem(n,"br")){if(!para.empty())para+=' ';continue;}
 if(block&&!para.empty()){if(!add_block(b,para)){bad=true;return;}para.clear();}
  size_t before=b.blocks.size();walk_text(n->children,b,para,hidden,bad,anchors);if(bad)return;
  if(block){std::string s=para;while(!s.empty()&&std::isspace((unsigned char)s.back()))s.pop_back();size_t a=s.find_first_not_of(" \t\r\n");if(a!=s.npos){s.erase(0,a);if(heading){if(add_block(b,s,1))b.blocks.back().level=(uint8_t)(n->name[1]-'1');else bad=true;}else if(!add_block(b,s))bad=true;}para.clear();}
  if(!id.empty()){uint32_t bi=(uint32_t)(b.blocks.size()>before?before:(b.blocks.empty()?0:b.blocks.size()-1));anchors[id]=bi;}
 }
}
std::string fragment(const std::string&s){size_t p=s.find('#');return p==s.npos?std::string():s.substr(p+1);}
std::string href_file(const std::string&s){size_t p=s.find('#');return p==s.npos?s:s.substr(0,p);}
void collect_ncx(xmlNodePtr n,unsigned depth,const std::string&base,std::vector<std::tuple<std::string,std::string,unsigned>>&links){
 for(;n;n=n->next)if(elem(n,"navPoint")){if(links.size()>=5000||depth>32)continue;std::string label,src;for(xmlNodePtr c=n->children;c;c=c->next){if(elem(c,"navLabel")){for(xmlNodePtr x=c->children;x;x=x->next)if(elem(x,"text")){label=content(x);break;}}else if(elem(c,"content"))src=prop(c,"src");}if(!src.empty())links.emplace_back(join(base,href_file(src))+ (src.find('#')==src.npos?"":"#"+fragment(src)),label,depth);for(xmlNodePtr c=n->children;c;c=c->next)collect_ncx(c,depth+1,base,links);}
}
void collect_nav(xmlNodePtr n,unsigned depth,const std::string&base,std::vector<std::tuple<std::string,std::string,unsigned>>&links,bool in_toc=false){
 for(;n;n=n->next){bool toc=in_toc;if(elem(n,"nav")){std::string t=prop(n,"type");for(xmlNsPtr ns=n->nsDef;ns;ns=ns->next)if(ns->prefix&&xmlStrEqual(ns->prefix,BAD_CAST "epub")){xmlChar*v=xmlGetNsProp(n,BAD_CAST "type",ns->href);if(v){t=(char*)v;xmlFree(v);break;}}toc=t=="toc";}
 if(toc&&elem(n,"a")){std::string href=prop(n,"href");if(!href.empty()&&links.size()<5000)links.emplace_back(join(base,href_file(href))+(href.find('#')==href.npos?"":"#"+fragment(href)),content(n),depth?depth-1:0);}
 collect_nav(n->children,toc&&elem(n,"li")?depth+1:depth,base,links,toc);
 }
}
}
bool load_epub(const std::string&path,Book&out,std::string&err){
 err.clear();int ze=0;zip_t*z=zip_open(path.c_str(),ZIP_RDONLY,&ze);if(!z){err="Повреждённый EPUB ZIP";return false;}zip_int64_t count=zip_get_num_entries(z,0);if(count<0||count>2000){err="Слишком много файлов EPUB";zip_close(z);return false;}size_t budget=0;std::string s;
 zip_stat_t es;zip_stat_init(&es);if(zip_stat(z,"META-INF/encryption.xml",0,&es)==0){err="Книга защищена";zip_close(z);return false;}
 if(!entry(z,"META-INF/container.xml",s,budget)){err="Некорректный EPUB container";zip_close(z);return false;}xmlDocPtr d=parse(s);if(!d){err="Некорректный EPUB container XML";zip_close(z);return false;}xmlNodePtr root=xmlDocGetRootElement(d),rf=nullptr;for(xmlNodePtr n=root?root->children:nullptr;n&&!rf;n=n->next)for(xmlNodePtr c=n->children;c;c=c->next)if(elem(c,"rootfile")){rf=c;break;}std::string opf=rf?prop(rf,"full-path"):"";xmlFreeDoc(d);if(!path_ok(opf)){err="Некорректный EPUB package path";zip_close(z);return false;}
 if(!entry(z,opf,s,budget)){err="Не удалось прочитать EPUB package";zip_close(z);return false;}d=parse(s);if(!d){err="Некорректный EPUB OPF";zip_close(z);return false;}
 std::map<std::string,Item> items;std::vector<std::string> spine;Book b;std::string base=opf.substr(0,opf.find_last_of('/')==opf.npos?0:opf.find_last_of('/')+1);
 for(xmlNodePtr n=xmlDocGetRootElement(d)->children;n;n=n->next){if(elem(n,"metadata")){for(xmlNodePtr c=n->children;c;c=c->next){if(elem(c,"title")&&b.title.empty())b.title=content(c);if(elem(c,"creator")&&b.author.empty())b.author=content(c);}}else if(elem(n,"manifest")){for(xmlNodePtr c=n->children;c;c=c->next)if(elem(c,"item")){std::string id=prop(c,"id"),href=prop(c,"href");Item it{join(base,href),prop(c,"media-type"),prop(c,"properties")};if(id.empty()||it.href.empty()){xmlFreeDoc(d);zip_close(z);err="Некорректный EPUB manifest path";return false;}items[id]=it;}}else if(elem(n,"spine")){for(xmlNodePtr c=n->children;c;c=c->next)if(elem(c,"itemref")&&prop(c,"linear")!="no")spine.push_back(prop(c,"idref"));}}
 xmlFreeDoc(d);if(spine.empty()||spine.size()>500){zip_close(z);err="В EPUB нет основного текста";return false;}
 std::map<std::string,uint32_t> targets;std::map<std::string,uint32_t> first_blocks;
 for(const auto&id:spine){auto it=items.find(id);if(it==items.end()||it->second.type!="application/xhtml+xml"){zip_close(z);err="Некорректный EPUB spine";return false;}if(!entry(z,it->second.href,s,budget)){zip_close(z);err="Не удалось прочитать XHTML EPUB";return false;}d=parse(s);if(!d){zip_close(z);err="Некорректный EPUB XHTML";return false;}uint32_t start=(uint32_t)b.blocks.size();std::string para;bool bad=false,skip=false;Anchors a;walk_text(xmlDocGetRootElement(d),b,para,skip,bad,a);if(!para.empty()&&!bad)add_block(b,para);xmlFreeDoc(d);first_blocks[it->second.href]=start;for(auto&kv:a)targets[it->second.href+"#"+kv.first]=kv.second;if(bad||b.arena.size()>TEXT_LIMIT){zip_close(z);err="Текст EPUB превышает лимит";return false;}}
 std::vector<std::tuple<std::string,std::string,unsigned>> links;std::string ncx,nav;for(auto&kv:items){if(kv.second.type=="application/x-dtbncx+xml"&&ncx.empty())ncx=kv.second.href;if(kv.second.properties.find("nav")!=std::string::npos&&nav.empty())nav=kv.second.href;}
 bool toc_ok=false;if(!nav.empty()&&entry(z,nav,s,budget)){d=parse(s);if(d){std::string nb=nav;collect_nav(xmlDocGetRootElement(d),0,nb,links);xmlFreeDoc(d);toc_ok=!links.empty();}}
 if(!toc_ok&&!ncx.empty()){links.clear();if(entry(z,ncx,s,budget)){d=parse(s);if(d){collect_ncx(xmlDocGetRootElement(d),0,ncx.substr(0,ncx.find_last_of('/')==ncx.npos?0:ncx.find_last_of('/')+1),links);xmlFreeDoc(d);toc_ok=!links.empty();}}}
 if(toc_ok){for(const auto&v:links){const std::string&target=std::get<0>(v);auto it=targets.find(target);if(it==targets.end()){std::string f=href_file(target);auto first=first_blocks.find(f);if(first!=first_blocks.end())it=targets.emplace(target,first->second).first;}if(it!=targets.end()&&it->second<b.blocks.size()){b.blocks[it->second].flags|=1;b.blocks[it->second].level=(uint8_t)std::min<unsigned>(std::get<2>(v),255);}}}
 if(!toc_ok){bool headings=false;for(const auto&bl:b.blocks)if(bl.flags&1){headings=true;break;}if(!headings){for(size_t i=0;i<spine.size();++i){auto it=items.find(spine[i]);auto f=it==items.end()?first_blocks.end():first_blocks.find(it->second.href);if(f==first_blocks.end()||f->second>=b.blocks.size())continue;Block&bl=b.blocks[f->second];bl.flags|=1;bl.level=0;}}}
 zip_close(z);if(b.blocks.empty()){err="EPUB не содержит читаемого текста";return false;}out=std::move(b);return true;
}
}
