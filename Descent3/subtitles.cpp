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

#include "subtitles.h"
#include "../movie/SubtitlePresentation.h"
#include "gamefont.h"
#include "gameloop.h"
//#include "gamefont.h"
//#include "grtext.h"
//#include "renderer.h"
#include "pserror.h"
#include "ddio.h"
//#include "ddvid.h"
#include "manage.h"
#include "application.h"
#include "mem.h"
#include "movie.h"
#include "args.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Subtitle data
struct subtitle
{
	int first_frame, last_frame;
	char* msg;
};

#define MAX_SUBTITLES						500
#define MOVIE_SUBTITLE_EXTENSION			".msb"
#define MESSAGE_LENGTH						300

static subtitle Subtitles[MAX_SUBTITLES];
int Num_subtitles;
static d3movie::SubtitleTimeline Subtitle_playback;

//Prints out a subtitle on the screen, centered, at y pos y, with color color.
void PrintSubTitle(int y, ddgr_color color, char* msg);
//Parses a subtitle file
void SubtParseSubtitles(CFILE* file);
//Resets the subtitle information
void SubtResetSubTitles(void);

void readline(CFILE* file, char* buf, int maxsize)
{
	ASSERT(file);
	ASSERT(buf);
	buf[0] = '\0';

	bool done = false;
	while (!done)
	{
		if (cfeof(file))
		{
			done = true;
			buf[0] = '\0';
		}
		if (cf_ReadString(buf, maxsize, file))
			done = true;
	}
}

char* parse_whitespace(char* p)
{
	ASSERT(p);
	while (isspace(*p))
		p++;
	return p;
}

char* parse_int(char* buf, int* d)
{
	ASSERT(buf);
	ASSERT(d);

	bool got_digit = 0;
	char* t = buf = parse_whitespace(buf);

	//Check for negative sign
	if (*t == '-')
		t++;

	//Check for valid characters in number
	while (isdigit(*t))
	{
		t++;
		got_digit = 1;
	}

	//Make sure at least one digit
	if (!got_digit)
	{
		*d = 0;
		return NULL;
	}

	//Check for valid seperator, and convert
	if ((*t == 0) || isspace(*t) || (*t == ','))
	{
		//valid number
		*d = atoi(buf);
		return t;
	}
	else
	{
		return NULL;
	}
}

char* parse_token(char* p, char* token)
{
	ASSERT(p);
	ASSERT(token);

	int len = strlen(token);
	if (!strnicmp(p, token, len))
	{
		return p + len;
	}

	return NULL;
}

#define PARSE_NEXT_LINE()	do{readline(file,buf,MESSAGE_LENGTH); p = buf;}while(0)

//Parses a subtitle file
void SubtParseSubtitles(CFILE* file)
{
	SubtResetSubTitles();

	int first_frame, last_frame;
	char buf[MESSAGE_LENGTH];
	char* p = buf;

	PARSE_NEXT_LINE();

	while (!parse_token(buf, "#End"))
	{
		if (Num_subtitles >= MAX_SUBTITLES) goto subt_parse_error;

		// starting frame number
		p = parse_int(p, &first_frame);
		if (!p)
		{
			mprintf((0, "Couldn't parse first_frame\n"));
			goto subt_parse_error;
		}

		p = parse_int(p, &last_frame);
		if (!p)
		{
			mprintf((0, "Couldn't parse last_frame\n"));
			goto subt_parse_error;
		}

		p = parse_whitespace(p);
		if (first_frame < 0 || last_frame < first_frame ||
			(Num_subtitles && first_frame < Subtitles[Num_subtitles-1].first_frame)) goto subt_parse_error;

		Subtitles[Num_subtitles].first_frame = first_frame;
		Subtitles[Num_subtitles].last_frame = last_frame;
		Subtitles[Num_subtitles].msg = (char*)mem_malloc(strlen(p) + 1);
		if (!Subtitles[Num_subtitles].msg)
			goto subt_parse_error;
		strcpy(Subtitles[Num_subtitles].msg, p);          // be sure to free this later

		Num_subtitles++;

		PARSE_NEXT_LINE();
	}
	return;
subt_parse_error:
	mprintf((0, "Error Parsing SubTitle File!\n"));
	SubtResetSubTitles();
}

