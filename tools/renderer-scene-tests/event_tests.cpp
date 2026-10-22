// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/gameevent.h"
#include "../../Descent3/player.h"
#include "../../Descent3/gameloop.h"
#include "../../Descent3/fireball.h"
#include <cstdlib>
#include <stdexcept>
#include <limits>

// The actual event allocator/scheduler is linked. The callback boundary uses
// tiny producers to prove queue cadence/replay without drawing or game RNG.
// Inventory is not exercised by the queue fixture; satisfy player lifetime
// construction without linking unrelated inventory/gameplay implementations.
Inventory::Inventory() {}
Inventory::~Inventory() {}
player Players[MAX_PLAYERS]{};
int Player_num = 0;
float Render_zoom = 1;
bool Perf_markers_enabled = false;
void PerfMarkersBegin(const char*) {}
void PerfMarkersEnd(const char*) {}
void SpewInit() {}
void SpewEmitAll() {}
void* mem_malloc_sub(int size,const char*,int) {
    if (auto* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}
void mem_free_sub(void* memory) { std::free(memory); }
static int DamageTicks = 0;
static int ReplaceSlot = -1;
static const float Blend[4] = {1,.25f,.5f,.75f};
void DrawAlphaEvent(int,void* data) {
    const auto* color = static_cast<const float*>(data);
    EmitFrameScreenBlend(color[0],color[1],color[2],color[3]);
}
void DoDamageEffect(int,void*) {
    ++DamageTicks; EmitFrameScreenBlend(1,0,0,.25f);
    if (DamageTicks < 3) CreateNewEvent(RENDER_EVENT,DAMAGE_EFFECT,0,nullptr,0,DoDamageEffect);
}
void DoEDrainEffect(int,void*) {
    if (ReplaceSlot >= 0) {
        FreeEvent(ReplaceSlot);
        CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,const_cast<float*>(Blend),sizeof(Blend),DrawAlphaEvent);
        ReplaceSlot = -1;
    }
    EmitFrameScreenBlend(0,0,1,.5f);
}

int runFrameEventTests() {
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Game frame overlay events"); };
    const auto rejects = [&](const auto& operation) {
        bool failed = false;
        try { operation(); } catch (const std::logic_error&) { failed = true; }
        check(failed);
    };
    ClearAllEvents(); FrameCount = 4; Gametime = 10;
    rejects([&] { EmitFrameScreenBlend(1,0,0,.5f); });
    rejects([&] { CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,nullptr,0,DrawAlphaEvent); });
    CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,const_cast<float*>(Blend),sizeof(Blend),DrawAlphaEvent);
    CreateNewEvent(RENDER_EVENT,DAMAGE_EFFECT,0,nullptr,0,DoDamageEffect);
    const auto& first = AdvanceFrameOverlayEvents(12,4);
    check(first.size() == 2 && first[0].alpha == .75f && first[1].red == 1);
    check(DamageTicks == 1 && Num_events == 1);
    check(&AdvanceFrameOverlayEvents(12,4) == &first && DamageTicks == 1 && Num_events == 1);
    check(AdvanceFrameOverlayEvents(12,5).size() == 1 && DamageTicks == 2 && Num_events == 1);
    check(AdvanceFrameOverlayEvents(12,6).size() == 1 && DamageTicks == 3 && Num_events == 0);
    check(AdvanceFrameOverlayEvents(12,7).empty());
    rejects([&] { AdvanceFrameOverlayEvents(12,6); });
    check(AdvanceFrameOverlayEvents(13,0).empty()); // new scene may restart logical frame numbering

    // A callback replaces a slot already in this frame's due list. Its new
    // occupant must wait; a slot number is not an allocation identity.
    CreateNewEvent(RENDER_EVENT,EDRAIN_EFFECT,0,nullptr,0,DoEDrainEffect);
    ReplaceSlot = CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,const_cast<float*>(Blend),sizeof(Blend),DrawAlphaEvent);
    check(AdvanceFrameOverlayEvents(13,1).size() == 1 && Num_events == 1);
    check(AdvanceFrameOverlayEvents(13,2).size() == 1 && Num_events == 0);
    const auto invalid = CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,nullptr,0,nullptr);
    CreateNewEvent(RENDER_EVENT,SCREEN_BLEND,0,const_cast<float*>(Blend),sizeof(Blend),DrawAlphaEvent);
    rejects([&] { AdvanceFrameOverlayEvents(13,3); });
    check(Num_events == 2); // complete callback inventory preflight, no partial drain
    FreeEvent(invalid);
    check(AdvanceFrameOverlayEvents(13,3).size() == 1 && Num_events == 0);
    ClearAllEvents();
    return checks;
}
