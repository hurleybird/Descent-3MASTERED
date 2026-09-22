// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
d3scene::OcclusionClass SceneRoomOcclusionClass(int texture, bool exterior);
bool SceneFacingUsesSoftIntersection(const char* model_name,const char* texture_name);
bool SceneGlowUsesSoftIntersection(const char* model_name);
d3scene::Vec3 ScenePlayerColor(int index);
int GetHighResolutionSkyBitmap(short texture_handle, int fallback_bitmap);
struct SplitSpecularTexturePair
{
	int base_tmap;
	int spec_tmap;
	int base_bm;
	int spec_bm;
	bool valid;
};

bool SplitSpecularTexturePathEnabled();
void ResetSplitSpecularTexturePairCache();
SplitSpecularTexturePair FindSplitSpecularTexturePair(int tmap);
