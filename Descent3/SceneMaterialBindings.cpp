// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneMaterialBindings.h"
#include "gametexture.h"
#include "bitmap.h"
#include "renderer.h"
#include "config.h"
#include "game.h"
#include <cstring>

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
