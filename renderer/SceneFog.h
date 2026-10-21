// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
struct Camera;
// Conservative storage eligibility, including repeated store-rounding growth.
// This is an overflow guard, not an image-quality guarantee.
bool fogFieldFitsHalf(double optical_bound,size_t volume_count);
// Exact common-color factorization; densities may differ. False leaves color zero.
bool fogFieldCommonColor(const std::vector<FogVolume>&,const std::vector<uint32_t>& candidates,Vec3& color);
// Fog integrates camera-to-surface rays, not just the geometry clipping range.
// Never apply the geometry near plane or mirror plane to optical candidates.
bool fogMayIntersectView(const Bounds&,const Camera&,bool perspective);
struct FogViewNode { uint32_t source,escape; };
// View-local threaded traversal of the immutable boundary tree. Omitted
// branches cannot intersect any camera-to-surface ray in this optical cone.
void prepareFogViewNodes(const FogVolume&,const Camera&,bool optical_cone,std::vector<FogViewNode>&);
// Connected authored space, compiled before any camera query. Interior portal
// faces disappear from the boundary; all other faces close the volume.
struct FogPortalSource { int32_t polygon, destination_room, destination_portal; bool open; };
struct FogRoomSource {
    int32_t number;
    RoomAssetRef asset;
    Vec3 color;
    float depth;
    std::vector<FogPortalSource> portals;
};
class FogAsset final {
public:
    AssetId id() const { return id_; }
    const std::vector<FogVolume>& volumes() const { return volumes_; }
private:
    friend class SceneAssets;
    FogAsset(AssetId id,std::vector<FogRoomSource> source,std::vector<FogVolume> volumes) :
        id_(id), source_(std::move(source)), volumes_(std::move(volumes)) {}
    AssetId id_;
    std::vector<FogRoomSource> source_;
    std::vector<FogVolume> volumes_;
};
const std::vector<FogVolume>& fogVolumes(const FrameData&);
void buildFogAcceleration(FogVolume&);
bool fogContains(const FogVolume&,Vec3);
int fogWinding(const FogVolume&,Vec3);
// Start distance trims the optical interval, but crossings before that distance
// still establish containment. No per-fragment containment query is necessary.
float fogSegmentLength(const FogVolume&,Vec3 eye,Vec3 endpoint,bool eye_inside,float start_distance = 0);
// Slow independent reference for field audits/tests: sort crossings and integrate
// positive occupancy, including overlapping sheets. Not a per-frame CPU query.
float fogOccupiedLength(const FogVolume&,Vec3 eye,Vec3 endpoint,int eye_winding,float start_distance = 0);
float fogRayStart(const View&,Vec3 endpoint);
}
