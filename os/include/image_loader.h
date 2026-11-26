#pragma once
#include <cstdint>
#include <string>

struct Image {
    int width = 0;
    int height = 0;
    unsigned char *pixels = nullptr; // tightly packed RGB888
};

bool image_load(const std::string &path, Image &out, std::string &err);
void image_free(Image &image);

struct ImageFit { int x = 0, y = 0, width = 0, height = 0; };
bool image_fit(int source_width, int source_height, int target_width,
               int target_height, ImageFit &out);
