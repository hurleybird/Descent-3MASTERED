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

#include "TelComAutoMap.h"
#include "SceneAutomap.h"
#include "SceneExtraction.h"
#include "SceneCanvas.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "CFILE.H"
#include "pserror.h"
#include "ddio.h"
#include "bitmap.h"
#include "room.h"

#include "TelCom.h"
#include "TelComEffects.h"
#include "renderer.h"
#include "game.h"
#include "mem.h"
#include "stringtable.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "controls.h"
#include "terrain.h"
#include "config.h"
#include "gameloop.h"
#include "multi.h"
#include "fireball.h"
#include "gamesequence.h"
#include "Mission.h"
#include "polymodel.h"
#include "player.h"
#include "marker.h"
#include "hlsoundlib.h"
#include "args.h"

// Variables needed for automap
vector AM_view_pos = { 0,0,0 };
matrix AM_view_orient;
tTelComInfo* AM_tcs;
int AM_ship_model_handle = -1;

ubyte AutomapVisMap[MAX_ROOMS];

float AM_heading;
float AM_pitch;

int AM_terrain = 0, AM_realign = 0, AM_center_on_player = 0, AM_current_marker;

void TCAMCallback(void);


// clears out the list of visible rooms that we've seen
void AutomapClearVisMap()
{
	memset(AutomapVisMap, 0, MAX_ROOMS);
}

// Returns the camera position to the player
void TCAMCenterOnPlayer()
{
	vm_MakeZero(&AM_view_pos);
	vm_MakeIdentity(&AM_view_orient);

	matrix newmat;
	angvec heading;
	AM_view_pos = Viewer_object->pos - (Viewer_object->orient.fvec * 10);
	vm_ExtractAnglesFromMatrix(&heading, &Player_object->orient);

	vm_AnglesToMatrix(&newmat, 0, heading.h, 0);
	AM_view_orient = newmat;

	AM_heading = heading.h;
	AM_pitch = heading.p;
}

// This is the function called by TelCom
//  return true if TelCom should exit to TelCom Main Menu
//  return false if you should exit out of TelCom completly
//  mmonitor = main monitor class
//  lmonitor = local monitor class (the monitor at the top of the screen)
bool TelComAutoMap(tTelComInfo* tcs)
{
	bool done = false;


	AM_tcs = tcs;
	AM_current_marker = -1;
	TelComEnableSystemKey(TCSYS_MAXKEYS, false);	//disable all telcom system keys
	TelComEnableSystemKey(TCSYS_F1, true);		//enable F1 and enter, those are all we may need in automap
	TelComEnableSystemKey(TCSYS_F2, true);		//enable F2 and enter, those are all we may need in automap
	TelComEnableSystemKey(TCSYS_F3, true);		//enable F3 and enter, those are all we may need in automap
	TelComEnableSystemKey(TCSYS_1, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_2, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_3, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_4, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_5, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_6, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_7, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_8, true);		//enable marker key
	TelComEnableSystemKey(TCSYS_ENTER, true);	//processing other keys is a waste of cpu
	TelComEnableSystemKey(TCSYS_PRINTSCRN, true);


	// Load ship model
	AM_ship_model_handle = LoadPolyModel("automapship.oof", 0);

	// Setup intial pos/orient
	TCAMCenterOnPlayer();
	if (OBJECT_OUTSIDE(Viewer_object))
		AM_terrain = 1;

	TelcomStartScreen(0);

	//setup general background, plain color
	TCBKGDESC backg;
	backg.color = BACKGROUND_COLOR;
	backg.caps = TCBGD_COLOR;
	backg.type = TC_BACK_STATIC;

	/*//load up the "fake automap" <----Start Remove---->
	TCBMPDESC bmpdesc;
	bmpdesc.type = TC_BMP_STATIC;
	bmpdesc.flags = 0;
	bmpdesc.caps=TCBD_XY;
	bmpdesc.x = 0; bmpdesc.y = 0;
	strcpy(bmpdesc.filename,"TCMap.ogf");
	//							 <----End Remove  ---->*/

	//setup text for the top monitor "Automap"
	TCTEXTDESC textdesc;
	textdesc.type = TC_TEXT_STATIC;
	textdesc.font = BBRIEF_FONT_INDEX;
	textdesc.caps = TCTD_TEXTBOX | TCTD_COLOR | TCTD_FONT;
	textdesc.textbox.left = 65;
	textdesc.textbox.right = 380;
	textdesc.textbox.top = 2;
	textdesc.textbox.bottom = 50;
	textdesc.color = GR_RGB(255, 255, 255);

	//Setup Main Monitor
	//CreateBitmapEffect(&bmpdesc,MONITOR_MAIN,0);
	CreateBackgroundEffect(&backg, MONITOR_MAIN, 0);//<---- Replace above line with this line to get a background

	//Setup Location Monitor (small window at top)
	CreateBackgroundEffect(&backg, MONITOR_TOP, 0);
	CreateTextEffect(&textdesc, TXT_TCAUTOMAP, MONITOR_TOP, 0);
	TelcomEndScreen();
	TelcomRenderSetScreen(0);
	TelcomRenderSetCallback(TCAMCallback);

	ResumeControls();										// need to do because we're most likely coming from game.

	while (!done)
	{
		ct_packet automap_key;
		ct_packet automap_button;

		Sound_system.BeginSoundFrame(Telcom_called_from_game);

		if (Telcom_system.state != TCS_POWERON || tcs->current_status != TS_MAP)
		{
			//we're done with the loop
			done = true;
		}

		//Process all waiting events for the TelCom	(we may only want to handle this once a frame!)
		TelComHandleAllEvents(tcs);

		TelcomRenderScreen();
		Descent->defer();
		if (IsAltF4QuitConfirmationPending())
		{
			AM_tcs->state = TCS_POWEROFF;
			done = true;
		}
		else if (KEY_STATE(KEY_ESC))
			AM_tcs->state = TCS_POWEROFF;

		//		if(KEY_STATE(KEY_TAB))
		//			AM_tcs->state = TCS_POWEROFF;

			//	read controls
		Controller->poll();
		Controller->get_packet(ctfAUTOMAP_KEY, &automap_key);
		Controller->get_packet(ctfAUTOMAP_BUTTON, &automap_button);
		if (automap_key.value > 0.0f || automap_button.value > 0.0f)
			AM_tcs->state = TCS_POWEROFF;

		Sound_system.EndSoundFrame();
	}

	SuspendControls();

	DestroyAllScreens();
	TelcomRenderSetScreen(DUMMY_SCREEN);
	TelcomRenderSetCallback(NULL);


	if (AM_ship_model_handle != -1)
		FreePolyModel(AM_ship_model_handle);

	TelComEnableSystemKey(TCSYS_MAXKEYS, true);	//re-enable all telcom keys
	TelComEnableSystemKey(TCSYS_F1, false);		//disable the T key, not needed outside of automap
	return true;
}

