// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "object.h"
#include "room.h"
#include "gametexture.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
uint64_t Sequence = 0;
std::vector<d3scene::ObjectState> PreviousObjects;
uint64_t PreviousLifetime = 0;
double PreviousTime = 0;
uint64_t GeometryEpoch = 1;
uint64_t AppearanceEpoch = 1;
constexpr uint32_t SurfaceAppearanceFlags = FF_LIGHTMAP|FF_DESTROYED|FF_FLOATING_TRIG;
std::array<uint64_t,MAX_ROOMS> PublishedGeometryEpoch{};
d3scene::Vec3 value(const vector& v) { return {v.x, v.y, v.z}; }
d3scene::Transform transform(const vector& p, const matrix& m) {
    return {value(p), value(m.rvec), value(m.uvec), value(m.fvec)};
}
bool same(d3scene::Vec3 a, const vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool matchesVolume(const d3scene::VolumeGrid& grid, const room& source) {
    if (!source.volume_lights) return grid.colors.empty();
    if (!same(grid.origin,source.min_xyz) || grid.width != source.volume_width || grid.height != source.volume_height ||
        grid.depth != source.volume_depth) return false;
    const size_t count = size_t(grid.width)*grid.height*grid.depth;
    return count == grid.colors.size() && std::equal(grid.colors.begin(),grid.colors.end(),source.volume_lights);
}
d3scene::VolumeGrid extractVolume(const room& source) {
    d3scene::VolumeGrid grid;
    if (!source.volume_lights) return grid;
    if (source.volume_width <= 0 || source.volume_height <= 0 || source.volume_depth <= 0)
        throw std::invalid_argument("Invalid source room light volume dimensions");
    grid.origin = value(source.min_xyz);
    grid.width = source.volume_width; grid.height = source.volume_height; grid.depth = source.volume_depth;
    grid.colors.assign(source.volume_lights,source.volume_lights+size_t(grid.width)*grid.height*grid.depth);
    return grid;
}

// Compare only after a producer invalidation (or in the explicit audit). Normal
// frames do not rediscover immutable level geometry by walking every corner.
bool matches(const d3scene::MeshGeometry& geometry, const room& source) {
    if (geometry.vertices.size() != static_cast<size_t>(source.num_verts) ||
        geometry.polygons.size() != static_cast<size_t>(source.num_faces)) return false;
    for (int i = 0; i < source.num_verts; ++i)
        if (!same(geometry.vertices[i], source.verts[i])) return false;
    for (int i = 0; i < source.num_faces; ++i) {
        const auto& polygon = geometry.polygons[i]; const face& face = source.faces[i];
        if (polygon.bounds_volume != ((face.flags & FF_FLOATING_TRIG) == 0)) return false;
        if (polygon.corners.size() != face.num_verts || !same(polygon.normal, face.normal)) return false;
        for (int j = 0; j < face.num_verts; ++j) {
            const auto& c = polygon.corners[j]; const auto& uv = face.face_uvls[j];
            if (c.vertex != static_cast<uint32_t>(face.face_verts[j]) || c.u != uv.u || c.v != uv.v ||
                c.light_u != uv.u2 || c.light_v != uv.v2 || c.alpha != uv.alpha) return false;
        }
    }
    return true;
}
d3scene::MeshGeometry extractGeometry(const room& source) {
    d3scene::MeshGeometry geometry;
    geometry.vertices.reserve(source.num_verts);
    for (int i = 0; i < source.num_verts; ++i) geometry.vertices.push_back(value(source.verts[i]));
    geometry.polygons.reserve(source.num_faces);
    for (int i = 0; i < source.num_faces; ++i) {
        const face& face = source.faces[i];
        d3scene::MeshPolygon polygon;
        polygon.normal = value(face.normal);
        polygon.bounds_volume = (face.flags & FF_FLOATING_TRIG) == 0;
        polygon.corners.reserve(face.num_verts);
        for (int j = 0; j < face.num_verts; ++j) {
            const auto& uv = face.face_uvls[j];
            polygon.corners.push_back({static_cast<uint32_t>(face.face_verts[j]), uv.u, uv.v, uv.u2, uv.v2, uv.alpha});
        }
        geometry.polygons.push_back(std::move(polygon));
    }
    return geometry;
}
}

