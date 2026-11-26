#pragma once
#include "reader.h"
namespace reader {
// Convert a complete TXT byte buffer into normalized UTF-8 blocks.
bool load_txt(const std::string &bytes, Book &out, std::string &error);
}
