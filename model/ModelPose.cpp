/* Descent 3 - Copyright (C) 2024 Parallax Software
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Animation math migrated from polymodel.cpp. */
#include "ModelPose.h"
#include "vecmat.h"
#include <cmath>
#include <stdexcept>

void BuildModelAngleMatrix(matrix* mat, angle ang, const vector* axis) {
    const float x = axis->x, y = axis->y, z = axis->z;
    const float s = FixSin(ang), c = FixCos(ang), t = 1.f - c;
    mat->rvec = {t*x*x+c, t*x*y+s*z, t*x*z-s*y};
    mat->uvec = {t*x*y-s*z, t*y*y+c, t*y*z+s*x};
    mat->fvec = {t*x*z+s*y, t*y*z-s*x, t*z*z+c};
}

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::invalid_argument(message);
}
struct Sample { int key; float fraction; };
Sample uniformSample(int count, float time) {
    if (count <= 1 || time == 1.f) return {count - 1, 0};
    // Preserve the original arithmetic, including its rounding at key edges.
    const float duration = 1.f / (count - 1);
    const int key = static_cast<int>(time / duration);
    return {key, (time - key * duration) / duration};
}
Sample timedSample(int count, float time, int first, int last,
    const ushort* remap, const int* starts) {
    if (time == 1.f) return {count - 1, 0};
    require(last >= first && remap && starts, "Invalid model animation track");
    const float tick = time * (last - first) + first;
    const int index = static_cast<int>(tick) - first;
    require(index >= 0 && index <= last - first, "Invalid animation tick");
    const int key = remap[index];
    require(key >= 0 && key < count, "Invalid animation key remap");
    if (key == count - 1) return {key, 0};
    const int duration = starts[key + 1] - starts[key];
    require(duration > 0, "Non-increasing animation keys");
    return {key, (tick - starts[key]) / duration};
}
angvec angles(matrix value) {
    angvec result{};
    vm_ExtractAnglesFromMatrix(&result, &value);
    return result;
}
float cycle(float time) {
    // No integer conversion overflow for long-running sessions; negative times
    // are well-defined too. Positive ordinary game times retain old results.
    return time - std::floor(time);
}
}

ModelPose EvaluateModelPose(const poly_model& model, const float* normalized_time,
    float simulation_time) {
    require(!(model.flags & PMF_NOT_RESIDENT), "Cannot evaluate an unloaded model");
    require(model.n_models >= 0 && model.n_models <= MAX_SUBOBJECTS &&
        (model.n_models == 0 || model.submodel), "Invalid model submodel count");
    require(std::isfinite(simulation_time), "Invalid model animation clock");
    ModelPose pose;
    pose.count = model.n_models;
    const bool timed = (model.flags & PMF_TIMED) != 0;
    for (int i = 0; i < pose.count; ++i) {
        const auto& sm = model.submodel[i];
        auto& out = pose.submodels[i];
        const float time = normalized_time ? normalized_time[i] : 0.f;
        require(std::isfinite(time) && time >= 0.f && time <= 1.f,
            "Model animation time outside normalized range");
        // Timed models without animation input have always used the rest pose.
        if (timed && !normalized_time) continue;
        const int positions = timed ? sm.num_key_pos : model.num_key_pos;
        if (normalized_time && positions > (timed ? 1 : 0)) {
            require(sm.keyframe_pos != nullptr, "Missing model position keys");
            const auto sample = timed
                ? timedSample(positions, time, sm.pos_track_min, sm.pos_track_max, sm.tick_pos_remap, sm.pos_start_time)
                : uniformSample(positions, time);
            if (sample.key == positions - 1) out.translation = sm.keyframe_pos[sample.key];
            else if (!timed && sample.key == 0)
                out.translation = sample.fraction * sm.keyframe_pos[1]; // legacy implicit origin
            else out.translation = sm.keyframe_pos[sample.key] +
                (sm.keyframe_pos[sample.key + 1] - sm.keyframe_pos[sample.key]) * sample.fraction;
        }
        const int rotations = timed ? sm.num_key_angles : model.num_key_angles;
        const bool keyed = normalized_time && rotations > 0;
        if (sm.flags & (SOF_ROTATE | SOF_TURRET)) {
            require(std::isfinite(sm.rps), "Invalid model rotation speed");
            const float turn = (sm.flags & SOF_ROTATE) ? cycle(simulation_time * sm.rps)
                : (timed || keyed) ? time : cycle(simulation_time);
            require(std::isfinite(turn), "Model rotation clock overflow");
            const int axis_index = (sm.flags & SOF_ROTATE) || timed || keyed ? 1 : 0;
            require(!model.new_style || sm.keyframe_axis, "Missing model rotation axis");
            const vector& axis = model.new_style ? sm.keyframe_axis[axis_index] : sm.norm;
            matrix rotation;
            BuildModelAngleMatrix(&rotation, static_cast<angle>(turn * 65535), &axis);
            out.angles = angles(rotation);
        } else if (keyed && (!timed || rotations > 1)) {
            require(sm.keyframe_matrix != nullptr, "Missing model rotation keys");
            const auto sample = timed
                ? timedSample(rotations, time, sm.rot_track_min, sm.rot_track_max, sm.tick_ang_remap, sm.rot_start_time)
                : uniformSample(rotations, time);
            matrix rotation = sm.keyframe_matrix[sample.key];
            if (sample.key < rotations - 1) rotation = rotation +
                (sm.keyframe_matrix[sample.key + 1] - rotation) * sample.fraction;
            out.angles = angles(rotation);
        }
    }
    return pose;
}