void ResetGamePresentationScene() {
    PreviousObjects.clear(); PreviousLifetime = 0;
    d3scene::assets().reset();
}
void InvalidateGameSceneGeometry() {
    if (++GeometryEpoch == 0) throw std::overflow_error("Room geometry epoch exhausted");
    InvalidateGameSceneAppearance();
}
void InvalidateGameSceneAppearance() {
    if (++AppearanceEpoch == 0) throw std::overflow_error("Room appearance epoch exhausted");
}
uint64_t GameSceneGeometryRevision() { return GeometryEpoch; }

d3scene::FrameData ExtractGameFrame(double time, float delta, bool paused, bool advance_history,
    const d3scene::Presentation* retained_frame,bool audit_geometry) {
    if (Sequence == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Presentation sequence exhausted");
    auto& assets = d3scene::assets();
    auto data = d3scene::acquireFrameData();
    data.sequence = Sequence + 1;
    data.scene_lifetime = assets.lifetime();
    data.simulation_time = time;
    data.delta_time = delta;
    data.paused = paused;
    // No domain is marked executable until its complete preparation exists.
    // Capturing source object transforms is NOT equivalent to preparing models.
    data.pending_domains = d3scene::AllPendingDomains;
    if (retained_frame && retained_frame->data().scene_lifetime != data.scene_lifetime) retained_frame = nullptr;
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (!Rooms[i].used) { assets.removeRoom(i); continue; }
        const room& source = Rooms[i];
        auto asset = assets.room(i);
        if (asset && PublishedGeometryEpoch[i] == GeometryEpoch) {
            if (audit_geometry && (!matches(asset->geometry(),source) || !matchesVolume(asset->volume(),source)))
                throw std::logic_error("Room geometry changed without producer invalidation");
        } else {
            if (!asset || !matches(asset->geometry(),source) || !matchesVolume(asset->volume(),source))
                asset = assets.publishRoom(i,extractGeometry(source),extractVolume(source));
            PublishedGeometryEpoch[i] = GeometryEpoch;
        }
        d3scene::RoomState state;
        state.number = i;
        state.source_material_revision = AppearanceEpoch;
        state.source_mirror_face = source.mirror_face;
        state.asset = std::move(asset);
        state.bounds = {value(source.min_xyz), value(source.max_xyz)};
        state.authored_flags = source.flags & ~RF_MIRROR_VISIBLE;
        state.observation_unclipped = (source.flags & RF_DOOR) != 0;
        state.fog_depth = source.fog_depth;
        state.fog_color = {source.fog_r, source.fog_g, source.fog_b};
        state.pulse_time = source.pulse_time;
        state.pulse_offset = source.pulse_offset;
        const auto* previous_room = retained_frame ? retained_frame->roomIndices().find(i) : nullptr;
        const d3scene::RoomState* cached = previous_room ? &retained_frame->data().rooms[previous_room->second] : nullptr;
        bool reuse = cached && cached->source_mirror_face == source.mirror_face && cached->asset->id() == state.asset->id() &&
            cached->surfaces.publishedFor(state.asset->id()) && cached->authored_flags == state.authored_flags &&
            cached->portals.size() == size_t(source.num_portals);
        for (int p = 0; reuse && p < source.num_portals; ++p)
            reuse = cached->portals[p].flags == uint32_t(source.portals[p].flags);
        const bool appearance_current = cached && cached->source_material_revision == AppearanceEpoch;
        const bool audit_retained = reuse && appearance_current && audit_geometry;
        for (int j = 0; reuse && (!appearance_current || audit_geometry) && j < source.num_faces; ++j) {
            const auto& face = source.faces[j]; const auto& binding = cached->surfaces[j];
            if (face.tmap < 0 || face.tmap >= MAX_TEXTURES) { reuse = false; break; }
            // Texture sequences are frame resources; UV scrolling is evaluated
            // only for submitted draws. Neither changes this surface table.
            reuse = binding.texture == face.tmap &&
                binding.lightmap == ((face.flags & FF_LIGHTMAP) ? face.lmi_handle : -1) &&
                binding.special == face.special_handle && binding.portal == face.portal_num &&
                binding.authored_flags == (face.flags & SurfaceAppearanceFlags) &&
                binding.light_multiple == face.light_multiple;
        }
        if (audit_retained && !reuse) throw std::logic_error("Room appearance changed without producer invalidation");
        if (reuse) {
            state.surfaces = cached->surfaces;
            state.mirrors = cached->mirrors;
            state.uses_room_illumination = cached->uses_room_illumination;
        } else {
        state.surfaces.reserve(source.num_faces);
        for (int j = 0; j < source.num_faces; ++j) {
            const face& face = source.faces[j];
            d3scene::SurfaceBinding binding;
            binding.texture = face.tmap;
            binding.lightmap = (face.flags & FF_LIGHTMAP) ? face.lmi_handle : -1;
            binding.special = face.special_handle;
            binding.portal = face.portal_num;
            binding.authored_flags = face.flags & SurfaceAppearanceFlags;
            binding.light_multiple = face.light_multiple;
            state.surfaces.push_back(binding);
        }
        }
        bool retain_portals = cached && cached->portals.size() == size_t(source.num_portals);
        for (int j = 0; retain_portals && j < source.num_portals; ++j) {
            const auto& current = source.portals[j]; const auto& old = cached->portals[j];
            retain_portals = old.polygon == current.portal_face && old.destination_room == current.croom &&
                old.destination_portal == current.cportal && old.combine_master == current.combine_master && old.flags == uint32_t(current.flags);
        }
        if (retain_portals) state.portals = cached->portals;
        else for (int j = 0; j < source.num_portals; ++j) {
            const portal& p = source.portals[j];
            state.portals.push_back({p.portal_face, p.croom, p.cportal, p.combine_master, static_cast<uint32_t>(p.flags)});
        }
        data.rooms.push_back(std::move(state));
    }
    d3scene::SceneLookup<uint32_t, const d3scene::ObjectState*> previous_objects;
    previous_objects.reserve(PreviousObjects.size());
    if (PreviousLifetime == data.scene_lifetime && PreviousTime <= time)
        for (const auto& object : PreviousObjects) previous_objects.emplace(object.handle, &object);
    for (int i = 0; i <= Highest_object_index; ++i) {
        const object& source = Objects[i];
        if (source.type == OBJ_NONE) continue;
        d3scene::ObjectState state;
        state.handle = static_cast<uint32_t>(source.handle);
        state.room = source.roomnum;
        state.type = source.type;
        state.render_type = source.render_type;
        state.lighting_type = source.lighting_render_type;
        state.authored_id = source.id;
        state.current = transform(source.pos, source.orient);
        state.previous = state.current;
        state.radius = source.size;
        const auto previous = previous_objects.find(state.handle);
        if (previous != previous_objects.end()) {
            state.previous = previous->second->current;
            state.has_previous = true;
        }
        data.objects.push_back(state);
    }
    // Commit presentation history once, never when rendering a view/pass.
    ++Sequence;
    if (advance_history) { PreviousObjects = data.objects; PreviousLifetime = data.scene_lifetime; PreviousTime = time; }
    return data;
}
d3scene::PresentationRef ExtractGamePresentation(double time,float delta,bool paused,bool advance_history,
    const d3scene::Presentation* retained_frame) {
    return d3scene::Presentation::freeze(ExtractGameFrame(time,delta,paused,advance_history,retained_frame));
}

