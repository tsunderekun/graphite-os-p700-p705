#include "image_loader.h"
#include "stb_image.h"

#include <sys/stat.h>
#include <algorithm>

namespace {
constexpr long long kMaxFile = 12LL * 1024 * 1024;
constexpr int kMaxSide = 4096;
}

void image_free(Image &image) {
    if (image.pixels) stbi_image_free(image.pixels);
    image = Image{};
}

bool image_load(const std::string &path, Image &out, std::string &err) {
    image_free(out);
    err.clear();
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size < 0) {
        err = "Не удалось прочитать файл";
        return false;
    }
    if (st.st_size > kMaxFile) {
        err = "Файл больше 12 МиБ";
        return false;
    }
    int w = 0, h = 0, channels = 0;
    // Reject oversized images before allocating the decoded buffer: stbi_info
    // only reads the header, stbi_load would allocate width*height*3 first.
    if (!stbi_info(path.c_str(), &w, &h, &channels)) {
        err = "Не удалось прочитать изображение";
        return false;
    }
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) {
        err = "Размер изображения превышает 4096 px";
        return false;
    }
    unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 3);
    if (!pixels) {
        err = "Не удалось прочитать изображение";
        return false;
    }
    out.width = w;
    out.height = h;
    out.pixels = pixels;
    return true;
}

bool image_fit(int sw, int sh, int tw, int th, ImageFit &out) {
    out = ImageFit{};
    if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0) return false;
    const double scale = std::min((double)tw / sw, (double)th / sh);
    out.width = std::max(1, (int)(sw * scale + 0.5));
    out.height = std::max(1, (int)(sh * scale + 0.5));
    out.x = (tw - out.width) / 2;
    out.y = (th - out.height) / 2;
    return true;
}
