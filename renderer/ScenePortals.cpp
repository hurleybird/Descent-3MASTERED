// Descent 3MASTERED - GPL-3.0-or-later
#include "ScenePortals.h"
#include <algorithm>
#include <unordered_map>
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
    const double infinity = std::numeric_limits<double>::infinity();
    PortalWindow result{infinity,infinity,-infinity,-infinity,false};
    const auto include = [&](const ClipPosition& p) {
        result.visible = true;
        result.left = std::min(result.left,p.x/p.w); result.right = std::max(result.right,p.x/p.w);
        result.bottom = std::min(result.bottom,p.y/p.w); result.top = std::max(result.top,p.y/p.w);
    };
    // Portal apertures before the geometry near plane must remain open: clipping
    // them at near=1 causes disappearing rooms as the camera crosses a doorway.
    constexpr double eye_clip = 1.e-5;
    // The consumer needs only bounds, not a temporary clipped polygon. Stream
    // the exact same vertices/intersections directly into those bounds.
    auto previous = project(camera,mesh.vertices[polygon.corners.back().vertex]);
    for (const auto& corner : polygon.corners) {
        const auto current = project(camera,mesh.vertices[corner.vertex]);
        if ((previous.w >= eye_clip) != (current.w >= eye_clip)) {
            const double t = (eye_clip-previous.w)/(current.w-previous.w);
            include({previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,0,eye_clip});
        }
        if (current.w >= eye_clip) include(current);
        previous = current;
    }
    if (!result.visible) {
        // At the portal plane, rounding must not close the aperture.
        if (std::abs(facing) <= tolerance) return {-1,-1,1,1,true};
        return {};
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
PortalWindow viewCoverage(const View& view) {
    if (!view.coverage.width) return {-1,-1,1,1,true};
    const auto& c = view.coverage;
    return {2.0*c.x/view.viewport.width-1,1-2.0*(c.y+c.height)/view.viewport.height,
        2.0*(c.x+c.width)/view.viewport.width-1,1-2.0*c.y/view.viewport.height,true};
}
PortalVisibility queryPortalVisibility(const Presentation& frame,const View& view,const Camera& camera) {
    const auto& rooms = frame.data().rooms;
    PortalVisibility result; result.rooms.resize(rooms.size());
    auto& windows = result.rooms;
    const auto& indices = frame.roomIndices();
    const auto root = indices.find(view.room);
    const auto coverage = viewCoverage(view);
    if (!view.portal_visibility || (root == indices.end() && !view.exterior_origin)) {
        for (auto& window : windows) window = coverage;
        result.exterior = coverage;
        return result;
    }
    // Scratch is query-local in meaning and never escapes into the result.
    // The queued bit bounds simultaneous entries by the number of rooms.
    static thread_local std::vector<size_t> queue;
    static thread_local std::vector<bool> queued;
    static thread_local std::vector<PortalWindow> exterior_bounds;
    queue.resize(rooms.size()); queued.assign(rooms.size(),false);
    exterior_bounds.assign(rooms.size(),{});
    size_t queue_head = 0, queue_count = 0;
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
        if (!queued[index]) {
            queue[(queue_head+queue_count)%queue.size()] = index;
            ++queue_count; queued[index] = true;
        }
    };
    if (view.exterior_origin) {
        // Seed available exterior shells, then enter interiors only through
        // their apertures. Terrain occlusion may refine these seeds later.
        for (size_t i = 0; i < rooms.size(); ++i)
            if (rooms[i].exterior) widen(i,coverage);
    } else widen(root->second,coverage);
    while (queue_count) {
        const size_t index = queue[queue_head];
        queue_head = (queue_head+1)%queue.size(); --queue_count; queued[index] = false;
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
    if (view.exterior_origin) result.exterior = coverage;
    else for (size_t i = 0; i < rooms.size(); ++i) if (rooms[i].exterior && windows[i].visible) {
        const auto& window = windows[i];
        if (!result.exterior.visible) result.exterior = window;
        else result.exterior = {std::min(result.exterior.left,window.left),std::min(result.exterior.bottom,window.bottom),
            std::max(result.exterior.right,window.right),std::max(result.exterior.top,window.top),true};
    }
    return result;
}
}