#define AM_TRANSLATION_SCALAR	200.0f
#define AM_ROTATIONAL_SCALAR		90

// Reads the controls for the automap, and adjusts viewpoint accordingly
extern float last_frametime;
void TCAMReadControls()
{
	Frametime = last_frametime;
	int save_mode = Game_interface_mode;
	game_controls controls;

	controls.toggle_slide = 0;
	controls.toggle_bank = 0;
	Game_interface_mode = GAME_INTERFACE;

	PollControls();
	DoMovement(&controls);

	// Do translational movement
	AM_view_pos += (controls.forward_thrust * last_frametime * AM_TRANSLATION_SCALAR) * AM_view_orient.fvec;
	AM_view_pos += (controls.vertical_thrust * last_frametime * AM_TRANSLATION_SCALAR) * AM_view_orient.uvec;
	AM_view_pos += (controls.sideways_thrust * last_frametime * AM_TRANSLATION_SCALAR) * AM_view_orient.rvec;

	// Do rotational movement

	matrix newmat;
	float norm = ((65536 / 360) * AM_ROTATIONAL_SCALAR) * last_frametime;

	AM_pitch += (norm * controls.pitch_thrust);
	AM_heading += (norm * controls.heading_thrust);

	if (AM_pitch < 0)
		AM_pitch += 65536;
	if (AM_pitch >= 65536)
		AM_pitch -= 65536;

	if (AM_heading < 0)
		AM_heading += 65536;
	if (AM_heading >= 65536)
		AM_heading -= 65536;

	vm_AnglesToMatrix(&newmat, (ushort)AM_pitch, (ushort)AM_heading, 0);
	AM_view_orient = newmat;

	vm_Orthogonalize(&AM_view_orient);
	Game_interface_mode = save_mode;
}

#include "hud.h"

