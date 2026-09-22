// Descent 3MASTERED - GPL-3.0-or-later
#include "ScenePortals.h"
#include <algorithm>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <limits>
namespace d3scene {
namespace {
PortalWindow projectedPortal(const Camera& camera,const MeshGeometry& mesh,const MeshPolygon& polygon) {
    const auto first = mesh.vertices[polygon.corners[0].vertex];
    const auto eye = camera.transform.position;
    const auto n = polygon.normal;
    const double facing = double(n.x)*(eye.x-first.x)+double(n.y)*(eye.y-first.y)+double(n.z)*(eye.z-first.z);
    const double tolerance = 2.e-6*(1+std::abs(eye.x)+std::abs(eye.y)+std::abs(eye.z));
    if (facing < -tolerance) return {};
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
std::vector<PortalWindow> queryPortalWindows(const Presentation& frame,const View& view,const Camera& camera) {
    const auto& rooms = frame.data().rooms;
    std::vector<PortalWindow> windows(rooms.size());
    std::unordered_map<int32_t,size_t> indices;
    for (size_t i = 0; i < rooms.size(); ++i) indices.emplace(rooms[i].number,i);
    const auto root = indices.find(view.room);
    if (!view.portal_visibility || root == indices.end()) {
        for (auto& window : windows) window.visible = true;
        return windows;
    }
    std::deque<size_t> queue{root->second};
    windows[root->second].visible = true;
    std::vector<bool> queued(rooms.size(),false); queued[root->second] = true;
    const auto widen = [&](size_t index,PortalWindow window) {
        if (!window.visible) return;
        auto& target = windows[index];
        if (target.visible) {
            if (target.left <= window.left && target.right >= window.right && target.bottom <= window.bottom && target.top >= window.top) return;
            window = {std::min(target.left,window.left),std::min(target.bottom,window.bottom),
                std::max(target.right,window.right),std::max(target.top,window.top),true};
        }
        target = window;
        if (!queued[index]) { queue.push_back(index); queued[index] = true; }
    };
    while (!queue.empty()) {
        const size_t index = queue.front(); queue.pop_front(); queued[index] = false;
        const auto& room = rooms[index];
        const auto window = windows[index];
        if (room.exterior) {
            for (size_t other = 0; other < rooms.size(); ++other)
                if (rooms[other].exterior) widen(other,window);
        }
        if (room.blocks_portals && index != root->second) continue;
        for (const auto& portal : room.portals) {
            if (!portal.traversable) continue;
            const auto destination = indices.find(portal.destination_room);
            if (destination == indices.end()) continue;
            const auto& mesh = room.asset->geometry();
            auto aperture = projectedPortal(camera,mesh,mesh.polygons[portal.polygon]);
            widen(destination->second,intersect(window,aperture));
        }
    }
    return windows;
}
}