d3scene::View MakeGameSceneView(d3scene::ViewKind kind, uint64_t history_key,
    const object* viewer, const vector& eye, int room, const matrix& orientation,
    float zoom, bool rear, d3scene::Rect viewport, d3scene::Rect anchor) {
    d3scene::View view;
    view.kind = kind;
    view.history_key = history_key;
    view.camera_variant = rear ? 1 : 0;
    view.camera = transform(eye, orientation);
    view.attached_camera = viewer != nullptr;
    if (viewer) view.camera_owner = static_cast<uint32_t>(viewer->handle);
    if (rear) {
        auto& r = view.camera.right; r = {-r.x, -r.y, -r.z};
        auto& f = view.camera.forward; f = {-f.x, -f.y, -f.z};
    }
    // Rear-view requests never temporarily rewrite a simulation object's pose.
    view.room = room;
    view.exterior_origin = room != -1 && ROOMNUM_OUTSIDE(room);
    view.exclude_camera_rig = viewer != nullptr;
    view.zoom = zoom;
    view.viewport = viewport;
    view.projection_anchor = anchor;
    view.dynamic_lighting = kind != d3scene::ViewKind::Auxiliary;
    // Feedback census/commit is intentionally a compile barrier, not guessed
    // policy hidden in an executor. Save/preview views default to no feedback.
    return view;
}
