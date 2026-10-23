// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "../renderer/SceneGeometry.h"
#include "../renderer/SceneView.h"
#include "../renderer/SceneAnimation.h"
#include "viseffect.h"
#include "fireball.h"
#include "vclip.h"
#include "gametexture.h"
#include "bitmap.h"
#include "descent.h"
#include "config.h"
#include "gameloop.h"
#include "game.h"
#include "object.h"
#include "args.h"
#include "weapon.h"
#include "SceneWeaponVisuals.h"
#include "matcen.h"
#include "weather.h"
#include "polymodel.h"
#include <array>
#include <cmath>
#include <algorithm>
namespace {
bool special(int id) {
    switch (id) {
    case LIGHTNING_BOLT_INDEX: case GRAY_LIGHTNING_BOLT_INDEX: case MASSDRIVER_EFFECT_INDEX:
    case MERCBOSS_MASSDRIVER_EFFECT_INDEX: case BILLBOARD_SMOKETRAIL_INDEX: case THICK_LIGHTNING_INDEX:
    case SINE_WAVE_INDEX: case BLAST_RING_INDEX: case FADING_LINE_INDEX: case SNOWFLAKE_INDEX:
    case RAINDROP_INDEX: case PUDDLEDROP_INDEX: case AXIS_BILLBOARD_INDEX: return true;
    default: return false;
    }
}
d3scene::Vec3 value(const vector& v) { return {v.x,v.y,v.z}; }
d3scene::Transform basis(const matrix& m) { return {{},value(m.rvec),value(m.uvec),value(m.fvec)}; }
float alphaByte(float alpha) { return static_cast<int>((std::max)(0.f,(std::min)(255.f,alpha*255.f)))/255.f; }
struct FragmentAsset {
    uint32_t owner = 0;
    d3scene::MeshAssetRef mesh;
    int texture = -1;
};
std::array<FragmentAsset,MAX_OBJECTS> FragmentAssets{};
uint64_t FragmentLifetime = 0;
enum class EffectTopology { BlastRing, GravityDisk, WeaponRingEven, WeaponRingOdd, MassDriverTube, Count };
d3scene::MeshAssetRef effectTopology(EffectTopology kind) {
    static uint64_t lifetime = 0;
    static std::array<d3scene::MeshAssetRef,size_t(EffectTopology::Count)> assets{};
    if (lifetime != d3scene::assets().lifetime()) { assets.fill({}); lifetime = d3scene::assets().lifetime(); }
    auto& asset = assets[size_t(kind)];
    if (!asset) {
        if (kind == EffectTopology::MassDriverTube) {
            d3scene::MeshGeometry geometry;
            for (int end = 0; end < 2; ++end) for (int i = 0; i < 16; ++i) {
                const auto angle = static_cast<::angle>(i*(65536/16));
                geometry.vertices.push_back({-FixCos(angle),FixSin(angle),float(end)});
            }
            for (uint32_t i = 0; i < 16; ++i) {
                const uint32_t next = (i+1)%16;
                d3scene::MeshPolygon polygon;
                const auto angle = static_cast<::angle>((i+.5f)*(65536/16));
                polygon.normal = {-FixCos(angle),FixSin(angle),0};
                polygon.corners = {{i+16,0,0},{i,0,1},{next,1,1},{next+16,1,0}};
                geometry.polygons.push_back(std::move(polygon));
            }
            asset = d3scene::assets().createMesh(std::move(geometry));
            return asset;
        }
        const bool disk = kind == EffectTopology::GravityDisk;
        const bool weapon = kind == EffectTopology::WeaponRingEven || kind == EffectTopology::WeaponRingOdd;
        const int segments = disk ? 32 : 20;
        const float inner = disk ? 0.f : weapon ? .8f : .5f;
        auto geometry = d3scene::billboardRing(inner,disk ? 255 : weapon ? 85 : 0,disk ? 0 : 255,segments);
        for (int i = 0; i < segments; ++i) {
            const auto angle = static_cast<::angle>(i*(65536/segments));
            geometry.vertices[i*2] = {FixCos(angle),FixSin(angle),0};
            geometry.vertices[i*2+1] = {FixCos(angle)*inner,FixSin(angle)*inner,0};
        }
        if (weapon) {
            const size_t odd = kind == EffectTopology::WeaponRingOdd ? 1 : 0;
            for (size_t i = 0; i < 10; ++i) if (i != i*2+odd) geometry.polygons[i] = std::move(geometry.polygons[i*2+odd]);
            geometry.polygons.resize(10);
        } else if (!disk) {
            for (auto& polygon : geometry.polygons) {
                polygon.corners[0].u = 0; polygon.corners[0].v = 0;
                polygon.corners[1].u = 1; polygon.corners[1].v = 0;
                polygon.corners[2].u = 1; polygon.corners[2].v = 1;
                polygon.corners[3].u = 0; polygon.corners[3].v = 1;
            }
        }
        asset = d3scene::assets().createMesh(std::move(geometry));
    }
    return asset;
}
bool prepareFragment(d3scene::FrameData& frame,const d3scene::ObjectState& owner,const object& source) {
    const bool shard = source.type == OBJ_SHARD && source.render_type == RT_SHARD;
    const bool splinter = source.type == OBJ_SPLINTER && source.render_type == RT_SPLINTER;
    if (!shard && !splinter) return false;
    auto& cached = FragmentAssets[OBJNUM(&source)];
    if (!cached.mesh || cached.owner != owner.handle) {
        d3scene::MeshGeometry geometry;
        d3scene::MeshPolygon polygon;
        int texture = -1;
        if (shard) {
            const auto& fragment = source.rtype.shard_info;
            texture = fragment.tmap;
            for (uint32_t i = 0; i < 3; ++i) {
                geometry.vertices.push_back(value(fragment.points[i]));
                d3scene::MeshCorner corner; corner.vertex = i; corner.u = fragment.u[i]; corner.v = fragment.v[i];
                polygon.corners.push_back(corner);
            }
        } else {
            if (source.id >= MAX_POLY_MODELS || !Poly_models[source.id].used) return false;
            const auto& model = Poly_models[source.id];
            const auto& fragment = source.ctype.splinter_info;
            if (fragment.subobj_num >= model.n_models) return false;
            const auto& part = model.submodel[fragment.subobj_num];
            if (fragment.facenum < 0 || fragment.facenum >= part.num_faces) return false;
            const auto& face = part.faces[fragment.facenum];
            if (face.texnum < 0 || face.texnum >= model.n_textures) return false;
            texture = model.textures[face.texnum];
            const int count = std::min(int(face.nverts),MAX_VERTS_PER_SPLINTER);
            if (count < 3) return false;
            for (int i = 0; i < count; ++i) {
                geometry.vertices.push_back(value(fragment.verts[i]));
                d3scene::MeshCorner corner; corner.vertex = uint32_t(i); corner.u = face.u[i]; corner.v = face.v[i];
                polygon.corners.push_back(corner);
            }
        }
        if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) return false;
        vector a{geometry.vertices[1].x-geometry.vertices[0].x,geometry.vertices[1].y-geometry.vertices[0].y,geometry.vertices[1].z-geometry.vertices[0].z};
        vector b{geometry.vertices[2].x-geometry.vertices[0].x,geometry.vertices[2].y-geometry.vertices[0].y,geometry.vertices[2].z-geometry.vertices[0].z};
        vector normal; vm_CrossProduct(&normal,&a,&b);
        if (!(vm_NormalizeVector(&normal) > .000001f)) return false;
        polygon.normal = value(normal); geometry.polygons.push_back(std::move(polygon));
        cached.mesh = d3scene::assets().createMesh(std::move(geometry));
        cached.owner = owner.handle; cached.texture = texture;
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene fragment asset owner=%u type=%s revision=%llu",owner.handle,shard ? "shard" : "splinter",
                (unsigned long long)cached.mesh->id().revision);
    }
    const int bitmap = GetTextureBitmapAtTime(cached.texture,0,float(frame.simulation_time));
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    d3scene::PreparedMesh mesh; mesh.asset = cached.mesh;
    mesh.owner = owner.handle; mesh.has_owner = true; mesh.compiled_owner = size_t(&owner-frame.objects.data());
    mesh.room = owner.room; mesh.world = owner.current;
    if (splinter) {
        // The producer stores world-relative vertices and its original draw
        // rotates them by the matrix rows, unlike normal model-local pieces.
        mesh.world.right = {source.orient.rvec.x,source.orient.uvec.x,source.orient.fvec.x};
        mesh.world.up = {source.orient.rvec.y,source.orient.uvec.y,source.orient.fvec.y};
        mesh.world.forward = {source.orient.rvec.z,source.orient.uvec.z,source.orient.fvec.z};
    }
    mesh.surface.bitmap = bitmap; mesh.surface.unlit = true; mesh.surface.two_sided = shard;
    mesh.surface.alpha = alphaByte(shard ? .2f+.8f*GameTextures[cached.texture].alpha :
        source.lifetime > 0 ? source.lifeleft/source.lifetime : 0);
    mesh.surface.blend = mesh.surface.alpha < 1 ? d3scene::BlendMode::Alpha : d3scene::BlendMode::Solid;
    mesh.surface.depth = mesh.surface.alpha < 1 ? d3scene::DepthMode::ReadOnly : d3scene::DepthMode::ReadWrite;
    frame.meshes.push_back(std::move(mesh));
    return true;
}
void prepareMatcens(d3scene::FrameData& frame) {
    unsigned count = 0;
    for (int i = 0; i < Num_matcens; ++i) {
        MatcenVisualDisk disk;
        if (!Matcen[i] || !Matcen[i]->GetVisualDisk(disk) || !(disk.radius > 0)) continue;
        d3scene::PreparedMesh mesh;
        mesh.asset = d3scene::assets().radialDiskMesh(); mesh.room = disk.room;
        mesh.world.position = value(disk.position); mesh.alignment = d3scene::MeshAlignment::Camera;
        mesh.world.right.x = mesh.world.up.y = disk.radius;
        auto& surface = mesh.surface;
        surface.unlit = surface.two_sided = true; surface.blend = d3scene::BlendMode::Additive;
        surface.depth = d3scene::DepthMode::ReadOnly; surface.alpha = .7f;
        surface.color_modulation = disk.sine ? d3scene::Vec3{.6f,.4f,1} : d3scene::Vec3{.3f,.5f,1};
        frame.meshes.push_back(std::move(mesh));
        ++count;
    }
    if (count && AutomatedCapturePending()) AutomatedCaptureLog("scene Matcen disks=%u",count);
}
void prepareStroke(d3scene::FrameData& frame,d3scene::Vec3 from,d3scene::Vec3 to,
    int room,d3scene::Vec3 color,float alpha,float bias = 0) {
    if (from.x == to.x && from.y == to.y && from.z == to.z) return;
    d3scene::PreparedMesh mesh;
    mesh.asset = d3scene::assets().strokeMesh(); mesh.room = room;
    mesh.world = d3scene::axisBillboardPlacement(from,to,0); mesh.raster_stroke_width = 1;
    mesh.surface.unlit = mesh.surface.two_sided = true;
    mesh.surface.depth = d3scene::DepthMode::ReadOnly;
    mesh.surface.blend = d3scene::BlendMode::Additive; mesh.surface.alpha = alphaByte(alpha);
    mesh.surface.base_color = color; mesh.surface.effect_depth_offset = bias;
    frame.meshes.push_back(std::move(mesh));
}
bool prepareLineEffect(d3scene::FrameData& frame,const vis_effect& effect,unsigned slot) {
    const bool sine = effect.id == SINE_WAVE_INDEX;
    auto direction = effect.end_pos-effect.pos;
    const float length = vm_NormalizeVector(&direction);
    if (!(length >= 1) || !(effect.lifetime > 0)) return false;
    const float density = sine ? effect.velocity.x : effect.velocity.y;
    if (!std::isfinite(density) || !(density > 0)) return false;
    const int count = int(std::min(sine ? 256.f : 50.f,length*density));
    if (count < 2) return false;
    float alpha = sine || (effect.flags & VF_EXPAND) ? effect.lifeleft/effect.lifetime : .7f;
    if (!sine && !(effect.flags & VF_EXPAND) && (effect.flags & VF_NO_Z_ADJUST) && length > 30)
        alpha *= std::max(0.f,1-(length-30)/150);
    matrix plane; vm_VectorToMatrix(&plane,&direction,nullptr,nullptr);
    auto previous = effect.pos;
    uint32_t noise = uint32_t(std::floor(frame.simulation_time*60))*1664525u+slot*1013904223u;
    const auto jitter = [&] { noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
        return (int(noise%200)-100)/100.f; };
    const auto packed = GR_16_TO_COLOR(effect.lighting_color);
    const auto color = effect.id == GRAY_LIGHTNING_BOLT_INDEX ?
        d3scene::Vec3{GR_COLOR_RED(packed)/255.f,GR_COLOR_GREEN(packed)/255.f,GR_COLOR_BLUE(packed)/255.f} :
        d3scene::Vec3{10/255.f,60/255.f,200/255.f};
    // One immutable path for all cameras, generated without simulation RNG.
    // Segments share canonical topology and allocate no per-segment geometry.
    for (int i = 1; i < count; ++i) {
        auto next = i == count-1 ? effect.end_pos : effect.pos+direction*(length*i/(count-1));
        if (sine) {
            const auto phase = d3scene::visualAngle(frame.simulation_time,2000,slot*5000+i*4000);
            if (i < count-1) next += plane.rvec*(FixCos(phase)*effect.velocity.y)+plane.uvec*(FixSin(phase)*effect.velocity.y);
        } else if (i < count-1) next += plane.rvec*(jitter()*effect.velocity.x)+plane.uvec*(jitter()*effect.velocity.x);
        prepareStroke(frame,value(previous),value(next),effect.roomnum,color,alpha,(effect.flags & VF_ATTACHED) ? -.1f : 0);
        previous = next;
    }
    return true;
}
bool prepareRainDrop(d3scene::FrameData& frame,const vis_effect& effect,const d3scene::MeshAssetRef& quad,
    const GameMainView* main_view) {
    const bool windshield = effect.id == RAINDROP_INDEX;
    if (windshield && (!main_view || !main_view->player_first_person)) return false;
    if (!(effect.lifetime > 0) || !(effect.size > 0)) return false;
    const int bitmap = Fireballs[effect.id].bm_handle;
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
    if (width <= 0 || height <= 0) return false;
    const float age = std::max(0.f,float(frame.simulation_time)-effect.creation_time);
    const float life = std::min(.99999f,age/effect.lifetime);
    const float size = effect.size*(1-life*.5f);
    const float opacity = life < .1f ? life/.1f : life > .5f ? 2*(1-life) : 1;
    d3scene::PreparedMesh drop;
    drop.asset = quad; drop.room = effect.roomnum;
    if (windshield) {
        drop.collection = SceneCloseScreenEffects;
        drop.alignment = d3scene::MeshAlignment::Camera;
        drop.world.position = d3scene::transformPoint(main_view->world.camera,value(effect.pos));
    } else {
        auto normal = effect.end_pos;
        if (!(vm_GetMagnitudeFast(&normal) > .0001f)) return false;
        matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
        drop.world = basis(plane); drop.world.position = value(effect.pos);
    }
    drop.world.right.x *= size; drop.world.right.y *= size; drop.world.right.z *= size;
    const float vertical = size*height/width;
    drop.world.up.x *= vertical; drop.world.up.y *= vertical; drop.world.up.z *= vertical;
    auto& surface = drop.surface;
    surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
    surface.texture_alpha = true; surface.texture_wrap = false;
    surface.blend = d3scene::BlendMode::Additive;
    surface.depth = windshield ? d3scene::DepthMode::Disabled : d3scene::DepthMode::ReadOnly;
    surface.alpha = alphaByte(opacity*(windshield ? .4f : .2f));
    frame.meshes.push_back(std::move(drop));
    return true;
}
bool prepareFadingLine(d3scene::FrameData& frame,const vis_effect& effect) {
    vector end = effect.end_pos;
    const bool rain = (effect.flags & VF_WINDSHIELD_EFFECT) != 0;
    if (!rain) {
        auto direction = -effect.velocity;
        if (!(vm_NormalizeVectorFast(&direction) > 0)) return false;
        end = effect.pos+direction*effect.size;
    }
    if (effect.pos.x == end.x && effect.pos.y == end.y && effect.pos.z == end.z) return false;
    const float age = std::max(0.f,float(frame.simulation_time)-effect.creation_time);
    const float life = effect.lifetime > 0 ? std::min(.99999f,age/effect.lifetime) : 1.f;
    d3scene::PreparedMesh stroke;
    stroke.asset = d3scene::assets().fadingStrokeMesh();
    stroke.world = d3scene::axisBillboardPlacement(value(effect.pos),value(end),0);
    stroke.raster_stroke_width = 1;
    stroke.room = effect.roomnum;
    stroke.surface.unlit = true; stroke.surface.two_sided = true;
    stroke.surface.blend = d3scene::BlendMode::Additive;
    stroke.surface.alpha = alphaByte(rain ? .3f : 1-life);
    const auto color = GR_16_TO_COLOR(effect.lighting_color);
    stroke.surface.base_color = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
    frame.meshes.push_back(std::move(stroke));
    return true;
}
d3scene::PreparedMesh prepareRing(float radius,float phase,float alpha,int bitmap,int room) {
    // Shape and opacity profile are canonical; radius/phase are instance data.
    d3scene::PreparedMesh ring;
    ring.asset = effectTopology(EffectTopology::BlastRing);
    ring.room = room;
    const auto angle = static_cast<::angle>(int(phase*65536)%65536);
    ring.world.right = {FixCos(angle)*radius,FixSin(angle)*radius,0};
    ring.world.up = {-FixSin(angle)*radius,FixCos(angle)*radius,0};
    auto& surface = ring.surface;
    surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
    surface.texture_alpha = true; surface.texture_wrap = true;
    surface.blend = d3scene::BlendMode::Additive; surface.alpha = alpha;
    return ring;
}
bool prepareVisRing(d3scene::FrameData& frame,const vis_effect& effect) {
    if (!(effect.lifetime > 0) || !std::isfinite(effect.size) || effect.size <= 0 ||
        effect.custom_handle <= BAD_BITMAP_HANDLE || effect.custom_handle >= MAX_BITMAPS ||
        !GameBitmaps[effect.custom_handle].used) return false;
    float life = (effect.lifetime-effect.lifeleft)/effect.lifetime;
    if (effect.flags & VF_REVERSE) life = 1-life;
    life = (std::max)(0.f,(std::min)(1.f,life));
    auto ring = prepareRing(life*effect.size,0,1-life,effect.custom_handle,effect.roomnum);
    if (effect.flags & VF_PLANAR) {
        vector normal = effect.end_pos;
        if (vm_GetMagnitudeFast(&normal) < .5f) return false;
        matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
        ring.world = d3scene::compose(basis(plane),ring.world);
    } else {
        ring.alignment = d3scene::MeshAlignment::FacingEye;
        // FacingEye's canonical forward points away from the eye; this authored
        // effect uses the opposite normal, including its texture handedness.
        d3scene::Transform flip; flip.right = {-1,0,0}; flip.forward = {0,0,-1};
        ring.world = d3scene::compose(flip,ring.world);
    }
    ring.world.position = value(effect.pos);
    ring.surface.u_offset = life; ring.surface.effect_depth_offset = -1;
    ring.surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
    frame.meshes.push_back(std::move(ring));
    return true;
}
bool prepareObjectRing(d3scene::FrameData& frame,const d3scene::ObjectState& owner,const object& source,
    const d3scene::MeshAssetRef& quad) {
    if (!(source.lifetime > 0) || !(source.ctype.blast_info.max_size > 0)) return false;
    const int bitmap = source.ctype.blast_info.bm_handle;
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    const float life = (std::max)(0.f,(std::min)(1.f,(source.lifetime-source.lifeleft)/source.lifetime));
    const bool gravity = source.id == GRAVITY_FIELD_INDEX;
    const float radius = (gravity ? 1-life : life)*source.ctype.blast_info.max_size;
    const float phase = life < .2f ? life*3 : .6f+(life-.2f)*.5f;
    auto ring = prepareRing(radius*2,phase,1-phase,bitmap,source.roomnum);
    // The old gravity draw forced ObjSetOrient(identity) during rendering.
    // This is a presentation choice, never a simulation orientation update.
    const auto& orientation = gravity ? Identity_matrix : source.orient;
    auto plane = basis(orientation);
    plane.up = value(orientation.fvec); plane.forward = value(-orientation.uvec);
    ring.world = d3scene::compose(plane,ring.world);
    ring.world.position = value(source.pos); ring.has_owner = true; ring.owner = owner.handle;
    ring.surface.effect_depth_offset = -radius*.5f;
    ring.surface.soft_depth_range = Render_soft_vis_effects ? 2.f : 0.f;
    const float disk_scale = life < .1f ? life*10 : (std::max)(0.f,1-(life-.1f)*1.5f);
    const int disk_bitmap = Fireballs[SHRINKING_BLAST_INDEX].bm_handle;
    if (gravity && disk_scale > 0) {
        d3scene::PreparedMesh disk;
        disk.asset = effectTopology(EffectTopology::GravityDisk);
        disk.has_owner = true; disk.owner = owner.handle; disk.room = source.roomnum;
        disk.alignment = d3scene::MeshAlignment::Camera; disk.world.position = value(source.pos);
        const float size = disk_scale*source.ctype.blast_info.max_size;
        disk.world.right.x = size; disk.world.up.y = size;
        disk.surface = ring.surface; disk.surface.bitmap = -1; disk.surface.texture_alpha = false;
        disk.surface.alpha = .9f; disk.surface.base_color = {.3f,.5f,1};
        frame.meshes.push_back(std::move(disk));
    } else if (!gravity && disk_scale > 0 && disk_bitmap > BAD_BITMAP_HANDLE && disk_bitmap < MAX_BITMAPS && GameBitmaps[disk_bitmap].used) {
        const int width = bm_w(disk_bitmap,0), height = bm_h(disk_bitmap,0);
        if (width > 0 && height > 0) {
            d3scene::PreparedMesh disk;
            disk.asset = quad; disk.has_owner = true; disk.owner = owner.handle; disk.room = source.roomnum;
            disk.alignment = d3scene::MeshAlignment::Camera;
            matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(d3scene::visualAngle(frame.simulation_time,life < .1f ? 20.f : 550.f,0)));
            vm_TransposeMatrix(&spin); disk.world = basis(spin); disk.world.position = value(source.pos);
            const float size = disk_scale*source.ctype.blast_info.max_size;
            disk.world.right = value(spin.rvec*size); disk.world.up = value(spin.uvec*(size*height/width));
            disk.surface = ring.surface; disk.surface.bitmap = disk_bitmap;
            disk.surface.alpha = alphaByte(disk_scale); disk.surface.texture_wrap = false;
            frame.meshes.push_back(std::move(disk));
        }
    }
    frame.meshes.push_back(std::move(ring));
    return true;
}
bool prepareAxisEffect(d3scene::FrameData& frame,const vis_effect& effect,const d3scene::MeshAssetRef& quad) {
    const bool trail = effect.id == BILLBOARD_SMOKETRAIL_INDEX;
    const bool beam = effect.id == THICK_LIGHTNING_INDEX;
    const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-effect.creation_time);
    const float normalized = effect.lifetime > 0 ? (std::min)(.99999f,age/effect.lifetime) : 0;
    const float remaining = effect.lifetime > 0 ? (std::max)(0.f,effect.lifeleft/effect.lifetime) : 0;
    float width = effect.billboard_info.width;
    float alpha = effect.flags & VF_EXPAND ? remaining : 1.f;
    if (beam && !(effect.flags & VF_EXPAND)) alpha = .7f;
    if (trail) {
        width *= remaining;
        if (remaining > .8f) width *= 1-(remaining-.8f)/.2f;
        alpha = remaining*.7f;
    }
    if (!(width > 0) || !std::isfinite(width)) return false;
    d3scene::PreparedMesh rod;
    rod.asset = quad; rod.room = effect.roomnum; rod.alignment = d3scene::MeshAlignment::Axis;
    rod.world = d3scene::axisBillboardPlacement(value(effect.pos),value(effect.end_pos),width);
    auto& surface = rod.surface;
    surface.unlit = true; surface.two_sided = true;
    surface.texture_alpha = true; surface.texture_wrap = false;
    surface.blend = d3scene::BlendMode::Alpha;
    surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
    int bitmap = effect.custom_handle;
    if (beam || trail || effect.billboard_info.texture) {
        const int texture = effect.custom_handle;
        if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) return false;
        const auto& material = GameTextures[texture];
        if (beam) bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time),true);
        else if (material.flags & TF_ANIMATED) {
            const int handle = material.bm_handle;
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return false;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (!animation.frames || animation.num_frames <= 0) return false;
            bitmap = animation.frames[d3scene::animationFrames(animation.num_frames,normalized,false).frame0];
        } else bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
        if (material.flags & TF_SATURATE) surface.blend = d3scene::BlendMode::Additive;
        if (!trail && !beam) {
            alpha *= material.alpha;
            surface.u_offset = material.slide_u ? std::fmod(static_cast<float>(frame.simulation_time)/material.slide_u,1.f) : 0;
            surface.v_offset = material.slide_v ? std::fmod(static_cast<float>(frame.simulation_time)/material.slide_v,1.f) : 0;
            surface.texture_wrap = surface.u_offset != 0 || surface.v_offset != 0;
        }
    }
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    if (trail || beam) {
        const auto color = GR_16_TO_COLOR(effect.lighting_color);
        surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
    }
    if (beam) {
        auto delta = effect.end_pos-effect.pos;
        const float length = vm_GetMagnitude(&delta);
        if (length < 1 || !std::isfinite(effect.velocity.x) || effect.velocity.x <= 0) return false;
        surface.texture_wrap = true; surface.blend = d3scene::BlendMode::Additive;
        surface.texture_scale.y = effect.velocity.z*(effect.billboard_info.texture ? length/width : 1.f);
        surface.v_offset = effect.velocity.y ? std::fmod(static_cast<float>(frame.simulation_time)/effect.velocity.y,1.f) : 0;
        // Legacy repeats an identical additive quad ceil(velocity.x) times.
        // Publish its total radiance once instead of replaying geometry.
        const float copies = std::ceil(effect.velocity.x);
        surface.color_modulation.x *= copies; surface.color_modulation.y *= copies; surface.color_modulation.z *= copies;
    }
    surface.bitmap = bitmap;
    surface.alpha = trail ? (std::min)(1.f,alpha) : alphaByte(alpha);
    frame.meshes.push_back(std::move(rod));
    return true;
}
bool prepareMassDriver(d3scene::FrameData& frame,const vis_effect& effect,const d3scene::MeshAssetRef& quad) {
    if (!(effect.lifetime > 0) || !(effect.billboard_info.width > 0) ||
        effect.custom_handle < 0 || effect.custom_handle >= MAX_TEXTURES || !GameTextures[effect.custom_handle].used) return false;
    vector direction = effect.end_pos-effect.pos;
    const float length = vm_NormalizeVector(&direction);
    if (!(length > .001f) || !std::isfinite(length)) return false;
    const bool boss = effect.id == MERCBOSS_MASSDRIVER_EFFECT_INDEX;
    const float radius = effect.billboard_info.width*.25f*(boss ? 3.f : 1.f);
    const float alpha = std::max(0.f,std::min(1.f,effect.lifeleft/effect.lifetime*.5f));
    if (!(alpha > 0)) return true;
    const int bitmap = GetTextureBitmapAtTime(effect.custom_handle,0,float(frame.simulation_time));
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    matrix plane; vm_VectorToMatrix(&plane,&direction,nullptr,nullptr);
    const auto packed = GR_16_TO_COLOR(effect.lighting_color);
    const d3scene::Vec3 outer{GR_COLOR_RED(packed)/255.f,GR_COLOR_GREEN(packed)/255.f,GR_COLOR_BLUE(packed)/255.f};
    for (int layer = 0; layer < 2; ++layer) {
        const float size = layer ? radius : radius*.25f;
        d3scene::PreparedMesh tube; tube.asset = effectTopology(EffectTopology::MassDriverTube);
        tube.world = basis(plane); tube.world.position = value(effect.pos);
        tube.world.right = value(plane.rvec*size); tube.world.up = value(plane.uvec*size); tube.world.forward = value(plane.fvec*length);
        tube.room = effect.roomnum; tube.surface.bitmap = bitmap;
        tube.surface.unlit = tube.surface.two_sided = tube.surface.texture_alpha = true;
        tube.surface.blend = d3scene::BlendMode::Additive; tube.surface.depth = d3scene::DepthMode::ReadOnly;
        tube.surface.alpha = alpha; tube.surface.texture_scale.y = length*.5f;
        tube.surface.color_modulation = layer ? outer : boss ? d3scene::Vec3{1,170/255.f,170/255.f} : d3scene::Vec3{200/255.f,200/255.f,1};
        frame.meshes.push_back(std::move(tube));
    }
    if (boss) return true;
    static int trail_texture = -1;
    if (trail_texture < 0 || trail_texture >= MAX_TEXTURES || !GameTextures[trail_texture].used ||
        stricmp(GameTextures[trail_texture].name,"MassTrail")) trail_texture = FindTextureName("MassTrail");
    if (trail_texture < 0) return true;
    const int trail_bitmap = GetTextureBitmapAtTime(trail_texture,0,float(frame.simulation_time));
    if (trail_bitmap <= BAD_BITMAP_HANDLE || trail_bitmap >= MAX_BITMAPS || !GameBitmaps[trail_bitmap].used) return true;
    const int width = bm_w(trail_bitmap,0),height = bm_h(trail_bitmap,0);
    if (width <= 0 || height <= 0) return true;
    const int rings = int(std::min(400.f,length/3));
    const float interval = length/3 > 400 ? length/400 : 3;
    const auto roll = d3scene::visualAngle(frame.simulation_time,-4*65536.f,0);
    for (int i = 1; i < rings; ++i) {
        const float size = 1+FixSin(static_cast<::angle>(i*9000+roll))*.3f;
        matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<::angle>(i*2000));
        d3scene::PreparedMesh ring; ring.asset = quad; ring.world = d3scene::compose(basis(plane),basis(spin));
        ring.world.right = d3scene::transformDirection(basis(plane),value(spin.rvec*size));
        ring.world.up = d3scene::transformDirection(basis(plane),value(spin.uvec*(size*height/width)));
        ring.world.position = value(effect.pos+direction*(i*interval)); ring.room = effect.roomnum;
        ring.surface.bitmap = trail_bitmap; ring.surface.unlit = ring.surface.two_sided = ring.surface.texture_alpha = true;
        ring.surface.blend = d3scene::BlendMode::Additive; ring.surface.depth = d3scene::DepthMode::ReadOnly;
        ring.surface.alpha = alpha; ring.surface.texture_wrap = false;
        frame.meshes.push_back(std::move(ring));
    }
    return true;
}
bool prepareLegacySnow(d3scene::FrameData& frame,const vis_effect& effect,const d3scene::MeshAssetRef& quad) {
    if (!(effect.lifetime > 0) || !(effect.size > 0)) return false;
    const int bitmap = Fireballs[SNOWFLAKE_INDEX].bm_handle;
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    const int width = bm_w(bitmap,0),height = bm_h(bitmap,0);
    if (width <= 0 || height <= 0) return false;
    const float age = std::max(0.f,float(frame.simulation_time)-effect.creation_time);
    const float life = std::min(.99999f,age/effect.lifetime);
    d3scene::PreparedMesh snow; snow.asset = quad; snow.room = effect.roomnum;
    snow.alignment = d3scene::MeshAlignment::Camera; snow.world.position = value(effect.pos);
    snow.world.right.x = effect.size; snow.world.up.y = effect.size*height/width;
    const auto color = GR_16_TO_COLOR(effect.lighting_color);
    snow.surface.bitmap = bitmap; snow.surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
    snow.surface.unlit = snow.surface.two_sided = snow.surface.texture_alpha = true;
    snow.surface.texture_wrap = false; snow.surface.blend = d3scene::BlendMode::Additive; snow.surface.depth = d3scene::DepthMode::ReadOnly;
    snow.surface.alpha = alphaByte((1-life)*std::min(1.f,age/.1f)*.6f);
    snow.surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0;
    frame.meshes.push_back(std::move(snow));
    return true;
}
bool prepareEnhancedSnow(d3scene::FrameData& frame,const enhanced_snow_particle& particle,
    int bitmap,const d3scene::MeshAssetRef& quad) {
    if (!(particle.lifetime > 0) || !(particle.size > 0) || !(particle.lifeleft > 0) || bitmap <= BAD_BITMAP_HANDLE) return false;
    const float age = std::max(0.f,float(frame.simulation_time)-particle.creation_time);
    const float life = std::min(.99999f,age/particle.lifetime);
    const bool settled = (particle.flags & 2) != 0;
    const float phase = particle.phase+age*particle.flutter_frequency;
    d3scene::PreparedMesh snow; snow.asset = quad; snow.room = particle.roomnum;
    float alpha;
    if (settled) {
        vector normal = particle.velocity;
        if (!(vm_NormalizeVector(&normal) > .000001f)) return false;
        matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
        snow.world = basis(plane);
        snow.world.right = value(plane.rvec*(particle.size*.82f)); snow.world.up = value(plane.uvec*(particle.size*.48f));
        alpha = std::min(1.f,particle.lifeleft/.32f)*.26f;
    } else {
        snow.alignment = d3scene::MeshAlignment::Velocity;
        snow.world.right.x = particle.size*(.42f+(.48f+.52f*std::abs(std::cos(phase*.73f)))*.38f);
        snow.world.up.y = particle.size*1.10f;
        snow.billboard_motion.velocity = value(particle.velocity); snow.billboard_motion.bank = std::sin(phase)*.42f;
        snow.billboard_motion.speed_stretch = particle.size*.015f;
        snow.billboard_motion.maximum_stretch = particle.size*1.45f; snow.billboard_motion.proximity_range = 85;
        alpha = std::min(1.f,age/.24f)*(life > .82f ? (1-life)/.18f : 1)*std::min(.75f,.45f+particle.size*.28f);
        if (particle.flags & 4) alpha *= std::min(1.f,particle.lifeleft/.32f);
    }
    snow.world.position = value(particle.pos);
    const auto color = GR_16_TO_COLOR(particle.lighting_color);
    auto& surface = snow.surface;
    surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
    surface.bitmap = bitmap; surface.unlit = surface.two_sided = surface.texture_alpha = true;
    surface.texture_scale.x = .25f; surface.u_offset = (particle.variant&3)*.25f; surface.texture_wrap = false;
    surface.alpha = alphaByte(alpha); surface.blend = d3scene::BlendMode::Additive; surface.depth = d3scene::DepthMode::ReadOnly;
    frame.meshes.push_back(std::move(snow));
    return true;
}
void prepareWeaponContributions(d3scene::FrameData& frame,const d3scene::ObjectState& owner,
    const object& source,const d3scene::MeshAssetRef& quad) {
    const auto& weapon = Weapons[source.id];
    const auto plan = CompileWeaponVisuals(weapon.flags);
    const float remaining = source.lifetime > 0 ? std::max(0.f,std::min(1.f,source.lifeleft/source.lifetime)) : 0;
    const float streamer_remaining = source.lifetime > 0 ?
        std::max(0.f,std::min(1.f,1-float(frame.simulation_time-source.creation_time)/source.lifetime)) : 0;
    const d3scene::Vec3 color{weapon.lighting_info.red_light1,weapon.lighting_info.green_light1,weapon.lighting_info.blue_light1};
    if (plan.streamer && source.lifetime > 0) {
        const auto* parent = ObjGet(source.parent_handle);
        if (parent) {
            const float length = vm_VectorDistance(&parent->pos,&source.pos);
            if (length > .00001f && streamer_remaining > 0) {
                d3scene::PreparedMesh stroke;
                stroke.asset = d3scene::assets().fadingStrokeMesh();
                stroke.world = d3scene::axisBillboardPlacement(value(source.pos),value(source.pos-source.orient.fvec*length),0);
                stroke.raster_stroke_width = 1; stroke.room = owner.room;
                stroke.has_owner = true; stroke.owner = owner.handle;
                stroke.surface.unlit = stroke.surface.two_sided = true;
                stroke.surface.blend = d3scene::BlendMode::Alpha;
                stroke.surface.depth = d3scene::DepthMode::ReadOnly;
                stroke.surface.alpha = streamer_remaining*.3f; stroke.surface.base_color = color;
                frame.meshes.push_back(std::move(stroke));
            }
        }
    }
    if (!plan.ring || !(source.size > 0) || !(remaining > 0)) return;
    if (weapon.flags & (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP)) {
        int bitmap = -1;
        if (weapon.flags & WF_IMAGE_BITMAP) bitmap = GetWeaponFireImageAtTime(source.id,0,float(frame.simulation_time));
        else {
            const int clip = weapon.fire_image_handle;
            if (clip < 0 || clip >= MAX_VCLIPS || !GameVClips[clip].used) return;
            PageInVClip(clip);
            const auto& animation = GameVClips[clip];
            if (!animation.frames || animation.num_frames <= 0) return;
            bitmap = animation.frames[d3scene::animationFrames(animation.num_frames,std::min(.999f,1-remaining),false).frame0];
        }
        if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return;
        const int width = bm_w(bitmap,0),height = bm_h(bitmap,0);
        if (width <= 0 || height <= 0) return;
        for (int i = 0; i < 20; ++i) {
            const auto angle = static_cast<::angle>(i*(65536/20));
            matrix spin; vm_AnglesToMatrix(&spin,0,0,angle); vm_TransposeMatrix(&spin);
            d3scene::PreparedMesh blob; blob.asset = quad;
            blob.world = basis(spin); blob.alignment = d3scene::MeshAlignment::Camera;
            blob.world.position = value(source.pos+(source.orient.rvec*FixCos(angle)+source.orient.uvec*FixSin(angle))*source.size);
            blob.world.right = value(spin.rvec*(source.size*.25f));
            blob.world.up = value(spin.uvec*(source.size*.25f*height/width));
            blob.has_owner = true; blob.owner = owner.handle; blob.room = owner.room;
            blob.surface.bitmap = bitmap; blob.surface.texture_alpha = true; blob.surface.texture_wrap = false;
            blob.surface.unlit = blob.surface.two_sided = true; blob.surface.alpha = alphaByte(remaining);
            blob.surface.blend = d3scene::BlendMode::Alpha; blob.surface.depth = d3scene::DepthMode::ReadOnly;
            frame.meshes.push_back(std::move(blob));
        }
    } else {
        d3scene::PreparedMesh ring;
        ring.asset = effectTopology(OBJNUM(&source)%2 ? EffectTopology::WeaponRingOdd : EffectTopology::WeaponRingEven);
        ring.world = owner.current;
        ring.world.right = value(source.orient.rvec*source.size);
        ring.world.up = value(source.orient.uvec*source.size);
        ring.has_owner = true; ring.owner = owner.handle; ring.room = owner.room;
        ring.surface.unlit = ring.surface.two_sided = true;
        ring.surface.blend = d3scene::BlendMode::Additive; ring.surface.depth = d3scene::DepthMode::ReadOnly;
        ring.surface.base_color = color; ring.surface.alpha = remaining*.3f;
        frame.meshes.push_back(std::move(ring));
    }
}
}
// Object effects share owned geometry with model attachments. Publication is
// after object eligibility; per-view exclusion is applied by the scene query.
void PrepareGameObjectEffects(d3scene::FrameData& frame) {
    const auto quad = d3scene::assets().quadMesh();
    if (FragmentLifetime != frame.scene_lifetime) {
        for (auto& fragment : FragmentAssets) fragment = {};
        FragmentLifetime = frame.scene_lifetime;
    }
    unsigned fragments = 0;
    for (const auto& owner : frame.objects) {
        auto* source = ObjGet(owner.handle);
        if (!source || !owner.visible) continue;
        if (source->render_type == RT_WEAPON && source->type == OBJ_WEAPON && source->id < MAX_WEAPONS) {
            const auto before = frame.meshes.size();
            prepareWeaponContributions(frame,owner,*source,quad);
            if (frame.meshes.size() != before && AutomatedCapturePending())
                AutomatedCaptureLog("scene weapon independent contributions owner=%u invisible=%d meshes=%u",owner.handle,
                    (Weapons[source->id].flags & WF_INVISIBLE) != 0,unsigned(frame.meshes.size()-before));
        }
        if (prepareFragment(frame,owner,*source)) { ++fragments; continue; }
        if (source->render_type == RT_FIREBALL && source->type == OBJ_FIREBALL && source->id == SMOLDERING_INDEX) {
            const int clip = Fireballs[SMOLDERING_INDEX].bm_handle;
            if (clip < 0 || clip >= MAX_VCLIPS || !GameVClips[clip].used || !(source->ctype.blast_info.max_size > 0)) continue;
            PageInVClip(clip);
            if (!GameVClips[clip].frames || GameVClips[clip].num_frames < 4) continue;
            const int bitmap = GameVClips[clip].frames[3];
            if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) continue;
            d3scene::PreparedMesh smoke; smoke.asset = quad;
            smoke.has_owner = true; smoke.owner = owner.handle; smoke.room = owner.room;
            smoke.alignment = d3scene::MeshAlignment::Axis;
            auto top = value(source->pos); top.y += source->ctype.blast_info.max_size;
            smoke.world = d3scene::axisBillboardPlacement(top,value(source->pos),10);
            smoke.surface.bitmap = bitmap; smoke.surface.unlit = smoke.surface.two_sided = true;
            smoke.surface.texture_alpha = true; smoke.surface.texture_wrap = false;
            smoke.surface.alpha = alphaByte(.4f); smoke.surface.blend = d3scene::BlendMode::Alpha;
            smoke.surface.depth = d3scene::DepthMode::ReadOnly;
            frame.meshes.push_back(std::move(smoke));
            continue;
        }
        if (source->render_type == RT_FIREBALL && source->type == OBJ_FIREBALL &&
            (source->id == BLAST_RING_INDEX || source->id == GRAVITY_FIELD_INDEX)) {
            prepareObjectRing(frame,owner,*source,quad);
            continue;
        }
        if (source->render_type == RT_WEAPON && source->type == OBJ_WEAPON) {
            const auto& weapon = Weapons[source->id];
            if (CompileWeaponVisuals(weapon.flags).body != WeaponBodyVisual::Sprite || WeaponIsCloseScreenEffectObject(source)) continue;
            if (weapon.flags & WF_IMAGE_VCLIP) {
                const int clip = weapon.fire_image_handle;
                if (clip < 0 || clip >= MAX_VCLIPS || !GameVClips[clip].used) continue;
                PageInVClip(clip);
                if (!GameVClips[clip].frames || GameVClips[clip].num_frames <= 0 || !(GameVClips[clip].frame_time > 0)) continue;
            }
            const int bitmap = GetWeaponFireImageAtTime(source->id,0,static_cast<float>(frame.simulation_time));
            if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) continue;
            const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
            if (width <= 0 || height <= 0 || !(source->size > 0)) continue;
            const int rotation = weapon.flags & WF_NO_ROTATE ? static_cast<int>(source-Objects)*1000%65536 :
                d3scene::weaponSpriteAngle(frame.simulation_time,weapon.phys_info.rotvel.z);
            matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(rotation));
            d3scene::PreparedMesh sprite;
            sprite.asset = quad; sprite.has_owner = true; sprite.owner = owner.handle; sprite.room = source->roomnum;
            if (weapon.flags & WF_PLANAR) {
                vector normal = source->mtype.phys_info.velocity;
                if (vm_GetMagnitude(&normal) < .0001f) continue;
                matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
                sprite.world = d3scene::compose(basis(plane),basis(spin));
            } else {
                vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.alignment = d3scene::MeshAlignment::Camera;
            }
            sprite.world.position = value(source->pos);
            auto& r = sprite.world.right; auto& u = sprite.world.up;
            const float size = source->size, vertical = size*height/width;
            r = {r.x*size,r.y*size,r.z*size}; u = {u.x*vertical,u.y*vertical,u.z*vertical};
            auto& surface = sprite.surface;
            surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
            surface.texture_alpha = true; surface.texture_wrap = false;
            surface.alpha = alphaByte(weapon.alpha);
            surface.blend = weapon.flags & WF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
            frame.meshes.push_back(std::move(sprite));
            continue;
        }
        if (source->render_type != RT_FIREBALL || source->type != OBJ_FIREBALL) continue;
        if (source->id >= NUM_FIREBALLS) continue;
        const auto& definition = Fireballs[source->id];
        const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-source->creation_time);
        const float normalized = source->lifetime > 0 ? (std::min)(.99999f,age/source->lifetime) : .99999f;
        float size = source->size;
        int bitmap = -1;
        const auto clip = [&](int handle) {
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (animation.frames && animation.num_frames > 0)
                bitmap = animation.frames[d3scene::animationFrames(animation.num_frames,normalized,false).frame0];
        };
        if (source->id == SMOKE_TRAIL_INDEX || source->id == CUSTOM_EXPLOSION_INDEX) {
            const int texture = source->ctype.blast_info.bm_handle;
            if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) continue;
            const auto& material = GameTextures[texture];
            if (source->id == SMOKE_TRAIL_INDEX) {
                if (material.flags & TF_ANIMATED) clip(material.bm_handle);
                else bitmap = material.bm_handle;
            } else {
                bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
                if (!(material.flags & TF_ANIMATED))
                    size *= normalized < .5f ? (std::min)(1.f,normalized/.2f) : 2*(1-normalized);
            }
        } else if (definition.type == FT_SPARK) { bitmap = definition.bm_handle; size *= 1-normalized; }
        else clip(definition.bm_handle);
        const bool smoke = definition.type == FT_SMOKE;
        if (smoke && normalized > .3f) size *= 1+(normalized-.3f)/.7f*2.3f;
        size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE));
        if (!(size > 0) || !std::isfinite(size) || bitmap <= BAD_BITMAP_HANDLE ||
            bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) continue;
        const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
        if (width <= 0 || height <= 0) continue;
        d3scene::PreparedMesh sprite;
        sprite.asset = quad; sprite.has_owner = true; sprite.owner = owner.handle;
        sprite.room = source->roomnum; sprite.alignment = d3scene::MeshAlignment::Camera;
        matrix spin;
        vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(d3scene::visualAngle(frame.simulation_time,
            definition.type == FT_BILLOW ? 160.f : 30.f,static_cast<int>(source-Objects)*5000)));
        vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.world.position = value(source->pos);
        sprite.world.right = value(spin.rvec*size); sprite.world.up = value(spin.uvec*(size*height/width));
        auto& surface = sprite.surface;
        surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
        surface.texture_alpha = true; surface.texture_wrap = false;
        surface.blend = smoke || source->id == CUSTOM_EXPLOSION_INDEX ? d3scene::BlendMode::Alpha : d3scene::BlendMode::Additive;
        surface.alpha = alphaByte((normalized > .5f ? 2*(1-normalized) : 1)*(smoke ? SMOKE_ALPHA : FIREBALL_ALPHA));
        surface.soft_depth_range = Render_soft_vis_effects ? 2.f : 0.f;
        surface.effect_depth_offset = Render_soft_vis_effects && smoke ? 0.f : -2.f;
        frame.meshes.push_back(std::move(sprite));
    }
    // Drop source ownership for dead allocations. Retained presentations keep
    // their own assets alive; a recycled object index cannot inherit geometry.
    for (auto& fragment : FragmentAssets) if (fragment.mesh) {
        const auto* source = ObjGet(fragment.owner);
        if (!source || (source->render_type != RT_SHARD && source->render_type != RT_SPLINTER)) fragment = {};
    }
    if (fragments && AutomatedCapturePending()) AutomatedCaptureLog("scene fragments instances=%u",fragments);
}
void PrepareGameEffects(d3scene::FrameData& frame,const GameMainView* main_view) {
    prepareMatcens(frame);
    const auto quad = d3scene::assets().quadMesh();
    unsigned prepared = 0, pending = 0, windshield = 0;
    unsigned mass_drivers = 0,boss_mass_drivers = 0,legacy_snow = 0,enhanced_snow = 0;
    int snow_count = 0;
    const auto* snow_particles = Render_enhanced_weather ? GetEnhancedSnowParticles(&snow_count) : nullptr;
    const int snow_atlas = snow_count > 0 ? PrepareEnhancedSnowAtlas() : BAD_BITMAP_HANDLE;
    for (int i = 0; i < snow_count; ++i) if (prepareEnhancedSnow(frame,snow_particles[i],snow_atlas,quad)) { ++prepared; ++enhanced_snow; }
    for (int slot = 0; VisEffects && slot <= Highest_vis_effect_index; ++slot) {
        const auto& effect = VisEffects[slot];
        if (effect.type == VIS_NONE || (effect.flags & VF_DEAD)) continue;
        if (effect.type == VIS_FIREBALL && effect.id == SNOWFLAKE_INDEX && (effect.flags & VF_ENHANCED_SNOW) &&
            !(effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            enhanced_snow_particle particle{};
            particle.pos = effect.pos; particle.velocity = effect.velocity; particle.size = effect.size;
            particle.creation_time = effect.creation_time; particle.lifeleft = effect.lifeleft; particle.lifetime = effect.lifetime;
            particle.phase = effect.mass; particle.flutter_frequency = effect.drag;
            particle.roomnum = effect.roomnum; particle.lighting_color = effect.lighting_color; particle.variant = effect.custom_handle&3;
            particle.flags = ((effect.custom_handle&4) ? 2 : 0) | ((effect.custom_handle&8) ? 4 : 0);
            if (prepareEnhancedSnow(frame,particle,PrepareEnhancedSnowAtlas(),quad)) { ++prepared; ++enhanced_snow; }
            continue;
        }
        if (effect.type == VIS_FIREBALL && (effect.id == MASSDRIVER_EFFECT_INDEX || effect.id == MERCBOSS_MASSDRIVER_EFFECT_INDEX)) {
            if (prepareMassDriver(frame,effect,quad)) { ++prepared; if (effect.id == MASSDRIVER_EFFECT_INDEX) ++mass_drivers; else ++boss_mass_drivers; }
            continue;
        }
        if (effect.type == VIS_FIREBALL && effect.id == SNOWFLAKE_INDEX &&
            !(effect.flags & (VF_ENHANCED_SNOW | VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            if (prepareLegacySnow(frame,effect,quad)) { ++prepared; ++legacy_snow; }
            continue;
        }
        if (effect.type == VIS_FIREBALL && (effect.id == LIGHTNING_BOLT_INDEX ||
            effect.id == GRAY_LIGHTNING_BOLT_INDEX || effect.id == SINE_WAVE_INDEX)) {
            if (prepareLineEffect(frame,effect,slot)) ++prepared;
            continue;
        }
        if (effect.type == VIS_FIREBALL && (effect.id == RAINDROP_INDEX || effect.id == PUDDLEDROP_INDEX)) {
            if (prepareRainDrop(frame,effect,quad,main_view)) {
                ++prepared;
                if (effect.id == RAINDROP_INDEX) ++windshield;
            }
            continue;
        }
        if (effect.type == VIS_FIREBALL && effect.id == FADING_LINE_INDEX) {
            if (prepareFadingLine(frame,effect)) ++prepared;
            continue;
        }
        if (effect.type == VIS_FIREBALL && effect.id == BLAST_RING_INDEX &&
            !(effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            if (prepareVisRing(frame,effect)) ++prepared;
            continue;
        }
        if (effect.type == VIS_FIREBALL && (effect.id == AXIS_BILLBOARD_INDEX || effect.id == BILLBOARD_SMOKETRAIL_INDEX || effect.id == THICK_LIGHTNING_INDEX) &&
            !(effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            if (prepareAxisEffect(frame,effect,quad)) ++prepared;
            continue;
        }
        if (effect.type != VIS_FIREBALL || special(effect.id) || effect.id >= NUM_FIREBALLS ||
            (effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) { ++pending; continue; }
        const auto& definition = Fireballs[effect.id];
        const bool attached = (effect.flags & VF_ATTACHED) != 0;
        const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-effect.creation_time);
        float normalized = effect.lifetime > 0 ? age/effect.lifetime : 0;
        if (attached) normalized = age-std::floor(age);
        normalized = (std::min)(.99999f,normalized);
        float size = effect.size;
        if (Katmai && (effect.id == BIG_EXPLOSION_INDEX || effect.id == MED_EXPLOSION_INDEX ||
            effect.id == MED_EXPLOSION_INDEX2 || effect.id == MED_EXPLOSION_INDEX3)) size *= 1.8f;
        int rotation;
        if (definition.type == FT_BILLOW) rotation = d3scene::visualAngle(frame.simulation_time,160,slot*5000);
        else if (attached) rotation = 0;
        else if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX) rotation = d3scene::visualAngle(frame.simulation_time,860,slot*5000);
        else if (effect.id == SUN_CORONA_INDEX) {
            rotation = d3scene::visualAngle(frame.simulation_time,500,slot*5000);
            uint32_t noise = static_cast<uint32_t>(static_cast<int64_t>(std::floor(frame.simulation_time*60)))*1664525u+uint32_t(slot)*1013904223u;
            noise ^= noise >> 16; size *= 1+(noise%10)/100.f;
        } else rotation = (slot*5000)%65536;
        if (effect.flags & VF_EXPAND) size = effect.size*.5f*(1+normalized);
        int first_bitmap = -1, second_bitmap = -1;
        float second_weight = 0;
        const auto clip = [&](int handle) {
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (!animation.frames || animation.num_frames <= 0) return;
            const auto selection = d3scene::animationFrames(animation.num_frames,normalized,attached);
            first_bitmap = animation.frames[selection.frame0];
            if (selection.has_frame1) { second_bitmap = animation.frames[selection.frame1]; second_weight = selection.frame1_weight; }
        };
        const bool custom = effect.id == SMOKE_TRAIL_INDEX || effect.id == CUSTOM_EXPLOSION_INDEX || effect.id == PARTICLE_INDEX;
        const texture* material = nullptr;
        if (custom) {
            if (effect.custom_handle < 0 || effect.custom_handle >= MAX_TEXTURES || !GameTextures[effect.custom_handle].used) continue;
            material = &GameTextures[effect.custom_handle];
            if (material->flags & TF_ANIMATED) clip(material->bm_handle);
            else first_bitmap = GetTextureBitmapAtTime(effect.custom_handle,0,static_cast<float>(frame.simulation_time),false);
        } else if (effect.id == SPRAY_INDEX) clip(effect.custom_handle);
        else if (definition.type == FT_SPARK) { first_bitmap = definition.bm_handle; size *= 1-normalized; }
        else if (effect.id == SUN_CORONA_INDEX || effect.id == MUZZLE_FLASH_INDEX || effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX)
            first_bitmap = definition.bm_handle;
        else clip(definition.bm_handle);
        if (definition.type == FT_SMOKE && normalized > .3f) {
            const float expansion = 1+(normalized-.3f)/.7f*2.3f;
            if (effect.flags & VF_REVERSE) size /= expansion; else size *= expansion;
        }
        size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE));
        if (effect.id != BIG_EXPLOSION_INDEX && effect.id != BLUE_EXPLOSION_INDEX) size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE/2));
        if (!(size > 0) || !std::isfinite(size)) continue;
        const float fade = normalized > .5f ? 2*(1-normalized) : 1;
        float alpha;
        if (custom) alpha = fade*material->alpha;
        else if (definition.type == FT_SMOKE) alpha = fade*SMOKE_ALPHA;
        else if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX) alpha = 1;
        else if (effect.id == MUZZLE_FLASH_INDEX) alpha = 128/255.f;
        else alpha = (attached ? 1 : fade)*FIREBALL_ALPHA;
        d3scene::PreparedMesh sprite;
        sprite.asset = quad; sprite.room = effect.roomnum;
        sprite.surface.unlit = true; sprite.surface.two_sided = true;
        sprite.surface.texture_alpha = true; sprite.surface.texture_wrap = false;
        sprite.surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
        sprite.surface.effect_depth_offset = (effect.flags & VF_NO_Z_ADJUST) ||
            (definition.type == FT_SMOKE && sprite.surface.soft_depth_range > 0) ? 0 : -size;
        if (custom) sprite.surface.blend = material->flags & TF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
        else if (effect.id == BLACK_SMOKE_INDEX) sprite.surface.blend = d3scene::BlendMode::Multiply;
        else if ((definition.type == FT_SMOKE && effect.id != MED_SMOKE_INDEX) || effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX)
            sprite.surface.blend = d3scene::BlendMode::Alpha;
        else sprite.surface.blend = d3scene::BlendMode::Additive;
        if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX || effect.id == GRAY_SPARK_INDEX) {
            const auto color = GR_16_TO_COLOR(effect.lighting_color);
            sprite.surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
        }
        matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(rotation));
        if (effect.flags & VF_PLANAR) {
            vector normal = effect.end_pos;
            if (vm_GetMagnitude(&normal) < .0001f) continue;
            matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
            sprite.world = d3scene::compose(basis(plane),basis(spin));
        } else {
            vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.alignment = d3scene::MeshAlignment::Camera;
        }
        sprite.world.position = value(effect.pos);
        // Smoke is a low-frequency volume, not cutout geometry or flying debris.
        // Declare this at extraction rather than guessing from a finished image.
        if (definition.type == FT_SMOKE && !(effect.flags & VF_PLANAR) &&
            sprite.surface.soft_depth_range > 0 && sprite.surface.blend != d3scene::BlendMode::Multiply)
            sprite.raster_domain = d3scene::RasterDomain::SoftVolume;
        const auto publish = [&](int bitmap,float weight) {
            if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used || weight <= .001f) return;
            const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
            if (width <= 0 || height <= 0) return;
            auto instance = sprite;
            const float vertical = size*height/width;
            instance.world.right = {sprite.world.right.x*size,sprite.world.right.y*size,sprite.world.right.z*size};
            instance.world.up = {sprite.world.up.x*vertical,sprite.world.up.y*vertical,sprite.world.up.z*vertical};
            instance.surface.bitmap = bitmap; instance.surface.alpha = alphaByte(alpha)*weight;
            frame.meshes.push_back(std::move(instance)); ++prepared;
        };
        const bool blending = second_bitmap > BAD_BITMAP_HANDLE && second_weight > .001f;
        publish(first_bitmap,blending ? 1-second_weight : 1);
        if (blending) publish(second_bitmap,second_weight);
    }
    if (SceneGeometryInspection() && FindArg("-scene-axis-probe") && Viewer_object) {
        // A local fixture, never inserted into VisEffects or the simulation.
        // Exercise the same material/placement compiler as real trail content.
        vis_effect probe{};
        probe.type = VIS_FIREBALL; probe.id = AXIS_BILLBOARD_INDEX;
        probe.roomnum = Viewer_object->roomnum;
        probe.lifetime = 1; probe.lifeleft = .65f;
        probe.creation_time = static_cast<float>(frame.simulation_time)-.35f;
        probe.custom_handle = -1;
        for (const auto& room : frame.rooms) for (const auto& surface : room.surfaces)
            if (probe.custom_handle < 0 && surface.texture >= 0 && surface.bitmap > BAD_BITMAP_HANDLE)
                probe.custom_handle = surface.texture;
        if (AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene axis fixture texture=%d",probe.custom_handle);
        probe.billboard_info.texture = 1; probe.billboard_info.width = 1;
        const auto center = Viewer_object->pos+Viewer_object->orient.fvec*12;
        probe.pos = center-Viewer_object->orient.rvec*4+Viewer_object->orient.uvec*2;
        probe.end_pos = center+Viewer_object->orient.rvec*4+Viewer_object->orient.uvec*2;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Axis fixture could not publish its source texture");
        probe.id = BILLBOARD_SMOKETRAIL_INDEX;
        probe.lighting_color = GR_RGB16(180,220,255);
        probe.pos -= Viewer_object->orient.uvec*4;
        probe.end_pos -= Viewer_object->orient.uvec*4;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Trail fixture could not publish its source texture");
        probe.id = THICK_LIGHTNING_INDEX;
        probe.velocity = {2.2f,2.f,.5f};
        probe.pos += Viewer_object->orient.uvec*2;
        probe.end_pos += Viewer_object->orient.uvec*2;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Beam fixture could not publish its source texture");
        prepared += 3;
    }
    if (SceneGeometryInspection() && FindArg("-scene-depth-policy-probe") && Viewer_object) {
        auto pair = d3scene::unitBillboardQuad();
        const auto far = pair;
        for (auto position : far.vertices) { position.z = 1; pair.vertices.push_back(position); }
        auto face = far.polygons[0];
        for (auto& corner : face.corners) corner.vertex += 4;
        pair.polygons.push_back(face);
        pair.vertex_colors = {{0,0,1},{0,0,1},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{1,0,0}};
        const auto pair_asset = d3scene::assets().publishMesh(7,std::move(pair));
        for (int column = 0; column < 4; ++column) {
            d3scene::PreparedMesh panel;
            panel.asset = quad; panel.room = Viewer_object->roomnum; panel.world = basis(Viewer_object->orient);
            panel.world.right = value(Viewer_object->orient.rvec*.65f);
            panel.world.up = value(Viewer_object->orient.uvec*.65f);
            const auto center = Viewer_object->pos+Viewer_object->orient.fvec*6+Viewer_object->orient.rvec*((column-1.5f)*1.7f);
            panel.world.position = value(center+Viewer_object->orient.fvec*2);
            panel.surface.unlit = true; panel.surface.two_sided = true; panel.surface.base_color = {0,0,0};
            frame.meshes.push_back(panel);
            if (column >= 2) {
                panel.world.position = value(center+Viewer_object->orient.fvec*.5f);
                panel.surface.base_color = {0,1,0}; frame.meshes.push_back(panel);
            }
            panel.asset = pair_asset; panel.world.position = value(center);
            panel.surface.base_color = {1,1,1}; panel.surface.alpha = .5f;
            panel.surface.blend = d3scene::BlendMode::Alpha;
            panel.surface.depth = column == 0 ? d3scene::DepthMode::ReadWrite :
                column == 2 ? d3scene::DepthMode::Disabled : d3scene::DepthMode::ReadOnly;
            frame.meshes.push_back(panel);
        }
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene depth fixture columns=write,read,disabled,read_occluded expected=blue,purple,pink,cyan");
    }
    if (SceneGeometryInspection() && FindArg("-scene-ring-probe") && Viewer_object) {
        vis_effect probe{};
        probe.type = VIS_FIREBALL; probe.id = BLAST_RING_INDEX; probe.roomnum = Viewer_object->roomnum;
        probe.lifetime = 1; probe.lifeleft = .65f; probe.size = 7;
        probe.custom_handle = Fireballs[BLUE_BLAST_RING_INDEX].bm_handle;
        const auto center = Viewer_object->pos+Viewer_object->orient.fvec*12;
        probe.pos = center-Viewer_object->orient.rvec*3;
        if (!prepareVisRing(frame,probe)) Error("Facing ring fixture could not publish");
        probe.pos = center+Viewer_object->orient.rvec*3;
        probe.flags = VF_PLANAR | VF_REVERSE;
        probe.end_pos = -Viewer_object->orient.fvec+Viewer_object->orient.rvec*.6f;
        vm_NormalizeVector(&probe.end_pos);
        if (!prepareVisRing(frame,probe)) Error("Planar reverse ring fixture could not publish");
        object blast{};
        blast.pos = center-Viewer_object->orient.uvec*3;
        blast.orient = Viewer_object->orient;
        blast.orient.fvec = Viewer_object->orient.uvec;
        blast.orient.uvec = -Viewer_object->orient.fvec;
        blast.roomnum = probe.roomnum; blast.lifetime = 1; blast.lifeleft = .85f;
        blast.ctype.blast_info.max_size = 3; blast.ctype.blast_info.bm_handle = probe.custom_handle;
        const auto first_blast_mesh = frame.meshes.size();
        if (frame.objects.empty() || !prepareObjectRing(frame,frame.objects.front(),blast,quad))
            Error("Object blast ring fixture could not publish");
        // Fixture ownership must not inherit camera exclusion from the first
        // game object. The synthetic effect is never inserted into simulation.
        for (size_t i = first_blast_mesh; i < frame.meshes.size(); ++i) frame.meshes[i].has_owner = false;
        blast.id = GRAVITY_FIELD_INDEX; blast.pos = center+Viewer_object->orient.uvec*3;
        blast.lifeleft = .4f; blast.ctype.blast_info.max_size = 3;
        const auto first_gravity_mesh = frame.meshes.size();
        if (!prepareObjectRing(frame,frame.objects.front(),blast,quad)) Error("Gravity ring fixture could not publish");
        for (size_t i = first_gravity_mesh; i < frame.meshes.size(); ++i) frame.meshes[i].has_owner = false;
        prepared += 6;
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene ring fixture facing=1 planar_reverse=1 object=1 gravity=1 shared_topology=1");
    }
    if (windshield) {
        d3scene::PreparedForeground foreground;
        foreground.identity = SceneCloseScreenEffects; foreground.order = 35;
        auto view = d3scene::cameraOverlayView(main_view->world,SceneCloseScreenEffects,main_view->display_viewport);
        view.geometry_inspection = SceneGeometryInspection();
        foreground.layer.display_views.push_back(std::move(view));
        frame.foregrounds.push_back(std::move(foreground));
    }
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene effects sprites=%u pending=%u windshield=%u",prepared,pending,windshield);
    if ((mass_drivers || boss_mass_drivers || legacy_snow || enhanced_snow) && AutomatedCapturePending())
        AutomatedCaptureLog("scene effect families mass_drivers=%u boss_mass_drivers=%u legacy_snow=%u enhanced_snow=%u",mass_drivers,boss_mass_drivers,legacy_snow,enhanced_snow);
}