ModelPose OffsetModelPose(const poly_model& model, const ModelPose& pose, const vector* offsets, int count) {
    require(count >= 0 && count <= pose.count && (!count || offsets), "Invalid model pose offsets");
    const auto transforms = ComposeModelPose(model,pose);
    auto result = pose;
    for (int i = 0; i < count; ++i) {
        auto offset = offsets[i];
        require(std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z), "Invalid model root offset");
        const int parent = model.submodel[i].parent;
        if (parent >= 0) offset = offset * transforms.submodels[parent].orientation;
        result.submodels[i].translation += offset;
    }
    return result;
}

vector TransformModelDirection(const ModelSubmodelTransform& transform, const vector& direction) {
    return transform.orientation.rvec * direction.x + transform.orientation.uvec * direction.y +
        transform.orientation.fvec * direction.z;
}
vector TransformModelPoint(const ModelSubmodelTransform& transform, const vector& point) {
    return TransformModelDirection(transform, point) + transform.translation;
}

ModelTransforms ComposeModelPose(const poly_model& model, const ModelPose& pose) {
    require(pose.count == model.n_models && pose.count >= 0 && pose.count <= MAX_SUBOBJECTS &&
        (pose.count == 0 || model.submodel), "Pose does not match model hierarchy");
    ModelTransforms result;
    result.count = pose.count;
    std::array<unsigned char, MAX_SUBOBJECTS> visited{};
    // Parents need not precede children in the file. Validate cycles even for
    // submodels excluded from a particular view or draw mask.
    const auto compose = [&](auto&& self, int index) -> void {
        require(index >= 0 && index < pose.count, "Invalid model parent index");
        if (visited[index] == 2) return;
        require(visited[index] == 0, "Cyclic model hierarchy");
        visited[index] = 1;
        const auto& local = pose.submodels[index];
        auto& out = result.submodels[index];
        vm_AnglesToMatrix(&out.orientation, local.angles.p, local.angles.h, local.angles.b);
        out.translation = model.submodel[index].offset + local.translation;
        const int parent = model.submodel[index].parent;
        if (parent != -1) {
            self(self, parent);
            const auto& ancestor = result.submodels[parent];
            out.translation = TransformModelPoint(ancestor, out.translation);
            out.orientation = {TransformModelDirection(ancestor, out.orientation.rvec),
                TransformModelDirection(ancestor, out.orientation.uvec),
                TransformModelDirection(ancestor, out.orientation.fvec)};
        }
        visited[index] = 2;
    };
    for (int i = 0; i < pose.count; ++i) compose(compose, i);
    return result;
}
