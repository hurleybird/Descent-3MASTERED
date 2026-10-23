// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameDisplayLabels.h"
#include <limits>
#include <stdexcept>
#include <string>

int runDisplayLabelTests() {
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Owned display label contract"); };
    GameDisplayLabelQueue queue;
    char borrowed[] = "Player";
    check(queue.append(borrowed,41,52,true,1,.8f,0x123456,100,3)); borrowed[0] = 'X';
    check(queue.size() == 1 && std::strcmp(queue[0].text,"Player") == 0 && queue[0].x == 41 &&
        queue[0].y == 52 && queue[0].centered && queue[0].alpha == 100 && queue[0].flags == 3 && queue[0].color == 0x123456);
    const auto held = queue[0]; queue.clear();
    check(!queue.size() && std::strcmp(held.text,"Player") == 0);
    const std::string long_text(300,'a');
    check(queue.append(long_text.c_str(),0,0,false,1,1,0,255,0) && std::strlen(queue[0].text) == 255);
    for (int i = 1; i < 64; ++i) check(queue.append("label",i,i,false,1,1,0,255,0));
    check(!queue.append("overflow",0,0,false,1,1,0,255,0) && queue.size() == 64);
    queue.clear();
    check(!queue.append(nullptr,0,0,false,1,1,0,255,0));
    check(!queue.append("",0,0,false,1,1,0,255,0));
    check(!queue.append("label",0,0,false,-1,1,0,255,0));
    check(!queue.append("label",0,0,false,1,0,0,255,0));
    check(!queue.append("label",0,0,false,1,std::numeric_limits<float>::infinity(),0,255,0));
    check(!queue.append("label",0,0,false,1,1,0,255,4) && !queue.size());
    d3scene::View view; view.history_key = 1; view.viewport = {100,50,800,600}; view.projection_anchor = {0,0,800,600};
    auto camera = d3scene::prepareCamera(view); d3scene::Vec2 position;
    check(ProjectGameLabel(camera,view.viewport,{0,0,10},.5f,position) && position.x == 500 && position.y == 350);
    check(ProjectGameLabel(camera,view.viewport,{1,1,10},.5f,position) && position.x > 500 && position.y < 350);
    check(!ProjectGameLabel(camera,view.viewport,{0,0,-10},.5f,position));
    check(!ProjectGameLabel(camera,view.viewport,{0,0,.1f},.5f,position));
    check(!ProjectGameLabel(camera,view.viewport,{20,0,10},.95f,position));
    check(!ProjectGameLabel(camera,view.viewport,{0,0,10},0,position));
    check(!ProjectGameLabel(camera,view.viewport,{0,0,0},.5f,position));
    check(!ProjectGameLabel(camera,view.viewport,{0,0,std::numeric_limits<float>::quiet_NaN()},.5f,position));
    view.camera.forward = {0,0,-1}; view.camera.right = {-1,0,0};
    camera = d3scene::prepareCamera(view);
    check(ProjectGameLabel(camera,view.viewport,{0,0,-10},.5f,position) && position.x == 500);
    check(GameLabelSightClear(HIT_NONE,-1,10));
    check(GameLabelSightClear(HIT_OBJECT,10,10));
    check(GameLabelSightClear(HIT_SPHERE_2_POLY_OBJECT,10,10));
    check(!GameLabelSightClear(HIT_OBJECT,11,10));
    check(!GameLabelSightClear(HIT_SPHERE_2_POLY_OBJECT,11,10));
    check(!GameLabelSightClear(HIT_WALL,10,10));
    check(!GameLabelSightClear(HIT_TERRAIN,10,10));
    return checks;
}
