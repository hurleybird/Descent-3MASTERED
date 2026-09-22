// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
#include <cstddef>
#include <array>
namespace d3scene {
// Borrowed only during asset publication. Matching revisions are compared in
// place; pixels are copied only when an immutable image actually changes.
struct ImageMipSource {
    uint32_t width = 0, height = 0;
    const uint8_t* pixels = nullptr;
    size_t bytes = 0;
};
struct ImageSource {
    ImageFormat format = ImageFormat::ARGB1555;
    std::vector<ImageMipSource> mips;
    uint32_t generated_levels = 0;
    uint32_t padded_extent = 0; // authored lightmap storage square; padding is defined zero
};
// Preserve D3's float-to-byte truncation, not driver-specific 5-bit expansion.
std::array<uint8_t,4> imageRgbaPixel(ImageFormat,uint16_t);
}
