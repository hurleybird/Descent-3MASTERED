// Descent 3MASTERED - GPL-3.0-or-later
#include "GameRoomCoronas.h"
#include "SceneExtraction.h"
#include "../renderer/SceneView.h"
#include "../renderer/SceneCache.h"
#include "room.h"
#include "gametexture.h"
#include "fireball.h"
#include "bitmap.h"
#include "game.h"
#include "object.h"
#include "objinfo.h"
#include "findintersection.h"
#include "gameloop.h"
#include "args.h"
#include "config.h"
#include "grdefs.h"

extern int FastCoronas;
namespace {
constexpr uint64_t Collection = 0x524f4f4d474c4f57ull;
struct RoomSources {
    d3scene::AssetId geometry;
    uint64_t appearance = 0, materials = 0;
    bool fast = false;
    std::vector<GameRoomCorona> sources;
    std::vector<int> polygons;
};
struct CoronaState {
    std::array<RoomSources,MAX_ROOMS> sources;
    GameRoomCoronaClock clock;
    uint64_t lifetime = 0, world_sequence = 0, logical_frame = 0;
    int main_room = -1;
    d3scene::PresentationRef published;
    d3scene::View camera;
};
std::shared_ptr<CoronaState> coronaState() {
    static d3scene::SceneCache<CoronaState> cache;
    return cache.acquire(d3scene::assets());
}
uint64_t Materials = 1;

const RoomSources& roomSources(CoronaState& state,const d3scene::RoomState& room) {
    auto& cache = state.sources.at(room.number);
    if (cache.geometry == room.asset->id() && cache.appearance == room.source_material_revision && cache.materials == Materials && cache.fast == bool(FastCoronas))
        return cache;
    cache.geometry = room.asset->id(); cache.appearance = room.source_material_revision; cache.materials = Materials; cache.fast = FastCoronas != 0;
    cache.sources.clear(); cache.polygons.assign(room.asset->geometry().polygons.size(),-1);
    const auto& authored = Rooms[room.number];
    for (unsigned i = 0; i < cache.polygons.size() && i < unsigned(authored.num_faces); ++i) {
        const auto& face = authored.faces[i];
        if (face.portal_num != -1 || (!(face.flags&FF_CORONA) && !FastCoronas) || !(face.flags&FF_LIGHTMAP) ||
            face.tmap < 0 || face.tmap >= MAX_TEXTURES) continue;
        const auto& texture = GameTextures[face.tmap];
        if (!texture.used || !(texture.flags&TF_LIGHT) || DEFAULT_CORONA_INDEX+texture.corona_type >= NUM_FIREBALLS ||
            face.num_verts < 3 || face.num_verts > MAX_VERTS_PER_FACE) continue;
        vector vertices[MAX_VERTS_PER_FACE],center;
        for (int corner = 0; corner < face.num_verts; ++corner) vertices[corner] = authored.verts[face.face_verts[corner]];
        const float area = vm_GetCentroid(&center,vertices,face.num_verts);
        if (!(area > 0) || !std::isfinite(area)) continue;
        center += face.normal/4;
        const auto point = vertices[0]; const auto normal = face.normal;
        const float maximum = (std::max)({1.f,texture.r,texture.g,texture.b});
        const auto channel = [&](float v) { return float(int((std::max)(0.f,(std::min)(1.f,v/maximum))*255))/255; };
        GameRoomCorona source;
        source.geometry = cache.geometry; source.appearance = cache.appearance; source.materials = Materials;
        source.room = room.number; source.polygon = int(i); source.bitmap = Fireballs[DEFAULT_CORONA_INDEX+texture.corona_type].bm_handle;
        source.center = {center.x,center.y,center.z}; source.face_point = {point.x,point.y,point.z};
        source.normal = {normal.x,normal.y,normal.z}; source.size = 2*std::sqrt(area);
        source.color = {channel(texture.r),channel(texture.g),channel(texture.b)}; source.fast = cache.fast;
        cache.polygons[i] = int(cache.sources.size()); cache.sources.push_back(source);
    }
    return cache;
}
bool sightClear(const GameRoomCorona& source,const d3scene::PreparedView& main) {
    const auto eye = main.visibility().camera.transform.position;
    vector from{eye.x,eye.y,eye.z},to{source.center.x,source.center.y,source.center.z};
    fvi_query query{}; fvi_info hit{};
    query.startroom = main.view().room;
    const auto* viewer = main.view().attached_camera ? ObjGet(int(main.view().camera_owner)) : nullptr;
    query.thisobjnum = viewer ? OBJNUM(viewer) : -1;
    if (source.fast) {
        if (Rooms[source.room].flags&RF_EXTERNAL) return true;
        vector direction = from-to;
        vm_NormalizeVector(&direction); direction *= source.size;
        from = to; to += direction; query.startroom = source.room;
    }
    query.p0 = &from; query.p1 = &to;
    query.flags = FQ_CHECK_OBJS|FQ_NO_RELINK|FQ_IGNORE_WEAPONS|FQ_ROBOTS_AS_SPHERE|FQ_PLAYERS_AS_SPHERE;
    return fvi_FindIntersection(&query,&hit) == HIT_NONE;
}
}

