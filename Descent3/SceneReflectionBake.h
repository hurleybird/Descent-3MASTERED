// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "special_face.h"
#include "room.h"
#include "renderer.h"
#include <vector>
#include "../renderer/Scene.h"
// Export a selected baked appearance as an owned scene asset. The GPU never
// reads the mutable bake intermediates below.
d3scene::ReflectionAssetRef PublishRoomReflectionBake(int room,int face,uint32_t slot,bool field,bool split);
// Game-side authored appearance compiler. These bake intermediates are not a
// renderer submission API; scene publication exports owned immutable assets.
struct PrecomputedSpecularSourceSet
{
	int count;
	specular_instance sources[MAX_SPECULARS];
	vector linear_colors[MAX_SPECULARS];
};

struct PrecomputedSpecularFaceSources
{
	PrecomputedSpecularSourceSet exact;
	PrecomputedSpecularSourceSet split;
	PrecomputedSpecularSourceSet field;
	std::vector<PrecomputedSpecularSourceSet> field_vertices;
	std::vector<vector> normals;
	std::vector<vector> lightmap_colors;
	d3scene::ReflectionAssetRef published_field, published_split;
};


extern std::vector<std::vector<PrecomputedSpecularFaceSources>> Precomputed_specular_sources;
extern int Precomputed_specular_highest_room;
void PrecomputeMineSpecularSources();
inline bool FaceHasSmoothSpecularNormals(face* fp) {
    return fp->special_handle != BAD_SPECIAL_FACE_INDEX &&
        (SpecialFaces[fp->special_handle].flags & SFF_SPEC_SMOOTH) &&
        SpecialFaces[fp->special_handle].vertnorms != nullptr;
}
