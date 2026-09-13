#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anitoplume
{
struct ImageData
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

bool load_png_rgba(const std::string& path, ImageData& image, std::string& error);
}
