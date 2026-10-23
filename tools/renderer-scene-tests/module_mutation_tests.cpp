// Descent 3MASTERED - GPL-3.0-or-later
#include "../../netgames/includes/scene_mutation_api.h"
#include "../../netgames/includes/room_material_ops.h"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

int runModuleMutationTests() {
    static_assert(std::is_standard_layout<GameSceneMutationAPI>::value &&
        std::is_trivially_copyable<GameSceneMutationAPI>::value,"Mutation extension must retain a plain ABI");
    static_assert(offsetof(GameSceneMutationAPI,owner) == 8 && offsetof(GameSceneMutationAPI,notify_room) == 16,
        "Mutation extension field ABI changed");
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Module room mutation contract"); };
    check(!GameModuleTracksRoomWrites(0,false,false));
    check(GameModuleTracksRoomWrites(DOF_TRACKED_ROOM_WRITES,false,false));
    const int required = DOF_TRACKED_ROOM_WRITES|DOF_ROOM_WRITE_BIND_REQUIRED;
    check(!GameModuleTracksRoomWrites(required,false,false));
    check(!GameModuleTracksRoomWrites(required,true,false));
    check(GameModuleTracksRoomWrites(required,true,true));
    check(!GameModuleTracksRoomWrites(0,true,true)); // an unknown module cannot opt in accidentally
    GameSceneMutationAPI api{sizeof(GameSceneMutationAPI),GameSceneMutationVersion,100,
        +[](uint64_t owner,int32_t room,uint32_t changes) { return int(owner == 100 && room == 7 && changes == GameSceneAppearanceChanged); }};
    check(ValidGameSceneMutationAPI(&api) && api.notify_room(api.owner,7,GameSceneAppearanceChanged) == 1);
    check(!ValidGameSceneMutationAPI(nullptr));
    auto bad = api; bad.size = 8; check(!ValidGameSceneMutationAPI(&bad));
    bad = api; ++bad.version; check(!ValidGameSceneMutationAPI(&bad));
    bad = api; bad.owner = 0; check(!ValidGameSceneMutationAPI(&bad));
    bad = api; bad.notify_room = nullptr; check(!ValidGameSceneMutationAPI(&bad));

    const std::array<texture,4> saved{{GameTextures[0],GameTextures[1],GameTextures[2],GameTextures[3]}};
    for (int i = 0; i < 4; ++i) GameTextures[i] = {};
    GameTextures[3].flags = TF_ALPHA;
    face faces[3]{}; faces[1].flags = faces[2].flags = FF_GOALFACE; faces[2].tmap = 3;
    room source{}; source.faces = faces; source.num_faces = 3;
    check(PaintGoalRoomMaterials(source,GameTextures,1));
    check(faces[0].tmap == 0 && faces[1].tmap == 1 && faces[2].tmap == 3);
    check((source.room_change_flags&RCF_TEXTURE) && !(faces[0].flags&FF_TEXTURE_CHANGED) && (faces[1].flags&FF_TEXTURE_CHANGED));
    source.room_change_flags = 0; faces[1].flags &= ~FF_TEXTURE_CHANGED;
    check(!PaintGoalRoomMaterials(source,GameTextures,1)); // no renderer invalidation for unchanged values
    check((source.room_change_flags&RCF_TEXTURE) && (faces[1].flags&FF_TEXTURE_CHANGED)); // replication intent remains
    for (auto& face : faces) face.flags = 0;
    check(PaintGoalRoomMaterials(source,GameTextures,2) && faces[0].tmap == 2 && faces[1].tmap == 2 && faces[2].tmap == 3);
    source.room_change_flags = 0;
    check(!PaintGoalRoomMaterials(source,GameTextures,-1) && !source.room_change_flags);
    check(!PaintGoalRoomMaterials(source,GameTextures,MAX_TEXTURES));
    GameTextures[2].r = .02f;
    check(!PaintGoalRoomMaterials(source,GameTextures,1)); // authored emissive faces are not goal paint
    faces[0].tmap = -1;
    check(!PaintGoalRoomMaterials(source,GameTextures,1)); // malformed old material does not index outside the table
    for (int i = 0; i < 4; ++i) GameTextures[i] = saved[i];
    return checks;
}
