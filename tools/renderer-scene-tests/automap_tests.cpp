// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneAutomap.h"
#include "../../renderer/SceneView.h"
#include <stdexcept>

int runAutomapTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    MeshGeometry source; source.vertices = {{-1,-1,10},{1,-1,10},{0,1,10}};
    MeshPolygon polygon; polygon.normal = {0,0,-1}; polygon.corners = {{0},{1},{2}};
    source.polygons.assign(4,polygon);
    std::vector<AutomapFaceSource> faces(4);
    faces[0].appearance = AutomapFaceAppearance::Baked; faces[0].colors = {{1,0,0},{0,1,0},{0,0,1}};
    faces[1].appearance = AutomapFaceAppearance::Black; faces[1].detail = 1;
    faces[2].appearance = AutomapFaceAppearance::Unexplored; faces[2].detail = 2;
    faces[3].appearance = AutomapFaceAppearance::Hidden;
    auto groups = CompileAutomapGeometry(source,faces);
    check(groups[0].polygons.size() == 1 && groups[4].polygons.size() == 1 && groups[8].polygons.size() == 1,
        "Automap appearance and detail tiers were not independently batched");
    check(groups[0].vertex_colors[1].y == 1 && groups[4].vertex_colors[1].y == 0 && groups[8].vertex_colors[1].y == 1,
        "Automap corner colors or secret/unexplored portal treatment changed");
    size_t count = 0; for (const auto& group : groups) count += group.polygons.size();
    check(count == 3 && source.polygons.size() == 4,"Automap compilation exposed a hidden opening or mutated source topology");
    faces[3] = faces[0]; faces[3].colors = {{.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,.5f}};
    groups = CompileAutomapGeometry(source,faces);
    check(groups[0].vertices.size() == 6 && groups[0].vertex_colors[3].x == .5f && groups[0].vertex_colors[0].x == 1,
        "Shared room vertices erased per-face baked color discontinuities");
    faces[0].colors.clear(); bool rejected = false;
    try { CompileAutomapGeometry(source,faces); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Incomplete automap corner colors accepted");
    faces[0] = faces[3]; faces[0].detail = 3; rejected = false;
    try { CompileAutomapGeometry(source,faces); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Unknown automap detail tier accepted");

    SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
    PreparedMesh mesh; mesh.asset = assets.publishMesh(1,groups[0]); mesh.surface.unlit = true;
    mesh.maximum_depth = 300; mesh.depth_reference = {{-1,-1,10},{1,1,20}}; data.meshes.push_back(mesh);
    auto frame = Presentation::freeze(data);
    View view; view.kind = ViewKind::Automap; view.portal_visibility = false;
    view.viewport = view.projection_anchor = {0,0,800,600}; view.history_key = 1;
    check(queryView(*frame,view).surfaces.size() == 1,"Near detail group was culled");
    view.camera.position.z = -290;
    check(queryView(*frame,view).surfaces.size() == 1,"Detail cutoff excluded its boundary");
    view.camera.position.z = -291;
    check(queryView(*frame,view).surfaces.empty(),"Detail cutoff used polygon center instead of the reference volume");
    view.camera.position.z = 0;
    check(queryView(*frame,view).surfaces.size() == 1,"Querying another camera mutated the automap detail set");
    data.meshes[0].maximum_depth = -1; rejected = false;
    try { Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Negative mesh detail depth accepted");
    data.meshes[0].maximum_depth = 300; data.meshes[0].depth_reference.maximum.z = 0; rejected = false;
    try { Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Reversed mesh detail volume accepted");
    return checks;
}
