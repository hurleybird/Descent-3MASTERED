// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneFog.h"
#include "SceneView.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>
namespace d3scene {
bool fogFieldCommonColor(const std::vector<FogVolume>& volumes,const std::vector<uint32_t>& candidates,Vec3& color) {
    color = {};
    if (candidates.empty()) return false;
    const auto first = volumes.at(candidates.front()).color;
    for (auto index : candidates) {
        const auto c = volumes.at(index).color;
        if (c.x != first.x || c.y != first.y || c.z != first.z) return false;
    }
    color = first;
    return true;
}
bool fogFieldFitsHalf(double optical_bound,size_t volume_count) {
    if (!std::isfinite(optical_bound) || optical_bound < 0) return false;
    return optical_bound*std::pow(1.0+1.0/1024.0,double(volume_count)) < 32768;
}
bool fogMayIntersectView(const Bounds& bounds,const Camera& camera,bool perspective) {
    if (!perspective) return true; // orthographic endpoints still use eye-origin optical rays
    for (size_t i = 0; i < 5; ++i) {
        auto plane = camera.planes[i];
        if (i == 4) {
            plane.normal = camera.transform.forward;
            const auto& n = plane.normal; const auto& eye = camera.transform.position;
            plane.constant = -(double(n.x)*eye.x+double(n.y)*eye.y+double(n.z)*eye.z);
        }
        const auto& n = plane.normal;
        const Vec3 p{n.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
            n.y >= 0 ? bounds.maximum.y : bounds.minimum.y,n.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
        const double d = double(n.x)*p.x+double(n.y)*p.y+double(n.z)*p.z+plane.constant;
        const double epsilon = 2.e-6*(1+std::abs(double(n.x)*p.x)+std::abs(double(n.y)*p.y)+
            std::abs(double(n.z)*p.z)+std::abs(plane.constant));
        if (d < -epsilon) return false;
    }
    return true;
}
void prepareFogViewNodes(const FogVolume& volume,const Camera& camera,bool optical_cone,std::vector<FogViewNode>& output) {
    output.clear();
    size_t index = 0;
    while (index < volume.nodes.size()) {
        const auto& node = volume.nodes[index];
        if (!fogMayIntersectView(node.bounds,camera,optical_cone)) { index = node.escape; continue; }
        output.push_back({uint32_t(index),node.escape}); ++index;
    }
    // Source traversal order is unchanged. Remap each skip to the first
    // retained node at/after its old escape, or the view-local end sentinel.
    for (auto& node : output) {
        node.escape = uint32_t(std::lower_bound(output.begin(),output.end(),node.escape,
            [](const FogViewNode& value,uint32_t source) { return value.source < source; })-output.begin());
    }
}
void appendFogViewRecords(const FogVolume& volume,const std::vector<FogViewNode>& nodes,std::vector<float>& output) {
    for (size_t i=0;i<nodes.size();++i) {
        const auto& view = nodes[i];
        const auto& node = volume.nodes.at(view.source);
        if (view.escape <= i || view.escape > nodes.size() || (node.count && view.escape != i+1))
            throw std::logic_error("Invalid threaded fog traversal");
        const auto& a = node.bounds.minimum; const auto& b = node.bounds.maximum;
        output.insert(output.end(),{a.x,a.y,a.z,float(node.count ? node.first : view.escape),
            b.x,b.y,b.z,float(node.count)});
    }
}
uint32_t appendFogFieldRecords(const FogVolume& volume,const Camera& camera,const std::vector<FogViewNode>& nodes,uint32_t mask_base,std::vector<float>& output) {
    constexpr int tiles=FogFieldTileSide;
    if (output.size()%4) throw std::invalid_argument("Fog GPU records require vec4 alignment");
    if (camera.projection.orthographic_depth_scale!=0 || !std::isfinite(camera.projection.x_scale) ||
        !std::isfinite(camera.projection.y_scale) || camera.projection.x_scale==0 || camera.projection.y_scale==0)
        throw std::invalid_argument("Fog field requires a finite perspective projection");
    const size_t start=output.size();
    output.resize(start+FogFieldHeaderWords);
    appendFogViewRecords(volume,nodes,output);
    const size_t records=output.size();
    const auto& transform=camera.transform;
    const auto project=[&](Vec3 point,Vec3 axis) {
        return (double(point.x)-transform.position.x)*axis.x+
            (double(point.y)-transform.position.y)*axis.y+(double(point.z)-transform.position.z)*axis.z;
    };
    for (const auto& view : nodes) {
        const auto& node=volume.nodes.at(view.source);
        for (uint32_t triangle=node.first;triangle<node.first+node.count;++triangle) {
            const auto& t=volume.boundary.at(triangle);
            double low[2]={1.e30,1.e30},high[2]={-1.e30,-1.e30};
            double low_z=1.e30,high_z=-1.e30;
            for (const auto point : {t.a,t.b,t.c}) {
                const double z=project(point,transform.forward);
                low_z=std::min(low_z,z); high_z=std::max(high_z,z);
                if (z<=1.e-5) continue;
                const double xy[2]={project(point,transform.right)/z*camera.projection.x_scale+camera.projection.x_offset,
                    project(point,transform.up)/z*camera.projection.y_scale+camera.projection.y_offset};
                for (int axis=0;axis<2;++axis) {low[axis]=std::min(low[axis],xy[axis]); high[axis]=std::max(high[axis],xy[axis]);}
            }
            if (high_z<=0) continue;
            // Eye-plane crossings cannot safely divide by forward depth. Keep
            // them in every tile instead of clipping away optical prefixes.
            if (low_z<=1.e-5) {low[0]=low[1]=-1; high[0]=high[1]=1;}
            for (int axis=0;axis<2;++axis) {
                const double margin=1.e-4*(1+std::max(std::abs(low[axis]),std::abs(high[axis])));
                low[axis]-=margin; high[axis]+=margin;
            }
            if (low[0]>1 || high[0]<-1 || low[1]>1 || high[1]<-1) continue;
            output.insert(output.end(),{float(low[0]),float(low[1]),float(high[0]),float(high[1])});
            const Vec3 ab{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z},ac{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
            const Vec3 normal{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
            const double vertices[3][3]={{double(t.a.x)-transform.position.x,double(t.a.y)-transform.position.y,double(t.a.z)-transform.position.z},
                {double(t.b.x)-transform.position.x,double(t.b.y)-transform.position.y,double(t.b.z)-transform.position.z},
                {double(t.c.x)-transform.position.x,double(t.c.y)-transform.position.y,double(t.c.z)-transform.position.z}};
            const double plane_distance=normal.x*vertices[0][0]+normal.y*vertices[0][1]+normal.z*vertices[0][2];
            const double orientation=plane_distance<0 ? -1 : 1;
            const int drop=std::abs(normal.x)>=std::abs(normal.y) && std::abs(normal.x)>=std::abs(normal.z) ? 0 :
                (std::abs(normal.y)>=std::abs(normal.z) ? 1 : 2);
            const double normal_components[3]={normal.x,normal.y,normal.z};
            const double projected_orientation=normal_components[drop]<0 ? -1 : 1;
            int flags=0;
            // An edge through the eye is homogeneous: substituting the view's
            // ray equation gives one affine half-plane in NDC. It also works
            // for triangles crossing the eye plane, without a clipped polygon.
            // Keep the old dominant-axis half-open rule on shared boundaries.
            for (int edge=0;edge<3;++edge) {
                const auto& p=vertices[edge]; const auto& q=vertices[(edge+1)%3];
                const double n[3]={p[1]*q[2]-p[2]*q[1],p[2]*q[0]-p[0]*q[2],p[0]*q[1]-p[1]*q[0]};
                const auto axisDot=[&](Vec3 axis) {return (n[0]*axis.x+n[1]*axis.y+n[2]*axis.z)*orientation;};
                const double a=axisDot(transform.right)/camera.projection.x_scale,b=axisDot(transform.up)/camera.projection.y_scale;
                const double c=axisDot(transform.forward)-a*camera.projection.x_offset-b*camera.projection.y_offset;
                const double margin=1.e-4*(1+std::abs(a)+std::abs(b)+std::abs(c));
                output.insert(output.end(),{float(a),float(b),float(c),float(margin)});
                const double dx=(q[(drop+1)%3]-p[(drop+1)%3])*projected_orientation;
                const double dy=(q[(drop+2)%3]-p[(drop+2)%3])*projected_orientation;
                if (dy>0 || (dy==0 && dx<0)) flags|=1<<edge;
            }
            // Authored inward normals can differ from geometric normals on
            // nonplanar faces. Preserve them for oriented crossing signs.
            output.insert(output.end(),{normal.x,normal.y,normal.z,float(plane_distance),t.inward.x,t.inward.y,t.inward.z,float(flags)});
        }
    }
    const size_t candidate_count=(output.size()-records)/FogFieldTriangleWords;
    if (output.size()-start>size_t(INT32_MAX) || candidate_count>size_t(INT32_MAX)-31)
        throw std::length_error("Fog field records exceed GPU addressing");
    const uint32_t count=uint32_t(candidate_count),words=(count+31)/32;
    if (uint64_t(mask_base)+uint64_t(words)*tiles*tiles>INT32_MAX)
        throw std::length_error("Fog field masks exceed GPU addressing");
    const uint32_t header[8]={uint32_t(records-start),count,mask_base,words,tiles,tiles,0,0};
    std::memcpy(output.data()+start,header,sizeof(header));
    return words;
}
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
    const auto merge = [](Bounds& bounds,const Bounds& other) {
        bounds.minimum.x = std::min(bounds.minimum.x,other.minimum.x); bounds.maximum.x = std::max(bounds.maximum.x,other.maximum.x);
        bounds.minimum.y = std::min(bounds.minimum.y,other.minimum.y); bounds.maximum.y = std::max(bounds.maximum.y,other.maximum.y);
        bounds.minimum.z = std::min(bounds.minimum.z,other.minimum.z); bounds.maximum.z = std::max(bounds.maximum.z,other.maximum.z);
    };
    const auto triangleBounds = [&](const FogTriangle& t) {
        Bounds bounds{t.a,t.a}; merge(bounds,{t.b,t.b}); merge(bounds,{t.c,t.c}); return bounds;
    };
    const auto area = [](const Bounds& bounds) {
        const auto d = sub(bounds.maximum,bounds.minimum);
        return double(d.x)*d.y+double(d.x)*d.z+double(d.y)*d.z;
    };
    const auto center = [](const FogTriangle& t,int axis) {
        return axis == 0 ? (double(t.a.x)+t.b.x+t.c.x)/3 :
            axis == 1 ? (double(t.a.y)+t.b.y+t.c.y)/3 : (double(t.a.z)+t.b.z+t.c.z)/3;
    };
    std::function<void(uint32_t,uint32_t)> build = [&](uint32_t first,uint32_t count) {
        const uint32_t index = static_cast<uint32_t>(volume.nodes.size());
        FogNode node; node.bounds.minimum = node.bounds.maximum = volume.boundary[first].a;
        for (uint32_t i = first; i < first+count; ++i) for (const auto& p : {volume.boundary[i].a,volume.boundary[i].b,volume.boundary[i].c}) {
            node.bounds.minimum.x = std::min(node.bounds.minimum.x,p.x); node.bounds.maximum.x = std::max(node.bounds.maximum.x,p.x);
            node.bounds.minimum.y = std::min(node.bounds.minimum.y,p.y); node.bounds.maximum.y = std::max(node.bounds.maximum.y,p.y);
            node.bounds.minimum.z = std::min(node.bounds.minimum.z,p.z); node.bounds.maximum.z = std::max(node.bounds.maximum.z,p.z);
        }
        volume.nodes.push_back(node);
        int best_axis = -1,best_split = 0;
        double best_low = 0,best_scale = 0,best_cost = area(node.bounds)*count;
        // Spend compilation work once on spatial quality, not every pixel on
        // overlapping median partitions. The cost includes one node traversal.
        constexpr int bins = 16;
        if (count > 2) for (int axis = 0; axis < 3; ++axis) {
            double low = center(volume.boundary[first],axis),high = low;
            for (uint32_t i = first+1; i < first+count; ++i) {
                const double value = center(volume.boundary[i],axis); low = std::min(low,value); high = std::max(high,value);
            }
            if (!(high > low)) continue;
            const double scale = bins/(high-low);
            struct Bin { Bounds bounds{}; uint32_t count = 0; } bucket[bins],prefix[bins];
            for (uint32_t i = first; i < first+count; ++i) {
                auto& b = bucket[std::min(bins-1,int((center(volume.boundary[i],axis)-low)*scale))];
                const auto bounds = triangleBounds(volume.boundary[i]);
                if (b.count++) merge(b.bounds,bounds); else b.bounds = bounds;
            }
            const auto append = [&](Bin& to,const Bin& from) {
                if (!from.count) return;
                if (to.count) merge(to.bounds,from.bounds); else to.bounds = from.bounds;
                to.count += from.count;
            };
            Bin left;
            for (int b = 0; b < bins; ++b) { append(left,bucket[b]); prefix[b] = left; }
            Bin right;
            for (int b = bins-1; b > 0; --b) {
                append(right,bucket[b]); const auto& l = prefix[b-1];
                if (!l.count || !right.count) continue;
                const double cost = area(node.bounds)+area(l.bounds)*l.count+area(right.bounds)*right.count;
                if (cost < best_cost) { best_cost = cost; best_axis = axis; best_split = b; best_low = low; best_scale = scale; }
            }
        }
        if (best_axis < 0) { volume.nodes[index].first = first; volume.nodes[index].count = count; }
        else {
            const auto middle = std::partition(volume.boundary.begin()+first,volume.boundary.begin()+first+count,
                [&](const FogTriangle& t) { return std::min(bins-1,int((center(t,best_axis)-best_low)*best_scale)) < best_split; });
            const auto left = uint32_t(middle-(volume.boundary.begin()+first));
            build(first,left); build(first+left,count-left);
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
    // Compare current inputs directly against the owned compilation source.
    // Unchanged fog must not allocate/copy a room-and-portal graph just to
    // discover that it already has the correct geometry and optical boundary.
    if (fog_) {
        size_t index = 0;
        bool same = true;
        for (const auto& room : rooms) {
            if (!room.fog_enabled) continue;
            if (!std::isfinite(room.fog_depth)) throw std::invalid_argument("Invalid fog depth");
            if (!(room.fog_depth > 0)) continue;
            if (index == fog_->source_.size()) { same = false; break; }
            const auto& source = fog_->source_[index++];
            if (!room.asset || room.number != source.number || room.asset->id() != source.asset->id() ||
                room.fog_depth != source.depth || room.fog_color.x != source.color.x ||
                room.fog_color.y != source.color.y || room.fog_color.z != source.color.z ||
                room.portals.size() != source.portals.size()) { same = false; break; }
            for (size_t i = 0; i < room.portals.size(); ++i) {
                const auto& a = room.portals[i]; const auto& b = source.portals[i];
                if (a.polygon != b.polygon || a.destination_room != b.destination_room ||
                    a.destination_portal != b.destination_portal || a.polygon < 0 ||
                    size_t(a.polygon) >= room.surfaces.size() ||
                    (!room.surfaces[a.polygon].draw_surface) != b.open) { same = false; break; }
            }
            if (!same) break;
        }
        if (same && index == fog_->source_.size()) return fog_;
    }
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
int fogWinding(const FogVolume& volume,Vec3 position) {
    // Net oriented crossings remain valid for non-convex connected rooms and
    // disconnected ray intervals; no camera-room ID shortcut is required.
    int winding = 0;
    const Vec3 direction{.819231f,.37139f,.437123f};
    visitCrossings(volume,position,direction,1.e30,[&](const FogTriangle& triangle,double) {
        winding += dot(triangle.inward,direction) < 0 ? 1 : -1;
    });
    return winding;
}
bool fogContains(const FogVolume& volume,Vec3 position) { return fogWinding(volume,position) > 0; }
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
float fogOccupiedLength(const FogVolume& volume,Vec3 eye,Vec3 endpoint,int winding,float start_distance) {
    if (!std::isfinite(start_distance) || start_distance < 0) throw std::invalid_argument("Invalid fog interval start");
    const auto delta = sub(endpoint,eye); const double length = std::sqrt(dot(delta,delta));
    if (!(length > start_distance)) return 0;
    const Vec3 direction{float(delta.x/length),float(delta.y/length),float(delta.z/length)};
    std::vector<std::pair<double,int>> crossings;
    visitCrossings(volume,eye,direction,length,[&](const FogTriangle& triangle,double distance) {
        crossings.emplace_back(distance,dot(triangle.inward,direction) > 0 ? 1 : -1);
    });
    std::sort(crossings.begin(),crossings.end());
    double previous = start_distance,occupied = 0;
    for (const auto& crossing : crossings) {
        const double distance = std::max(double(start_distance),crossing.first);
        if (winding > 0) occupied += distance-previous;
        winding += crossing.second; previous = distance;
    }
    if (winding > 0) occupied += length-previous;
    return float(occupied);
}
}
