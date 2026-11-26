#pragma once

#include "reader.h"

namespace reader {
// Parse FB2 XML bytes (up to 20 MiB) into UTF-8 blocks. ZIP/XML external
// entities are disabled; embedded binary image data is ignored.
bool load_fb2(const std::string &bytes, Book &out, std::string &error);
// Read the largest .fb2 entry from a ZIP file path without extracting it.
// Archives are limited to 10,000 entries and FB2 entries to 20 MiB.
bool load_fb2_zip(const std::string &path, Book &out, std::string &error);
} // namespace reader
