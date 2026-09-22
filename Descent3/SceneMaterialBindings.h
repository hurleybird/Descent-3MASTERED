// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
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
