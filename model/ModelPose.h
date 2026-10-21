// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "robotfirestruct.h"
#include "polymodel_external.h"
#include <array>
#include <vector>

// Immutable animation source, independent of the mutable game model registry.
// Field names retain the authored track semantics used by the pose evaluator;
// all arrays are owned, and no geometry or simulation scratch pointers survive.
struct ModelAnimationPart {
    int parent = -1, flags = 0;
    vector offset{}, norm{};
    float rps = 0;
    int num_key_pos = 0, num_key_angles = 0;
    int pos_track_min = 0, pos_track_max = 0, rot_track_min = 0, rot_track_max = 0;
    std::vector<vector> keyframe_pos, keyframe_axis;
    std::vector<matrix> keyframe_matrix;
    std::vector<ushort> tick_pos_remap, tick_ang_remap;
    std::vector<int> pos_start_time, rot_start_time;
};
struct ModelAnimation {
    int flags = 0, n_models = 0, num_key_pos = 0, num_key_angles = 0;
    bool new_style = false;
    std::vector<ModelAnimationPart> submodel;
};
ModelAnimation CompileModelAnimation(const poly_model&);
bool operator==(const ModelAnimation&,const ModelAnimation&);

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
ModelPose EvaluateModelPose(const ModelAnimation&,const float* normalized_time,float simulation_time);

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
ModelTransforms ComposeModelPose(const ModelAnimation&,const ModelPose&);
// Per-instance offsets expressed in model-root axes, independent of each
// parent's animation. Does not touch the shared asset's scratch transforms.
ModelPose OffsetModelPose(const poly_model&, const ModelPose&, const vector* root_offsets, int count);
vector TransformModelPoint(const ModelSubmodelTransform& transform, const vector& point);
vector TransformModelDirection(const ModelSubmodelTransform& transform, const vector& direction);

// Also used by the asset loader to compile authored rotation tracks.
void BuildModelAngleMatrix(matrix* result, angle rotation, const vector* axis);
