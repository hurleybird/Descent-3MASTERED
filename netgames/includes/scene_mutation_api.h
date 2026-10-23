// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>

constexpr uint32_t GameSceneMutationVersion = 1;
constexpr int DOF_TRACKED_ROOM_WRITES = 0x0004;
constexpr int DOF_ROOM_WRITE_BIND_REQUIRED = 0x0008;
inline bool GameModuleTracksRoomWrites(int flags,bool has_binding,bool binding_accepted) {
    return (flags & DOF_TRACKED_ROOM_WRITES) &&
        (has_binding ? binding_accepted : !(flags & DOF_ROOM_WRITE_BIND_REQUIRED));
}
enum GameSceneMutation : uint32_t { GameSceneAppearanceChanged = 1, GameSceneGeometryChanged = 2 };
// Optional, explicitly negotiated extension. Neither the legacy game_api
// table nor public room/HUD structs change size or meaning. Writes and their
// notification are synchronous game-thread work, never renderer callbacks.
struct GameSceneMutationAPI {
    uint32_t size, version;
    uint64_t owner;
    int (*notify_room)(uint64_t owner,int32_t room,uint32_t changes);
};
inline bool ValidGameSceneMutationAPI(const GameSceneMutationAPI* api) {
    return api && api->size >= sizeof(GameSceneMutationAPI) && api->version == GameSceneMutationVersion &&
        api->owner != 0 && api->notify_room;
}
