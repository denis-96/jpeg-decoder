#include "../decoder.h"

#include "jpeg_decoder.h"

#include <fstream>
#include <stdexcept>

Image Decode(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    JpegDecoder decoder(file);
    return decoder.Decode();
}
