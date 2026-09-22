// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
namespace d3scene {
struct PortalWindow {
    double left = -1, bottom = -1, right = 1, top = 1;
    bool visible = false;
};
// Windows are conservative NDC unions indexed by frame room index. They belong
// to the query, not mutable room/face marks. Exterior reachability is conservative
// until the terrain visibility service is migrated.
std::vector<PortalWindow> queryPortalWindows(const Presentation&,const View&,const Camera&);
}
