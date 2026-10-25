// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneImages.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <cmath>
namespace d3scene {
ImageMip compositeDisplayReadback(ImageMip base,const ImageMip& overlay,float gamma) {
    const auto valid = [](const ImageMip& image) {
        return image.width && image.height && image.width <= INT32_MAX && image.height <= INT32_MAX &&
            uint64_t(image.width)*image.height <= SIZE_MAX/4 &&
            image.pixels.size() == size_t(image.width)*image.height*4;
    };
    if (!valid(base) || !valid(overlay) || !std::isfinite(gamma) || gamma <= 0)
        throw std::invalid_argument("Invalid display export layers");
    const auto sample = [&](int x,int y,int channel) {
        x = (std::max)(0,(std::min)(int(overlay.width)-1,x));
        y = (std::max)(0,(std::min)(int(overlay.height)-1,y));
        return overlay.pixels[(size_t(y)*overlay.width+x)*4+channel]/255.f;
    };
    for (uint32_t y = 0; y < base.height; ++y) for (uint32_t x = 0; x < base.width; ++x) {
        const float u = (x+.5f)*overlay.width/base.width-.5f, v = (y+.5f)*overlay.height/base.height-.5f;
        const int left = int(std::floor(u)), bottom = int(std::floor(v));
        const float dx = u-left, dy = v-bottom;
        float color[4];
        for (int c = 0; c < 4; ++c) color[c] =
            (sample(left,bottom,c)*(1-dx)+sample(left+1,bottom,c)*dx)*(1-dy)+
            (sample(left,bottom+1,c)*(1-dx)+sample(left+1,bottom+1,c)*dx)*dy;
        auto* pixel = base.pixels.data()+(size_t(y)*base.width+x)*4;
        for (int c = 0; c < 4; ++c) {
            const float source = c == 3 || gamma == 1 ? color[c] : std::pow(color[c],gamma);
            const float value = source+pixel[c]/255.f*(1-color[3]);
            pixel[c] = uint8_t(std::lround((std::max)(0.f,(std::min)(1.f,value))*255));
        }
    }
    return base;
}
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
void expandImageRgba(ImageFormat format,const uint8_t* source,size_t count,uint8_t* destination) {
    if (format == ImageFormat::RGBA8) {
        if (count) std::memcpy(destination,source,count*4);
        return;
    }
    // Tiny channel tables keep D3's float truncation without a floating-point
    // conversion (or an out-of-line call) for every uploaded texel.
    static const auto five = [] {
        std::array<uint8_t,32> values{};
        for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<uint8_t>(255*(float(i)/31.f));
        return values;
    }();
    static const auto four = [] {
        std::array<uint8_t,16> values{};
        for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<uint8_t>(255*(float(i)/15.f));
        return values;
    }();
    if (format != ImageFormat::ARGB1555 && format != ImageFormat::ARGB4444)
        throw std::invalid_argument("Unknown packed image format");
    if (format == ImageFormat::ARGB1555) {
        for (size_t i = 0; i < count; ++i) {
            uint16_t pixel; std::memcpy(&pixel,source+i*2,2);
            auto* rgba = destination+i*4;
            if (!(pixel & 0x8000)) { std::memset(rgba,0,4); continue; }
            rgba[0] = five[(pixel>>10)&31]; rgba[1] = five[(pixel>>5)&31]; rgba[2] = five[pixel&31]; rgba[3] = 255;
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint16_t pixel; std::memcpy(&pixel,source+i*2,2);
            auto* rgba = destination+i*4;
            rgba[0] = four[(pixel>>8)&15]; rgba[1] = four[(pixel>>4)&15]; rgba[2] = four[pixel&15]; rgba[3] = four[pixel>>12];
        }
    }
}
ImageAsset::ImageAsset(ConstructionKey,AssetId id,const ImageSource& source)
    : id_(id),format_(source.format),base_extent_{source.mips[0].width,source.mips[0].height},
      mip_count_(uint32_t(source.mips.size())),deferred_(source.deferred),
      generated_levels_(source.generated_levels),padded_extent_(source.padded_extent) {
    for (const auto& mip : source.mips) {
        if (!deferred_) pixels_.emplace_back(mip.pixels,mip.pixels+mip.bytes);
    }
}
const std::vector<uint8_t>& ImageAsset::pixels(size_t level) const {
    if (level >= mip_count_) throw std::out_of_range("Image mip index outside asset");
    return deferred_ ? deferred_->bytes() : pixels_[level];
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
    if (source.deferred && (source.mips.size() != 1 || source.generated_levels || source.padded_extent ||
        source.format == ImageFormat::RGBA8 || source.deferred->size() != source.mips[0].bytes))
        throw std::invalid_argument("Invalid deferred image recipe");
    if (uint64_t(source.padded_extent)*source.padded_extent > std::numeric_limits<size_t>::max()/4)
        throw std::invalid_argument("Image storage extent overflows");
    if (source.padded_extent && (source.mips.size() != 1 || source.generated_levels ||
        source.padded_extent < source.mips[0].width || source.padded_extent < source.mips[0].height))
        throw std::invalid_argument("Invalid image padding extent");
    if (source.generated_levels && (source.mips.size() != 1 || source.format != ImageFormat::RGBA8 || source.generated_levels > 32))
        throw std::invalid_argument("Invalid generated image mip chain");
    for (size_t i = 0; i < source.mips.size(); ++i) {
        const auto& mip = source.mips[i];
        if (!mip.width || !mip.height || (!mip.pixels && !source.deferred) || uint64_t(mip.width)*mip.height > std::numeric_limits<size_t>::max()/pixel_bytes ||
            size_t(mip.width)*mip.height*pixel_bytes != mip.bytes)
            throw std::invalid_argument("Invalid image mip dimensions or bytes");
        if (i && (mip.width != std::max(1u,source.mips[i-1].width/2) || mip.height != std::max(1u,source.mips[i-1].height/2)))
            throw std::invalid_argument("Non-contiguous image mip chain");
    }
    auto previous = image(slot);
    bool matches = previous && previous->format() == source.format && previous->generatedLevels() == source.generated_levels &&
        previous->paddedExtent() == source.padded_extent &&
        previous->mipCount() == source.mips.size();
    for (size_t i = 0; matches && i < source.mips.size(); ++i) {
        const auto a = previous->extent(i); const auto& b = source.mips[i];
        matches = a.width == b.width && a.height == b.height && (source.deferred ? previous->deferred_ == source.deferred :
            (previous->pixels(i).size() == b.bytes && std::memcmp(previous->pixels(i).data(),b.pixels,b.bytes) == 0));
    }
    if (matches) {
        if (slot >= image_source_revisions_.size()) image_source_revisions_.resize(size_t(slot)+1);
        image_source_revisions_[slot] = source.content_revision;
        return previous;
    }
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    ImageAssetRef replacement = std::make_shared<ImageAsset>(ImageAsset::ConstructionKey{},AssetId{lifetime_,revision_+1,slot},source);
    if (slot >= images_.size()) images_.resize(size_t(slot)+1);
    images_[slot] = replacement; ++revision_;
    if (slot >= image_source_revisions_.size()) image_source_revisions_.resize(size_t(slot)+1);
    image_source_revisions_[slot] = source.content_revision;
    return replacement;
}
}
