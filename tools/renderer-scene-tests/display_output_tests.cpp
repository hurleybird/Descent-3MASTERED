// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/DisplayImage.h"
#include "../../renderer/SceneImages.h"
#include <stdexcept>
int runDisplayOutputTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Display publication contract failed"); };
    const auto rejects = [&](auto operation) {
        bool rejected = false; try { operation(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected);
    };
    DisplayImagePublication display;
    check(!display.readable() && !display.composing() && !display.hasOverlay());
    rejects([&] {display.reuseBase();}); rejects([&] {display.present();}); rejects([&] {display.publishOverlay();});
    display.beginBase(); const auto first = display.revision();
    check(display.readable() && display.composing());
    rejects([&] {display.beginBase();});
    display.present(); check(display.readable() && !display.composing() && display.revision() == first);
    // Scene-target reuse/independent exports do not invalidate this publication.
    display.reuseBase(); display.publishOverlay(); display.present();
    check(display.hasOverlay() && display.readable() && display.revision() > first);
    display.reuseBase(); check(!display.hasOverlay()); display.publishOverlay(); display.present();
    display.beginBase(); check(!display.hasOverlay()); display.present();
    display.invalidate(); display.invalidate(); check(!display.readable() && !display.hasOverlay());
    rejects([&] {display.reuseBase();});
    display.beginBase(); display.present(); check(display.readable());
    display.beginBase(DisplayImagePublication::Source::EncodedCanvas);
    check(display.encodedCanvasActive()); display.present(); check(!display.encodedCanvasActive());
    display.reuseBase(); check(display.encodedCanvasActive()); display.present();
    display.beginBase(); check(!display.encodedCanvasActive()); display.present();
    rejects([&] {display.beginBase(static_cast<DisplayImagePublication::Source>(99));});
    check(!display.composing());
    const auto pillar = displayViewport(640,480,1280,720);
    check(pillar.x == 160 && pillar.y == 0 && pillar.width == 960 && pillar.height == 720);
    const auto letter = displayViewport(640,360,640,480);
    check(letter.x == 0 && letter.y == 60 && letter.width == 640 && letter.height == 360);
    const auto same = displayViewport(640,360,1280,720);
    check(same.x == 0 && same.y == 0 && same.width == 1280 && same.height == 720);
    const auto narrow = displayViewport(1,1000000,100,1);
    check(narrow.width == 1 && narrow.height == 1 && narrow.x == 49);
    rejects([&] {displayViewport(0,480,640,480);});

    ImageMip base; base.width = base.height = 1; base.pixels = {0,0,255,255};
    ImageMip ui = base; ui.pixels = {128,0,0,128};
    auto result = compositeDisplayReadback(base,ui,1);
    check(result.pixels == std::vector<uint8_t>({128,0,127,255}));
    check(base.pixels == std::vector<uint8_t>({0,0,255,255}) && ui.pixels[0] == 128);
    result = compositeDisplayReadback(base,ui,2);
    check(result.pixels == std::vector<uint8_t>({64,0,127,255}));
    ui.width = 2; ui.pixels = {255,0,0,255,0,255,0,255};
    result = compositeDisplayReadback(base,ui,1);
    check(result.pixels == std::vector<uint8_t>({128,128,0,255}));
    ui.pixels = {0,0,0,0,0,0,0,0};
    check(compositeDisplayReadback(base,ui,1).pixels == base.pixels);
    rejects([&] {compositeDisplayReadback(base,ui,0);});
    ui.pixels.clear(); rejects([&] {compositeDisplayReadback(base,ui,1);});
    return checks;
}