void TCAMDrawMenu()
{
	char str[255];
	g3Point* pntlist[4], points[4];
	grtext_SetFont(BRIEFING_FONT);
	int fontheight = grfont_GetHeight(BRIEFING_FONT);

	points[0].p3_sx = AM_tcs->Monitor_coords[MONITOR_MAIN].left;
	points[0].p3_sy = AM_tcs->Monitor_coords[MONITOR_MAIN].bottom - (fontheight * 3);

	points[1].p3_sx = AM_tcs->Monitor_coords[MONITOR_MAIN].right;
	points[1].p3_sy = AM_tcs->Monitor_coords[MONITOR_MAIN].bottom - (fontheight * 3);

	points[2].p3_sx = AM_tcs->Monitor_coords[MONITOR_MAIN].right;
	points[2].p3_sy = AM_tcs->Monitor_coords[MONITOR_MAIN].bottom;

	points[3].p3_sx = AM_tcs->Monitor_coords[MONITOR_MAIN].left;
	points[3].p3_sy = AM_tcs->Monitor_coords[MONITOR_MAIN].bottom;

	for (int i = 0; i < 4; i++)
	{
		points[i].p3_z = 0;
		points[i].p3_flags = PF_PROJECTED;
		pntlist[i] = &points[i];
	}

	rend_SetZBufferState(0);
	rend_SetTextureType(TT_FLAT);
	rend_SetAlphaType(AT_CONSTANT);
	rend_SetLighting(LS_NONE);
	rend_SetFlatColor(GR_BLACK);
	rend_SetAlphaValue(192);
	rend_DrawPolygon2D(0, pntlist, 4);

	grtext_SetAlpha(250);
	grtext_SetColor(GR_RGB(255, 255, 255));

	sprintf(str, TXT_TCAM_TERRAIN, AM_terrain ? TXT_STRING_OFF : TXT_STRING_ON);
	grtext_CenteredPrintf(0, AM_tcs->Monitor_coords[MONITOR_MAIN].bottom - (fontheight * 3) + (fontheight / 2), str);
	sprintf(str, TXT_TCAM_INSTRUCTIONS);
	grtext_CenteredPrintf(0, AM_tcs->Monitor_coords[MONITOR_MAIN].bottom - (fontheight * 2) + (fontheight / 2), str);

	//Use HUD font for part that overlays the 3D
	fontheight = grfont_GetHeight(HUD_FONT);
	grtext_SetFont(HUD_FONT);
	grtext_SetColor(HUD_COLOR);

	sprintf(str, "%s", Level_info.name);
	grtext_CenteredPrintf(0, AM_tcs->Monitor_coords[MONITOR_MAIN].top + (fontheight / 2), str);

	if (AM_current_marker != -1)
	{
		int markernum = (Game_mode & GM_MULTI) ? (Player_num * 2 + AM_current_marker) : AM_current_marker;
		sprintf(str, TXT_VIEW_MARKER, AM_current_marker + 1);
		strcat(str, ": ");
		strcat(str, MarkerMessages[markernum]);
		grtext_CenteredPrintf(0, AM_tcs->Monitor_coords[MONITOR_MAIN].top + (fontheight / 2) + fontheight, str);
	}


	grtext_Flush();
}

// The callback function that the telcom calls every frame
void TCAMCallback(void)
{
    if (!SceneGeometryInspection()) Error("Automap migration requires renderer inspection");
    if (AM_center_on_player) { TCAMCenterOnPlayer(); AM_center_on_player = 0; }
    if (AM_realign) { AM_pitch = 0; AM_realign = 0; }
    const auto& monitor = Telcom_system.Monitor_coords[MONITOR_MAIN];
    StartFrame(monitor.left,monitor.top,monitor.right,monitor.bottom);
    static uint64_t sequence = uint64_t(1)<<63;
    const double display_time = FindArg("-scene-automap-telcom-probe") ? .375 : timer_GetTime();
    const auto frame = PrepareGameAutomap(++sequence,display_time,AM_terrain != 0,AM_ship_model_handle,AM_current_marker);
    const d3scene::Rect viewport{monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top};
    auto view = MakeGameSceneView(d3scene::ViewKind::Automap,0x4155544f4d415001ull,nullptr,
        AM_view_pos,-1,AM_view_orient,Render_zoom,false,viewport,{0,0,viewport.width,viewport.height});
    view.geometry_inspection = true; view.portal_visibility = false; view.dynamic_lighting = false; view.clear_depth = true;
    d3scene::renderView(*frame,view);
    EndFrame();
    rend_SetFiltering(0);
    TCAMDrawMenu();
    rend_FlushTextLayer();
    // Keep the diagnostic warning inside the monitor, not behind its surround.
    auto banner = MakeSceneCanvasView(SceneInspectionBanner,
        {viewport.x,viewport.y+16,viewport.width,viewport.height-16},d3scene::ViewTarget::Scene,true);
    d3scene::renderView(*frame,banner);
    TCAMReadControls();
}
