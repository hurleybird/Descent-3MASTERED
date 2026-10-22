// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include "vecmat.h"

struct object;
struct ModelTransforms;
struct HUDCanvasLayout;
class SceneFeedbackTransaction;
struct FrameScreenBlend;
// Capture the physical main camera and its logical crop once, before scene
// publication. World and camera-bound foregrounds consume the same value.
struct GameMainView {
    d3scene::View world;
    d3scene::Rect display_viewport;
    bool player_first_person = false;
};
d3scene::FrameData ExtractGameFrame(double time,float delta,bool paused,bool advance_history = true,
    const d3scene::Presentation* retained_frame = nullptr,bool audit_geometry = false);

// One extraction per presentation, shared by main and auxiliary views. This
// reads simulation state but does not draw, advance RNG, or mark visibility.
d3scene::PresentationRef ExtractGamePresentation(double time, float delta, bool paused, bool advance_history = true,
    const d3scene::Presentation* retained_frame = nullptr);
void ResetGamePresentationScene();
// Geometry/volume writers invalidate at their mutation boundary. Material,
// light intensity and visibility changes are not geometry mutations.
void InvalidateGameSceneGeometry();
void InvalidateGameSceneAppearance();
uint64_t GameSceneGeometryRevision();
void PrepareGameModelInstances(d3scene::FrameData&, const d3scene::Presentation* previous,bool retain_materials = false);
d3scene::ModelAssetRef PrepareGameModelAsset(int number,bool audit_geometry = false);
void AuditGameModelPoses(const d3scene::Presentation&);
void PrepareGameStaticModel(d3scene::FrameData&,int model,const d3scene::Transform&,uint64_t identity,uint64_t collection = 0);
void PrepareGameSceneResources(d3scene::FrameData&);
void PrepareGameInspectionBanner(d3scene::FrameData&);
void PrepareGameSceneLights(d3scene::FrameData&);
void PrepareGameTerrain(d3scene::FrameData&);
void PrepareGameSky(d3scene::FrameData&);
void PrepareGameEffects(d3scene::FrameData&,const GameMainView* = nullptr);
void PrepareGameObjectEffects(d3scene::FrameData&);
void PrepareGameScorches(d3scene::FrameData&);
constexpr uint64_t SceneCockpitBase = 0x434f434b50495401ull;
constexpr uint64_t SceneCockpitLate = 0x434f434b50495402ull;
constexpr uint64_t SceneCockpitAfterGauges = 0x434f434b50495403ull;
constexpr uint64_t SceneCockpitGauges = 0x434f434b50495404ull;
constexpr uint64_t SceneCockpitMonitorBacking = 0x434f434b50495405ull;
constexpr uint64_t SceneCockpitText = 0x434f434b50495406ull;
constexpr uint64_t SceneCockpitPrimaryView = 0x434f434b50495407ull;
constexpr uint64_t SceneCockpitSecondaryView = 0x434f434b50495408ull;
constexpr uint64_t SceneReticle = 0x4855445245540001ull;
constexpr uint64_t SceneHUDItems = 0x4855444954450001ull;
constexpr uint64_t SceneHUDMessages = 0x4855444d53470001ull;
constexpr uint64_t SceneHUDAuxiliary = 0x4855444155580001ull;
constexpr uint64_t SceneViewImageProbe = 0x56494557494d4701ull;
constexpr uint64_t SceneInspectionBanner = 0x5445535442414e01ull;
constexpr uint64_t SceneCloseScreenEffects = 0x5745415448455201ull;
constexpr uint64_t SceneFrameScreenBlend = 0x53435245454e0001ull;
constexpr float SceneCockpitZoom = .56f;
void PrepareGameCockpitGauges(d3scene::FrameData&,const ModelTransforms&,const d3scene::Transform&,bool moving);
void PrepareGameCockpit(d3scene::FrameData&, bool advance,const d3scene::FrameData* previous = nullptr,bool retain_materials = false);
void PrepareGameReticle(d3scene::FrameData&,const HUDCanvasLayout&);
void PrepareGameHUD(d3scene::FrameData&,const HUDCanvasLayout&,bool advance);
void RenderPreparedReticle(const d3scene::Presentation&);
void RenderGameForegrounds(const d3scene::Presentation&,d3scene::ForegroundPhase);
void PrepareCockpitComposition(d3scene::FrameData&,d3scene::Rect viewport,float zoom);
// Independent captures publish their own scene without advancing cockpit/HUD
// animation or replacing the gameplay presentation's motion-history source.
d3scene::PresentationRef PrepareGamePresentation(bool advance = true,const GameMainView* = nullptr,
    const std::vector<FrameScreenBlend>* overlays = nullptr);
void RenderGameSceneView(const d3scene::Presentation&, d3scene::View,
    SceneFeedbackTransaction* game_observations = nullptr);
bool SceneGeometryInspection();
void RenderModuleSceneView(object*, vector*, int, matrix*, float, bool,
    int anchor_x = 0, int anchor_y = 0, int anchor_w = 0, int anchor_h = 0);

d3scene::View MakeGameSceneView(d3scene::ViewKind kind, uint64_t history_key,
    const object* viewer, const vector& eye, int room, const matrix& orientation,
    float zoom, bool rear, d3scene::Rect viewport, d3scene::Rect projection_anchor);
