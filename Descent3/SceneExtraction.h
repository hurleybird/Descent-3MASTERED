// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include "vecmat.h"

struct object;
struct ModelTransforms;
struct HUDCanvasLayout;
d3scene::FrameData ExtractGameFrame(double time,float delta,bool paused,bool advance_history = true,
    const d3scene::FrameData* retained_rooms = nullptr,bool audit_geometry = false);

// One extraction per presentation, shared by main and auxiliary views. This
// reads simulation state but does not draw, advance RNG, or mark visibility.
d3scene::PresentationRef ExtractGamePresentation(double time, float delta, bool paused, bool advance_history = true,
    const d3scene::FrameData* retained_rooms = nullptr);
void ResetGamePresentationScene();
// Geometry/volume writers invalidate at their mutation boundary. Material,
// light intensity and visibility changes are not geometry mutations.
void InvalidateGameSceneGeometry();
uint64_t GameSceneGeometryRevision();
void PrepareGameModelInstances(d3scene::FrameData&, const d3scene::FrameData* previous,bool retain_materials = false);
d3scene::ModelAssetRef PrepareGameModelAsset(int number,bool audit_geometry = false);
void PrepareGameStaticModel(d3scene::FrameData&,int model,const d3scene::Transform&,uint64_t identity,uint64_t collection = 0);
void PrepareGameSceneResources(d3scene::FrameData&);
void PrepareGameInspectionBanner(d3scene::FrameData&);
void PrepareGameSceneLights(d3scene::FrameData&);
void PrepareGameTerrain(d3scene::FrameData&);
void PrepareGameSky(d3scene::FrameData&);
void PrepareGameEffects(d3scene::FrameData&);
void PrepareGameObjectEffects(d3scene::FrameData&);
void PrepareGameScorches(d3scene::FrameData&);
constexpr uint64_t SceneCockpitBase = 0x434f434b50495401ull;
constexpr uint64_t SceneCockpitLate = 0x434f434b50495402ull;
constexpr uint64_t SceneCockpitAfterGauges = 0x434f434b50495403ull;
constexpr uint64_t SceneCockpitGauges = 0x434f434b50495404ull;
constexpr uint64_t SceneCockpitMonitorBacking = 0x434f434b50495405ull;
constexpr uint64_t SceneCockpitText = 0x434f434b50495406ull;
constexpr uint64_t SceneReticle = 0x4855445245540001ull;
constexpr uint64_t SceneHUDItems = 0x4855444954450001ull;
constexpr uint64_t SceneHUDMessages = 0x4855444d53470001ull;
constexpr uint64_t SceneHUDAuxiliary = 0x4855444155580001ull;
constexpr uint64_t SceneViewImageProbe = 0x56494557494d4701ull;
constexpr uint64_t SceneInspectionBanner = 0x5445535442414e01ull;
constexpr float SceneCockpitZoom = .56f;
void PrepareGameCockpitGauges(d3scene::FrameData&,const ModelTransforms&,const d3scene::Transform&,bool moving);
void PrepareGameCockpit(d3scene::FrameData&, bool advance,const d3scene::FrameData* previous = nullptr,bool retain_materials = false);
void PrepareGameReticle(d3scene::FrameData&,const HUDCanvasLayout&);
void PrepareGameHUD(d3scene::FrameData&,const HUDCanvasLayout&,bool advance);
void RenderPreparedReticle(const d3scene::Presentation&);
void RenderGameForegrounds(const d3scene::Presentation&,d3scene::ForegroundPhase);
void PrepareCockpitComposition(d3scene::FrameData&,d3scene::Rect viewport,float zoom);
d3scene::PresentationRef PrepareGamePresentation();
d3scene::VisibilityReport RenderGameSceneView(const d3scene::Presentation&, d3scene::View);
bool SceneGeometryInspection();
void RenderModuleSceneView(object*, vector*, int, matrix*, float, bool,
    int anchor_x = 0, int anchor_y = 0, int anchor_w = 0, int anchor_h = 0);

d3scene::View MakeGameSceneView(d3scene::ViewKind kind, uint64_t history_key,
    const object* viewer, const vector& eye, int room, const matrix& orientation,
    float zoom, bool rear, d3scene::Rect viewport, d3scene::Rect projection_anchor);

// Migration barrier: preserve observed AI/automap/effect timing when implementing
// this commit step. GPU execution must not write those game structures itself.
void CommitGameVisibility(const d3scene::Presentation&, const d3scene::VisibilityReport&) = delete;