void InvalidateGameRoomCoronaMaterials() {
    if (++Materials == 0) throw std::overflow_error("Corona material revision exhausted");
}

void AdvanceGameRoomCoronaFixture() {
    const int argument = FindArg("-scene-room-corona-probe");
    if (!argument || AutomatedCaptureFrameIndex() < 0) return;
    const char* mode = GetArg(argument+1);
    if (!mode) Error("Room corona fixture requires a mode");
    const int frame = AutomatedCaptureFrameIndex();
    if (!stricmp(mode,"fast")) { FastCoronas = 1; return; }
    if (!stricmp(mode,"replay")) return;
    if (stricmp(mode,"blocked") && stricmp(mode,"recolor") && stricmp(mode,"pulse")) Error("Unknown room corona fixture");
    if (frame != 10) return;
    const auto state = coronaState();
    auto& Clock = state->clock;
    if (!Clock.count()) Error("Room corona fixture requires a visible authored corona");
    const auto source = Clock[0].source;
    if (!stricmp(mode,"recolor")) {
        auto& texture = GameTextures[Rooms[source.room].faces[source.polygon].tmap];
        texture.r = 0; texture.g = 1; texture.b = 0;
    } else if (!stricmp(mode,"pulse")) {
        auto& room = Rooms[source.room]; room.flags |= RF_STROBE; room.pulse_time = 8; room.pulse_offset = 0;
    } else {
        const auto eye = state->camera.camera.position;
        vector from{eye.x,eye.y,eye.z},to{(eye.x+source.center.x)/2,(eye.y+source.center.y)/2,(eye.z+source.center.z)/2};
        fvi_query query{}; fvi_info hit{}; query.p0 = &from; query.p1 = &to; query.startroom = state->main_room; query.thisobjnum = -1;
        if (fvi_FindIntersection(&query,&hit) != HIT_NONE || hit.hit_room < 0) Error("Corona occluder fixture cannot locate its room");
        matrix facing = IDENTITY_MATRIX;
        const int object = ObjCreate(OBJ_ROBOT,GENOBJ_GUIDEBOT,hit.hit_room,&to,&facing);
        if (object < 0) Error("Corona occluder fixture cannot create its object");
        Objects[object].control_type = CT_NONE; Objects[object].movement_type = MT_NONE;
        AutomatedCaptureLog("scene room corona occluder owner=%d room=%d face=%d",Objects[object].handle,source.room,source.polygon);
    }
    AutomatedCaptureLog("scene room corona fixture mode=%s room=%d face=%d",mode,source.room,source.polygon);
}

