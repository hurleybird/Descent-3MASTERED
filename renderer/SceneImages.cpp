// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneImages.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <algorithm>
namespace d3scene {
std::array<uint8_t,4> imageRgbaPixel(ImageFormat format,uint16_t pixel) {
    const auto expand = [](uint16_t channel,float maximum) { return static_cast<uint8_t>(255*(static_cast<float>(channel)/maximum)); };
    if (format == ImageFormat::ARGB1555) {
        if (!(pixel & 0x8000)) return {{0,0,0,0}};
        return {{expand((pixel>>10)&31,31),expand((pixel>>5)&31,31),expand(pixel&31,31),255}};
    }
    if (format == ImageFormat::ARGB4444)
        return {{expand((pixel>>8)&15,15),expand((pixel>>4)&15,15),expand(pixel&15,15),expand(pixel>>12,15)}};
    throw std::invalid_argument("RGBA8 has no 16-bit conversion");
}
ImageAsset::ImageAsset(AssetId id,const ImageSource& source)
    : id_(id),format_(source.format),generated_levels_(source.generated_levels),padded_extent_(source.padded_extent) {
    for (const auto& mip : source.mips) {
        ImageMip copy; copy.width = mip.width; copy.height = mip.height;
        copy.pixels.assign(mip.pixels,mip.pixels+mip.bytes);
        mips_.push_back(std::move(copy));
    }
}
ImageAssetRef SceneAssets::image(uint32_t slot) const { return slot < images_.size() ? images_[slot] : nullptr; }
ImageAssetRef SceneAssets::image(uint32_t slot,uint64_t revision) const {
    return revision && slot < image_source_revisions_.size() && image_source_revisions_[slot] == revision ? image(slot) : nullptr;
}
ImageAssetRef SceneAssets::publishImage(uint32_t slot,const ImageSource& source) {
    if (source.mips.empty() || source.mips.size() > 32 ||
        (source.format != ImageFormat::ARGB1555 && source.format != ImageFormat::ARGB4444 && source.format != ImageFormat::RGBA8))
        throw std::invalid_argument("Invalid image format or mip count");
    const size_t pixel_bytes = source.format == ImageFormat::RGBA8 ? 4 : 2;
    if (uint64_t(source.padded_extent)*source.padded_extent > std::numeric_limits<size_t>::max()/4)
        throw std::invalid_argument("Image storage extent overflows");
    if (source.padded_extent && (source.mips.size() != 1 || source.generated_levels ||
        source.padded_extent < source.mips[0].width || source.padded_extent < source.mips[0].height))
        throw std::invalid_argument("Invalid image padding extent");
    if (source.generated_levels && (source.mips.size() != 1 || source.format != ImageFormat::RGBA8 || source.generated_levels > 32))
        throw std::invalid_argument("Invalid generated image mip chain");
    for (size_t i = 0; i < source.mips.size(); ++i) {
        const auto& mip = source.mips[i];
        if (!mip.width || !mip.height || !mip.pixels || uint64_t(mip.width)*mip.height > std::numeric_limits<size_t>::max()/pixel_bytes ||
            size_t(mip.width)*mip.height*pixel_bytes != mip.bytes)
            throw std::invalid_argument("Invalid image mip dimensions or bytes");
        if (i && (mip.width != std::max(1u,source.mips[i-1].width/2) || mip.height != std::max(1u,source.mips[i-1].height/2)))
            throw std::invalid_argument("Non-contiguous image mip chain");
    }
    auto previous = image(slot);
    bool matches = previous && previous->format() == source.format && previous->generatedLevels() == source.generated_levels &&
        previous->paddedExtent() == source.padded_extent &&
        previous->mips().size() == source.mips.size();
    for (size_t i = 0; matches && i < source.mips.size(); ++i) {
        const auto& a = previous->mips()[i]; const auto& b = source.mips[i];
        matches = a.width == b.width && a.height == b.height && a.pixels.size() == b.bytes &&
            std::memcmp(a.pixels.data(),b.pixels,b.bytes) == 0;
    }
    if (matches) {
        if (slot >= image_source_revisions_.size()) image_source_revisions_.resize(size_t(slot)+1);
        image_source_revisions_[slot] = source.content_revision;
        return previous;
    }
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    ImageAssetRef replacement(new ImageAsset({lifetime_,revision_+1,slot},source));
    if (slot >= images_.size()) images_.resize(size_t(slot)+1);
    images_[slot] = replacement; ++revision_;
    if (slot >= image_source_revisions_.size()) image_source_revisions_.resize(size_t(slot)+1);
    image_source_revisions_[slot] = source.content_revision;
    return replacement;
}
}
