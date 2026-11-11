// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace d3scene {
// Experimental room execution contract: four RGBA32F texels per face, addressed
// by geometry-owned allocation + local polygon ID, never publication order.
// The remaining draw constants still describe the compatible material/instance.
struct RoomSurfaceRecord {
    std::array<float,16> words{};
};
inline RoomSurfaceRecord splitRoomSurfaceRecord(float* draw,bool preserve_lighting_uniforms=false) {
    constexpr int fields[14] = {56,57,58,59,60,61,62,63,19,27,35,43,77,78};
    RoomSurfaceRecord result;
    for (std::size_t i=0;i<14;++i) {
        result.words[i]=draw[fields[i]];
        if (!preserve_lighting_uniforms || (i!=6 && i!=7 && i!=12 && i!=13)) draw[fields[i]]=0;
    }
    // The vertex stage now supplies transformed coordinates. Fragment-stage
    // transforms are identity, avoiding face-table fetches at pixel frequency.
    draw[56]=draw[57]=draw[60]=draw[61]=1;
    // The indexed light list replaces the draw's inline light shortcut.
    if (!preserve_lighting_uniforms) for (int i=96;i<100;++i) draw[i]=0;
    return result;
}
static_assert(sizeof(RoomSurfaceRecord)==64,"Room face table layout differs from shader");
}
