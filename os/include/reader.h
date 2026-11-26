#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reader {

// All offsets are byte offsets into Book::arena and must lie on UTF-8
// codepoint boundaries. Run offsets are relative to their containing block.
struct Block { uint32_t off = 0, len = 0, first_run = 0; uint16_t nruns = 0, flags = 0; uint8_t level = 0; int16_t indent_px = 0; };
struct Run { uint32_t off = 0, len = 0; uint16_t flags = 0; };
struct Anchor { uint32_t block = 0, off = 0; };
struct PageStart { uint32_t block = 0, off = 0; uint16_t line = 0, chapter = 0; };
// y is the top of the line slot in page coordinates (first baseline is the
// caller's responsibility); empty blocks produce a zero-length blank slot.
struct Line { uint32_t block = 0, off = 0, len = 0; int width = 0, x = 0, y = 0; };
struct Book { std::string arena; std::vector<Block> blocks; std::vector<Run> runs; std::string title, author; uint64_t hash = 0; };
struct PageMap { std::vector<PageStart> pages; bool complete = false; uint32_t total_chars = 0; uint64_t layout_hash = 0; };
struct LayoutCfg {
    // Width 432; line height 29; 24 lines: 480x800, 24px side margins.
    int width = 432, line_height = 29, lines_per_page = 24, font_px = 20, margin_px = 24;
    uint64_t font_id = 0;
};
using Advance = std::function<int(uint32_t codepoint)>;

bool valid_utf8(const std::string &s);
bool boundary(const std::string &s, size_t off);
size_t next_boundary(const std::string &s, size_t off);
uint64_t layout_hash(const LayoutCfg &cfg);
bool add_block(Book &book, const std::string &utf8, uint16_t flags = 0);
bool add_empty_block(Book &book, uint16_t flags = 0);
bool load_epub(const std::string &path, Book &out, std::string &err);

// Paginates a whole book deterministically. Advance returns pixel width per
// Unicode scalar (0 means unavailable; fallback advance is used).
bool paginate(const Book &book, const LayoutCfg &cfg, const Advance &advance,
              PageMap &out);
bool page_lines(const Book &book, const LayoutCfg &cfg, const Advance &advance,
                const PageMap &map, size_t page, std::vector<Line> &out);
size_t page_for_anchor(const PageMap &map, Anchor anchor);
Anchor anchor_for_page(const PageMap &map, size_t page);
std::vector<uint8_t> serialize(const PageMap &map);
bool deserialize(const uint8_t *data, size_t size, PageMap &out);

} // namespace reader
