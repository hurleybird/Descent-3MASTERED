// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"

namespace d3scene {
// Reference algebra for the scene MRT contract. `eligible` contains only the
// opaque surface's AO-eligible radiance after fog transmission and subsequent
// compositing. It is not alpha, fog coverage, or the final pixel brightness.
struct RadianceSample { Vec3 color, eligible; };
inline RadianceSample composeRadiance(RadianceSample destination, RadianceSample source,
    BlendMode blend, float alpha) {
    if (blend == BlendMode::Solid) return source;
    const auto channel = [&](float& color,float& eligible,float incoming) {
        if (blend == BlendMode::Alpha) { color = color*(1-alpha)+incoming*alpha; eligible *= 1-alpha; }
        else if (blend == BlendMode::Additive) color += incoming*alpha;
        else { color *= incoming; eligible *= incoming; } // source is the final fogged multiplier
    };
    channel(destination.color.x,destination.eligible.x,source.color.x);
    channel(destination.color.y,destination.eligible.y,source.color.y);
    channel(destination.color.z,destination.eligible.z,source.color.z);
    return destination;
}
inline Vec3 applyOcclusion(RadianceSample sample,float factor) {
    return {sample.color.x+(factor-1)*sample.eligible.x,
        sample.color.y+(factor-1)*sample.eligible.y,sample.color.z+(factor-1)*sample.eligible.z};
}
}
