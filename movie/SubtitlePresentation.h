// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace d3movie {
struct SubtitleLayout {
    float font_scale;
    int first_y, line_pitch;
};
inline SubtitleLayout subtitleLayout(int width,int height,int font_height,int lines) {
    if (width <= 0 || height <= 0 || font_height <= 0 || lines <= 0 || lines > 32)
        throw std::invalid_argument("Invalid subtitle display layout");
    const double scale = double(height)/480;
    const double pitch = std::round((int64_t(font_height)+font_height/4+1)*scale);
    if (pitch > (std::numeric_limits<int>::max)()) throw std::invalid_argument("Subtitle pitch overflow");
    const int line_pitch = pitch < 1 ? 1 : int(pitch);
    const int64_t top = int64_t(height)-int64_t(lines)*line_pitch-int64_t(std::ceil(2*scale));
    return {float(scale),int(top < 0 ? 0 : top),line_pitch};
}
// The track owner resets this at open/close, including an empty or missing
// track. Duplicate frames are stable; backward playback rebuilds admission.
// Work/storage is bounded, and skipped/expired cues never occupy active slots.
class SubtitleTimeline {
public:
    static constexpr size_t capacity = 3;
    void reset() { active_count_ = next_ = 0; frame_ = -1; active_ = {}; }
    size_t size() const { return active_count_; }
    const std::array<size_t,capacity>& active() const { return active_; }
    template<class Cue> void advance(int frame,const Cue* cues,size_t count) {
        if (frame < 0 || (!cues && count)) throw std::invalid_argument("Invalid subtitle track/frame");
        auto next = *this;
        if (frame < next.frame_) next.reset();
        size_t retained = 0;
        for (size_t i = 0; i < next.active_count_; ++i) {
            if (next.active_[i] >= count) throw std::logic_error("Subtitle track changed without reset");
            if (frame <= cues[next.active_[i]].last_frame) next.active_[retained++] = next.active_[i];
        }
        next.active_count_ = retained;
        while (next.next_ < count && cues[next.next_].first_frame <= frame) {
            const auto index = next.next_++;
            if (cues[index].last_frame < frame) continue;
            if (next.active_count_ == capacity) throw std::logic_error("Too many simultaneous subtitles");
            next.active_[next.active_count_++] = index;
        }
        next.frame_ = frame;
        *this = next;
    }
private:
    std::array<size_t,capacity> active_{};
    size_t active_count_ = 0, next_ = 0;
    int frame_ = -1;
};
}
