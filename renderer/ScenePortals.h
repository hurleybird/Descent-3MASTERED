// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
namespace d3scene {
PortalWindow viewCoverage(const View&);
// One spatial result for geometry and observations. Room windows and exterior
// admission are conservative NDC unions; neither is a mutable room/face mark.
PortalVisibility queryPortalVisibility(const Presentation&,const View&,const Camera&);
void queryPortalVisibility(const Presentation&,const View&,const Camera&,PortalVisibility&);
}
