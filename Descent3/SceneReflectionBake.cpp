// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneReflectionBake.h"
#include "SceneMaterialBindings.h"
#include "config.h"
#include "gametexture.h"
#include "bitmap.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

d3scene::ReflectionAssetRef PublishRoomReflectionBake(int room,int face,uint32_t slot,bool field,bool split) {
    if (!field && !split) return {};
    if (room < 0 || room >= static_cast<int>(Precomputed_specular_sources.size()) ||
        face < 0 || face >= static_cast<int>(Precomputed_specular_sources[room].size())) return {};
    auto& baked = Precomputed_specular_sources[room][face];
    auto& published = field ? baked.published_field : baked.published_split;
    if (published && published->id().lifetime == d3scene::assets().lifetime() && published->id().slot == slot)
        return published;
    d3scene::ReflectionData result;
    const auto value = [](const vector& v) { return d3scene::Vec3{v.x,v.y,v.z}; };
    if (field) {
        result.field_corners.resize(Rooms[room].faces[face].num_verts);
        for (size_t corner = 0; corner < result.field_corners.size(); ++corner) {
            if (corner < baked.field_vertices.size()) {
                const auto& source = baked.field_vertices[corner];
                for (int lobe = 0; lobe < source.count && lobe < 4; ++lobe)
                    result.field_corners[corner].lobes[lobe] = {value(source.sources[lobe].bright_center),value(source.linear_colors[lobe])};
            }
            if (corner < baked.lightmap_colors.size()) result.field_corners[corner].baked_modulation = value(baked.lightmap_colors[corner]);
        }
    } else if (split && baked.split.count) {
        for (int index = 0; index < baked.split.count; ++index) {
            const auto& source = baked.split.sources[index]; const auto color = source.bright_color;
            result.sources.push_back({value(source.bright_center),{float((color>>10)&31)/31.f,float((color>>5)&31)/31.f,float(color&31)/31.f}});
        }
    } else return {};
    for (const auto normal : baked.normals) result.corner_normals.push_back(value(normal));
    published = d3scene::assets().publishReflection(slot,std::move(result));
    return published;
}

struct SpecularFaceBasis
{
	vector center;
	vector normal;
	vector tangent;
	vector bitangent;
	bool valid;
};

struct SpecularFaceInfo
{
	int room_index;
	int face_index;
	int face_info_index;
	int exact_tmap;
	int split_tmap;
	SpecularFaceBasis basis;
	float field_weight;
};

struct SpecularFaceAdjacency
{
	int face_info_index;
	float base_cost;
};

struct SpecularQuantizedPosition
{
	int x;
	int y;
	int z;

	bool operator==(const SpecularQuantizedPosition& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}

	bool operator<(const SpecularQuantizedPosition& other) const
	{
		if (x != other.x)
			return x < other.x;
		if (y != other.y)
			return y < other.y;
		return z < other.z;
	}
};

struct SpecularEdgeKey
{
	SpecularQuantizedPosition a;
	SpecularQuantizedPosition b;

	bool operator==(const SpecularEdgeKey& other) const
	{
		return a == other.a && b == other.b;
	}
};

struct SpecularEdgeKeyHash
{
	size_t operator()(const SpecularEdgeKey& key) const
	{
		auto mix = [](size_t seed, int value) {
			return seed ^ ((size_t)(uint32_t)value + 0x9e3779b9u +
				(seed << 6) + (seed >> 2));
		};
		size_t result = 0;
		result = mix(result, key.a.x);
		result = mix(result, key.a.y);
		result = mix(result, key.a.z);
		result = mix(result, key.b.x);
		result = mix(result, key.b.y);
		result = mix(result, key.b.z);
		return result;
	}
};

struct SpecularQuantizedPositionHash
{
	size_t operator()(const SpecularQuantizedPosition& position) const
	{
		auto mix = [](size_t seed, int value) {
			return seed ^ ((size_t)(uint32_t)value + 0x9e3779b9u +
				(seed << 6) + (seed >> 2));
		};
		size_t result = 0;
		result = mix(result, position.x);
		result = mix(result, position.y);
		result = mix(result, position.z);
		return result;
	}
};

struct SpecularDonorFace
{
	int room_index;
	int face_index;
	int face_info_index;
	int exact_tmap;
	int split_tmap;
	SpecularFaceBasis basis;
	int source_count;
	specular_instance sources[MAX_SPECULARS];
};

struct SpecularFloodNode
{
	float cost;
	int face_info_index;

	bool operator<(const SpecularFloodNode& other) const
	{
		return cost > other.cost;
	}
};

struct SpecularResolvedDonor
{
	int donor_index;
	float cost;
	float weight;
};

struct SpecularFieldSample
{
	vector position;
	vector source_center;
	vector direction;
	vector normal;
	float distance;
	float r;
	float g;
	float b;
	float weight;
	int source_id;
};

struct SpecularFieldLobe
{
	vector position_sum;
	vector direction_sum;
	vector normal_sum;
	float distance_sum;
	float r_sum;
	float g_sum;
	float b_sum;
	float weight;
};

struct SpecularFieldCell
{
	SpecularFieldLobe lobes[MAX_SPECULARS];
};

struct SpecularSpatialField
{
	vector min_bound;
	vector max_bound;
	float cell_size;
	int x_cells;
	int y_cells;
	int z_cells;
	SpecularFieldCell global;
	std::vector<SpecularFieldSample> exact_samples;
	std::vector<vector> exact_source_centers;
	std::unordered_map<int, std::vector<int>> exact_sample_cells;
};

std::vector<std::vector<PrecomputedSpecularFaceSources>> Precomputed_specular_sources;
static uint64_t ReflectionBakeRevision = 0;
uint64_t SceneReflectionBakeRevision() { return ReflectionBakeRevision; }
int Precomputed_specular_highest_room = -1;

static vector SpecularFaceCenter(room* rp, face* fp)
{
	vector center = { 0, 0, 0 };
	for (int vn = 0; vn < fp->num_verts; vn++)
		center += rp->verts[fp->face_verts[vn]];
	if (fp->num_verts > 0)
		center /= (float)fp->num_verts;
	return center;
}

static int SplitSpecularSourceMatchTmap(int tmap)
{
	SplitSpecularTexturePair pair = FindSplitSpecularTexturePair(tmap);
	return pair.valid ? pair.base_tmap : tmap;
}

static bool SpecularTextureHasOwnOrSplitMask(face* fp)
{
	int bm_handle = GetTextureBitmap(fp->tmap, 0);
	if (bm_handle >= 0 && bm_format(bm_handle) == BITMAP_FORMAT_4444)
		return true;

	return FindSplitSpecularTexturePair(fp->tmap).valid;
}

static float SpecularClamp(float value, float low, float high)
{
	return std::max(low, std::min(value, high));
}

static SpecularQuantizedPosition SpecularQuantizePosition(const vector& position)
{
	constexpr float scale = 1000.0f;
	return {
		(int)lroundf(position.x * scale),
		(int)lroundf(position.y * scale),
		(int)lroundf(position.z * scale)
	};
}

static SpecularEdgeKey SpecularBuildEdgeKey(const vector& a, const vector& b)
{
	SpecularEdgeKey key = {
		SpecularQuantizePosition(a),
		SpecularQuantizePosition(b)
	};
	if (key.b < key.a)
		std::swap(key.a, key.b);
	return key;
}

