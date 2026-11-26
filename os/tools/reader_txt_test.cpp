#include "reader_txt.h"
#include <cassert>
#include <iostream>

int main() {
    reader::Book b; std::string err;
    assert(reader::load_txt("Глава 1\r\n\r\nТекст\rещё", b, err));
    assert(b.blocks.size() >= 3);
    std::string joined;
    for (const auto &x : b.blocks) { if (x.len) joined.append(b.arena, x.off, x.len); joined += "\n"; }
    assert(joined.find("Текст") != std::string::npos && joined.find("ещё") != std::string::npos);
    bool chapter = false; for (const auto &x : b.blocks) if (x.flags & 1) chapter = true;
    assert(chapter);
    const std::string le("\xff\xfe\x1f\x04\x38\x04\x32\x04\x30\x04", 10);
    assert(reader::load_txt(le,b,err)); assert(b.arena.find("Пива") != std::string::npos);
    assert(!reader::load_txt("",b,err));
    assert(!reader::load_txt(std::string("\x01\x02",2),b,err));
    std::cout << "reader_txt tests: PASS\n";
}