void CaptureGameRoomCoronaView(const d3scene::PreparedView& main) {
    const auto& world = main.frame(); const auto& request = main.view();
    if (request.kind != d3scene::ViewKind::Main || request.collection || request.target != d3scene::ViewTarget::Scene) return;
    const auto state = coronaState();
    auto& Clock = state->clock;
    auto& Lifetime = state->lifetime;
    auto& WorldSequence = state->world_sequence;
    auto& LogicalFrame = state->logical_frame;
    auto& Published = state->published;
    auto& Camera = state->camera;
    if (Lifetime == world.data().scene_lifetime && WorldSequence == world.data().sequence) {
        if (AutomatedCapturePending() && FindArg("-scene-room-corona-probe")) AutomatedCaptureLog("scene room corona publication replay retained=%u",Clock.count());
        return;
    }
    if (Lifetime == world.data().scene_lifetime && LogicalFrame == uint64_t(FrameCount) && WorldSequence)
        throw std::logic_error("Corona clock received two world publications in one game frame");
    if (d3scene::assets().lifetime() != world.data().scene_lifetime)
        throw std::logic_error("Cannot capture corona sources from a retired world");
    if (!request.attached_camera || !request.portal_visibility || request.has_clip_plane) {
        // Synthetic main-role diagnostic views are not a physical gameplay
        // observer. Publish an explicit empty exposure, not a stale last view.
        Lifetime = world.data().scene_lifetime; WorldSequence = world.data().sequence; LogicalFrame = uint64_t(FrameCount);
        Published.reset(); return;
    }
    GameRoomCoronaBatch candidates;
    unsigned authored = 0, rays = 0;
    if (Detail_settings.Coronas_enabled) for (const auto& item : main.visibility().surfaces) {
        if (item.model || item.mesh || item.terrain || candidates.count == GameRoomCoronaLimit) continue;
        const auto& room = world.data().rooms.at(item.room_index);
        const auto& cache = roomSources(*state,room);
        if (item.polygon_index >= cache.polygons.size() || cache.polygons[item.polygon_index] < 0) continue;
        const auto& source = cache.sources[cache.polygons[item.polygon_index]]; ++authored;
        // Visibility exposure is independent of a pulsing lamp's instantaneous
        // intensity. A dark strobe phase must not restart its visibility fade.
        if (!(GameCoronaOpacity(source,main.visibility().camera.transform.position,1,1) > 0)) continue;
        ++rays;
        if (sightClear(source,main)) candidates.values[candidates.count++] = source;
    }
    Clock.advance(world.data().scene_lifetime,uint64_t(FrameCount),world.data().paused ? 0 : world.data().delta_time,candidates);
    Lifetime = world.data().scene_lifetime; WorldSequence = world.data().sequence; LogicalFrame = uint64_t(FrameCount);
    state->main_room = request.room;
    Camera = request;
    Camera.kind = d3scene::ViewKind::Preview; Camera.history_key = Collection;
    Camera.camera = main.visibility().camera.transform; Camera.camera_effects_resolved = true;
    Camera.camera_velocity = {}; Camera.world_deformation = {}; Camera.clear_depth = false;
    Camera.collection = Collection; Camera.feedback = d3scene::NoFeedback; Camera.portal_visibility = false;
    Camera.attached_camera = Camera.exclude_camera_rig = Camera.exterior_origin = false;
    Camera.room = -1; Camera.excluded_objects.clear(); Camera.excluded_surfaces.clear(); Camera.image_inputs.clear();
    Camera.dynamic_lighting = false;
    auto output = d3scene::acquireFrameData();
    output.scene_lifetime = Lifetime; output.sequence = AllocateGamePresentationSequence();
    output.simulation_time = world.data().simulation_time; output.delta_time = world.data().delta_time; output.paused = world.data().paused;
    const auto quad = d3scene::assets().quadMesh();
    if (Detail_settings.Coronas_enabled) for (unsigned i = 0; i < Clock.count(); ++i) {
        const auto& exposure = Clock[i]; const auto& source = exposure.source;
        const auto* room_index = world.roomIndices().find(source.room);
        if (!room_index) continue;
        const auto& room = world.data().rooms[room_index->second];
        if (room.asset->id() != source.geometry || room.source_material_revision != source.appearance || source.materials != Materials) continue;
        const float alpha = GameCoronaOpacity(source,Camera.camera.position,exposure.fade,room.light_scalar);
        if (!(alpha > 0) || source.bitmap <= BAD_BITMAP_HANDLE || source.bitmap >= MAX_BITMAPS || !GameBitmaps[source.bitmap].used) continue;
        d3scene::PreparedMesh mesh; mesh.asset = quad; mesh.collection = Collection;
        mesh.world.position = source.center; mesh.alignment = d3scene::MeshAlignment::Camera;
        mesh.world.right.x = source.size; mesh.world.up.y = source.size*bm_h(source.bitmap,0)/bm_w(source.bitmap,0);
        mesh.deformation_response = {0,0};
        auto& surface = mesh.surface;
        surface.bitmap = source.bitmap; surface.texture_alpha = true; surface.texture_wrap = false;
        surface.unlit = surface.two_sided = true; surface.medium_transport = false;
        surface.blend = d3scene::BlendMode::Additive; surface.depth = source.fast ? d3scene::DepthMode::ReadOnly : d3scene::DepthMode::Disabled;
        surface.effect_depth_offset = source.fast ? -source.size/2 : 0;
        surface.alpha = int((std::min)(1.f,alpha)*255)/255.f; surface.color_modulation = source.color;
        output.meshes.push_back(std::move(mesh));
        if (AutomatedCapturePending()) AutomatedCaptureLog("scene room corona value room=%d face=%d size=%.6g fade=%.6g alpha=%.6g color=%.3g,%.3g,%.3g",
            source.room,source.polygon,source.size,exposure.fade,alpha,source.color.x,source.color.y,source.color.z);
    }
    const unsigned draws = unsigned(output.meshes.size());
    if (draws) {
        static GameSceneResources resources;
        resources.prepare(output);
        Published = d3scene::Presentation::publish(std::move(output));
    }
    else Published.reset();
    if (AutomatedCapturePending()) AutomatedCaptureLog("scene room coronas authored=%u rays=%u visible=%u retained=%u draws=%u fast=%d",
        authored,rays,candidates.count,Clock.count(),draws,FastCoronas);
}

void RenderGameRoomCoronas(const d3scene::Presentation& world) {
    const auto state = coronaState();
    if (state->lifetime != world.data().scene_lifetime || state->world_sequence != world.data().sequence)
        throw std::logic_error("Room corona exposure does not belong to the main world");
    if (state->published) d3scene::renderView(*state->published,state->camera);
}
