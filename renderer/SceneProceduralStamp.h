// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace d3scene {
// The authored radial disturbance is independent of position, tick and water
// state. Compile its exact integer additions; executing a stamp only clips,
// addresses and adds. A changed radius/height explicitly recompiles the kernel.
class ProceduralSineStamp {
    int radius_ = -1, height_ = -1;
    std::vector<int16_t> additions_;
public:
    template<class Cosine> void prepare(uint8_t radius,uint8_t height,Cosine cosine) {
        if (radius_ == radius && height_ == height) return;
        radius_ = -1; // a failed allocation must not publish a valid kernel
        additions_.resize(size_t(radius)*radius);
        if (radius) {
            // Keep the legacy float assignment and integer truncation order.
            const float length = (1024.0 / float(radius)) * (1024.0 / float(radius));
            for (size_t square = 0; square < additions_.size(); ++square) {
                const int distance = int(std::sqrt(float(square)*length));
                const int value = int(cosine(distance % 65536)*float(height));
                additions_[square] = int16_t(value/8);
            }
        }
        radius_ = radius; height_ = height;
    }
    void apply(int16_t* destination,int width,int height,int x,int y) const {
        if (radius_ <= 0) return;
        const int left = std::max(-radius_,1-x), right = std::min(radius_,width-1-x);
        const int top = std::max(-radius_,1-y), bottom = std::min(radius_,height-1-y);
        const int radius_square = radius_*radius_;
        for (int cy = top; cy < bottom; ++cy) for (int cx = left; cx < right; ++cx) {
            const int square = cy*cy+cx*cx;
            if (square < radius_square) {
                auto& value = destination[width*(cy+y)+cx+x];
                value = int16_t(value+additions_[square]);
            }
        }
    }
};
}
