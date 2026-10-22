/*
* Descent 3
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "object.h"
#include "game.h"
#include "gameevent.h"
#include "descent.h"
#include "gameloop.h"
#include "spew.h"
#include <string.h>
#include "mem.h"
#include "player.h"
#include "fireball.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

void DoDamageEffect(int,void*);
void DoEDrainEffect(int,void*);

namespace {
std::array<uint64_t,MAX_EVENTS> EventGenerations{};
uint64_t NextEventGeneration = 0;
struct OverlayFrame {
    std::vector<FrameScreenBlend> blends;
    uint64_t lifetime = 0, frame = 0;
    bool valid = false, advancing = false;
    void reset() { blends.clear(); valid = false; }
} Overlays;
bool knownOverlayCallback(void (*callback)(int,void*)) {
    return callback == DrawAlphaEvent || callback == DoDamageEffect || callback == DoEDrainEffect;
}
}


int Num_events = 0;
game_event GameEvent[MAX_EVENTS];

// Inits the event system
void InitEvents()
{
	Overlays.reset();
	Overlays.blends.reserve(MAX_EVENTS);
	for (int i = 0; i < MAX_EVENTS; i++)
	{
		GameEvent[i].used = 0;
		GameEvent[i].data = NULL;
	}
}

// Clears the event list
void ClearAllEvents()
{
	Overlays.reset();
	for (int i = 0; i < MAX_EVENTS; i++)
	{
		if (GameEvent[i].used)
			FreeEvent(i);
	}

	SpewInit();
	Render_zoom = D3_DEFAULT_ZOOM;
}

// Returns an index of an allocated event
// or -1 if all events are spoken for
int AllocEvent()
{
	static int first_time = 1;

	if (first_time)
	{
		InitEvents();
		first_time = 0;
	}

	for (int i = 0; i < MAX_EVENTS; i++)
	{
		if (GameEvent[i].used == 0)
		{
			if (NextEventGeneration == UINT64_MAX) throw std::overflow_error("Game event generation exhausted");
			ASSERT(GameEvent[i].data == NULL);
			memset(&GameEvent[i], 0, sizeof(game_event));
			GameEvent[i].used = 1;
			EventGenerations[i] = ++NextEventGeneration;
			GameEvent[i].objhandle_detonator = OBJECT_HANDLE_NONE;
			Num_events++;
			return i;
		}

	}

	return -1;	// no events free!
}

// Frees an event for use by others
void FreeEvent(int index)
{
	if (GameEvent[index].used != 1)
	{
		Int3();	// Get Jason, trying to free an event that is not used
		return;
	}
	GameEvent[index].used = 0;
	if (GameEvent[index].data != NULL)
	{
		mem_free(GameEvent[index].data);
		GameEvent[index].data = NULL;
	}

	Num_events--;
}

// Processes all pending events, removing the ones that are expired
void ProcessNormalEvents()
{
	int count = 0;
	bool skip_event;

	for (int i = 0; i < MAX_EVENTS && count < Num_events; i++)
	{
		if (GameEvent[i].used)
		{
			count++;
			skip_event = false;

			if (GameEvent[i].objhandle_detonator != OBJECT_HANDLE_NONE) {
				//this object has a detonator, see if the object is still alive
				object* obj = ObjGet(GameEvent[i].objhandle_detonator);
				if (!obj || (obj->type == OBJ_GHOST) || (obj->type == OBJ_PLAYER && Players[obj->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD))) {
					//the object has died, kill the event
					FreeEvent(i);
					skip_event = true;
					mprintf((0, "Game Event %d cancelled\n", i));
				}
			}

			if (GameEvent[i].type != RENDER_EVENT && GameEvent[i].end_time <= Gametime && !skip_event)
			{
				HandleEvent(&GameEvent[i]);
				FreeEvent(i);
			}

		}
	}

	{
		PERF_MARKER_SCOPE("Spew.EmitAll");
		SpewEmitAll();
	}
}


// Goes through all events and returns the first index of the id matching the argument passed.
// If not match found, returns -1
int FindEventID(int id)
{
	for (int i = 0; i < MAX_EVENTS; i++)
	{
		if (GameEvent[i].used && GameEvent[i].id == id)
			return i;
	}

	return -1;
}

// Processes all pending events, removing the ones that are expired
void EmitFrameScreenBlend(float red,float green,float blue,float alpha)
{
	if (!Overlays.advancing) throw std::logic_error("Screen blend emitted outside its game frame update");
	if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) || !std::isfinite(alpha))
		throw std::invalid_argument("Nonfinite screen blend event");
	if (alpha <= 0) return;
	if (Overlays.blends.size() == MAX_EVENTS) throw std::length_error("Screen blend frame capacity exceeded");
	const auto unit = [](float value) { return std::max(0.f,std::min(1.f,value)); };
	Overlays.blends.push_back({unit(red),unit(green),unit(blue),unit(alpha)});
}

const std::vector<FrameScreenBlend>& AdvanceFrameOverlayEvents(uint64_t lifetime,uint64_t logical_frame) {
	if (Overlays.advancing) throw std::logic_error("Reentrant screen overlay advancement");
	if (Overlays.valid && Overlays.lifetime == lifetime) {
		if (Overlays.frame == logical_frame) return Overlays.blends;
		if (logical_frame < Overlays.frame) throw std::logic_error("Screen overlay frame reordered");
	}
	struct Due { int slot; uint64_t generation; };
	std::array<Due,MAX_EVENTS> due;
	size_t count = 0;
	// Validate the complete callback inventory before changing the queue. These
	// three producers emit data only; arbitrary historical draw callbacks cannot
	// enter this phase and become an implicit rendering compatibility backend.
	for (int i = 0; i < MAX_EVENTS; ++i) if (GameEvent[i].used &&
		GameEvent[i].type == RENDER_EVENT && GameEvent[i].end_time <= Gametime) {
		if (!knownOverlayCallback(GameEvent[i].subfunction))
			throw std::invalid_argument("Unsupported screen overlay event callback");
		due[count++] = {i,EventGenerations[i]};
	}
	Overlays.blends.clear(); Overlays.blends.reserve(MAX_EVENTS);
	struct Scope {
		Scope() { Overlays.advancing = true; }
		~Scope() { Overlays.advancing = false; }
	} scope;
	for (size_t i = 0; i < count; ++i) {
		const auto event = due[i];
		if (!GameEvent[event.slot].used || EventGenerations[event.slot] != event.generation) continue;
		HandleEvent(&GameEvent[event.slot]);
		if (GameEvent[event.slot].used && EventGenerations[event.slot] == event.generation) FreeEvent(event.slot);
	}
	Overlays.lifetime = lifetime; Overlays.frame = logical_frame; Overlays.valid = true;
	return Overlays.blends;
}


// Adds new event to the event list.  The event will trigger at Gametime+length.
// data and size represent event specific data - if there is no event specific data
// then set data to NULL and size to 0
// Subfunction is the function that is called when the event is up
int CreateNewEvent(int type, int id, float length, void* data, int size, void(*subfunction)(int eventnum, void* data), int objhandle_detonate)
{
	if (type == RENDER_EVENT && subfunction == DrawAlphaEvent && (!data || size < int(sizeof(float)*4)))
		throw std::invalid_argument("Screen blend event has no RGBA payload");
	if (type == RENDER_EVENT && subfunction == DrawAlphaEvent) {
		const auto* color = static_cast<const float*>(data);
		for (int i = 0; i < 4; ++i) if (!std::isfinite(color[i]))
			throw std::invalid_argument("Screen blend event has nonfinite RGBA payload");
	}
	int num;
	game_event* ge;

	num = AllocEvent();
	if (num == -1)
	{
		mprintf((0, "WARNING: Event (type %d, id %d) did not get allocated\n", type, id));
		return -1;	// couldn't do this event, no slots free
	}

	ge = &GameEvent[num];

	ge->type = type;
	ge->id = id;
	ge->start_time = Gametime;
	ge->end_time = Gametime + length;
	ge->frame_born = FrameCount;
	ge->objhandle_detonator = objhandle_detonate;

	if (size)
	{
		ge->data = (void*)mem_malloc(size);
		ASSERT(ge->data);
		memcpy(ge->data, data, size);
	}

	ge->subfunction = subfunction;

	return num;
}

// Does a specific action according to an event
void HandleEvent(game_event* ge)
{
	switch (ge->type)
	{
	case RENDER_EVENT:
		ge->subfunction(ge - GameEvent, ge->data);
		break;
	default:
		ge->subfunction(ge - GameEvent, ge->data);
		break;
	}
}
