// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneFog.h"
#include "../../renderer/SceneView.h"
#include <cmath>
#include <stdexcept>
#include <limits>
#include <cstring>
namespace {
int Checks = 0;
void check(bool value,const char* message) { ++Checks; if (!value) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b) < .0001f; }
uint32_t fieldWord(const std::vector<float>& values,size_t offset) {
    uint32_t value; std::memcpy(&value,&values.at(offset),sizeof(value)); return value;
}
double preparedFogPath(const std::vector<float>& values,size_t base,const d3scene::Camera& camera,
    d3scene::Vec3 end,bool inside,double start) {
    const auto eye=camera.transform.position;
    const double delta[3]={double(end.x)-eye.x,double(end.y)-eye.y,double(end.z)-eye.z};
    const double length=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    const double direction[3]={delta[0]/length,delta[1]/length,delta[2]/length};
    const auto axisDot=[&](d3scene::Vec3 axis) {return direction[0]*axis.x+direction[1]*axis.y+direction[2]*axis.z;};
    const double forward=axisDot(camera.transform.forward);
    const double uv[2]={axisDot(camera.transform.right)/forward*camera.projection.x_scale+camera.projection.x_offset,
        axisDot(camera.transform.up)/forward*camera.projection.y_scale+camera.projection.y_offset};
    const int side=int(d3scene::FogFieldTileSide);
    const int tile[2]={std::max(0,std::min(side-1,int((uv[0]*.5+.5)*side))),
        std::max(0,std::min(side-1,int((uv[1]*.5+.5)*side)))};
    double path=inside ? std::max(0.0,length-start) : 0;
    for (uint32_t i=0;i<fieldWord(values,base+1);++i) {
        const size_t record=base+fieldWord(values,base)+i*d3scene::FogFieldTriangleWords;
        const double denominator=values[record+16]*direction[0]+values[record+17]*direction[1]+values[record+18]*direction[2];
        if (std::abs(denominator)<1.e-12) continue;
        const double crossing=values[record+19]/denominator;
        if (crossing<=1.e-5 || crossing>=length) continue;
        bool hit=true;
        const int flags=int(values[record+23]);
        for (int edge=0;edge<3;++edge) {
            const size_t p=record+4+edge*4;
            const double value=values[p]*uv[0]+values[p+1]*uv[1]+values[p+2];
            if (value<0 || (value==0 && !(flags&(1<<edge)))) hit=false;
        }
        if (!hit) continue;
        const double low[2]={double(tile[0])*2/side-1,double(tile[1])*2/side-1};
        const double high[2]={low[0]+2.0/side,low[1]+2.0/side};
        check(values[record]<=high[0] && values[record+1]<=high[1] && values[record+2]>=low[0] && values[record+3]>=low[1],
            "Projected rectangle lost an optical crossing");
        for (int edge=0;edge<3;++edge) {
            const size_t p=record+4+edge*4;
            const double maximum=values[p]*(values[p]>=0 ? high[0] : low[0])+
                values[p+1]*(values[p+1]>=0 ? high[1] : low[1])+values[p+2];
            check(maximum>=-values[p+3],"GPU tile edge rejection lost an optical crossing");
        }
        const double inward=values[record+20]*direction[0]+values[record+21]*direction[1]+values[record+22]*direction[2];
        path+=(inward>0 ? 1 : -1)*(length-std::max(start,crossing));
    }
    return std::max(0.0,std::min(std::max(0.0,length-start),path));
}
d3scene::RoomState box(d3scene::SceneAssets& assets,int id,float z) {
    d3scene::MeshGeometry mesh;
    mesh.vertices = {{-1,-1,z},{1,-1,z},{1,1,z},{-1,1,z},{-1,-1,z+2},{1,-1,z+2},{1,1,z+2},{-1,1,z+2}};
    mesh.polygons = {{{0,0,1},{{0},{1},{2},{3}}},{{0,0,-1},{{4},{7},{6},{5}}},
        {{1,0,0},{{0},{3},{7},{4}}},{{-1,0,0},{{1},{5},{6},{2}}},
        {{0,1,0},{{0},{4},{5},{1}}},{{0,-1,0},{{3},{2},{6},{7}}}};
    d3scene::RoomState room; room.number = id; room.asset = assets.publishRoom(id,mesh);
    room.surfaces.resize(6); room.fog_enabled = true; room.fog_depth = 10; room.fog_color = {.2f,.4f,.8f};
    return room;
}
}
int runFogTests() {
    {
        // A broad, shallow clear connector between fogged rooms. Enabling its
        // authored fog must remove both internal boundaries, including for an
        // eye inside that connector, without changing previously held assets.
        using namespace d3scene;
        SceneAssets fixtures;
        std::vector<RoomState> rooms;
        for (int i = 0; i < 3; ++i) {
            auto room = box(fixtures,i,2.f+2*i);
            auto geometry = room.asset->geometry();
            for (auto& vertex : geometry.vertices) { vertex.x *= 100; vertex.y *= 100; }
            room.asset = fixtures.publishRoom(i,geometry);
            rooms.push_back(std::move(room));
        }
        rooms[0].portals.push_back({1,1,0}); rooms[0].surfaces[1].draw_surface = false;
        rooms[1].portals.push_back({0,0,0}); rooms[1].surfaces[0].draw_surface = false;
        rooms[1].portals.push_back({1,2,0}); rooms[1].surfaces[1].draw_surface = false;
        rooms[2].portals.push_back({0,1,1}); rooms[2].surfaces[0].draw_surface = false;
        rooms[1].fog_enabled = false;
        const auto clear = fixtures.publishFog(rooms);
        check(clear->volumes().size() == 2,"Clear connector was implicitly fogged");
        rooms[1].fog_enabled = true;
        const auto filled = fixtures.publishFog(rooms);
        check(filled != clear && filled->volumes().size() == 1 && filled->volumes()[0].rooms.size() == 3,
            "Continuous fog did not rebuild and merge connector boundaries");
        const auto& joined = filled->volumes()[0];
        check(joined.boundary.size() == 28,"Continuous connector retained internal portal triangles");
        check(near(fogSegmentLength(joined,{0,0,0},{0,0,10},false),6),"Continuous connector lost axial fog");
        for (float z : {4.f,4.001f,5.f,5.999f,6.f}) {
            check(fogContains(joined,{0,0,z}),"Connector eye is outside continuous fog");
            check(near(fogSegmentLength(joined,{0,0,z},{99,0,z},true),99),
                "Broad shallow connector lost grazing fog");
        }
        check(clear->volumes().size() == 2 && !fogContains(clear->volumes()[0],{0,0,5}) &&
            !fogContains(clear->volumes()[1],{0,0,5}),"Fog authoring preview mutated held clear space");
        rooms[1].fog_enabled = false;
        check(fixtures.publishFog(rooms)->volumes().size() == 2,"Restoring clear connector retained merged fog");
    }
    {
        using namespace d3scene;
        check(fogFieldFitsHalf(1,1) && fogFieldFitsHalf(100,32),"Ordinary fog rejected compact storage");
        check(!fogFieldFitsHalf(32768,0) && !fogFieldFitsHalf(65504,1) && !fogFieldFitsHalf(1.e12,1),
            "Dense/long-range fog can overflow compact storage");
        check(fogFieldFitsHalf(32760,0) && !fogFieldFitsHalf(32760,1),"Store-rounding growth omitted");
        check(!fogFieldFitsHalf(-1,1) && !fogFieldFitsHalf(std::numeric_limits<double>::infinity(),1) &&
            !fogFieldFitsHalf(std::numeric_limits<double>::quiet_NaN(),1),"Invalid compact storage bound accepted");
        check(!fogFieldFitsHalf(1,1000000),"Accumulation bound overflow accepted");
        std::vector<FogVolume> volumes(3);
        volumes[0].color = volumes[1].color = {.25f,.5f,.75f};
        volumes[0].depth = 10; volumes[1].depth = 100;
        volumes[2].color = {.25f,.5f,.5f};
        Vec3 color{1,1,1};
        check(!fogFieldCommonColor(volumes,{},color) && color.x == 0,"Empty field has a common color");
        check(fogFieldCommonColor(volumes,{0,1},color) && color.x == .25f && color.z == .75f,
            "Density differences prevent exact color factorization");
        check(!fogFieldCommonColor(volumes,{0,2},color) && color.z == 0,"Mixed colors collapsed into scalar field");
        check(fogFieldCommonColor(volumes,{2},color) && color.z == .5f,"Invisible colors affect field classification");
        volumes[1].color.x = std::nextafter(.25f,1.f);
        check(!fogFieldCommonColor(volumes,{0,1},color),"Approximate color equality changed field appearance");
    }
    {
        using namespace d3scene;
        View request; request.history_key = 1; request.viewport = request.projection_anchor = {0,0,800,600};
        request.has_clip_plane = true; request.clip_plane = {{0,0,1},-4};
        auto camera = prepareCamera(request);
        check(fogMayIntersectView({{-.01f,-.01f,.01f},{.01f,.01f,.02f}},camera,true),
            "Optical candidates incorrectly used geometry near/mirror clipping");
        check(!fogMayIntersectView({{100,100,1},{101,101,2}},camera,true),"Off-axis fog remained a candidate");
        check(!fogMayIntersectView({{-1,-1,-4},{1,1,-2}},camera,true),"Fog wholly behind the eye remained a candidate");
        check(fogMayIntersectView({{-100,-100,-100},{100,100,100}},camera,true),"Enclosing fog was rejected");
        check(fogMayIntersectView({{100,100,1},{101,101,2}},camera,false),"Orthographic optical rays were perspective-culled");
        request.viewport = {200,100,400,300}; request.camera.position = {100,-20,30};
        camera = prepareCamera(request);
        uint32_t random = 887;
        const auto sample = [&] { random = random*1664525u+1013904223u; return float(random>>8)/16777216.f; };
        for (int i = 0; i < 2000; ++i) {
            const auto end = unproject(camera,sample()*2-1,sample()*2-1,.999f);
            const float t = sample(); const auto eye = camera.transform.position;
            const Vec3 p{eye.x+t*(end.x-eye.x),eye.y+t*(end.y-eye.y),eye.z+t*(end.z-eye.z)};
            check(fogMayIntersectView({{p.x-.01f,p.y-.01f,p.z-.01f},{p.x+.01f,p.y+.01f,p.z+.01f}},camera,true),
                "Fog candidate rejection lost a camera-to-surface ray segment");
        }
    }
    d3scene::SceneAssets assets;
    d3scene::FrameData frame; frame.rooms.push_back(box(assets,0,2));
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 1 && frame.fog->volumes()[0].boundary.size() == 12,"Fog box boundary is incomplete");
    const auto volume = frame.fog->volumes()[0];
    const auto original_asset = frame.fog;
    {
        using namespace d3scene;
        View request; request.history_key=1;
        request.viewport=request.projection_anchor={0,0,800,600};
        uint32_t random=7341;
        const auto sample=[&] {random=random*1664525u+1013904223u; return float(random>>8)/16777216.f;};
        for (int scenario=0;scenario<4;++scenario) {
            auto view=request;
            view.has_clip_plane=true; view.clip_plane={{0,0,1},-100};
            if (scenario==1) view.camera.position.z=3; // eye-plane crossings
            if (scenario==2) {view.camera.right={-1,0,0}; view.viewport={80,60,500,350};}
            if (scenario==3) {view.camera.position.z=8; view.camera.right={-1,0,0}; view.camera.forward={0,0,-1};}
            const auto camera=prepareCamera(view);
            std::vector<FogViewNode> nodes;
            prepareFogViewNodes(volume,camera,true,nodes);
            std::vector<float> packed;
            appendFogFieldRecords(volume,camera,nodes,0,packed);
            const auto held=packed;
            const bool inside=fogContains(volume,view.camera.position);
            for (int i=0;i<500;++i) {
                const auto end=unproject(camera,sample()*1.4f-.7f,sample()*1.4f-.7f,.5f+sample()*.4f);
                const float start=sample();
                check(std::abs(preparedFogPath(packed,0,camera,end,inside,start)-
                    fogSegmentLength(volume,view.camera.position,end,inside,start))<.002,
                    "Reflected/cropped/eye-plane field changed optical prefixes");
            }
            std::vector<float> other;
            appendFogFieldRecords(volume,camera,nodes,79,other);
            check(packed==held,"Preparing another field mutated a held field");
            auto invalid=camera; invalid.projection.orthographic_depth_scale=.1f;
            bool rejected=false;
            try {appendFogFieldRecords(volume,invalid,nodes,0,other);} catch (const std::invalid_argument&) {rejected=true;}
            check(rejected,"Orthographic projection entered the perspective field contract");
            rejected=false;
            try {appendFogFieldRecords(volume,camera,nodes,INT32_MAX,other);} catch (const std::length_error&) {rejected=true;}
            check(rejected,"Field bitset addressing overflow was accepted");
        }
        FogVolume empty;
        std::vector<FogViewNode> nodes;
        std::vector<float> packed;
        check(appendFogFieldRecords(empty,prepareCamera(request),nodes,0,packed)==0 &&
            packed.size()==FogFieldHeaderWords && fieldWord(packed,1)==0,"Empty field kept stale candidates");
    }
    {
        using namespace d3scene;
        FogVolume many = volume;
        for (int x = -4; x <= 4; ++x) for (int z = -4; z <= 4; ++z) {
            if (!x && !z) continue;
            for (auto triangle : volume.boundary) {
                for (auto* point : {&triangle.a,&triangle.b,&triangle.c}) { point->x += x*8.f; point->z += z*8.f; }
                many.boundary.push_back(triangle);
            }
        }
        buildFogAcceleration(many);
        auto flat = many;
        flat.nodes.resize(1); flat.nodes[0].bounds = many.bounds;
        flat.nodes[0].first = 0; flat.nodes[0].count = uint32_t(flat.boundary.size()); flat.nodes[0].escape = 1;
        std::vector<unsigned> coverage(many.boundary.size());
        for (size_t i = 0; i < many.nodes.size(); ++i) {
            const auto& node = many.nodes[i];
            check(node.escape > i && node.escape <= many.nodes.size(),"Fog tree escape is not forward/bounded");
            check(size_t(node.first)+node.count <= many.boundary.size(),"Fog tree leaf exceeds geometry");
            for (uint32_t t = node.first; t < node.first+node.count; ++t) ++coverage[t];
        }
        for (auto count : coverage) check(count == 1,"Fog tree lost or duplicated a boundary triangle");
        View request; request.history_key = 1; request.viewport = request.projection_anchor = {0,0,800,600};
        std::vector<FogViewNode> nodes;
        auto camera = prepareCamera(request);
        prepareFogViewNodes(many,camera,false,nodes);
        bool identical = nodes.size() == many.nodes.size();
        for (size_t i = 0; identical && i < nodes.size(); ++i)
            identical = nodes[i].source == i && nodes[i].escape == many.nodes[i].escape;
        check(identical,"Unrestricted fog traversal changed tree ordering");
        uint32_t random = 12345;
        const auto sample = [&] { random = random*1664525u+1013904223u; return float(random>>8)/16777216.f; };
        for (const float eye_z : {0.f,3.f}) {
            request.camera.position.z = eye_z; camera = prepareCamera(request);
            prepareFogViewNodes(many,camera,true,nodes);
            check(!nodes.empty() && nodes.size() < many.nodes.size(),"Optical cone did not prune off-view fog branches");
            std::vector<float> packed;
            appendFogViewRecords(many,nodes,packed);
            check(packed.size() == nodes.size()*8,"Packed fog node stride");
            for (size_t i=0;i<nodes.size();++i) {
                const auto& source = many.nodes[nodes[i].source];
                check(packed[i*8] == source.bounds.minimum.x && packed[i*8+1] == source.bounds.minimum.y &&
                    packed[i*8+2] == source.bounds.minimum.z && packed[i*8+4] == source.bounds.maximum.x &&
                    packed[i*8+5] == source.bounds.maximum.y && packed[i*8+6] == source.bounds.maximum.z,
                    "Packed fog bounds changed");
                check(packed[i*8+3] == float(source.count ? source.first : nodes[i].escape) &&
                    packed[i*8+7] == float(source.count),"Packed fog payload changed");
            }
            auto narrowed = many; narrowed.nodes.clear();
            for (size_t i = 0; i < nodes.size(); ++i) {
                check(nodes[i].escape > i && nodes[i].escape <= nodes.size(),"Pruned fog escape is not forward/bounded");
                auto node = many.nodes[nodes[i].source]; node.escape = nodes[i].escape; narrowed.nodes.push_back(node);
            }
            const bool inside = fogContains(many,request.camera.position);
            std::vector<float> field(4,17);
            const uint32_t stride=appendFogFieldRecords(many,camera,nodes,123,field);
            check(field.front()==17 && field[3]==17,"Field preparation overwrote a preceding view");
            check(fieldWord(field,6)==123 && fieldWord(field,7)==stride &&
                stride==(fieldWord(field,5)+31)/32,"Field tile-mask addressing changed");
            check(fieldWord(field,8)==FogFieldTileSide && fieldWord(field,9)==FogFieldTileSide,
                "Field tile dimensions differ from the shader contract");
            for (size_t i=0;i<packed.size();++i)
                check(field[4+FogFieldHeaderWords+i]==packed[i],"Field packing changed reference traversal");
            for (int i = 0; i < 2000; ++i) {
                const auto end = unproject(camera,sample()*2-1,sample()*2-1,.5f+sample()*.499f);
                const float start = sample()*5;
                check(fogSegmentLength(many,request.camera.position,end,inside,start) ==
                    fogSegmentLength(flat,request.camera.position,end,inside,start),
                    "Fog acceleration changed brute-force integration");
                check(fogSegmentLength(many,request.camera.position,end,inside,start) ==
                    fogSegmentLength(narrowed,request.camera.position,end,inside,start),
                    "Pruned fog traversal changed exact optical integration");
                check(std::abs(preparedFogPath(field,4,camera,end,inside,start)-
                    fogSegmentLength(many,request.camera.position,end,inside,start))<.002,
                    "Prepared homogeneous boundary equations changed optical integration");
            }
            for (float x : {-.5f,0.f,.5f}) for (float y : {-.5f,0.f,.5f}) {
                const auto end=unproject(camera,x,y,.99f);
                check(std::abs(preparedFogPath(field,4,camera,end,inside,0)-
                    fogSegmentLength(many,request.camera.position,end,inside))<.002,
                    "Prepared field doubled or lost a shared triangle edge");
            }
        }
        FogVolume empty; prepareFogViewNodes(empty,camera,true,nodes);
        check(nodes.empty(),"Empty fog retained old view nodes");
    }
    check(assets.publishFog(frame.rooms) == original_asset,"Unchanged fog rebuilt its spatial asset");
    {
        auto invalid = frame.rooms;
        invalid[0].fog_depth = std::numeric_limits<float>::quiet_NaN();
        bool rejected = false;
        try { assets.publishFog(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Retained fog skipped source validation");
        auto changed = frame.rooms;
        auto geometry = changed[0].asset->geometry(); geometry.vertices[0].x -= .1f;
        changed[0].asset = assets.publishRoom(0,std::move(geometry));
        const auto replacement = assets.publishFog(changed);
        check(replacement != original_asset && assets.publishFog(changed) == replacement,
            "Fog compilation ignored geometry revision or failed reuse");
        check(assets.publishFog(frame.rooms)->volumes()[0].boundary[0].a.x == -1,
            "Held fog geometry was replaced by a newer source slot");
    }
    frame.rooms[0].fog_color.x = .7f;
    const auto recolored = assets.publishFog(frame.rooms);
    check(recolored->id() != original_asset->id() && original_asset->volumes()[0].color.x == .2f,
        "Fog material revision mutated held scene data");
    frame.rooms[0].fog_color.x = .2f;
    check(d3scene::fogContains(volume,{0,0,3}),"Fog containment missed inside point");
    check(d3scene::fogWinding(volume,{0,0,3}) == 1,"Closed fog winding is not one inside");
    {
        auto overlapping = volume;
        overlapping.boundary.insert(overlapping.boundary.end(),volume.boundary.begin(),volume.boundary.end());
        d3scene::buildFogAcceleration(overlapping);
        check(d3scene::fogWinding(overlapping,{0,0,3}) == 2,"Fog occupancy lost overlapping boundary winding");
        check(d3scene::fogContains(overlapping,{0,0,3}),"Overlapping fog boundaries lost containment");
        check(near(d3scene::fogOccupiedLength(overlapping,{0,0,3},{0,0,10},2),1),
            "Positive-occupancy reference cancelled the overlapping interior interval");
        check(near(d3scene::fogOccupiedLength(overlapping,{0,0,0},{0,0,10},0),2),
            "Positive-occupancy reference doubled overlapping fog density");
    }
    check(!d3scene::fogContains(volume,{0,0,0}),"Fog containment included outside point");
    check(near(d3scene::fogSegmentLength(volume,{0,0,0},{0,0,10},false),2),"Diagonal crossing counted fog triangles twice");
    check(near(d3scene::fogSegmentLength(volume,{0,0,0},{0,0,3},false),1),"Entry fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,10},true),1),"Exit fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,3.5f},true),.5f),"Interior fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{2,0,0},{2,0,10},false),0),"Ray beside fog volume was fogged");
    auto separated = volume;
    for (auto triangle : volume.boundary) {
        triangle.a.z += 4; triangle.b.z += 4; triangle.c.z += 4;
        separated.boundary.push_back(triangle);
    }
    d3scene::buildFogAcceleration(separated);
    check(!d3scene::fogContains(separated,{0,0,5}) && d3scene::fogContains(separated,{0,0,7}),
        "Non-convex fog containment filled a clear interval");
    check(near(d3scene::fogSegmentLength(separated,{0,0,0},{0,0,10},false),4),"Separated fog intervals lost an exit or entry");
    check(near(d3scene::fogSegmentLength(separated,{0,0,3},{0,0,7},true),2),"Clear space between fog intervals was integrated");
    // The excluded prefix still determines winding at the optical start. Test
    // starts before, inside, between and after disconnected fog intervals.
    for (int i = 0; i <= 100; ++i) {
        const float start = i*.1f;
        const float expected = (std::max)(0.f,4.f-(std::max)(2.f,start)) +
            (std::max)(0.f,8.f-(std::max)(6.f,start));
        check(near(d3scene::fogSegmentLength(separated,{0,0,0},{0,0,10},false,start),expected),
            "Trimmed fog interval lost pre-start containment");
        check(near(d3scene::fogOccupiedLength(separated,{0,0,0},{0,0,10},0,start),expected),
            "Positive-occupancy reference lost a disconnected or clipped interval");
        check(near(d3scene::fogSegmentLength(separated,{0,0,10},{0,0,0},false,start),expected),
            "Trimmed fog interval depends on ray orientation");
    }
    check(near(d3scene::fogSegmentLength(separated,{0,0,3},{0,0,9},true,4),1),
        "Trimmed fog ray from inside retained an excluded interval");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,3.5f},true,1),0),
        "Fog start beyond endpoint produced a negative interval");
    d3scene::View clipped; clipped.history_key = 1;
    clipped.viewport = clipped.projection_anchor = {0,0,800,600};
    clipped.has_clip_plane = true; clipped.clip_plane = {{0,0,1},-3};
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),0),"Geometry clipping implicitly changed optical policy");
    clipped.fog_path_origin = d3scene::FogPathOrigin::ClipPlane;
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),3),"Fog ray did not start on physical plane");
    check(near(d3scene::fogRayStart(clipped,{4,0,3}),5),"Oblique fog start used axial distance");
    check(near(d3scene::fogRayStart(clipped,{0,0,2}),2),"Behind-plane fragment integrated fog");
    check(near(d3scene::fogRayStart(clipped,{4,0,0}),4),"Parallel excluded ray integrated fog");
    clipped.camera.position.z = 4;
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),0),"Retained camera ray lost its prefix");
    clipped.has_clip_plane = false;
    bool missing_plane = false;
    try { d3scene::validateView(clipped); } catch (const std::invalid_argument&) { missing_plane = true; }
    check(missing_plane,"Optical plane policy accepted missing plane");
    auto trigger_geometry = frame.rooms[0].asset->geometry();
    auto trigger = trigger_geometry.polygons[0]; trigger.bounds_volume = false;
    trigger_geometry.polygons.push_back(trigger);
    frame.rooms[0].asset = assets.publishRoom(0,trigger_geometry); frame.rooms[0].surfaces.resize(7);
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes()[0].boundary.size() == 12,"Floating trigger became a spatial fog boundary");
    // Off-diagonal rays and reversed direction must agree with slab intersection.
    for (int i = -49; i <= 49; ++i) {
        const float x = i/50.f, y = i/99.f;
        check(near(d3scene::fogSegmentLength(volume,{x,y,0},{x,y,9},false),2),"Fog ray disagrees with analytic slab");
        check(near(d3scene::fogSegmentLength(volume,{x,y,9},{x,y,0},false),2),"Fog path depends on viewing direction");
    }
    frame.rooms.push_back(box(assets,1,4));
    frame.rooms[0].portals.push_back({1,1,0}); frame.rooms[0].surfaces[1].draw_surface = false;
    frame.rooms[1].portals.push_back({0,0,0}); frame.rooms[1].surfaces[0].draw_surface = false;
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 1 && frame.fog->volumes()[0].rooms.size() == 2 && frame.fog->volumes()[0].boundary.size() == 20,
        "Matching connected fog was split or retained internal portal faces");
    check(assets.publishFog(frame.rooms) == frame.fog,"Connected fog did not reuse its compiled portal source");
    {
        auto invalid = frame.rooms; invalid[0].surfaces.clear();
        bool rejected = false;
        try { assets.publishFog(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Retained fog indexed a missing portal surface");
    }
    check(near(d3scene::fogSegmentLength(frame.fog->volumes()[0],{0,0,0},{0,0,10},false),4),"Connected room seam changed optical distance");
    check(near(d3scene::fogSegmentLength(frame.fog->volumes()[0],{0,0,4},{0,0,10},true),2),"Camera on internal portal lost fog");
    frame.rooms[1].fog_color.x = .3f; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 2,"Different authored fog colors were merged");
    frame.rooms[1].fog_color.x = .2f; frame.rooms[1].surfaces[0].draw_surface = true; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 2,"Rendered portal sheet failed to separate fog components");
    frame.rooms[0].fog_enabled = frame.rooms[1].fog_enabled = false; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().empty(),"Disabling fog retained stale volumes");
    assets.reset();
    check(original_asset->volumes()[0].boundary.size() == 12,"Level unload destroyed held fog geometry");
    d3scene::FrameData stale; stale.sequence = 1; stale.scene_lifetime = assets.lifetime(); stale.fog = original_asset;
    bool rejected = false;
    try { d3scene::Presentation::freeze(stale); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Fog asset from another level lifetime was accepted");
    return Checks;
}
