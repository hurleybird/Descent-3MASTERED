// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/SceneView.h"
#include <array>
#include <cmath>
#include <stdexcept>
#include <limits>

constexpr int SVW_LEFT = 0, SVW_CENTER = 1, SVW_RIGHT = 2;
constexpr int SVF_POPUP = 1, SVF_BIGGER = 2, SVF_REARVIEW = 4, SVF_TIMED = 8, SVF_CROSSHAIRS = 16, SVF_STATIC = 32;
constexpr float SmallViewStaticTime = .25f;
struct GameAuxiliarySlot {
    int objhandle = -1, flags = 0;
    float timer = 0, zoom = 1;
    int gun_num = -1;
    char label[20]{};
};
inline bool ValidAuxiliaryRequest(int flags,float time,float zoom,int gun) {
    return !(flags & ~(SVF_POPUP|SVF_BIGGER|SVF_REARVIEW|SVF_CROSSHAIRS)) &&
        std::isfinite(time) && time >= 0 && std::isfinite(zoom) && zoom >= 0 && gun >= -1;
}
inline d3scene::Rect AuxiliaryWindowRectangle(d3scene::Rect window,int slot,bool bigger) {
    if (slot < 0 || slot > 2 || window.width <= 0 || window.height <= 0) return {};
    int64_t w = window.height/4, h = w, spacing = (window.width-3*w)/3;
    const int64_t x = (spacing+w)/2+slot*(spacing+w)+window.x;
    const int64_t y = int64_t(window.y)+window.height-h/2-window.height/24;
    if (bigger) { w += w/4; h += h/4; }
    const int64_t left = x-w/2, top = y-h/2, width = (w/2)*2, height = (h/2)*2;
    if (left < INT32_MIN || left > INT32_MAX || top < INT32_MIN || top > INT32_MAX || width > INT32_MAX || height > INT32_MAX)
        throw std::invalid_argument("Auxiliary window overflow");
    return {int32_t(left),int32_t(top),int32_t(width),int32_t(height)};
}
inline void OpenAuxiliarySlot(GameAuxiliarySlot& live,GameAuxiliarySlot& saved,const GameAuxiliarySlot& next) {
    if ((next.flags & SVF_POPUP) && live.objhandle != -1 && !(live.flags & SVF_POPUP)) saved = live;
    else if (!(next.flags & SVF_POPUP)) saved = {};
    live = next;
}
inline bool CloseAuxiliarySlot(GameAuxiliarySlot& live,GameAuxiliarySlot& saved) {
    live = saved; saved = {}; return live.objhandle != -1;
}
inline void ObserveAuxiliarySource(GameAuxiliarySlot& slot,bool alive) {
    if (slot.objhandle != -1 && !alive && !(slot.flags & SVF_STATIC)) {
        slot.flags |= SVF_STATIC|SVF_TIMED; slot.timer = SmallViewStaticTime; slot.gun_num = -1;
    }
}
inline bool AdvanceAuxiliaryTimer(GameAuxiliarySlot& slot,bool admitted,float delta) {
    if (!std::isfinite(delta) || delta < 0) throw std::invalid_argument("Invalid auxiliary display delta");
    if (!admitted || slot.objhandle == -1 || !(slot.flags & SVF_TIMED)) return false;
    slot.timer -= delta; return slot.timer < 0;
}
class AuxiliaryFrameReceipt {
    uint64_t scene_ = 0, frame_ = 0;
    bool valid_ = false;
public:
    void reset() { valid_ = false; }
    bool begin(uint64_t scene,uint64_t frame) {
        if (valid_ && scene_ == scene) {
            if (frame == frame_) return false;
            if (frame < frame_) throw std::logic_error("Auxiliary display frame reordered");
        }
        valid_ = true; scene_ = scene; frame_ = frame; return true;
    }
};
struct GameAuxiliaryView {
    d3scene::View world;
    d3scene::Rect rectangle;
    int slot = 0, flags = 0, bitmap = -1;
    uint64_t revision = 0;
    uint32_t owner = 0;
    uint32_t outline = 0;
    bool outlined = false, embedded = false;
    float timer = 0, font_scale = 1;
    char label[20]{};
};
struct GameAuxiliaryFrame {
    uint64_t scene = 0, frame = 0;
    unsigned count = 0;
    int cockpit_state = 0;
    bool hidden = false, alternative_cockpit = false, cockpit_visible = false;
    float delta = 0, time = 0;
    int supersampling = 1;
    d3scene::Rect window;
    std::array<GameAuxiliaryView,3> views;
};
struct GameAuxiliarySchedule {
    uint64_t scene = 0, frame = 0;
    uint64_t world_sequence = 0;
    unsigned count = 0, admitted = 0;
    float delta = 0, time = 0;
    int supersampling = 1;
    bool alternative_cockpit = false;
    std::array<GameAuxiliaryView,3> views;
};
