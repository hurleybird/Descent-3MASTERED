// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include "vecmat.h"

struct object;

// One extraction per presentation, shared by main and auxiliary views. This
// reads simulation state but does not draw, advance RNG, or mark visibility.
d3scene::PresentationRef ExtractGamePresentation(double time, float delta, bool paused);
void ResetGamePresentationScene();

d3scene::View MakeGameSceneView(d3scene::ViewKind kind, uint64_t history_key,
    const object* viewer, const vector& eye, int room, const matrix& orientation,
    float zoom, bool rear, d3scene::Rect viewport, d3scene::Rect projection_anchor);

// Migration barrier: preserve observed AI/automap/effect timing when implementing
// this commit step. GPU execution must not write those game structures itself.
void CommitGameVisibility(const d3scene::Presentation&, const d3scene::VisibilityReport&) = delete;