//Resets the subtitle information
void SubtResetSubTitles(void)
{
	for (int i = 0; i < Num_subtitles; i++)
	{
		if (Subtitles[i].msg)
			mem_free(Subtitles[i].msg);
		Subtitles[i].msg = NULL;
	}
	Num_subtitles = 0;
	Subtitle_playback.reset();
}

//Initializes the subtitles for a given movie file
void SubtInitSubtitles(const char* filename)
{
	//Load the subtitles
	SubtResetSubTitles();

	char subtitle_file[_MAX_FNAME], subtitle_path[_MAX_PATH], * p;

	// need a quick out here if no subtitles.
	if (!FindArg("-subtitles"))
		return;

	// find the start of the filename
	const char* pFilenameStart = strrchr(filename, '\\');
	if (pFilenameStart != NULL)
		filename = pFilenameStart + 1;

	pFilenameStart = strrchr(filename, '/');
	if (pFilenameStart != NULL)
		filename = pFilenameStart + 1;

	// strip off any extension for the movie and append the subtitle text
	strcpy(subtitle_file, filename);
	p = strchr(subtitle_file, '.');
	if (p)
		*p = '\0';
	
	strcat(subtitle_file, MOVIE_SUBTITLE_EXTENSION);

	ddio_MakePath(subtitle_path, LocalD3Dir, "movies", subtitle_file, NULL);

	mprintf((0, "Looking for the subtitle file %s\n", subtitle_path));

	const int capture_arg = FindArg("-capture-subtitle-input");
	const char* capture_file = capture_arg ? GetArg(capture_arg+1) : nullptr;
	if (capture_arg && (!capture_file || !*capture_file)) Error("Missing capture subtitle input");
	CFILE* ifile = cfopen(capture_file ? capture_file : subtitle_path, "rt");
	if (!ifile)
	{
		mprintf((0, "Movie: Couldn't find subtitle file %s\n", subtitle_path));
		return;
	}

	SubtResetSubTitles();
	SubtParseSubtitles(ifile);
	cfclose(ifile);
}

//Shutsdown the subtitle system
void SubtCloseSubtitles()
{
	SubtResetSubTitles();
}

//draw the subtitles for this frame
void SubtDrawSubtitles(int frame_num)
{
	try { Subtitle_playback.advance(frame_num,Subtitles,Num_subtitles); }
	catch (const std::exception& error) { Error("Invalid subtitle playback: %s",error.what()); }
	if (!Subtitle_playback.size()) return;
	rendering_state display; rend_GetRenderState(&display);
	const auto layout = d3movie::subtitleLayout(display.screen_width,display.screen_height,
		grfont_GetHeight(BRIEFING_FONT),int(d3movie::SubtitleTimeline::capacity));
	int y = layout.first_y;
	for (size_t i = 0; i < Subtitle_playback.size(); ++i) {
		PrintSubTitle(y,GR_RGB(255,255,255),Subtitles[Subtitle_playback.active()[i]].msg);
		y += layout.line_pitch;
	}
	if (FindArg("-capture-decoded-movie-output"))
		AutomatedCaptureLog("scene movie subtitle frame=%d lines=%u width=%d height=%d first_y=%d scale=%.6f",
			frame_num,unsigned(Subtitle_playback.size()),display.screen_width,display.screen_height,layout.first_y,layout.font_scale);
}

//Prints out a subtitle on the screen, centered, at y pos y, with color color.
void PrintSubTitle(int y, ddgr_color color, char* msg)
{
	mve_Puts(-1, y, color, msg);
}
