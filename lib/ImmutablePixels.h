// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstring>
#include <limits>

// An owned pixel value, optionally represented by an immutable recipe. Recipes
// own every input; evaluation cannot consult game state or advance simulation.
// Concurrent consumers share one evaluation, including retained old frames.
class ImmutablePixels {
    size_t size_;
    mutable std::once_flag ready_;
    mutable std::vector<uint8_t> bytes_;
protected:
    explicit ImmutablePixels(size_t size) : size_(size) {}
    virtual std::vector<uint8_t> evaluate() const = 0;
public:
    virtual ~ImmutablePixels() = default;
    size_t size() const { return size_; }
    const std::vector<uint8_t>& bytes() const {
        std::call_once(ready_,[this] {
            auto result = evaluate();
            if (result.size() != size_) throw std::logic_error("Pixel recipe returned the wrong extent");
            bytes_ = std::move(result);
        });
        return bytes_;
    }
};
using ImmutablePixelsRef = std::shared_ptr<const ImmutablePixels>;
class CopiedPixels final : public ImmutablePixels {
    std::vector<uint8_t> source_;
    std::vector<uint8_t> evaluate() const override { return source_; }
public:
    CopiedPixels(const uint8_t* source,size_t size) : ImmutablePixels(size),source_(source,source+size) {}
};
// An indexed image is a value, not a dependency on a live simulation buffer.
// Keep its compact indices until a pixel consumer actually needs packed color.
class IndexedPixels16 final : public ImmutablePixels {
    std::vector<uint8_t> indices_;
    std::array<uint16_t,256> palette_;
    std::vector<uint8_t> evaluate() const override {
        std::vector<uint8_t> output(size());
        for (size_t i = 0; i < indices_.size(); ++i) {
            const auto color = palette_[indices_[i]];
            std::memcpy(output.data()+i*2,&color,2);
        }
        return output;
    }
public:
    IndexedPixels16(const uint8_t* indices,size_t count,const uint16_t* palette)
        : ImmutablePixels(count*2) {
        if (!indices || !palette || !count || count > (std::numeric_limits<size_t>::max)()/2)
            throw std::invalid_argument("Invalid indexed pixel value");
        indices_.assign(indices,indices+count);
        std::copy_n(palette,palette_.size(),palette_.begin());
    }
};
