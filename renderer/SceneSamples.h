// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <stdexcept>
#include <vector>
namespace d3scene {
// Depth/identity/motion are one surface record. Never average motion across
// objects and then pair it with an independently selected integer identity.
inline size_t nearestDepthSample(const std::vector<float>& depth) {
    if (depth.empty()) throw std::invalid_argument("Cannot resolve zero samples");
    size_t selected = 0;
    for (size_t i = 1; i < depth.size(); ++i) if (depth[i] < depth[selected]) selected = i;
    return selected;
}
constexpr const char* SampleResolveGlsl = R"GLSL(#version 450 core
int nearestDepthSample(sampler2DMS depths,ivec2 pixel) {
    int selected = 0;
    float nearest = texelFetch(depths,pixel,0).r;
    for (int i = 1; i < textureSamples(depths); ++i) {
        float candidate = texelFetch(depths,pixel,i).r;
        if (candidate < nearest) { selected = i; nearest = candidate; }
    }
    return selected;
}
)GLSL";
}
