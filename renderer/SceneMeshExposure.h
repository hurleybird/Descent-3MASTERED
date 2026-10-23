// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
struct MeshExposure { Transform previous; bool valid = false; };
class MeshExposureHistory {
    AssetId asset_;
    uint64_t sequence_ = 0;
    Transform current_;
    MeshExposure exposure_;
    static bool same(Transform a,Transform b) {
        const auto v = [](Vec3 x,Vec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
        return v(a.position,b.position) && v(a.right,b.right) && v(a.up,b.up) && v(a.forward,b.forward);
    }
public:
    MeshExposure sample(AssetId asset,uint64_t sequence,uint64_t predecessor,Transform placement,bool advance) {
        if (!asset.lifetime || !sequence || predecessor >= sequence) throw std::invalid_argument("Invalid mesh exposure identity");
        if (asset.lifetime == asset_.lifetime && sequence == sequence_) {
            if (asset != asset_ || !same(placement,current_)) throw std::logic_error("Conflicting mesh exposure replay");
            return exposure_;
        }
        if (advance && asset.lifetime == asset_.lifetime && sequence < sequence_) throw std::logic_error("Mesh exposure reordered");
        MeshExposure result{placement,false};
        if (asset == asset_ && predecessor && sequence_ == predecessor) result = {current_,true};
        if (advance) { asset_ = asset; sequence_ = sequence; current_ = placement; exposure_ = result; }
        return result;
    }
};
}
