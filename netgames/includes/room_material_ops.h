// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "room_external.h"
#include "multi_world_state.h"
#include "gametexture.h"

// Shared authored goal-room paint policy. Return renderer dirtiness separately
// from network replication flags: repainting the same value still marks the
// network state, but does not invalidate immutable appearance publications.
inline bool PaintGoalRoomMaterials(room& source,const texture* textures,int target) {
    if (!textures || target < 0 || target >= MAX_TEXTURES) return false;
    source.room_change_flags |= RCF_TEXTURE;
    if (!source.faces) return false;
    bool goal_faces = false, changed = false;
    for (int i = 0; i < source.num_faces; ++i)
        if (source.faces[i].flags & FF_GOALFACE) { goal_faces = true; break; }
    for (int i = 0; i < source.num_faces; ++i) {
        auto& face = source.faces[i];
        if (face.tmap < 0 || face.tmap >= MAX_TEXTURES) continue;
        const auto& material = textures[face.tmap];
        if ((material.flags & TF_ALPHA) || material.r >= .01f || material.g >= .01f || material.b >= .01f ||
            (goal_faces && !(face.flags & FF_GOALFACE))) continue;
        changed = changed || face.tmap != target;
        face.tmap = static_cast<short>(target); face.flags |= FF_TEXTURE_CHANGED;
    }
    return changed;
}
