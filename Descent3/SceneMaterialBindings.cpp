// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneMaterialBindings.h"
#include "gametexture.h"
#include "bitmap.h"
#include "renderer.h"
#include "config.h"
#include "game.h"
#include <cstring>
bool SceneFacingUsesSoftIntersection(const char* model_name,const char* texture_name) {
    // Preserve the authored Gyro lamp treatment at material compilation, not
    // as a renderer-side model-name branch.
    return Render_soft_vis_effects && model_name && texture_name && !stricmp(model_name,"gyro.OOF") &&
        (!stricmp(texture_name,"red flare1") || !stricmp(texture_name,"blue flare1"));
}
struct high_resolution_sky
{
	const char* texture_name;
	const char* bitmap_filename;
	int bitmap_handle;
	bool attempted_load;
};

static high_resolution_sky High_resolution_skies[] = {
	{"Arora", "SkyHi_Arora.tga", BAD_BITMAP_HANDLE, false},
	{"CC_Sky", "SkyHi_CC_Sky.tga", BAD_BITMAP_HANDLE, false},
	{"CloudMess05", "SkyHi_CloudMess05.tga", BAD_BITMAP_HANDLE, false},
	{"CloudySky", "SkyHi_CloudySky.tga", BAD_BITMAP_HANDLE, false},
	{"Crazy Sci-Fi", "CrazySciFi_512.tga", BAD_BITMAP_HANDLE, false},
	{"EarthKoreaSky", "SkyHi_EarthKorea.tga", BAD_BITMAP_HANDLE, false},
	{"EXP_256_Sky", "SkyHi_EXP256.tga", BAD_BITMAP_HANDLE, false},
	{"M02Sky", "SkyHi_M02Sky.tga", BAD_BITMAP_HANDLE, false},
	{"M_Sky3", "SkyHi_MSky3.tga", BAD_BITMAP_HANDLE, false},
	{"MarsClif9", "SkyHi_MarsClif9.tga", BAD_BITMAP_HANDLE, false},
	{"MarsSkyDome", "SkyHi_MarsSkyDome.tga", BAD_BITMAP_HANDLE, false},
	{"Milky_Way", "SkyHi_MilkyWay.tga", BAD_BITMAP_HANDLE, false},
	{"Overcast3", "SkyHi_Overcast3.tga", BAD_BITMAP_HANDLE, false},
	{"PhobosSky", "SkyHi_PhobosSky.tga", BAD_BITMAP_HANDLE, false},
	{"RedAcropolisSky", "SkyHi_RedAcropolis.tga", BAD_BITMAP_HANDLE, false},
	{"Sol_sky4", "SkyHi_SolSky4.tga", BAD_BITMAP_HANDLE, false},
	{"Titan Sky", "SkyHi_TitanSky.tga", BAD_BITMAP_HANDLE, false},
	{"Vergence_Neb", "SkyHi_VergenceNeb.tga", BAD_BITMAP_HANDLE, false},
	{"Volcanic2", "SkyHi_Volcanic2.tga", BAD_BITMAP_HANDLE, false},
};

int GetHighResolutionSkyBitmap(short texture_handle, int fallback_bitmap)
{
	if (!Render_hires_skies)
		return fallback_bitmap;

	const char* texture_name = GameTextures[texture_handle].name;
	for (auto& sky : High_resolution_skies)
	{
		if (strcmpi(texture_name, sky.texture_name) != 0)
			continue;

		if (!sky.attempted_load)
		{
			sky.attempted_load = true;
			sky.bitmap_handle = bm_AllocLoadFileBitmapTrueColor(sky.bitmap_filename, 1);
		}

		return sky.bitmap_handle != BAD_BITMAP_HANDLE ? sky.bitmap_handle : fallback_bitmap;
	}

	return fallback_bitmap;
}

bool SplitSpecularTexturePathEnabled()
{
	return (Render_split_specular_textures || Render_per_pixel_field_static_specular) &&
		Render_preferred_state.per_pixel_lighting &&
		UseHardware;
}

static SplitSpecularTexturePair ComputeSplitSpecularTexturePair(int tmap)
{
	SplitSpecularTexturePair pair = { -1, -1, -1, -1, false };
	if (tmap < 0 || tmap >= MAX_TEXTURES || !GameTextures[tmap].used)
		return pair;
	if (GameTextures[tmap].flags & (TF_ANIMATED | TF_PROCEDURAL))
		return pair;

	char base_name[PAGENAME_LEN];
	char spec_name[PAGENAME_LEN];
	strncpy(base_name, GameTextures[tmap].name, sizeof(base_name));
	base_name[sizeof(base_name) - 1] = '\0';
	strncpy(spec_name, GameTextures[tmap].name, sizeof(spec_name));
	spec_name[sizeof(spec_name) - 1] = '\0';

	const size_t name_len = strlen(GameTextures[tmap].name);
	if (name_len == 0)
		return pair;

	if (GameTextures[tmap].name[name_len - 1] == 'S' || GameTextures[tmap].name[name_len - 1] == 's')
	{
		base_name[name_len - 1] = '\0';
	}
	else
	{
		if (name_len + 1 >= sizeof(spec_name))
			return pair;
		spec_name[name_len] = 'S';
		spec_name[name_len + 1] = '\0';
	}

	int base_tmap = FindTextureName(base_name);
	int spec_tmap = FindTextureName(spec_name);
	if (base_tmap < 0 || spec_tmap < 0 || base_tmap == spec_tmap)
		return pair;
	if ((GameTextures[base_tmap].flags | GameTextures[spec_tmap].flags) & (TF_ANIMATED | TF_PROCEDURAL))
		return pair;

	int base_bm = GetTextureBitmap(base_tmap, 0);
	int spec_bm = GetTextureBitmap(spec_tmap, 0);
	if (base_bm < 0 || spec_bm < 0)
		return pair;
	if (bm_format(base_bm) != BITMAP_FORMAT_1555 || bm_format(spec_bm) != BITMAP_FORMAT_4444)
		return pair;

	pair.base_tmap = base_tmap;
	pair.spec_tmap = spec_tmap;
	pair.base_bm = base_bm;
	pair.spec_bm = spec_bm;
	pair.valid = true;
	return pair;
}

static SplitSpecularTexturePair Split_specular_cached_pairs[MAX_TEXTURES];
static ubyte Split_specular_cached_pair_valid[MAX_TEXTURES] = {};

void ResetSplitSpecularTexturePairCache()
{
	memset(Split_specular_cached_pairs, 0, sizeof(Split_specular_cached_pairs));
	memset(Split_specular_cached_pair_valid, 0, sizeof(Split_specular_cached_pair_valid));
}

SplitSpecularTexturePair FindSplitSpecularTexturePair(int tmap)
{
	SplitSpecularTexturePair empty_pair = { -1, -1, -1, -1, false };
	if (tmap < 0 || tmap >= MAX_TEXTURES)
		return empty_pair;

	if (!Split_specular_cached_pair_valid[tmap])
	{
		Split_specular_cached_pairs[tmap] = ComputeSplitSpecularTexturePair(tmap);
		Split_specular_cached_pair_valid[tmap] = 1;
	}

	return Split_specular_cached_pairs[tmap];
}
