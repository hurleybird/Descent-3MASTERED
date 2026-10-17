// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneAutomap.h"
#include "SceneExtraction.h"
#include "../renderer/SceneGeometry.h"
#include "room.h"
#include "game.h"
#include "object.h"
#include "player.h"
#include "multi.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "gameloop.h"
#include "polymodel.h"
#include "args.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

extern ubyte AutomapVisMap[MAX_ROOMS];
namespace {
d3scene::Vec3 value(const vector& p) { return {p.x,p.y,p.z}; }
void markerDisk(d3scene::FrameData& data,const vector& p,d3scene::Vec3 color,float radius) {
    d3scene::PreparedMesh mesh;
    mesh.asset = d3scene::assets().publishMesh(8,d3scene::billboardRing(0,255,76,32));
    mesh.alignment = d3scene::MeshAlignment::Camera; mesh.world.position = value(p);
    mesh.world.right = {radius,0,0}; mesh.world.up = {0,radius,0};
    mesh.surface.unlit = true; mesh.surface.two_sided = true;
    mesh.surface.color_modulation = color; mesh.surface.blend = d3scene::BlendMode::Alpha;
    mesh.surface.depth = d3scene::DepthMode::ReadOnly;
    data.meshes.push_back(std::move(mesh));
}
}

d3scene::PresentationRef PrepareGameAutomap(uint64_t sequence,double display_time,bool terrain,int ship_model,int selected_marker) {
    if (!std::isfinite(display_time)) throw std::invalid_argument("Invalid automap display clock");
    // Reuse the canonical room asset publisher without advancing world history.
    // The map has its own frame/clock and does not prepare normal gameplay effects.
    const auto source = ExtractGamePresentation(Gametime,0,true,false);
    d3scene::FrameData data;
    data.sequence = sequence; data.scene_lifetime = source->data().scene_lifetime;
    data.simulation_time = Gametime; data.paused = true;
    const float pulse = float(display_time-std::floor(display_time));
    const int start = Viewer_object ? Viewer_object->roomnum : -1;
    size_t room_count = 0;
    for (const auto& room : source->data().rooms) {
        const auto& game_room = Rooms[room.number];
        const bool exterior = (room.authored_flags & RF_EXTERNAL) != 0;
        if (exterior ? !terrain : (!AutomapVisMap[room.number] && room.number != start)) continue;
        const auto& geometry = room.asset->geometry();
        std::vector<AutomapFaceSource> faces(geometry.polygons.size());
        for (size_t index = 0; index < faces.size(); ++index) {
            const auto& face = game_room.faces[index];
            auto& target = faces[index];
            std::vector<vector> vertices;
            for (const auto& corner : geometry.polygons[index].corners) {
                const auto& p = geometry.vertices[corner.vertex]; vertices.push_back({p.x,p.y,p.z});
            }
            if (vertices.size() >= 3) {
                vector center;
                const float area = vm_GetCentroidFast(&center,vertices.data(),int(vertices.size()));
                target.detail = area < 60 ? 2 : area < 120 ? 1 : 0;
            }
            if (face.portal_num >= 0) {
                const int other = game_room.portals[face.portal_num].croom;
                if (other < 0 || other >= MAX_ROOMS || !Rooms[other].used)
                    throw std::invalid_argument("Automap portal has no destination room");
                target.appearance = AutomapVisMap[other] ? AutomapFaceAppearance::Hidden :
                    (Rooms[other].flags & RF_SECRET) ? AutomapFaceAppearance::Black : AutomapFaceAppearance::Unexplored;
            } else if (face.flags & FF_LIGHTMAP) {
                if (face.lmi_handle < 0 || face.lmi_handle >= MAX_LIGHTMAP_INFOS)
                    throw std::invalid_argument("Automap face has no baked lighting");
                const int handle = LightmapInfo[face.lmi_handle].lm_handle;
                if (handle < 0 || handle >= MAX_LIGHTMAPS) throw std::invalid_argument("Automap lightmap is unavailable");
                const int width = lm_w(handle), height = lm_h(handle);
                const auto* texels = lm_data(handle);
                if (!texels || width <= 0 || height <= 0) throw std::invalid_argument("Automap lightmap is empty");
                target.appearance = AutomapFaceAppearance::Baked;
                for (int corner = 0; corner < face.num_verts; ++corner) {
                    const auto& uv = face.face_uvls[corner];
                    const int x = (std::max)(0,(std::min)(width-1,int(uv.u2*width)));
                    const int y = (std::max)(0,(std::min)(height-1,int(uv.v2*height)));
                    const auto texel = texels[y*width+x];
                    target.colors.push_back({float((texel>>10)&31)/31,float((texel>>5)&31)/31,float(texel&31)/31});
                }
            }
        }
        auto groups = CompileAutomapGeometry(geometry,faces);
        for (unsigned group = 0; group < groups.size(); ++group) {
            if (groups[group].polygons.empty()) continue;
            d3scene::PreparedMesh mesh;
            // The game reserves mesh slots 0x10000.. for automap room groups.
            mesh.asset = d3scene::assets().publishMesh(0x10000u+uint32_t(room.number)*9+group,std::move(groups[group]));
            mesh.surface.unlit = true;
            if (group/3 == 2) mesh.surface.color_modulation = {pulse,pulse,pulse};
            mesh.maximum_depth = group%3 == 2 ? 300.f : group%3 == 1 ? 600.f : 0.f;
            mesh.depth_reference = room.asset->bounds();
            data.meshes.push_back(std::move(mesh));
        }
        ++room_count;
    }
    if (terrain) {
        PrepareGameTerrain(data);
        // The authored map shows baked terrain illumination, not terrain albedo.
        for (auto& material : data.terrain.materials) {
            material.texture = -1;
        }
    }
    if (Player_object && ship_model >= 0 && (int(display_time*4)&1)) {
        const auto& object = *Player_object;
        PrepareGameStaticModel(data,ship_model,{value(object.pos),value(object.orient.rvec),value(object.orient.uvec),value(object.orient.fvec)},1);
    }
    if (Game_mode & GM_MULTI) for (int player = 0; player < MAX_PLAYERS; ++player) {
        if (player == Player_num || !(NetPlayers[player].flags & NPF_CONNECTED)) continue;
        const int index = Players[player].objnum;
        if (index < 0 || index > Highest_object_index) continue;
        const auto& object = Objects[index];
        if ((object.type == OBJ_PLAYER && object.render_type == RT_POLYOBJ) || object.type == OBJ_OBSERVER)
            markerDisk(data,object.pos,{1,1,.5f},3);
    }
    if (Player_object) for (int index = 0; index <= Highest_object_index; ++index) {
        const auto& object = Objects[index];
        if (object.type != OBJ_MARKER || object.parent_handle != Player_object->handle) continue;
        const int marker = (Game_mode & GM_MULTI) ? object.id-Player_num*2 : object.id;
        const bool selected = selected_marker >= 0 && selected_marker == marker;
        markerDisk(data,object.pos,selected ? d3scene::Vec3{0,1,1} : d3scene::Vec3{1,0,1},
            selected ? 9+9*pulse : 2.5f+2.5f*pulse);
    }
    PrepareGameInspectionBanner(data);
    PrepareGameSceneResources(data);
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene automap prepared rooms=%u meshes=%u terrain=%d models=%u",unsigned(room_count),
            unsigned(data.meshes.size()),terrain,unsigned(data.model_instances.size()));
    return d3scene::Presentation::freeze(data);
}

d3scene::PresentationRef PrepareAutomapInspection() {
    static const int ship = LoadPolyModel("automapship.oof",0);
    if (ship < 0) throw std::runtime_error("Automap inspection cannot load ship model");
    const int index = AutomatedCaptureFrameIndex();
    return PrepareGameAutomap(uint64_t(index)+1,.375+index/60.,FindArg("-scene-automap-terrain-probe") != 0,ship,-1);
}
