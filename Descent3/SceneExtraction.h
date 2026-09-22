// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include "vecmat.h"

struct object;
struct ModelTransforms;

// One extraction per presentation, shared by main and auxiliary views. This
// reads simulation state but does not draw, advance RNG, or mark visibility.
d3scene::PresentationRef ExtractGamePresentation(double time, float delta, bool paused, bool advance_history = true);
void ResetGamePresentationScene();
void PrepareGameModelInstances(d3scene::FrameData&, const d3scene::FrameData* previous);
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
void PrepareGameCockpitGauges(d3scene::FrameData&,const ModelTransforms&,const d3scene::Transform&);
void PrepareGameCockpit(d3scene::FrameData&, bool advance);
void RenderPreparedCockpit(const d3scene::Presentation&, d3scene::Rect viewport, float zoom);
d3scene::PresentationRef PrepareGamePresentation(bool advance_history = true);
void RenderGameSceneView(const d3scene::Presentation&, d3scene::View);
bool SceneGeometryInspection();
void RenderModuleSceneView(object*, vector*, int, matrix*, float, bool,
    int anchor_x = 0, int anchor_y = 0, int anchor_w = 0, int anchor_h = 0);

d3scene::View MakeGameSceneView(d3scene::ViewKind kind, uint64_t history_key,
    const object* viewer, const vector& eye, int room, const matrix& orientation,
    float zoom, bool rear, d3scene::Rect viewport, d3scene::Rect projection_anchor);

// Migration barrier: preserve observed AI/automap/effect timing when implementing
// this commit step. GPU execution must not write those game structures itself.
void CommitGameVisibility(const d3scene::Presentation&, const d3scene::VisibilityReport&) = delete;
