// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneFog.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>
namespace d3scene {
namespace {
Vec3 sub(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double dot(Vec3 a,Vec3 b) { return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z; }
// Half-open projected edges give a shared triangle diagonal to exactly one
// triangle. Inclusive barycentrics would double the optical path on that ray.
bool hit(const FogTriangle& triangle,Vec3 eye,Vec3 direction,double& t) {
    const auto n = cross(sub(triangle.b,triangle.a),sub(triangle.c,triangle.a));
    const double denominator = dot(n,direction);
    if (std::abs(denominator) < 1.e-12) return false;
    t = dot(n,sub(triangle.a,eye))/denominator;
    if (t < 0) return false;
    const double p[3] = {eye.x+direction.x*t,eye.y+direction.y*t,eye.z+direction.z*t};
    const int drop = std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z) ? 0 : (std::abs(n.y) >= std::abs(n.z) ? 1 : 2);
    const int x = (drop+1)%3, y = (drop+2)%3;
    const double v[3][3] = {{triangle.a.x,triangle.a.y,triangle.a.z},{triangle.b.x,triangle.b.y,triangle.b.z},{triangle.c.x,triangle.c.y,triangle.c.z}};
    const double normal[3] = {n.x,n.y,n.z}; const double sign = normal[drop] < 0 ? -1 : 1;
    for (int i = 0; i < 3; ++i) {
        const auto& a = v[i]; const auto& b = v[(i+1)%3];
        const double dx = (b[x]-a[x])*sign, dy = (b[y]-a[y])*sign;
        const double edge = dx*(p[y]-a[y])-dy*(p[x]-a[x]);
        if (edge < 0 || (edge == 0 && !(dy > 0 || (dy == 0 && dx < 0)))) return false;
    }
    return true;
}
bool matching(const FogRoomSource& a,const FogRoomSource& b) {
    return a.depth == b.depth &&
        a.color.x == b.color.x && a.color.y == b.color.y && a.color.z == b.color.z;
}
bool boundsHit(const Bounds& bounds,Vec3 eye,Vec3 direction,double length) {
    double entry = 0, exit = length;
    const float low[3] = {bounds.minimum.x,bounds.minimum.y,bounds.minimum.z};
    const float high[3] = {bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
    const float origin[3] = {eye.x,eye.y,eye.z}, delta[3] = {direction.x,direction.y,direction.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(delta[axis]) < 1.e-8) { if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false; }
        else {
            const double a = (double(low[axis])-origin[axis])/delta[axis], b = (double(high[axis])-origin[axis])/delta[axis];
            entry = std::max(entry,std::min(a,b)); exit = std::min(exit,std::max(a,b));
            if (entry > exit) return false;
        }
    }
    return true;
}
template<class Visitor> void visitCrossings(const FogVolume& volume,Vec3 eye,Vec3 direction,double length,Visitor visitor) {
    size_t index = 0;
    while (index < volume.nodes.size()) {
        const auto& node = volume.nodes[index];
        if (!boundsHit(node.bounds,eye,direction,length)) { index = node.escape; continue; }
        for (uint32_t i = node.first; i < node.first+node.count; ++i) {
            double distance;
            if (hit(volume.boundary[i],eye,direction,distance) && distance > 1.e-5 && distance < length)
                visitor(volume.boundary[i],distance);
        }
        ++index;
    }
}
}
void buildFogAcceleration(FogVolume& volume) {
    volume.nodes.clear();
    if (volume.boundary.empty()) { volume.bounds = {}; return; }
    std::function<void(uint32_t,uint32_t)> build = [&](uint32_t first,uint32_t count) {
        const uint32_t index = static_cast<uint32_t>(volume.nodes.size());
        FogNode node; node.bounds.minimum = node.bounds.maximum = volume.boundary[first].a;
        for (uint32_t i = first; i < first+count; ++i) for (const auto& p : {volume.boundary[i].a,volume.boundary[i].b,volume.boundary[i].c}) {
            node.bounds.minimum.x = std::min(node.bounds.minimum.x,p.x); node.bounds.maximum.x = std::max(node.bounds.maximum.x,p.x);
            node.bounds.minimum.y = std::min(node.bounds.minimum.y,p.y); node.bounds.maximum.y = std::max(node.bounds.maximum.y,p.y);
            node.bounds.minimum.z = std::min(node.bounds.minimum.z,p.z); node.bounds.maximum.z = std::max(node.bounds.maximum.z,p.z);
        }
        volume.nodes.push_back(node);
        if (count <= 8) { volume.nodes[index].first = first; volume.nodes[index].count = count; }
        else {
            const auto size = sub(node.bounds.maximum,node.bounds.minimum);
            const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
            const auto center = [axis](const FogTriangle& t) { return axis == 0 ? t.a.x+t.b.x+t.c.x : (axis == 1 ? t.a.y+t.b.y+t.c.y : t.a.z+t.b.z+t.c.z); };
            const auto middle = first+count/2;
            std::nth_element(volume.boundary.begin()+first,volume.boundary.begin()+middle,volume.boundary.begin()+first+count,
                [&](const FogTriangle& a,const FogTriangle& b) { return center(a) < center(b); });
            build(first,count/2); build(middle,count-count/2);
        }
        volume.nodes[index].escape = static_cast<uint32_t>(volume.nodes.size());
    };
    build(0,static_cast<uint32_t>(volume.boundary.size()));
    volume.bounds = volume.nodes[0].bounds;
}
std::vector<FogVolume> compileFogVolumes(const std::vector<FogRoomSource>& rooms) {
    std::vector<FogVolume> volumes;
    std::unordered_map<int32_t,size_t> room_index;
    for (size_t i = 0; i < rooms.size(); ++i) room_index.emplace(rooms[i].number,i);
    std::vector<int32_t> component(rooms.size(),-1);
    const auto continues = [&](const FogRoomSource& room,const FogPortalSource& portal) {
        const auto found = room_index.find(portal.destination_room);
        if (found == room_index.end() || !portal.open) return false;
        const auto& other = rooms[found->second];
        if (!matching(room,other) || portal.destination_portal < 0 || size_t(portal.destination_portal) >= other.portals.size()) return false;
        const auto& reciprocal = other.portals[portal.destination_portal];
        return reciprocal.destination_room == room.number && reciprocal.open;
    };
    for (size_t first = 0; first < rooms.size(); ++first) {
        const auto& start = rooms[first];
        if (component[first] >= 0) continue;
        const int32_t index = static_cast<int32_t>(volumes.size());
        FogVolume volume; volume.color = start.color; volume.depth = start.depth;
        std::vector<size_t> pending{first}; component[first] = index;
        for (size_t next = 0; next < pending.size(); ++next) {
            const auto& room = rooms[pending[next]];
            volume.rooms.push_back(room.number);
            for (const auto& portal : room.portals) if (continues(room,portal)) {
                const auto destination = room_index.at(portal.destination_room);
                if (component[destination] < 0) { component[destination] = index; pending.push_back(destination); }
            }
        }
        for (auto room_id : pending) {
            const auto& room = rooms[room_id]; const auto& geometry = room.asset->geometry();
            for (size_t face = 0; face < geometry.polygons.size(); ++face) {
                bool internal = false;
                for (const auto& portal : room.portals) if (portal.polygon == int32_t(face) && continues(room,portal)) { internal = true; break; }
                if (internal) continue;
                const auto& polygon = geometry.polygons[face];
                if (!polygon.bounds_volume) continue;
                for (size_t corner = 1; corner+1 < polygon.corners.size(); ++corner)
                    volume.boundary.push_back({geometry.vertices[polygon.corners[0].vertex],geometry.vertices[polygon.corners[corner].vertex],
                        geometry.vertices[polygon.corners[corner+1].vertex],polygon.normal});
            }
        }
        buildFogAcceleration(volume);
        volumes.push_back(std::move(volume));
    }
    return volumes;
}
FogAssetRef SceneAssets::publishFog(const std::vector<RoomState>& rooms) {
    std::vector<FogRoomSource> source;
    std::unordered_set<int32_t> numbers;
    for (const auto& room : rooms) {
        if (!room.fog_enabled) continue;
        if (!std::isfinite(room.fog_depth)) throw std::invalid_argument("Invalid fog depth");
        if (!(room.fog_depth > 0)) continue;
        if (!room.asset || room.asset->id().lifetime != lifetime_ || room.number < 0 || !numbers.insert(room.number).second ||
            !std::isfinite(room.fog_depth) || !std::isfinite(room.fog_color.x) || !std::isfinite(room.fog_color.y) || !std::isfinite(room.fog_color.z))
            throw std::invalid_argument("Invalid fog source room");
        FogRoomSource input{room.number,room.asset,room.fog_color,room.fog_depth,{}};
        for (const auto& portal : room.portals) {
            if (portal.polygon < 0 || size_t(portal.polygon) >= room.surfaces.size() ||
                size_t(portal.polygon) >= room.asset->geometry().polygons.size()) throw std::invalid_argument("Invalid fog portal");
            input.portals.push_back({portal.polygon,portal.destination_room,portal.destination_portal,!room.surfaces[portal.polygon].draw_surface});
        }
        source.push_back(std::move(input));
    }
    const auto same = [](const FogRoomSource& a,const FogRoomSource& b) {
        if (a.number != b.number || a.asset->id() != b.asset->id() || !matching(a,b) || a.portals.size() != b.portals.size()) return false;
        for (size_t i = 0; i < a.portals.size(); ++i) {
            const auto& x = a.portals[i]; const auto& y = b.portals[i];
            if (x.polygon != y.polygon || x.destination_room != y.destination_room || x.destination_portal != y.destination_portal || x.open != y.open) return false;
        }
        return true;
    };
    if (fog_ && source.size() == fog_->source_.size() && std::equal(source.begin(),source.end(),fog_->source_.begin(),same)) return fog_;
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    auto volumes = compileFogVolumes(source);
    const auto finite = [](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const auto require = [](bool valid,const char* message) { if (!valid) throw std::invalid_argument(message); };
    for (const auto& volume : volumes) {
        require(finite(volume.color) && std::isfinite(volume.depth) && volume.depth > 0,"Invalid fog volume");
        require(finite(volume.bounds.minimum) && finite(volume.bounds.maximum),"Invalid fog bounds");
        require(volume.boundary.empty() == volume.nodes.empty(),"Missing fog spatial index");
        for (size_t index = 0; index < volume.nodes.size(); ++index) {
            const auto& node = volume.nodes[index];
            require(finite(node.bounds.minimum) && finite(node.bounds.maximum) && node.escape > index && node.escape <= volume.nodes.size() &&
                uint64_t(node.first)+node.count <= volume.boundary.size(),"Invalid fog spatial node");
        }
        for (const auto& triangle : volume.boundary)
            require(finite(triangle.a) && finite(triangle.b) && finite(triangle.c) && finite(triangle.inward),"Invalid fog boundary");
    }
    auto replacement = FogAssetRef(new FogAsset({lifetime_,revision_+1,0},std::move(source),std::move(volumes)));
    fog_ = replacement; ++revision_;
    return replacement;
}
const std::vector<FogVolume>& fogVolumes(const FrameData& frame) {
    static const std::vector<FogVolume> empty;
    return frame.fog ? frame.fog->volumes() : empty;
}
bool fogContains(const FogVolume& volume,Vec3 position) {
    // Net oriented crossings remain valid for non-convex connected rooms and
    // disconnected ray intervals; no camera-room ID shortcut is required.
    int winding = 0;
    const Vec3 direction{.819231f,.37139f,.437123f};
    visitCrossings(volume,position,direction,1.e30,[&](const FogTriangle& triangle,double) {
        winding += dot(triangle.inward,direction) < 0 ? 1 : -1;
    });
    return winding > 0;
}
float fogRayStart(const View& view,Vec3 endpoint) {
    validateView(view);
    if (view.fog_path_origin == FogPathOrigin::Camera) return 0;
    const auto delta = sub(endpoint,view.camera.position);
    const double length = std::sqrt(dot(delta,delta));
    if (!(length > 0)) return 0;
    const auto plane = view.clip_plane;
    const double at_eye = dot(plane.normal,view.camera.position)+plane.constant;
    if (at_eye >= 0) return 0;
    const double advance = dot(plane.normal,delta);
    if (!(advance > 0)) return static_cast<float>(length);
    return static_cast<float>(length*(std::min)(1.0,-at_eye/advance));
}
float fogSegmentLength(const FogVolume& volume,Vec3 eye,Vec3 endpoint,bool eye_inside,float start_distance) {
    if (!std::isfinite(start_distance) || start_distance < 0) throw std::invalid_argument("Invalid fog interval start");
    const auto delta = sub(endpoint,eye); const double length = std::sqrt(dot(delta,delta));
    if (!(length > start_distance)) return 0;
    const Vec3 direction{float(delta.x/length),float(delta.y/length),float(delta.z/length)};
    const double interval = length-start_distance;
    double accumulated = eye_inside ? interval : 0;
    visitCrossings(volume,eye,direction,length,[&](const FogTriangle& triangle,double distance) {
        accumulated += (length-(std::max)(double(start_distance),distance))*(dot(triangle.inward,direction) > 0 ? 1 : -1);
    });
    return static_cast<float>(std::max(0.0,std::min(interval,accumulated)));
}
}
