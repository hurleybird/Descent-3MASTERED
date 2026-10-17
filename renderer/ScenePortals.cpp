// Descent 3MASTERED - GPL-3.0-or-later
#include "ScenePortals.h"
#include <algorithm>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <limits>
namespace d3scene {
namespace {
PortalWindow projectedBounds(const Camera& camera,const Bounds& bounds) {
    const double infinity = std::numeric_limits<double>::infinity();
    PortalWindow result{infinity,infinity,-infinity,-infinity,true};
    unsigned behind = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const auto p = project(camera,{i&1 ? bounds.maximum.x : bounds.minimum.x,
            i&2 ? bounds.maximum.y : bounds.minimum.y,i&4 ? bounds.maximum.z : bounds.minimum.z});
        if (p.w <= 1.e-5) { ++behind; continue; }
        const double epsilon = 2.e-6*(1+std::abs(p.x/p.w)+std::abs(p.y/p.w));
        result.left = std::min(result.left,p.x/p.w-epsilon); result.right = std::max(result.right,p.x/p.w+epsilon);
        result.bottom = std::min(result.bottom,p.y/p.w-epsilon); result.top = std::max(result.top,p.y/p.w+epsilon);
    }
    if (behind == 8) return {};
    // A box crossing the eye plane is unbounded in projection. Never use a
    // projected-corner shortcut to cull a large enclosing building.
    if (behind) return {-1,-1,1,1,true};
    return result;
}
PortalWindow projectedPortal(const Camera& camera,const MeshGeometry& mesh,const MeshPolygon& polygon,bool two_sided) {
    const auto first = mesh.vertices[polygon.corners[0].vertex];
    const auto eye = camera.transform.position;
    const auto n = polygon.normal;
    const double facing = double(n.x)*(eye.x-first.x)+double(n.y)*(eye.y-first.y)+double(n.z)*(eye.z-first.z);
    const double tolerance = 2.e-6*(1+std::abs(eye.x)+std::abs(eye.y)+std::abs(eye.z));
    if (!two_sided && facing < -tolerance) return {};
    std::vector<ClipPosition> input, clipped;
    for (const auto& corner : polygon.corners) input.push_back(project(camera,mesh.vertices[corner.vertex]));
    // Portal apertures before the geometry near plane must remain open: clipping
    // them at near=1 causes disappearing rooms as the camera crosses a doorway.
    constexpr double eye_clip = 1.e-5;
    auto previous = input.back();
    for (const auto current : input) {
        if ((previous.w >= eye_clip) != (current.w >= eye_clip)) {
            const double t = (eye_clip-previous.w)/(current.w-previous.w);
            clipped.push_back({previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,0,eye_clip});
        }
        if (current.w >= eye_clip) clipped.push_back(current);
        previous = current;
    }
    if (clipped.empty()) {
        // At the portal plane, rounding must not close the aperture.
        if (std::abs(facing) <= tolerance) return {-1,-1,1,1,true};
        return {};
    }
    const double infinity = std::numeric_limits<double>::infinity();
    PortalWindow result{infinity,infinity,-infinity,-infinity,true};
    for (const auto p : clipped) {
        result.left = std::min(result.left,p.x/p.w); result.right = std::max(result.right,p.x/p.w);
        result.bottom = std::min(result.bottom,p.y/p.w); result.top = std::max(result.top,p.y/p.w);
    }
    return result;
}
PortalWindow intersect(PortalWindow a,PortalWindow b) {
    if (!a.visible || !b.visible) return {};
    PortalWindow result{std::max(a.left,b.left),std::max(a.bottom,b.bottom),
        std::min(a.right,b.right),std::min(a.top,b.top),true};
    result.visible = result.left <= result.right && result.bottom <= result.top;
    return result;
}
}
PortalVisibility queryPortalVisibility(const Presentation& frame,const View& view,const Camera& camera) {
    const auto& rooms = frame.data().rooms;
    PortalVisibility result; result.rooms.resize(rooms.size());
    auto& windows = result.rooms;
    const auto& indices = frame.roomIndices();
    const auto root = indices.find(view.room);
    if (!view.portal_visibility || (root == indices.end() && !view.exterior_origin)) {
        for (auto& window : windows) window.visible = true;
        result.exterior = {-1,-1,1,1,true};
        return result;
    }
    std::deque<size_t> queue;
    std::vector<bool> queued(rooms.size(),false);
    std::vector<PortalWindow> exterior_bounds(rooms.size());
    for (size_t i = 0; i < rooms.size(); ++i) if (rooms[i].exterior)
        exterior_bounds[i] = rooms[i].asset->geometry().vertices.empty() ? PortalWindow{-1,-1,1,1,true} :
            projectedBounds(camera,rooms[i].asset->bounds());
    const auto widen = [&](size_t index,PortalWindow window) {
        if (!window.visible) return;
        const bool camera_room = !view.exterior_origin && root != indices.end() && index == root->second;
        if (rooms[index].exterior && !camera_room && !intersect(window,exterior_bounds[index]).visible) return;
        auto& target = windows[index];
        if (target.visible) {
            if (target.left <= window.left && target.right >= window.right && target.bottom <= window.bottom && target.top >= window.top) return;
            window = {std::min(target.left,window.left),std::min(target.bottom,window.bottom),
                std::max(target.right,window.right),std::max(target.top,window.top),true};
        }
        target = window;
        if (!queued[index]) { queue.push_back(index); queued[index] = true; }
    };
    if (view.exterior_origin) {
        // Seed available exterior shells, then enter interiors only through
        // their apertures. Terrain occlusion may refine these seeds later.
        for (size_t i = 0; i < rooms.size(); ++i)
            if (rooms[i].exterior) widen(i,{-1,-1,1,1,true});
    } else widen(root->second,{-1,-1,1,1,true});
    while (!queue.empty()) {
        const size_t index = queue.front(); queue.pop_front(); queued[index] = false;
        const auto& room = rooms[index];
        const auto window = windows[index];
        if (room.exterior) {
            for (size_t other = 0; other < rooms.size(); ++other)
                if (rooms[other].exterior) widen(other,window);
        }
        if (room.blocks_portals && (view.exterior_origin || root == indices.end() || index != root->second)) continue;
        for (const auto& portal : room.portals) {
            if (!portal.traversable) continue;
            const auto destination = indices.find(portal.destination_room);
            if (destination == indices.end()) continue;
            const auto& mesh = room.asset->geometry();
            auto aperture = projectedPortal(camera,mesh,mesh.polygons[portal.polygon],portal.two_sided);
            widen(destination->second,intersect(window,aperture));
        }
    }
    if (view.exterior_origin) result.exterior = {-1,-1,1,1,true};
    else for (size_t i = 0; i < rooms.size(); ++i) if (rooms[i].exterior && windows[i].visible) {
        const auto& window = windows[i];
        if (!result.exterior.visible) result.exterior = window;
        else result.exterior = {std::min(result.exterior.left,window.left),std::min(result.exterior.bottom,window.bottom),
            std::max(result.exterior.right,window.right),std::max(result.exterior.top,window.top),true};
    }
    return result;
}
}
