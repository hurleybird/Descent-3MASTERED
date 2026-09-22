// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
// Source effects are evaluated once. Surfaces retain indices into one emitter
// table; neither light identity nor fields are reconstructed from draw uniforms.
void prepareLightInfluences(FrameData&);
float lightAttenuation(const Light&, Vec3 world);
uint32_t visualNoise(double time, uint32_t key, uint32_t salt);
}
