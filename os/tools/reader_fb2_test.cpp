#include "reader_fb2.h"

#include <zip.h>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

static const std::string sample =
"<?xml version=\"1.0\"?><FictionBook><description><title-info><book-title>Заголовок &amp; тест</book-title><author><first-name>Иван</first-name><last-name>Петров</last-name></author></title-info></description>"
"<body><section><title><p>Глава первая</p></title><p>Текст <emphasis>с entity &amp; деталями</emphasis>.</p><section><title><p>Вложенная</p></title><p>Вложенный текст</p></section></section>"
"<section><title><p>Глава вторая</p></title><p>Еще.</p></section><section><title><p>Глава третья</p></title><p>Конец.</p></section></body></FictionBook>";

static void add_entry(zip_t *z, const char *name, const std::string &bytes) {
    zip_source_t *src=zip_source_buffer(z,bytes.data(),bytes.size(),0); assert(src);
    assert(zip_file_add(z,name,src,ZIP_FL_OVERWRITE)>=0);
}

int main() {
    reader::Book b; std::string error;
    assert(reader::load_fb2(sample,b,error));
    assert(b.title=="Заголовок & тест"); assert(b.author=="Иван Петров");
    size_t chapters=0,nested=0; for(const auto &x:b.blocks)if(x.flags&1){++chapters;if(x.level==1)++nested;}
    assert(chapters==4 && nested==1);
    assert(b.arena.find("с entity & деталями")!=std::string::npos);
    assert(!reader::load_fb2("<FictionBook><body><p>bad",b,error));
    assert(error.find("Malformed")!=std::string::npos);

    const char *path="reader_fb2_test.zip"; int ze=0; zip_t *z=zip_open(path,ZIP_CREATE|ZIP_TRUNCATE,&ze);assert(z);
    add_entry(z,"small.fb2","<FictionBook><body><p>x</p></body></FictionBook>");
    add_entry(z,"large.fb2",sample); assert(zip_close(z)==0);
    assert(reader::load_fb2_zip(path,b,error));assert(b.title=="Заголовок & тест");std::remove(path);

    z=zip_open(path,ZIP_CREATE|ZIP_TRUNCATE,&ze);assert(z);std::string bomb(20u*1024u*1024u+1,'x');add_entry(z,"bomb.fb2",bomb);assert(zip_close(z)==0);
    assert(!reader::load_fb2_zip(path,b,error));assert(error.find("20 MiB")!=std::string::npos);std::remove(path);
    std::cout<<"reader_fb2_test: ok (3 chapters, nested, entities, malformed XML, ZIP selection, size limit)\n";
}