static bool SpecularFaceCanReceivePpxSources(room* rp, face* fp)
{
	if (!rp->used || (rp->flags & RF_EXTERNAL))
		return false;
	if (fp->lmi_handle == BAD_LMI_INDEX || !(fp->flags & FF_LIGHTMAP))
		return false;
	if (!SpecularTextureHasOwnOrSplitMask(fp))
		return false;
	return true;
}

static vector SpecularAnyFaceTangent(room* rp, face* fp, const vector& normal)
{
	for (int vn = 1; vn < fp->num_verts; vn++)
	{
		vector tangent = rp->verts[fp->face_verts[vn]] - rp->verts[fp->face_verts[0]];
		tangent -= normal * (tangent * normal);
		if (vm_NormalizeVector(&tangent) > 0.0001f)
			return tangent;
	}

	vector tangent = { 1.0f, 0.0f, 0.0f };
	if (fabs(tangent * normal) > 0.95f)
		tangent = { 0.0f, 1.0f, 0.0f };
	tangent -= normal * (tangent * normal);
	vm_NormalizeVector(&tangent);
	return tangent;
}

static SpecularFaceBasis SpecularBuildFaceBasis(room* rp, face* fp)
{
	SpecularFaceBasis basis = {};
	basis.center = SpecularFaceCenter(rp, fp);
	basis.normal = fp->normal;
	if (vm_NormalizeVector(&basis.normal) <= 0.0001f)
		basis.normal = { 0.0f, 0.0f, 1.0f };

	basis.tangent = { 0.0f, 0.0f, 0.0f };
	vector uv_bitangent = { 0.0f, 0.0f, 0.0f };
	for (int vn = 2; vn < fp->num_verts; vn++)
	{
		const vector& p0 = rp->verts[fp->face_verts[0]];
		const vector& p1 = rp->verts[fp->face_verts[vn - 1]];
		const vector& p2 = rp->verts[fp->face_verts[vn]];
		vector edge1 = p1 - p0;
		vector edge2 = p2 - p0;
		const float du1 = fp->face_uvls[vn - 1].u - fp->face_uvls[0].u;
		const float dv1 = fp->face_uvls[vn - 1].v - fp->face_uvls[0].v;
		const float du2 = fp->face_uvls[vn].u - fp->face_uvls[0].u;
		const float dv2 = fp->face_uvls[vn].v - fp->face_uvls[0].v;
		const float determinant = du1 * dv2 - du2 * dv1;
		if (fabs(determinant) <= 0.000001f)
			continue;

		const float inv_det = 1.0f / determinant;
		basis.tangent = (edge1 * dv2 - edge2 * dv1) * inv_det;
		uv_bitangent = (edge2 * du1 - edge1 * du2) * inv_det;
		basis.tangent -= basis.normal * (basis.tangent * basis.normal);
		if (vm_NormalizeVector(&basis.tangent) > 0.0001f)
			break;
	}

	if (vm_GetMagnitude(&basis.tangent) <= 0.0001f)
		basis.tangent = SpecularAnyFaceTangent(rp, fp, basis.normal);

	basis.bitangent = basis.normal ^ basis.tangent;
	if (vm_NormalizeVector(&basis.bitangent) <= 0.0001f)
		basis.bitangent = { 0.0f, 1.0f, 0.0f };
	if (vm_GetMagnitude(&uv_bitangent) > 0.0001f && (basis.bitangent * uv_bitangent) < 0.0f)
		basis.bitangent *= -1.0f;

	basis.valid = true;
	return basis;
}

static int SpecularCopyFaceSources(face* fp, specular_instance* sources)
{
	if (fp->special_handle == BAD_SPECIAL_FACE_INDEX)
		return 0;

	int count = 0;
	special_face* sf = &SpecialFaces[fp->special_handle];
	for (int i = 0; i < sf->num && count < MAX_SPECULARS; i++)
	{
		if (sf->spec_instance[i].bright_color == 0)
			continue;
		sources[count++] = sf->spec_instance[i];
	}
	return count;
}

static bool SpecularFacesShareEdge(face* a, face* b)
{
	for (int ai = 0; ai < a->num_verts; ai++)
	{
		const int a0 = a->face_verts[ai];
		const int a1 = a->face_verts[(ai + 1) % a->num_verts];
		for (int bi = 0; bi < b->num_verts; bi++)
		{
			const int b0 = b->face_verts[bi];
			const int b1 = b->face_verts[(bi + 1) % b->num_verts];
			if ((a0 == b1 && a1 == b0) || (a0 == b0 && a1 == b1))
				return true;
		}
	}
	return false;
}

static bool SpecularFacesShareVertex(face* a, face* b)
{
	for (int ai = 0; ai < a->num_verts; ai++)
	{
		for (int bi = 0; bi < b->num_verts; bi++)
		{
			if (a->face_verts[ai] == b->face_verts[bi])
				return true;
		}
	}
	return false;
}

static void SpecularAddFaceAdjacency(std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	int a, int b, float base_cost)
{
	adjacency[a].push_back({ b, base_cost });
	adjacency[b].push_back({ a, base_cost });
}

static float SpecularFaceDistance(const SpecularFaceBasis& a, const SpecularFaceBasis& b)
{
	vector delta = a.center - b.center;
	return vm_GetMagnitude(&delta);
}

static float SpecularTransitionCost(const SpecularFaceInfo& from, const SpecularFaceInfo& to,
	const SpecularFaceAdjacency& edge)
{
	const float distance = SpecularFaceDistance(from.basis, to.basis);
	const float normal_dot = SpecularClamp(from.basis.normal * to.basis.normal, -1.0f, 1.0f);
	const float angle_penalty = (1.0f - std::max(normal_dot, 0.0f)) * 8.0f;
	return std::max(distance, 0.05f) * (1.0f + angle_penalty) + edge.base_cost;
}

static float SpecularDonorCompatibilityCost(const SpecularDonorFace& donor,
	const SpecularFaceInfo& target)
{
	const float distance = SpecularFaceDistance(donor.basis, target.basis);
	const float normal_dot = SpecularClamp(donor.basis.normal * target.basis.normal, -1.0f, 1.0f);
	if (normal_dot < -0.5f)
		return std::numeric_limits<float>::max();

	const float angle_penalty = (1.0f - std::max(normal_dot, 0.0f)) * 10.0f;
	return std::max(distance, 0.05f) * (1.0f + angle_penalty) + angle_penalty;
}

static vector SpecularTransformSourcePosition(const SpecularFaceBasis& donor_basis,
	const SpecularFaceBasis& target_basis, const vector& source_position)
{
	vector offset = source_position - donor_basis.center;
	const float tangent_offset = offset * donor_basis.tangent;
	const float bitangent_offset = offset * donor_basis.bitangent;
	const float normal_offset = offset * donor_basis.normal;

	vector transformed = target_basis.center +
		target_basis.tangent * tangent_offset +
		target_basis.bitangent * bitangent_offset +
		target_basis.normal * normal_offset;

	const float normal_dot = SpecularClamp(donor_basis.normal * target_basis.normal, -1.0f, 1.0f);
	if (normal_dot > 0.985f)
		return source_position;
	if (normal_dot > 0.8f)
	{
		const float transformed_weight = (0.985f - normal_dot) / (0.985f - 0.8f);
		return source_position * (1.0f - transformed_weight) + transformed * transformed_weight;
	}

	return transformed;
}

