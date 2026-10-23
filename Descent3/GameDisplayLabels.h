// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/SceneView.h"
#include "findintersection_external.h"
#include <array>
#include <cmath>
#include <cstring>

struct GameDisplayLabel {
    char text[256]{};
    int x = 0, y = 0, font = -1, flags = 0;
    float font_scale = 1;
    uint32_t color = 0xffffff;
    uint8_t alpha = 255;
    bool centered = false;
};
// Game-frame scratch, copied into immutable canvas geometry before execution.
// Compatibility producers may append values, never callbacks or borrowed text.
class GameDisplayLabelQueue {
    std::array<GameDisplayLabel,64> labels_{};
    unsigned count_ = 0;
public:
    void clear() { count_ = 0; }
    unsigned size() const { return count_; }
    const GameDisplayLabel& operator[](unsigned index) const { return labels_[index]; }
    bool append(const char* text,int x,int y,bool centered,int font,float scale,uint32_t color,uint8_t alpha,int flags) {
        if (!text || !*text || count_ == labels_.size() || font < 0 ||
            !std::isfinite(scale) || scale <= 0 || (flags & ~3)) return false;
        auto& label = labels_[count_++];
        std::strncpy(label.text,text,sizeof(label.text)-1); label.text[sizeof(label.text)-1] = 0;
        label.x = x; label.y = y; label.centered = centered; label.font = font;
        label.font_scale = scale; label.color = color; label.alpha = alpha; label.flags = flags;
        return true;
    }
};
inline bool ProjectGameLabel(const d3scene::Camera& camera,d3scene::Rect viewport,
    d3scene::Vec3 anchor,float cone,d3scene::Vec2& display) {
    const d3scene::Vec3 delta{anchor.x-camera.transform.position.x,anchor.y-camera.transform.position.y,anchor.z-camera.transform.position.z};
    const double distance2 = double(delta.x)*delta.x+double(delta.y)*delta.y+double(delta.z)*delta.z;
    const auto& forward = camera.transform.forward;
    if (!(distance2 > 0) || !(cone > 0) || !std::isfinite(cone) ||
        delta.x*forward.x+delta.y*forward.y+delta.z*forward.z < cone*std::sqrt(distance2)) return false;
    const auto point = d3scene::project(camera,anchor);
    if (!(point.w > 0) || point.z < -point.w || point.x < -point.w || point.x > point.w ||
        point.y < -point.w || point.y > point.w) return false;
    display = {float(viewport.x+(point.x/point.w+1)*viewport.width*.5),
        float(viewport.y+(1-point.y/point.w)*viewport.height*.5)};
    return std::isfinite(display.x) && std::isfinite(display.y);
}
struct SceneFrameFeedback;
void QueueGamePlayerLabels(const d3scene::Presentation&,const SceneFrameFeedback&);
inline bool GameLabelSightClear(int fate,int hit,int target) {
    return fate == HIT_NONE || ((fate == HIT_OBJECT || fate == HIT_SPHERE_2_POLY_OBJECT) && hit == target);
}
