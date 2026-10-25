// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>
namespace d3scene {
// Publication of the display's owned base and optional UI layer. Advancing a
// scene draw target or rendering an independent image cannot retire this image.
// Paused/modal frames reuse the base without re-running world postprocessing.
class DisplayImagePublication {
    enum class State { Empty, Composing, Presented } state_ = State::Empty;
    bool overlay_ = false;
    uint64_t revision_ = 0;
public:
    bool readable() const { return state_ != State::Empty; }
    bool composing() const { return state_ == State::Composing; }
    bool hasOverlay() const { return overlay_; }
    uint64_t revision() const { return revision_; }
    void beginBase() {
        if (composing()) throw std::logic_error("Display base already composing");
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
    void invalidate() { state_ = State::Empty; overlay_ = false; ++revision_; }
};
}
