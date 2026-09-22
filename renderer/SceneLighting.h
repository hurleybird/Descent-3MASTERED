// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
// Source effects are evaluated once. Surfaces retain indices into one emitter
// table; neither light identity nor fields are reconstructed from draw uniforms.
void prepareLightInfluences(FrameData&);
float lightAttenuation(const Light&, Vec3 world);
uint32_t visualNoise(double time, uint32_t key, uint32_t salt);
Vec3 sampleVolume(const VolumeGrid&,Vec3 position);
// Borrowed input for simulation adapters only; never retained in a presentation.
struct VolumeGridView {
    Vec3 origin;
    uint32_t width, height, depth;
    float spacing;
    const uint8_t* colors;
};
Vec3 sampleVolume(VolumeGridView,Vec3 position);
float roomLightScalar(float time, uint8_t pulse, uint8_t offset, uint32_t room, bool strobe, bool flicker, bool editor);
}
