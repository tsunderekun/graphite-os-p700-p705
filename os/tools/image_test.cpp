#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image_loader.h"

#include <cstdio>
#include <fstream>
#include <iostream>

static int failures = 0;
static void check(bool ok, const char *name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
}

int main() {
    ImageFit f;
    check(image_fit(800, 600, 480, 800, f) && f.x == 0 && f.y == 220 && f.width == 480 && f.height == 360, "fit landscape");
    check(image_fit(600, 800, 480, 800, f) && f.x == 0 && f.y == 80 && f.width == 480 && f.height == 640, "fit portrait");
    check(image_fit(500, 500, 480, 800, f) && f.x == 0 && f.y == 160 && f.width == 480 && f.height == 480, "fit square");
    check(image_fit(1, 1, 480, 800, f) && f.width == 480 && f.height == 480, "fit 1x1");
    check(!image_fit(0, 1, 480, 800, f), "fit rejects zero");

    const char *png = "artifacts/host/tasks.png";
    Image image;
    std::string err;
    if (image_load(png, image, err)) {
        check(image.width == 480 && image.height == 800, "tree PNG dimensions");
        check(image.pixels != nullptr, "tree PNG buffer");
        image_free(image);
    } else {
        std::cout << "SKIP tree PNG unavailable: " << png << '\n';
    }
    check(!image_load("/no/such/image.png", image, err) && !err.empty(), "missing file error");
    const char *empty = "/tmp/image_test_empty.png";
    { std::ofstream f0(empty, std::ios::binary); }
    check(!image_load(empty, image, err) && !err.empty(), "empty file error");
    std::remove(empty);
    const char *text = "/tmp/image_test_text.png";
    { std::ofstream f0(text, std::ios::binary); f0 << "not a png"; }
    check(!image_load(text, image, err) && !err.empty(), "text PNG error");
    std::remove(text);
    return failures ? 1 : 0;
}
