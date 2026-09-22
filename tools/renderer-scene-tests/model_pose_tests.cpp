// Descent 3MASTERED - GPL-3.0-or-later
#include "../../model/ModelPose.h"
#include "vecmat.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

// vector.cpp contains unrelated random-vector helpers. If pose evaluation ever
// reaches one, fail instead of silently consuming gameplay RNG.
int ps_rand() { throw std::runtime_error("Pose evaluation consumed random state"); }
static bool initializing_math = false;
void ps_srand(unsigned int) {
    if (!initializing_math) throw std::runtime_error("Pose evaluation reseeded random state");
}
bool Debug_break = true;
void (*DebugBreak_callback_stop)() = nullptr;
void (*DebugBreak_callback_resume)() = nullptr;
void debug_break() { throw std::runtime_error("Math debug break"); }
void AssertionFailed(char*, char*, int) { throw std::runtime_error("Math assertion failed"); }
void Int3MessageBox(char*, int) { throw std::runtime_error("Math breakpoint"); }

namespace {
#define ASSERT(x) assert(x)
#include "reference_model_pose.inc"
#undef ASSERT
int checks;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool same(const ModelSubmodelPose& pose, const bsp_info& sm) {
    return pose.angles.p == sm.angs.p && pose.angles.h == sm.angs.h && pose.angles.b == sm.angs.b &&
        std::fabs(pose.translation.x - sm.mod_pos.x) < 1.e-6f &&
        std::fabs(pose.translation.y - sm.mod_pos.y) < 1.e-6f &&
        std::fabs(pose.translation.z - sm.mod_pos.z) < 1.e-6f;
}
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid pose input accepted");
}
}
int runModelPoseTests() {
    initializing_math = true;
    InitMathTables();
    initializing_math = false;
    checks = 0;
    bsp_info source[3]{};
    matrix rotations[3];
    vm_MakeIdentity(&rotations[0]);
    vm_AnglesToMatrix(&rotations[1], 3000, 8000, 11000);
    vm_AnglesToMatrix(&rotations[2], 5000, 14000, 23000);
    vector positions[] = {{0, 0, 0}, {3, 4, 5}, {9, 8, 7}};
    vector axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int starts[] = {2, 5, 9};
    ushort remap[] = {0, 0, 0, 1, 1, 1, 1, 2};
    for (auto& sm : source) {
        sm.num_key_angles = sm.num_key_pos = 3;
        sm.keyframe_matrix = rotations; sm.keyframe_pos = positions; sm.keyframe_axis = axes;
        sm.rot_track_min = sm.pos_track_min = 2;
        sm.rot_track_max = sm.pos_track_max = 9;
        sm.rot_start_time = sm.pos_start_time = starts;
        sm.tick_ang_remap = sm.tick_pos_remap = remap;
        sm.rps = 0.3f; sm.norm = {0, 0, 1};
    }
    source[1].flags = SOF_ROTATE;
    source[2].flags = SOF_TURRET;
    poly_model model{};
    model.n_models = 3; model.num_key_angles = model.num_key_pos = 3; model.submodel = source;
    for (int style = 0; style < 2; ++style) for (int timed = 0; timed < 2; ++timed) {
        model.new_style = style; model.flags = timed ? PMF_TIMED : 0;
        for (int step = 0; step <= 100; ++step) {
            float times[] = {step / 100.f, (100 - step) / 100.f, step / 100.f};
            ReferenceClock = step * 0.071f;
            const auto before = std::array<bsp_info, 3>{source[0], source[1], source[2]};
            const auto pose = EvaluateModelPose(model, times, ReferenceClock);
            check(std::memcmp(source, before.data(), sizeof(source)) == 0, "Evaluator mutated shared model");
            ReferencePose(&model, times, 0xffffffffu);
            for (int i = 0; i < 3; ++i) check(same(pose.submodels[i], source[i]), "Animation differs from original evaluator");
            // Interleaving another instance cannot change an already-owned pose.
            const auto saved = pose;
            times[0] = 1 - times[0];
            (void)EvaluateModelPose(model, times, ReferenceClock + 0.5f);
            check(std::memcmp(&pose, &saved, sizeof(pose)) == 0, "Interleaved instance changed held pose");
        }
        // Null input with zeroed old scratch avoids the known uninitialized
        // bank-angle bug in the old non-timed rest-pose branch.
        for (auto& sm : source) { sm.angs = {}; sm.mod_pos = {}; }
        const auto rest = EvaluateModelPose(model, nullptr, ReferenceClock);
        ReferencePose(&model, nullptr, 0xffffffffu);
        for (int i = 0; i < 3; ++i) check(same(rest.submodels[i], source[i]), "Null-input pose semantics changed");
    }
    model.flags = 0;
    model.num_key_angles = model.num_key_pos = 0;
    source[0].angs = {111, 222, 333};
    const auto rest = EvaluateModelPose(model, nullptr, 0);
    check(rest.submodels[0].angles.b == 0, "Rest pose leaked previous instance bank angle");
    model.num_key_angles = model.num_key_pos = 1;
    float times[] = {0.5f, 0.5f, 0.5f};
    const auto single = EvaluateModelPose(model, times, 0);
    check(single.submodels[0].translation.x == 0, "Single-key track failed");
    rejects([&] { EvaluateModelPose(model, times, std::numeric_limits<float>::infinity()); });
    times[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { EvaluateModelPose(model, times, 0); });
    times[0] = 1.1f;
    rejects([&] { EvaluateModelPose(model, times, 0); });
    model.flags = PMF_NOT_RESIDENT;
    rejects([&] { EvaluateModelPose(model, nullptr, 0); });
    model.flags = PMF_TIMED; times[0] = 0.5f;
    const auto retained = EvaluateModelPose(model, times, 0);
    // Compare a non-topologically sorted hierarchy with the original
    // leaf-to-root attachment query, not another matrix composer.
    source[0].parent = 2; source[2].parent = 1; source[1].parent = -1;
    source[0].offset = {3, 7, -2}; source[1].offset = {-5, 1, 4}; source[2].offset = {1, 6, 9};
    const auto transforms = ComposeModelPose(model, retained);
    for (int leaf = 0; leaf < 3; ++leaf) {
        vector point{3, -4, 9}, direction{0.2f, 0.5f, -0.3f};
        vector expected_point = point, expected_direction = direction;
        for (int node = leaf; node != -1; node = source[node].parent) {
            const auto& local = retained.submodels[node];
            matrix rotation;
            vm_AnglesToMatrix(&rotation, local.angles.p, local.angles.h, local.angles.b);
            vm_TransposeMatrix(&rotation);
            expected_point = expected_point * rotation + source[node].offset + local.translation;
            expected_direction = expected_direction * rotation;
        }
        const auto actual = TransformModelPoint(transforms.submodels[leaf], point);
        const auto actual_direction = TransformModelDirection(transforms.submodels[leaf], direction);
        check(vm_VectorDistance(&actual, &expected_point) < 1.e-4f, "Hierarchy composition changed attachment position");
        check(vm_VectorDistance(&actual_direction, &expected_direction) < 1.e-5f, "Hierarchy composition changed direction");
    }
    const vector offsets[3] = {{2,-3,4},{-1,2,5},{4,1,-2}};
    const auto adjusted = OffsetModelPose(model,retained,offsets,3);
    const auto adjusted_transforms = ComposeModelPose(model,adjusted);
    for (int leaf = 0; leaf < 3; ++leaf) {
        vector expected = transforms.submodels[leaf].translation;
        for (int node = leaf; node != -1; node = source[node].parent) expected += offsets[node];
        const auto actual = adjusted_transforms.submodels[leaf].translation;
        check(vm_VectorDistance(&expected,&actual) < 1.e-4f,"Root-space instance offset rotated with its parent");
        check(retained.submodels[leaf].translation.x != adjusted.submodels[leaf].translation.x ||
            retained.submodels[leaf].translation.y != adjusted.submodels[leaf].translation.y,
            "Instance offset did not modify the copied pose");
    }
    rejects([&] { OffsetModelPose(model,retained,nullptr,3); });
    rejects([&] { OffsetModelPose(model,retained,offsets,4); });
    source[1].parent = 0;
    rejects([&] { ComposeModelPose(model, retained); });
    source[1].parent = 3;
    rejects([&] { ComposeModelPose(model, retained); });
    source[1].parent = -1;
    model.submodel = nullptr;
    check(retained.count == 3 && std::isfinite(retained.submodels[0].translation.x), "Pose depended on asset lifetime");
    rejects([&] { EvaluateModelPose(model, times, 0); });
    return checks;
}
