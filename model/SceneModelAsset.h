// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "ModelPose.h"
#include "../renderer/Scene.h"
#include <cstddef>

enum class SceneModelPartRole { Hidden, Surface, Facing, Glow };
SceneModelPartRole ResolveSceneModelPartRole(uint32_t flags,uint32_t mask,size_t part,bool has_custom_texture);
std::vector<SceneModelPartRole> ResolveSceneModelRoles(const d3scene::ModelGeometry&,uint32_t mask,bool has_custom_texture);

// Asset compiler: owned topology and validated shading data, no game globals.
d3scene::ModelGeometry CompileSceneModelAsset(const poly_model&, size_t* repaired_normals = nullptr);
// Compare source semantics without rebuilding/allocating a candidate model.
bool MatchesSceneModelAsset(const d3scene::ModelGeometry&,const poly_model&);
