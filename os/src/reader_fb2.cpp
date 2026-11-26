#include "reader_fb2.h"

#include <libxml/parser.h>
#include <libxml/SAX2.h>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <strings.h>

namespace reader { namespace {
constexpr size_t INPUT_LIMIT = 20u * 1024u * 1024u;
constexpr size_t TEXT_LIMIT = 20u * 1024u * 1024u;
constexpr zip_uint64_t ZIP_ENTRY_LIMIT = 10000;
constexpr uint16_t CHAPTER = 1;

struct Parser {
    Book book;
    std::string error, text, title, author, capture;
    std::vector<std::string> stack;
    bool in_body = false, in_main = false, in_title_info = false, in_notes = false;
    bool in_binary = false, in_section_title = false;
    bool failed = false;
    unsigned section_depth = 0;
};

bool has(const Parser &p, const char *name) { return std::find(p.stack.begin(), p.stack.end(), name) != p.stack.end(); }
void append_text(std::string &to, const xmlChar *s, int len) {
    if (len > 0 && to.size() <= INPUT_LIMIT - std::min<size_t>((size_t)len, INPUT_LIMIT))
        to.append(reinterpret_cast<const char *>(s), (size_t)len);
}
void sax_start(void *ctx, const xmlChar *local, const xmlChar *, const xmlChar *, int,
               const xmlChar **, int, int, const xmlChar **) {
    Parser &p = *static_cast<Parser *>(ctx);
    const std::string name(reinterpret_cast<const char *>(local));
    p.stack.push_back(name);
    if (name == "body") { p.in_body = true; p.in_main = true; }
    else if (name == "section" && p.in_main) ++p.section_depth;
    else if (name == "title-info") { p.in_title_info = true; p.capture.clear(); }
    else if (name == "notes") p.in_notes = true;
    else if (name == "binary") p.in_binary = true;
    else if (name == "title" && p.in_main && p.section_depth) { p.in_section_title = true; p.capture.clear(); }
    else if (name == "book-title" || name == "first-name" || name == "middle-name" || name == "last-name") p.capture.clear();
    else if (name == "p" && p.in_main) p.text.clear();
}
void sax_end(void *ctx, const xmlChar *local, const xmlChar *, const xmlChar *) {
    Parser &p = *static_cast<Parser *>(ctx);
    const std::string name(reinterpret_cast<const char *>(local));
    if (name == "book-title" && p.in_title_info) {
        auto a=p.capture.find_first_not_of(" \t\r\n"), b=p.capture.find_last_not_of(" \t\r\n");
        if(a!=std::string::npos)p.book.title=p.capture.substr(a,b-a+1);
    } else if (name == "first-name" || name == "middle-name" || name == "last-name") {
        if (has(p,"title-info")) { if(!p.author.empty())p.author+=' '; p.author+=p.capture; }
    } else if (name == "author" && has(p,"title-info")) p.book.author=p.author;
    else if (name == "title" && p.in_section_title) {
        auto a=p.capture.find_first_not_of(" \t\r\n"), b=p.capture.find_last_not_of(" \t\r\n");
        if(a!=std::string::npos && add_block(p.book,p.capture.substr(a,b-a+1),CHAPTER))
            p.book.blocks.back().level=(uint8_t)std::min<unsigned>(p.section_depth? p.section_depth-1:0,255);
        p.in_section_title=false;
    } else if (name == "p" && p.in_main && !has(p,"title")) {
        auto a=p.text.find_first_not_of(" \t\r\n"), b=p.text.find_last_not_of(" \t\r\n");
        if(a!=std::string::npos) {
            std::string s=p.text.substr(a,b-a+1);
            for(char &c:s) if(c=='\r'||c=='\t'||c=='\n') c=' ';
            // Collapse formatting whitespace but preserve a separator between SAX text chunks.
            std::string clean; bool ws=false;
            for(unsigned char c:s){if(std::isspace(c)){ws=true;continue;}if(ws&&!clean.empty())clean+=' ';ws=false;clean+=(char)c;}
            if(!clean.empty() && p.book.arena.size()+clean.size()<=TEXT_LIMIT) add_block(p.book,clean);
            else if(p.book.arena.size()+clean.size()>TEXT_LIMIT){p.failed=true;p.error="FB2 text exceeds 20 MiB limit";}
        }
    } else if (name == "binary") p.in_binary=false;
    else if (name == "notes") p.in_notes=false;
    else if (name == "title-info") p.in_title_info=false;
    else if (name == "section" && p.in_main && p.section_depth) --p.section_depth;
    else if (name == "body") { p.in_body=false; p.in_main=false; }
    if(!p.stack.empty())p.stack.pop_back();
    if(p.capture.size()>INPUT_LIMIT)p.capture.resize(INPUT_LIMIT);
}
void sax_chars(void *ctx, const xmlChar *s, int len) {
    Parser &p=*static_cast<Parser *>(ctx);
    if(p.in_binary)return;
    if(p.in_section_title || p.in_title_info) append_text(p.capture,s,len);
    if(p.in_main && !p.in_notes && has(p,"p") && !has(p,"title")) append_text(p.text,s,len);
}
void sax_error(void *ctx, const char *msg, ...) { Parser &p=*static_cast<Parser *>(ctx);if(p.error.empty())p.error=msg?msg:"Malformed FB2 XML"; }
bool parse(const std::string &bytes, Book &out, std::string &error) {
    error.clear();
    if(bytes.empty()){error="FB2 file is empty";return false;}
    if(bytes.size()>INPUT_LIMIT){error="FB2 exceeds 20 MiB limit";return false;}
    Parser p;
    xmlSAXHandler h{}; h.initialized=XML_SAX2_MAGIC; h.startElementNs=sax_start;h.endElementNs=sax_end;h.characters=sax_chars;h.error=sax_error;h.fatalError=sax_error;
    xmlParserCtxtPtr ctxt=xmlCreatePushParserCtxt(&h,&p,nullptr,0,nullptr);
    if(!ctxt){error="Could not initialize FB2 XML parser";return false;}
    xmlCtxtUseOptions(ctxt,XML_PARSE_NONET|XML_PARSE_COMPACT|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    int rc=xmlParseChunk(ctxt,bytes.data(),(int)bytes.size(),1);
    const bool wellformed=rc==0 && ctxt->wellFormed;
    xmlFreeParserCtxt(ctxt);
    if(p.failed){error=p.error;return false;}
    if(!wellformed){error="Malformed FB2 XML";return false;}
    if(p.book.blocks.empty()){error="FB2 contains no readable paragraphs";return false;}
    out=std::move(p.book);return true;
}
}

bool load_fb2(const std::string &bytes, Book &out, std::string &error) { return parse(bytes,out,error); }

bool load_fb2_zip(const std::string &path, Book &out, std::string &error) {
    error.clear(); int ze=0; zip_t *z=zip_open(path.c_str(),ZIP_RDONLY,&ze);
    if(!z){error="Could not open FB2 ZIP archive";return false;}
    zip_int64_t count=zip_get_num_entries(z,0);
    if(count<0 || (zip_uint64_t)count>ZIP_ENTRY_LIMIT){zip_close(z);error="ZIP entry count exceeds 10,000 limit";return false;}
    zip_uint64_t chosen=ZIP_UINT64_MAX, biggest=0;
    for(zip_uint64_t i=0;i<(zip_uint64_t)count;i++){
        const char *n=zip_get_name(z,i,ZIP_FL_ENC_GUESS); if(!n)continue;
        size_t len=std::strlen(n); if(len<4 || strcasecmp(n+len-4,".fb2"))continue;
        zip_stat_t st; zip_stat_init(&st);
        if(zip_stat_index(z,i,0,&st)!=0 || !(st.valid&ZIP_STAT_SIZE))continue;
        if(st.size>biggest){biggest=st.size;chosen=i;}
    }
    if(chosen==ZIP_UINT64_MAX){zip_close(z);error="ZIP archive contains no .fb2 entry";return false;}
    if(biggest>INPUT_LIMIT){zip_close(z);error="FB2 ZIP entry exceeds 20 MiB limit";return false;}
    zip_file_t *f=zip_fopen_index(z,chosen,0);
    if(!f){zip_close(z);error="Could not read FB2 ZIP entry";return false;}
    std::string bytes;bytes.reserve((size_t)biggest);char buf[32768];
    for(;;){zip_int64_t n=zip_fread(f,buf,sizeof buf);if(n<0){error="Corrupt FB2 ZIP entry";break;}if(!n)break;if(bytes.size()+(size_t)n>INPUT_LIMIT){error="FB2 ZIP entry exceeds 20 MiB limit";break;}bytes.append(buf,(size_t)n);}
    zip_fclose(f);zip_close(z);
    if(!error.empty())return false;
    return parse(bytes,out,error);
}
} // namespace reader
