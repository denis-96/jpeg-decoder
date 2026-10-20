#pragma once

#include <filesystem>

inline std::filesystem::path GetFileDir(const char* filename) {
    return std::filesystem::path(filename).parent_path();
}
