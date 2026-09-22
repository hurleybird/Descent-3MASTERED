// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "robotfirestruct.h"
#include "polymodel_external.h"
#include <array>

// Instance values, never scratch fields on a shared model asset. Evaluation
// has no clock, object, renderer, RNG, or motion-history dependency.
struct ModelSubmodelPose {
    angvec angles{};
    vector translation{};
};
struct ModelPose {
    int count = 0;
    std::array<ModelSubmodelPose, MAX_SUBOBJECTS> submodels{};
};
ModelPose EvaluateModelPose(const poly_model& model, const float* normalized_time,
    float simulation_time);

// Complete local-to-model transforms. Geometry, attachment points and future
// render passes consume the same result; none depends on shared scratch pose.
struct ModelSubmodelTransform {
    matrix orientation{};
    vector translation{};
};
struct ModelTransforms {
    int count = 0;
    std::array<ModelSubmodelTransform, MAX_SUBOBJECTS> submodels{};
};
ModelTransforms ComposeModelPose(const poly_model& model, const ModelPose& pose);
vector TransformModelPoint(const ModelSubmodelTransform& transform, const vector& point);
vector TransformModelDirection(const ModelSubmodelTransform& transform, const vector& direction);

// Also used by the asset loader to compile authored rotation tracks.
void BuildModelAngleMatrix(matrix* result, angle rotation, const vector* axis);
