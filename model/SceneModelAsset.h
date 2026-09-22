// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "ModelPose.h"
#include "../renderer/Scene.h"
#include <cstddef>

// Asset compiler: owned topology and validated shading data, no game globals.
d3scene::ModelGeometry CompileSceneModelAsset(const poly_model&, size_t* repaired_normals = nullptr);