static void SpecularUnpackColor(ushort color, float& r, float& g, float& b)
{
	r = (float)((color >> 10) & 0x1f) / 31.0f;
	g = (float)((color >> 5) & 0x1f) / 31.0f;
	b = (float)(color & 0x1f) / 31.0f;
}

static ushort SpecularPackColor(float r, float g, float b)
{
	const int ri = (int)(SpecularClamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
	const int gi = (int)(SpecularClamp(g, 0.0f, 1.0f) * 31.0f + 0.5f);
	const int bi = (int)(SpecularClamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
	return (ushort)((ri << 10) | (gi << 5) | bi);
}

static float SpecularFaceArea(room* rp, face* fp)
{
	float area = 0.0f;
	if (fp->num_verts < 3)
		return 1.0f;

	const vector& p0 = rp->verts[fp->face_verts[0]];
	for (int vn = 2; vn < fp->num_verts; vn++)
	{
		vector edge1 = rp->verts[fp->face_verts[vn - 1]] - p0;
		vector edge2 = rp->verts[fp->face_verts[vn]] - p0;
		vector cross = edge1 ^ edge2;
		area += vm_GetMagnitude(&cross) * 0.5f;
	}
	return std::max(area, 1.0f);
}

static vector SpecularFieldNormalizeOr(const vector& value, const vector& fallback)
{
	vector result = value;
	if (vm_NormalizeVector(&result) <= 0.0001f)
		return fallback;
	return result;
}

static int SpecularFieldIndex(const SpecularSpatialField& field, int x, int y, int z)
{
	return (z * field.y_cells + y) * field.x_cells + x;
}

static int SpecularFieldClampCell(int value, int max_value)
{
	if (value < 0)
		return 0;
	if (value >= max_value)
		return max_value - 1;
	return value;
}

static void SpecularFieldCellForPosition(const SpecularSpatialField& field, const vector& position,
	int& x, int& y, int& z)
{
	x = SpecularFieldClampCell((int)floor((position.x - field.min_bound.x) / field.cell_size), field.x_cells);
	y = SpecularFieldClampCell((int)floor((position.y - field.min_bound.y) / field.cell_size), field.y_cells);
	z = SpecularFieldClampCell((int)floor((position.z - field.min_bound.z) / field.cell_size), field.z_cells);
}

static float SpecularFieldSmoothKernel(float distance, float radius)
{
	if (radius <= 0.0f)
		return 0.0f;
	const float t = SpecularClamp(distance / radius, 0.0f, 1.0f);
	const float smooth = t * t * (3.0f - 2.0f * t);
	const float weight = 1.0f - smooth;
	return weight * weight;
}

static void SpecularFieldAddWeightedSample(SpecularFieldCell& cell, const SpecularFieldSample& sample,
	float weight)
{
	if (weight <= 0.0f)
		return;

	int best_lobe = -1;
	int weakest_lobe = 0;
	float best_score = -10.0f;
	float weakest_weight = cell.lobes[0].weight;
	for (int i = 0; i < MAX_SPECULARS; i++)
	{
		SpecularFieldLobe& lobe = cell.lobes[i];
		if (lobe.weight <= 0.0f)
		{
			best_lobe = i;
			break;
		}

		vector lobe_direction = SpecularFieldNormalizeOr(lobe.direction_sum, sample.direction);
		vector lobe_normal = SpecularFieldNormalizeOr(lobe.normal_sum, sample.normal);
		const float direction_dot = SpecularClamp(lobe_direction * sample.direction, -1.0f, 1.0f);
		const float normal_dot = SpecularClamp(lobe_normal * sample.normal, -1.0f, 1.0f);
		const float score = direction_dot * 0.75f + normal_dot * 0.25f;
		if (score > best_score)
		{
			best_score = score;
			best_lobe = i;
		}
		if (lobe.weight < weakest_weight)
		{
			weakest_weight = lobe.weight;
			weakest_lobe = i;
		}
	}

	if (best_lobe < 0)
		best_lobe = weakest_lobe;
	if (best_score < 0.25f && cell.lobes[best_lobe].weight > 0.0f && weight > weakest_weight)
	{
		best_lobe = weakest_lobe;
		cell.lobes[best_lobe] = {};
	}

	SpecularFieldLobe& lobe = cell.lobes[best_lobe];
	lobe.position_sum += sample.position * weight;
	lobe.direction_sum += sample.direction * weight;
	lobe.normal_sum += sample.normal * weight;
	lobe.distance_sum += sample.distance * weight;
	lobe.r_sum += sample.r * weight;
	lobe.g_sum += sample.g * weight;
	lobe.b_sum += sample.b * weight;
	lobe.weight += weight;
}

static void SpecularBuildSpatialField(SpecularSpatialField& field, const std::vector<SpecularFaceInfo>& faces,
	const std::vector<SpecularFieldSample>& samples)
{
	field = {};
	if (faces.empty())
		return;

	field.min_bound = faces[0].basis.center;
	field.max_bound = faces[0].basis.center;
	for (const SpecularFaceInfo& info : faces)
	{
		const vector& center = info.basis.center;
		field.min_bound.x = std::min(field.min_bound.x, center.x);
		field.min_bound.y = std::min(field.min_bound.y, center.y);
		field.min_bound.z = std::min(field.min_bound.z, center.z);
		field.max_bound.x = std::max(field.max_bound.x, center.x);
		field.max_bound.y = std::max(field.max_bound.y, center.y);
		field.max_bound.z = std::max(field.max_bound.z, center.z);
	}

	vector extent = field.max_bound - field.min_bound;
	const float max_extent = std::max(extent.x, std::max(extent.y, extent.z));
	const float field_resolution =
		ConfigNormalizePerPixelSpecularFieldResolution(Render_per_pixel_specular_field_resolution);
	field.cell_size = SpecularClamp(max_extent / field_resolution, 6.0f, 120.0f);
	if (field.cell_size <= 0.0f)
		field.cell_size = 20.0f;

	const int max_axis_cells = 256;

	field.x_cells = SpecularFieldClampCell((int)ceil(std::max(extent.x, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.y_cells = SpecularFieldClampCell((int)ceil(std::max(extent.y, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.z_cells = SpecularFieldClampCell((int)ceil(std::max(extent.z, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.min_bound.x -= field.cell_size;
	field.min_bound.y -= field.cell_size;
	field.min_bound.z -= field.cell_size;
	field.x_cells = std::max(1, std::min(field.x_cells + 2, max_axis_cells));
	field.y_cells = std::max(1, std::min(field.y_cells + 2, max_axis_cells));
	field.z_cells = std::max(1, std::min(field.z_cells + 2, max_axis_cells));
	field.exact_samples = samples;
	field.exact_source_centers.clear();
	field.exact_sample_cells.clear();
	field.exact_sample_cells.reserve(samples.size());

	// Keep four stable directional families as compact shader channels. Unlike
	// the old path, these families no longer reconstruct an averaged world-space
	// light; they only organize exact authored centers and their smooth energy.
	for (const SpecularFieldSample& sample : samples)
		SpecularFieldAddWeightedSample(field.global, sample, sample.weight);

	// An authored bright center is a position, not a directional hint. Preserve
	// that identity exactly and let only its influence vary over the receiving
	// surface. Quantization merges repeated serialized copies of the same center
	// without averaging distinct nearby lights into a moving virtual source.
	std::unordered_map<SpecularQuantizedPosition, int,
		SpecularQuantizedPositionHash> source_lookup;
	source_lookup.reserve(samples.size());
	for (int sample_index = 0; sample_index < (int)field.exact_samples.size(); sample_index++)
	{
		SpecularFieldSample& sample = field.exact_samples[sample_index];
		const SpecularQuantizedPosition key =
			SpecularQuantizePosition(sample.source_center);
		auto result = source_lookup.emplace(key,
			(int)field.exact_source_centers.size());
		if (result.second)
			field.exact_source_centers.push_back(sample.source_center);
		sample.source_id = result.first->second;

		int x, y, z;
		SpecularFieldCellForPosition(field, sample.position, x, y, z);
		field.exact_sample_cells[SpecularFieldIndex(field, x, y, z)].
			push_back(sample_index);
	}
}

struct SpecularFieldInfluence
{
	int source_id;
	vector color_sum;
	float color_weight;
	float influence;
	vector linear_color;
	vector reflected_sum;
	float reflected_weight;
	vector reflected_direction;
};

struct SpecularFieldCandidate
{
	int sample_index;
	float distance;
	float normal_weight;
};

static std::vector<SpecularFieldInfluence> SpecularEvaluateFieldInfluences(
	const SpecularSpatialField& field, const vector& target_position,
	const vector& target_normal)
{
	std::vector<SpecularFieldInfluence> influences;
	if (field.exact_samples.empty() || field.exact_source_centers.empty())
		return influences;

	std::unordered_map<int, int> influence_lookup;
	const float local_radius =
		ConfigNormalizePerPixelSpecularFieldSampleDistance(
			Render_per_pixel_specular_field_sample_distance);
	const float query_radius = local_radius * 3.0f;
	std::vector<SpecularFieldCandidate> candidates;
	float local_coverage = 0.0f;
	int cx, cy, cz;
	SpecularFieldCellForPosition(field, target_position, cx, cy, cz);
	const int query_radius_cells = std::max(1,
		std::min(24, (int)ceil(query_radius /
			std::max(field.cell_size, 0.001f))));

	for (int z = std::max(0, cz - query_radius_cells);
		z <= std::min(field.z_cells - 1, cz + query_radius_cells); z++)
	{
		for (int y = std::max(0, cy - query_radius_cells);
			y <= std::min(field.y_cells - 1, cy + query_radius_cells); y++)
		{
			for (int x = std::max(0, cx - query_radius_cells);
				x <= std::min(field.x_cells - 1, cx + query_radius_cells); x++)
			{
				auto cell_iter = field.exact_sample_cells.find(
					SpecularFieldIndex(field, x, y, z));
				if (cell_iter == field.exact_sample_cells.end())
					continue;

				for (int sample_index : cell_iter->second)
				{
					const SpecularFieldSample& sample =
						field.exact_samples[sample_index];
					vector delta = sample.position - target_position;
					const float distance = vm_GetMagnitude(&delta);
					if (distance >= query_radius)
						continue;

					const float normal_dot = SpecularClamp(
						sample.normal * target_normal, -1.0f, 1.0f);
					if (normal_dot < -0.2f)
						continue;
					const float normal_weight =
						(0.12f + std::max(normal_dot, 0.0f) *
							std::max(normal_dot, 0.0f)) / 1.12f;
					candidates.push_back(
						{ sample_index, distance, normal_weight });
					local_coverage = std::max(local_coverage,
						SpecularFieldSmoothKernel(distance, local_radius) *
						normal_weight);
				}
			}
		}
	}

	// Prefer the authored neighborhood. Expansion is only a continuous
	// reconstruction across holes in that neighborhood, not a permanently
	// broader light that can wash unrelated geometry into the same lobe.
	const float coverage_t = SpecularClamp(
		(local_coverage - 0.15f) / 0.50f, 0.0f, 1.0f);
	const float coverage_smooth =
		coverage_t * coverage_t * (3.0f - 2.0f * coverage_t);
	const float expansion = 1.0f - coverage_smooth;
	for (const SpecularFieldCandidate& candidate : candidates)
	{
		const SpecularFieldSample& sample =
			field.exact_samples[candidate.sample_index];
		const float local_weight =
			SpecularFieldSmoothKernel(candidate.distance, local_radius);
		const float expanded_weight =
			SpecularFieldSmoothKernel(candidate.distance, query_radius);
		const float spatial_weight =
			local_weight + (expanded_weight - local_weight) * expansion;
		const float influence = spatial_weight * candidate.normal_weight;
		if (influence <= 0.0001f)
			continue;

		int influence_index;
		auto result = influence_lookup.emplace(sample.source_id,
			(int)influences.size());
		if (result.second)
		{
			SpecularFieldInfluence added = {};
			added.source_id = sample.source_id;
			influences.push_back(added);
		}
		influence_index = result.first->second;
		SpecularFieldInfluence& output = influences[influence_index];
		const float color_weight =
			influence * std::max(sample.weight, 0.0001f);
		output.color_sum +=
			vector{ sample.r, sample.g, sample.b } * color_weight;
		output.color_weight += color_weight;
		output.influence = std::max(output.influence, influence);

		vector donor_incident = sample.position - sample.source_center;
		if (vm_NormalizeVector(&donor_incident) > 0.0001f)
		{
			const float normal_dot = donor_incident * sample.normal;
			vector donor_reflected =
				donor_incident - sample.normal * (2.0f * normal_dot);
			if (vm_NormalizeVector(&donor_reflected) > 0.0001f)
			{
				output.reflected_sum += donor_reflected * color_weight;
				output.reflected_weight += color_weight;
			}
		}
	}

	for (SpecularFieldInfluence& influence : influences)
	{
		const vector average_color = influence.color_weight > 0.0f ?
			influence.color_sum / influence.color_weight :
			vector{ 0.0f, 0.0f, 0.0f };
		influence.linear_color =
			average_color * sqrt(influence.influence);
		influence.reflected_direction = influence.reflected_sum;
		if (vm_NormalizeVector(&influence.reflected_direction) <= 0.0001f)
			influence.reflected_direction = { 0.0f, 0.0f, 0.0f };
	}
	std::sort(influences.begin(), influences.end(),
		[](const SpecularFieldInfluence& a,
			const SpecularFieldInfluence& b) {
			return a.source_id < b.source_id;
		});
	return influences;
}

static void SpecularBuildFieldSourceSet(PrecomputedSpecularSourceSet& set,
	const SpecularFaceInfo& target, const SpecularSpatialField& field,
	const vector& target_position, const vector& target_normal)
{
	(void)target;
	set = {};

	const std::vector<SpecularFieldInfluence> influences =
		SpecularEvaluateFieldInfluences(field, target_position, target_normal);
	if (influences.empty())
		return;

	// Four stable directional bases are the compact representation consumed by
	// the existing shader. Classify each exact source from the receiving point,
	// not permanently from its authored face: a center's incident direction
	// changes as it propagates around curved geometry.
	vector reference_directions[MAX_SPECULARS] = {};
	bool reference_valid[MAX_SPECULARS] = {};
	for (int family = 0; family < MAX_SPECULARS; family++)
	{
		const SpecularFieldLobe& reference = field.global.lobes[family];
		if (reference.weight <= 0.0f)
			continue;
		reference_directions[family] =
			SpecularFieldNormalizeOr(reference.direction_sum,
				vector{ 0.0f, 0.0f, 1.0f });
		reference_valid[family] = true;
	}

	vector family_colors[MAX_SPECULARS] = {};
	vector family_center_sums[MAX_SPECULARS] = {};
	vector incident_sums[MAX_SPECULARS] = {};
	float family_center_weights[MAX_SPECULARS] = {};
	float incident_weights[MAX_SPECULARS] = {};
	float family_weights[MAX_SPECULARS] = {};
	for (const SpecularFieldInfluence& influence : influences)
	{
		vector incident =
			target_position - field.exact_source_centers[influence.source_id];
		if (vm_NormalizeVector(&incident) <= 0.0001f)
			continue;
		const vector reflected = influence.reflected_direction;
		if ((reflected * reflected) <= 0.00000001f)
			continue;

		float assignment[MAX_SPECULARS] = {};
		float maximum_dot = -1.0f;
		for (int family = 0; family < MAX_SPECULARS; family++)
		{
			if (!reference_valid[family])
				continue;
			assignment[family] = reflected * reference_directions[family];
			maximum_dot = std::max(maximum_dot, assignment[family]);
		}
		float assignment_sum = 0.0f;
		for (int family = 0; family < MAX_SPECULARS; family++)
		{
			if (!reference_valid[family])
				continue;
			// A soft angular partition keeps family identities stable while
			// avoiding discrete source-slot changes at polygon boundaries.
			assignment[family] =
				exp((assignment[family] - maximum_dot) * 32.0f);
			assignment_sum += assignment[family];
		}
		if (assignment_sum <= 0.0001f)
			continue;

		const float direction_weight =
			std::max((influence.linear_color.x +
				influence.linear_color.y +
				influence.linear_color.z) / 3.0f, 0.0f);
		const float kernel_weight = sqrt(influence.influence);
		for (int family = 0; family < MAX_SPECULARS; family++)
		{
			if (!reference_valid[family])
				continue;
			const float family_assignment =
				assignment[family] / assignment_sum;
			family_colors[family] +=
				influence.linear_color * family_assignment;
			family_weights[family] +=
				kernel_weight * family_assignment;
			const float center_weight =
				direction_weight * family_assignment;
			family_center_sums[family] +=
				(target_position + reflected * 100.0f) * center_weight;
			family_center_weights[family] += center_weight;
			incident_sums[family] +=
				reflected * center_weight;
			incident_weights[family] +=
				center_weight;
		}
	}

	// Preserve point-light parallax by storing each family's smoothly weighted
	// authored center in world space. A per-vertex unit virtual segment turns a
	// fixed authored light into a directional lobe attached to the surface; as
	// the camera moves, that reads as a spotlight emitted by the viewer.
	float total_family_weight = 0.0f;
	for (float family_weight : family_weights)
		total_family_weight += std::max(family_weight, 0.0f);
	set.count = MAX_SPECULARS;
	for (int family = 0; family < MAX_SPECULARS; family++)
	{
		if (incident_weights[family] <= 0.0001f ||
			family_center_weights[family] <= 0.0001f)
			continue;
		const float incident_magnitude =
			vm_GetMagnitude(&incident_sums[family]);
		if (incident_magnitude <= 0.0001f)
			continue;
		const float concentration = SpecularClamp(
			incident_magnitude / incident_weights[family], 0.0f, 1.0f);
		incident_sums[family] /= incident_magnitude;

		// One shader lobe represents this complete directional family. Average
		// overlapping authored members instead of multiplying their energy;
		// max(..., 1) also provides a smooth fade where the field runs out.
		vector family_color =
			family_colors[family] / std::max(family_weights[family], 1.0f);
		// The four compact channels share one authored energy budget. Giving
		// every family full strength concentrates overlapping directions into a
		// camera-facing spotlight; normalize them as one lobe set instead.
		const float family_strength = total_family_weight > 0.0001f ?
			sqrt(SpecularClamp(family_weights[family] /
				total_family_weight, 0.0f, 1.0f)) : 0.0f;
		// A single mean direction otherwise puts the full energy of several
		// separated authored directions into one Phong peak. Their mean-resultant
		// length estimates that angular spread. This attenuation belongs to the
		// reconstructed light field and must be material-independent: material
		// shininess is applied later by the fragment BRDF. Baking it here made
		// adjacent materials observe different light colors at a shared point.
		constexpr float field_concentration_exponent = 6.0f;
		family_color *= family_strength *
			pow(concentration, field_concentration_exponent);
		// The legacy per-vertex path evaluates independently colored authored
		// sources before interpolation. Compacting several sources into one
		// continuous family preserves energy but partially averages their hue.
		// Restore some of that authored colorfulness without increasing luma.
		const float family_luminance =
			family_color.x * 0.2126f +
			family_color.y * 0.7152f +
			family_color.z * 0.0722f;
		const vector family_gray = {
			family_luminance, family_luminance, family_luminance
		};
		family_color =
			family_gray + (family_color - family_gray) * 1.6f;
		family_color.x = std::max(family_color.x, 0.0f);
		family_color.y = std::max(family_color.y, 0.0f);
		family_color.z = std::max(family_color.z, 0.0f);
		family_color.x = std::min(family_color.x, 1.0f);
		family_color.y = std::min(family_color.y, 1.0f);
		family_color.z = std::min(family_color.z, 1.0f);
		set.linear_colors[family] = family_color;
		set.sources[family].bright_center =
			family_center_sums[family] / family_center_weights[family];
		set.sources[family].bright_color = SpecularPackColor(
			family_color.x, family_color.y, family_color.z);
	}
}

static void SpecularRunSourceFlood(const std::vector<SpecularFaceInfo>& faces,
	const std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	const std::vector<SpecularDonorFace>& donors, bool split_match,
	std::vector<float>& costs, std::vector<int>& donor_indices)
{
	costs.assign(faces.size(), std::numeric_limits<float>::max());
	donor_indices.assign(faces.size(), -1);

	std::priority_queue<SpecularFloodNode> queue;
	for (int donor_index = 0; donor_index < (int)donors.size(); donor_index++)
	{
		const SpecularDonorFace& donor = donors[donor_index];
		const int face_info_index = donor.face_info_index;
		if (face_info_index < 0 || face_info_index >= (int)faces.size())
			continue;

		costs[face_info_index] = 0.0f;
		donor_indices[face_info_index] = donor_index;
		queue.push({ 0.0f, face_info_index });
	}

	while (!queue.empty())
	{
		const SpecularFloodNode node = queue.top();
		queue.pop();
		if (node.cost != costs[node.face_info_index])
			continue;

		const SpecularFaceInfo& current = faces[node.face_info_index];
		const int current_family = split_match ? current.split_tmap : current.exact_tmap;
		const int source_donor = donor_indices[node.face_info_index];
		if (source_donor < 0)
			continue;

		for (const SpecularFaceAdjacency& edge : adjacency[node.face_info_index])
		{
			const SpecularFaceInfo& next = faces[edge.face_info_index];
			const int next_family = split_match ? next.split_tmap : next.exact_tmap;
			if (next_family != current_family)
				continue;

			const float next_cost = node.cost + SpecularTransitionCost(current, next, edge);
			if (next_cost >= costs[edge.face_info_index])
				continue;

			costs[edge.face_info_index] = next_cost;
			donor_indices[edge.face_info_index] = source_donor;
			queue.push({ next_cost, edge.face_info_index });
		}
	}
}

static bool SpecularResolvedDonorAlreadySelected(const std::vector<SpecularResolvedDonor>& selected,
	int donor_index)
{
	for (const SpecularResolvedDonor& donor : selected)
	{
		if (donor.donor_index == donor_index)
			return true;
	}
	return false;
}

static bool SpecularResolvedDonorIsDiverse(const std::vector<SpecularResolvedDonor>& selected,
	const std::vector<SpecularDonorFace>& donors, const SpecularDonorFace& candidate,
	const SpecularFaceInfo& target)
{
	if (selected.empty())
		return true;

	vector target_delta = candidate.basis.center - target.basis.center;
	const float target_distance = std::max(vm_GetMagnitude(&target_delta), 1.0f);
	for (const SpecularResolvedDonor& selected_donor : selected)
	{
		vector donor_delta = candidate.basis.center - donors[selected_donor.donor_index].basis.center;
		if (vm_GetMagnitude(&donor_delta) > target_distance * 0.65f)
			return true;
	}
	return false;
}

static void SpecularSelectResolvedDonors(const SpecularFaceInfo& target,
	const std::vector<SpecularDonorFace>& donors, int family, bool split_match,
	int flood_donor, float flood_cost, std::vector<SpecularResolvedDonor>& selected)
{
	selected.clear();
	if (flood_donor >= 0 && flood_donor < (int)donors.size())
	{
		const float cost = std::max(flood_cost, SpecularDonorCompatibilityCost(donors[flood_donor], target));
		selected.push_back({ flood_donor, cost, 1.0f / (1.0f + cost) });
	}

	std::vector<SpecularResolvedDonor> candidates;
	for (int donor_index = 0; donor_index < (int)donors.size(); donor_index++)
	{
		const SpecularDonorFace& donor = donors[donor_index];
		if ((split_match ? donor.split_tmap : donor.exact_tmap) != family)
			continue;
		if (donor.room_index == target.room_index && donor.face_index == target.face_index)
			continue;
		if (SpecularResolvedDonorAlreadySelected(selected, donor_index))
			continue;

		const float cost = SpecularDonorCompatibilityCost(donor, target);
		if (cost == std::numeric_limits<float>::max())
			continue;
		candidates.push_back({ donor_index, cost, 1.0f / (1.0f + cost) });
	}

	std::sort(candidates.begin(), candidates.end(),
		[](const SpecularResolvedDonor& a, const SpecularResolvedDonor& b) {
			return a.cost < b.cost;
		});

	const float best_cost = !selected.empty() ? selected[0].cost :
		(!candidates.empty() ? candidates[0].cost : std::numeric_limits<float>::max());
	for (const SpecularResolvedDonor& candidate : candidates)
	{
		if (selected.size() >= 3)
			break;
		if (candidate.cost > best_cost * 2.25f + 4.0f)
			break;
		if (!SpecularResolvedDonorIsDiverse(selected, donors, donors[candidate.donor_index], target) &&
			!selected.empty() && candidate.cost >= selected[0].cost)
			continue;
		selected.push_back(candidate);
	}
}

static void SpecularBuildResolvedSourceSet(PrecomputedSpecularSourceSet& set,
	const SpecularFaceInfo& target, const std::vector<SpecularDonorFace>& donors,
	const std::vector<SpecularResolvedDonor>& selected)
{
	set.count = 0;
	memset(set.sources, 0, sizeof(set.sources));
	if (selected.empty())
		return;

	for (int source_slot = 0; source_slot < MAX_SPECULARS; source_slot++)
	{
		vector center = { 0.0f, 0.0f, 0.0f };
		float r = 0.0f, g = 0.0f, b = 0.0f;
		float total_weight = 0.0f;

		for (const SpecularResolvedDonor& selected_donor : selected)
		{
			const SpecularDonorFace& donor = donors[selected_donor.donor_index];
			if (source_slot >= donor.source_count)
				continue;

			const specular_instance& source = donor.sources[source_slot];
			if (source.bright_color == 0)
				continue;

			const vector transformed_center =
				SpecularTransformSourcePosition(donor.basis, target.basis, source.bright_center);
			const float weight = selected_donor.weight;
			center += transformed_center * weight;

			float sr, sg, sb;
			SpecularUnpackColor(source.bright_color, sr, sg, sb);
			r += sr * weight;
			g += sg * weight;
			b += sb * weight;
			total_weight += weight;
		}

		if (total_weight <= 0.0f)
			continue;

		specular_instance resolved = {};
		resolved.bright_center = center / total_weight;
		resolved.bright_color = SpecularPackColor(r / total_weight, g / total_weight, b / total_weight);
		if (resolved.bright_color != 0 && set.count < MAX_SPECULARS)
			set.sources[set.count++] = resolved;
	}
}

static int SpecularFindFaceCorner(face* fp, int vertex_index)
{
	for (int corner = 0; corner < fp->num_verts; corner++)
	{
		if (fp->face_verts[corner] == vertex_index)
			return corner;
	}
	return -1;
}

static int SpecularFindFaceCornerAtPosition(room* rp, face* fp,
	const vector& position)
{
	constexpr float position_epsilon_squared = 0.000001f;
	for (int corner = 0; corner < fp->num_verts; corner++)
	{
		vector delta = rp->verts[fp->face_verts[corner]] - position;
		if ((delta * delta) <= position_epsilon_squared)
			return corner;
	}
	return -1;
}

static vector SpecularFaceCornerNormal(const SpecularFaceInfo& info, int corner)
{
	face* fp = &Rooms[info.room_index].faces[info.face_index];
	vector normal = info.basis.normal;
	if (corner >= 0 && FaceHasSmoothSpecularNormals(fp))
		normal = SpecialFaces[fp->special_handle].vertnorms[corner];
	if (vm_NormalizeVector(&normal) <= 0.0001f)
		normal = info.basis.normal;
	return normal;
}

static float SpecularFaceCornerAngle(room* rp, face* fp, int corner)
{
	if (corner < 0 || fp->num_verts < 3)
		return 1.0f;

	const vector& center = rp->verts[fp->face_verts[corner]];
	vector previous = rp->verts[fp->face_verts[
		(corner + fp->num_verts - 1) % fp->num_verts]] - center;
	vector next = rp->verts[fp->face_verts[(corner + 1) % fp->num_verts]] - center;
	if (vm_NormalizeVector(&previous) <= 0.0001f ||
		vm_NormalizeVector(&next) <= 0.0001f)
	{
		return 1.0f;
	}

	const float dot = SpecularClamp(previous * next, -1.0f, 1.0f);
	return std::max(acosf(dot), 0.01f);
}

static vector SpecularFaceCornerLightmapColor(face* fp, int corner)
{
	vector color = { 1.0f, 1.0f, 1.0f };
	if (!fp || corner < 0 || corner >= fp->num_verts ||
		fp->lmi_handle == BAD_LMI_INDEX)
	{
		return color;
	}

	const int lightmap_handle = LightmapInfo[fp->lmi_handle].lm_handle;
	const int width = lm_w(lightmap_handle);
	const int height = lm_h(lightmap_handle);
	const ushort* data = lm_data(lightmap_handle);
	if (!data || width <= 0 || height <= 0)
		return color;

	const int x = std::max(0, std::min(width - 1,
		(int)(fp->face_uvls[corner].u2 * (float)width)));
	const int y = std::max(0, std::min(height - 1,
		(int)(fp->face_uvls[corner].v2 * (float)height)));
	const ushort texel = data[y * width + x];
	color.x = (float)((texel >> 10) & 0x1f) / 31.0f;
	color.y = (float)((texel >> 5) & 0x1f) / 31.0f;
	color.z = (float)(texel & 0x1f) / 31.0f;
	return color;
}

static void SpecularBuildResolvedNormalsForFace(PrecomputedSpecularFaceSources& cached,
	const SpecularFaceInfo& target, const std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	const std::vector<SpecularFaceInfo>& faces)
{
	room* rp = &Rooms[target.room_index];
	face* fp = &rp->faces[target.face_index];
	cached.normals.clear();
	cached.normals.resize(fp->num_verts);
	cached.lightmap_colors.clear();
	cached.lightmap_colors.resize(fp->num_verts);

	for (int vn = 0; vn < fp->num_verts; vn++)
	{
		const int vert_index = fp->face_verts[vn];
		const vector& vert_position = rp->verts[vert_index];
		std::vector<int> component;
		std::vector<int> pending;
		component.push_back(target.face_info_index);
		pending.push_back(target.face_info_index);
		for (size_t pending_index = 0; pending_index < pending.size(); pending_index++)
		{
			const int current_index = pending[pending_index];
			const SpecularFaceInfo& current = faces[current_index];
			for (const SpecularFaceAdjacency& edge : adjacency[current_index])
			{
				// Vertex-only contact is not a continuous surface. Restrict smoothing
				// to faces joined by an edge and in the same geometric crease
				// component. Material and UV seams do not make a smooth surface
				// geometrically discontinuous.
				if (edge.base_cost > 0.0f)
					continue;
				const SpecularFaceInfo& neighbor = faces[edge.face_info_index];
				room* neighbor_room = &Rooms[neighbor.room_index];
				face* neighbor_face =
					&neighbor_room->faces[neighbor.face_index];
				int neighbor_corner = -1;
				if (neighbor.room_index == target.room_index)
					neighbor_corner =
						SpecularFindFaceCorner(neighbor_face, vert_index);
				if (neighbor_corner < 0)
				{
					neighbor_corner = SpecularFindFaceCornerAtPosition(
						neighbor_room, neighbor_face, vert_position);
				}
				if (neighbor_corner < 0)
					continue;
				const float normal_dot = SpecularClamp(
					current.basis.normal * neighbor.basis.normal, -1.0f, 1.0f);
				if (normal_dot < 0.70f)
					continue;
				if (std::find(component.begin(), component.end(),
					edge.face_info_index) != component.end())
				{
					continue;
				}
				component.push_back(edge.face_info_index);
				pending.push_back(edge.face_info_index);
			}
		}

		// Every face in a connected smoothing component must sum the same inputs
		// in the same order. This gives duplicated retained corners bit-identical
		// normals instead of target-face-relative approximations.
		std::sort(component.begin(), component.end());
		vector normal_sum = { 0.0f, 0.0f, 0.0f };
		vector lightmap_sum = { 0.0f, 0.0f, 0.0f };
		float total_weight = 0.0f;
		for (int face_info_index : component)
		{
			const SpecularFaceInfo& info = faces[face_info_index];
			room* component_room = &Rooms[info.room_index];
			face* component_face = &component_room->faces[info.face_index];
			int corner = -1;
			if (info.room_index == target.room_index)
				corner = SpecularFindFaceCorner(component_face, vert_index);
			if (corner < 0)
			{
				corner = SpecularFindFaceCornerAtPosition(component_room,
					component_face, vert_position);
			}
			if (corner < 0)
				continue;
			const float weight =
				SpecularFaceCornerAngle(component_room, component_face, corner);
			normal_sum += SpecularFaceCornerNormal(info, corner) * weight;
			// Match the legacy smooth-specular energy convention: every
			// incident face contributes its corner lightmap value to the shared
			// vertex, while the geometric normal remains angle-weighted.
			lightmap_sum += SpecularFaceCornerLightmapColor(component_face, corner);
			total_weight += weight;
		}
		if (total_weight > 0.0f)
		{
			normal_sum /= total_weight;
		}
		if (vm_NormalizeVector(&normal_sum) <= 0.0001f)
			normal_sum = SpecularFaceCornerNormal(target, vn);
		cached.normals[vn] = normal_sum;
		cached.lightmap_colors[vn] = lightmap_sum;
	}
}

void PrecomputeMineSpecularSources()
{
    ++ReflectionBakeRevision;
	ResetSplitSpecularTexturePairCache();
	Precomputed_specular_sources.clear();
	Precomputed_specular_highest_room = Highest_room_index;

	if (Highest_room_index < 0)
		return;

	Precomputed_specular_sources.resize(Highest_room_index + 1);
	std::vector<SpecularFaceInfo> faces;
	std::vector<std::vector<int>> face_lookup(Highest_room_index + 1);
	std::vector<SpecularDonorFace> donors;
	std::vector<SpecularFieldSample> field_samples;

	for (int room_index = 0; room_index <= Highest_room_index; room_index++)
	{
		room* rp = &Rooms[room_index];
		if (!rp->used)
			continue;

		Precomputed_specular_sources[room_index].resize(rp->num_faces);
		face_lookup[room_index].assign(rp->num_faces, -1);
		if (rp->flags & RF_EXTERNAL)
			continue;

		for (int face_index = 0; face_index < rp->num_faces; face_index++)
		{
			face* fp = &rp->faces[face_index];
			if (!SpecularFaceCanReceivePpxSources(rp, fp))
				continue;

			SpecularFaceInfo info = {};
			info.room_index = room_index;
			info.face_index = face_index;
			info.face_info_index = (int)faces.size();
			info.exact_tmap = fp->tmap;
			info.split_tmap = SplitSpecularSourceMatchTmap(fp->tmap);
			info.basis = SpecularBuildFaceBasis(rp, fp);
			info.field_weight = std::max(0.25f, sqrt(SpecularFaceArea(rp, fp)));
			face_lookup[room_index][face_index] = info.face_info_index;
			faces.push_back(info);

			specular_instance sources[MAX_SPECULARS] = {};
			const int source_count = SpecularCopyFaceSources(fp, sources);
			if (source_count > 0)
			{
				SpecularDonorFace donor = {};
				donor.room_index = room_index;
				donor.face_index = face_index;
				donor.face_info_index = face_lookup[room_index][face_index];
				donor.exact_tmap = info.exact_tmap;
				donor.split_tmap = info.split_tmap;
				donor.basis = info.basis;
				donor.source_count = source_count;
				for (int i = 0; i < source_count; i++)
					donor.sources[i] = sources[i];
				donors.push_back(donor);

				const float face_weight = info.field_weight;
				for (int i = 0; i < source_count; i++)
				{
					vector toward_source = sources[i].bright_center - info.basis.center;
					const float source_distance = vm_NormalizeVector(&toward_source);
					if (source_distance <= 0.1f)
						continue;
					vector incident = -toward_source;
					const float normal_dot = incident * info.basis.normal;
					vector direction =
						incident - info.basis.normal * (2.0f * normal_dot);
					if (vm_NormalizeVector(&direction) <= 0.0001f)
						continue;

					float r, g, b;
					SpecularUnpackColor(sources[i].bright_color, r, g, b);
					const float intensity = (r + g + b) / 3.0f;
					SpecularFieldSample sample = {};
					sample.position = info.basis.center;
					sample.source_center = sources[i].bright_center;
					sample.direction = direction;
					sample.normal = info.basis.normal;
					sample.distance = source_distance;
					sample.r = r;
					sample.g = g;
					sample.b = b;
					sample.weight = face_weight * (0.25f + intensity);
					sample.source_id = -1;
					field_samples.push_back(sample);
				}
			}
		}
	}

	std::vector<std::vector<SpecularFaceAdjacency>> adjacency(faces.size());
	for (int room_index = 0; room_index <= Highest_room_index; room_index++)
	{
		room* rp = &Rooms[room_index];
		if (!rp->used || (rp->flags & RF_EXTERNAL) || room_index >= (int)face_lookup.size())
			continue;

		for (int a = 0; a < rp->num_faces; a++)
		{
			const int a_info = face_lookup[room_index][a];
			if (a_info < 0)
				continue;
			for (int b = a + 1; b < rp->num_faces; b++)
			{
				const int b_info = face_lookup[room_index][b];
				if (b_info < 0)
					continue;

				if (SpecularFacesShareEdge(&rp->faces[a], &rp->faces[b]))
					SpecularAddFaceAdjacency(adjacency, a_info, b_info, 0.0f);
				else if (SpecularFacesShareVertex(&rp->faces[a], &rp->faces[b]))
					SpecularAddFaceAdjacency(adjacency, a_info, b_info, 3.5f);
			}
		}
	}

	// Mine geometry duplicates boundary vertices both across rooms and within a
	// room. Join coincident edges into the same geometric smoothing graph so a
	// continuous wall does not acquire a specular seam merely because its
	// vertices have different indices. The normal-angle test below still keeps
	// authored hard creases separate.
	std::unordered_map<SpecularEdgeKey, std::vector<int>, SpecularEdgeKeyHash>
		coincident_edges;
	coincident_edges.reserve(faces.size() * 4);
	for (const SpecularFaceInfo& info : faces)
	{
		room* rp = &Rooms[info.room_index];
		face* fp = &rp->faces[info.face_index];
		for (int corner = 0; corner < fp->num_verts; corner++)
		{
			const vector& a = rp->verts[fp->face_verts[corner]];
			const vector& b =
				rp->verts[fp->face_verts[(corner + 1) % fp->num_verts]];
			vector edge = a - b;
			if (vm_GetMagnitude(&edge) <= 0.0001f)
				continue;

			const SpecularEdgeKey key = SpecularBuildEdgeKey(a, b);
			std::vector<int>& owners = coincident_edges[key];
			for (int owner_index : owners)
			{
				if (owner_index == info.face_info_index)
					continue;
				SpecularAddFaceAdjacency(adjacency, owner_index,
					info.face_info_index, 0.0f);
			}
			owners.push_back(info.face_info_index);
		}
	}

	std::vector<float> exact_costs;
	std::vector<int> exact_donors;
	std::vector<float> split_costs;
	std::vector<int> split_donors;
	if (!Render_per_pixel_field_static_specular)
	{
		SpecularRunSourceFlood(faces, adjacency, donors, false, exact_costs, exact_donors);
		SpecularRunSourceFlood(faces, adjacency, donors, true, split_costs, split_donors);
	}

	SpecularSpatialField spatial_field;
	SpecularBuildSpatialField(spatial_field, faces, field_samples);

	auto build_precomputed_face_sources = [&](int face_info_index)
	{
		const SpecularFaceInfo& info = faces[face_info_index];
		PrecomputedSpecularFaceSources& cached =
			Precomputed_specular_sources[info.room_index][info.face_index];

		if (!Render_per_pixel_field_static_specular)
		{
			std::vector<SpecularResolvedDonor> selected;
			SpecularSelectResolvedDonors(info, donors, info.exact_tmap, false,
				exact_donors[face_info_index], exact_costs[face_info_index], selected);
			SpecularBuildResolvedSourceSet(cached.exact, info, donors, selected);

			SpecularSelectResolvedDonors(info, donors, info.split_tmap, true,
				split_donors[face_info_index], split_costs[face_info_index], selected);
			SpecularBuildResolvedSourceSet(cached.split, info, donors, selected);
		}

		SpecularBuildResolvedNormalsForFace(cached, info, adjacency, faces);
	};

	const unsigned int hardware_threads = std::thread::hardware_concurrency();
	const int thread_count = std::max(1, std::min((int)faces.size(),
		std::min(hardware_threads > 0 ? (int)hardware_threads : 2, 16)));
	if (thread_count <= 1 || faces.size() < 32)
	{
		for (int face_info_index = 0; face_info_index < (int)faces.size(); face_info_index++)
			build_precomputed_face_sources(face_info_index);
	}
	else
	{
		std::atomic<int> next_face_info_index(0);
		std::vector<std::thread> workers;
		workers.reserve(thread_count);
		for (int thread_index = 0; thread_index < thread_count; thread_index++)
		{
			workers.emplace_back([&]() {
				for (;;)
				{
					const int face_info_index = next_face_info_index.fetch_add(1);
					if (face_info_index >= (int)faces.size())
						break;
					build_precomputed_face_sources(face_info_index);
				}
			});
		}
		for (std::thread& worker : workers)
			worker.join();
	}

	auto build_precomputed_face_field = [&](int face_info_index)
	{
		const SpecularFaceInfo& info = faces[face_info_index];
		PrecomputedSpecularFaceSources& cached =
			Precomputed_specular_sources[info.room_index][info.face_index];

		room* rp = &Rooms[info.room_index];
		face* fp = &rp->faces[info.face_index];
		cached.field = {};
		SpecularBuildFieldSourceSet(cached.field, info, spatial_field,
			info.basis.center, info.basis.normal);
		cached.field_vertices.clear();
		cached.field_vertices.resize(fp->num_verts);
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			vector vertex_normal = info.basis.normal;
			if (vn < (int)cached.normals.size())
				vertex_normal = cached.normals[vn];
			else if (FaceHasSmoothSpecularNormals(fp))
				vertex_normal = SpecialFaces[fp->special_handle].vertnorms[vn];
			if (vm_NormalizeVector(&vertex_normal) <= 0.0001f)
				vertex_normal = info.basis.normal;

			SpecularBuildFieldSourceSet(cached.field_vertices[vn], info,
				spatial_field, rp->verts[fp->face_verts[vn]], vertex_normal);
		}
	};

	if (thread_count <= 1 || faces.size() < 32)
	{
		for (int face_info_index = 0; face_info_index < (int)faces.size(); face_info_index++)
			build_precomputed_face_field(face_info_index);
	}
	else
	{
		std::atomic<int> next_face_info_index(0);
		std::vector<std::thread> workers;
		workers.reserve(thread_count);
		for (int thread_index = 0; thread_index < thread_count; thread_index++)
		{
			workers.emplace_back([&]() {
				for (;;)
				{
					const int face_info_index = next_face_info_index.fetch_add(1);
					if (face_info_index >= (int)faces.size())
						break;
					build_precomputed_face_field(face_info_index);
				}
			});
		}
		for (std::thread& worker : workers)
			worker.join();
	}

}
