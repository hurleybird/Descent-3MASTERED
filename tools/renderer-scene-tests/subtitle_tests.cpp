// Descent 3MASTERED - GPL-3.0-or-later
#include "../../movie/SubtitlePresentation.h"
#include <stdexcept>
int runSubtitleTests() {
    using namespace d3movie;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Subtitle presentation contract failed"); };
    const auto rejects = [&](auto operation) {
        bool failed = false; try { operation(); } catch (const std::logic_error&) { failed = true; } check(failed);
    };
    for (int height : {240,360,480,1080}) {
        const auto layout = subtitleLayout(640,height,12,3);
        check(layout.first_y >= 0 && layout.line_pitch > 0 && layout.font_scale == float(height)/480);
        check(layout.first_y+2*layout.line_pitch+std::ceil(12*layout.font_scale) < height);
    }
    check(subtitleLayout(640,480,12,3).first_y == 430);
    check(subtitleLayout(640,360,12,3).first_y == 322);
    rejects([&] {subtitleLayout(0,480,12,3);}); rejects([&] {subtitleLayout(640,0,12,3);});
    rejects([&] {subtitleLayout(640,480,0,3);}); rejects([&] {subtitleLayout(640,480,12,0);});
    struct Cue {int first_frame,last_frame;};
    const Cue cues[] = {{0,3},{1,2},{8,10},{50,60}};
    SubtitleTimeline timeline;
    timeline.advance(0,cues,4); check(timeline.size() == 1 && timeline.active()[0] == 0);
    timeline.advance(1,cues,4); check(timeline.size() == 2);
    timeline.advance(1,cues,4); check(timeline.size() == 2);
    timeline.advance(3,cues,4); check(timeline.size() == 1);
    timeline.advance(7,cues,4); check(timeline.size() == 0);
    timeline.advance(50,cues,4); check(timeline.size() == 1 && timeline.active()[0] == 3);
    timeline.advance(1,cues,4); check(timeline.size() == 2 && timeline.active()[0] == 0);
    timeline.reset(); timeline.advance(0,static_cast<const Cue*>(nullptr),0); check(timeline.size() == 0);
    const Cue overlap[] = {{0,10},{0,10},{0,10},{0,10}};
    rejects([&] {timeline.advance(1,overlap,4);}); check(timeline.size() == 0);
    timeline.advance(1,overlap,3); check(timeline.size() == 3);
    rejects([&] {timeline.advance(-1,cues,4);});
    timeline.reset(); timeline.advance(500,cues,4); check(timeline.size() == 0);
    return checks;
}
