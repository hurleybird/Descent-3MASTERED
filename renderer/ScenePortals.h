// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
namespace d3scene {
struct PortalWindow {
    double left = -1, bottom = -1, right = 1, top = 1;
    bool visible = false;
};
struct PortalVisibility {
    std::vector<PortalWindow> rooms;
    PortalWindow exterior;
};
// One spatial result for geometry and observations. Room windows and exterior
// admission are conservative NDC unions; neither is a mutable room/face mark.
PortalVisibility queryPortalVisibility(const Presentation&,const View&,const Camera&);
}
