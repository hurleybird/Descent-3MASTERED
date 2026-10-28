// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>
namespace d3scene {
struct DisplayViewport { int x,y,width,height; };
inline DisplayViewport displayViewport(int width,int height,int view_width,int view_height) {
    if (width <= 0 || height <= 0 || view_width <= 0 || view_height <= 0)
        throw std::invalid_argument("Invalid display image/viewport extent");
    int w = view_width,h = view_height;
    if (int64_t(width)*view_height == int64_t(view_width)*height) return {0,0,w,h};
    if (int64_t(width)*view_height < int64_t(view_width)*height) {
        w = int((int64_t(view_height)*width+height/2)/height);
        if (w < 1) w = 1;
        if (w > view_width) w = view_width;
    } else {
        h = int((int64_t(view_width)*height+width/2)/width);
        if (h < 1) h = 1;
        if (h > view_height) h = view_height;
    }
    return {(view_width-w)/2,(view_height-h)/2,w,h};
}
// Publication of the display's owned base and optional UI layer. Advancing a
// scene draw target or rendering an independent image cannot retire this image.
// Paused/modal frames reuse the base without re-running world postprocessing.
class DisplayImagePublication {
public:
    enum class Source { WorldComposite, EncodedCanvas };
private:
    enum class State { Empty, Composing, Presented } state_ = State::Empty;
    bool overlay_ = false;
    uint64_t revision_ = 0;
    Source source_ = Source::WorldComposite;
public:
    bool readable() const { return state_ != State::Empty; }
    bool composing() const { return state_ == State::Composing; }
    bool hasOverlay() const { return overlay_; }
    uint64_t revision() const { return revision_; }
    Source source() const { return source_; }
    bool encodedCanvasActive() const { return composing() && source_ == Source::EncodedCanvas; }
    void beginBase(Source source = Source::WorldComposite) {
        if (composing()) throw std::logic_error("Display base already composing");
        if (source != Source::WorldComposite && source != Source::EncodedCanvas) throw std::logic_error("Unknown display source");
        source_ = source;
        state_ = State::Composing; overlay_ = false; ++revision_;
    }
    void reuseBase() {
        if (state_ != State::Presented) throw std::logic_error("No presented display base to reuse");
        state_ = State::Composing; overlay_ = false; ++revision_;
    }
    void publishOverlay() {
        if (!composing()) throw std::logic_error("UI overlay has no display base");
        overlay_ = true;
    }
    void present() {
        if (!composing()) throw std::logic_error("Display presentation has no composed image");
        state_ = State::Presented;
    }
    void invalidate() { state_ = State::Empty; overlay_ = false; source_ = Source::WorldComposite; ++revision_; }
};
}
