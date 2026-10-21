// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../lib/ImmutablePixels.h"
#include <array>
#include <cstring>
#include <algorithm>

namespace d3scene {
struct WaterShading {
    std::array<std::array<uint16_t,256>,64> high;
    std::array<std::array<uint8_t,256>,64> low;
};
// Simulation publishes the height grid BEFORE its next wave step. Pixel work
// is a pure, demand-resolved image value, not another simulation evaluation.
class WaterImage final : public ImmutablePixels {
    size_t width_,height_;
    int light_;
    std::vector<int16_t> heights_;
    ImmutablePixelsRef base_;
    std::shared_ptr<const WaterShading> shading_;
    std::vector<uint8_t> evaluate() const override {
        const auto& source = base_->bytes();
        std::vector<uint8_t> output(size());
        for (size_t y = 0; y < height_; ++y) {
            const auto row = y*width_;
            const auto above = (y ? y-1 : height_-1)*width_;
            const auto below = (y+1 == height_ ? 0 : y+1)*width_;
            for (size_t x = 0; x < width_; ++x) {
                const auto left = x ? x-1 : width_-1, right = x+1 == width_ ? 0 : x+1;
                int dx,dy;
                if (light_ < 0) {
                    dx = std::max(0,int(heights_[row+x])-heights_[row+right]);
                    dy = std::max(0,int(heights_[row+x])-heights_[below+x]);
                } else {
                    dx = int(heights_[row+left])-heights_[row+right];
                    dy = int(heights_[above+x])-heights_[below+x];
                }
                const auto sx = size_t(int(x)+(dx>>3))&(width_-1);
                const auto sy = size_t(int(y)+(dy>>3))&(height_-1);
                uint16_t color; std::memcpy(&color,source.data()+2*(sy*width_+sx),2);
                if (light_ >= 0) {
                    const int shade = std::max(0,std::min(63,32-(dx>>(unsigned(light_)&31))));
                    color &= 0x7fff;
                    color = shading_->high[shade][color>>8]+shading_->low[shade][color&255];
                }
                std::memcpy(output.data()+2*(row+x),&color,2);
            }
        }
        return output;
    }
public:
    WaterImage(size_t width,size_t height,const int16_t* heights,ImmutablePixelsRef base,
        int light,std::shared_ptr<const WaterShading> shading)
        : ImmutablePixels(width*height*2),width_(width),height_(height),light_(light),
          base_(std::move(base)),shading_(std::move(shading)) {
        if (width < 2 || height < 2 || width > 4096 || height > 4096 ||
            (width&(width-1)) || (height&(height-1)) || !heights || !base_ ||
            base_->size() != size() || (light >= 0 && !shading_))
            throw std::invalid_argument("Invalid water image inputs");
        heights_.assign(heights,heights+width*height);
    }
};
}
