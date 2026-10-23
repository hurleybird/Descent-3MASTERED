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

#include "args.h"
#include "mono.h"
#include <string.h>
#ifdef __LINUX__
#include "lnxfix.h"
#ifndef _WIN32
#include <strings.h>
#endif
#else
#define strcasecmp stricmp
#endif

//rcg07062000 used to be static.
int TotalArgs = 0;
char GameArgs[MAX_ARGS][MAX_CHARS_PER_ARG];

const char* GetArg(int index)
{
	if (index >= TotalArgs || index <= 0 || index >= MAX_ARGS)
		return NULL;
	return GameArgs[index];
}

namespace {
const char* Argument_error = nullptr;
void resetArguments() {
    TotalArgs = 0; Argument_error = nullptr;
    memset(GameArgs,0,sizeof(GameArgs));
}
bool invalidArguments(const char* reason) {
    resetArguments(); Argument_error = reason; return false;
}
bool appendArgumentByte(int slot,int& length,char byte) {
    if (length >= MAX_CHARS_PER_ARG-1) return false;
    GameArgs[slot][length++] = byte; return true;
}
bool argumentSpace(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}
}
const char* ArgumentErrorMessage() { return Argument_error; }

// Preserve the exported fixed table, but reject overflow atomically instead
// of corrupting the following argument or applying a partial command line.
bool GatherArgs(char* text)
{
    resetArguments();
    int slot = 1; // WinMain excludes the executable-name operand.
    const char* cursor = text ? text : "";
    while (*cursor) {
        while (argumentSpace(*cursor)) ++cursor;
        if (!*cursor) break;
        if (slot >= MAX_ARGS) return invalidArguments("Too many command-line arguments (maximum 63 options/values).");
        bool quoted = false;
        int length = 0;
        while (*cursor && (quoted || !argumentSpace(*cursor))) {
            if (*cursor == '"') { quoted = !quoted; ++cursor; continue; }
            if (!appendArgumentByte(slot,length,*cursor++))
                return invalidArguments("Command-line argument exceeds the legacy module ABI limit of 99 bytes.");
        }
        if (quoted) return invalidArguments("Unterminated quote in command-line arguments.");
        ++slot;
    }
    TotalArgs = slot;
    return true;
}

bool GatherArgs(char** argv)
{
    resetArguments();
    if (!argv) return true;
    for (int slot = 0; argv[slot]; ++slot) {
        if (slot >= MAX_ARGS) return invalidArguments("Too many command-line arguments (maximum 63 options/values).");
        // GetArg never exposes argv[0]. A bounded compatibility snapshot of the
        // program name must not impose a path-size restriction on launching it.
        if (!slot) strncpy(GameArgs[0],argv[0],MAX_CHARS_PER_ARG-1);
        else {
            int length = 0;
            for (const char* byte = argv[slot]; *byte; ++byte)
                if (!appendArgumentByte(slot,length,*byte))
                    return invalidArguments("Command-line argument exceeds the legacy module ABI limit of 99 bytes.");
        }
        ++TotalArgs;
    }
    return true;
}

// Returns index of argument sought, or 0 if not found


int FindArg(char* which)
{
	if (which == NULL || !*which)
		return(0);

    // Argument storage is exported to modules, so do not cache mutable table
    // contents. Comparing the caller's spelling needs no temporary allocation.
#ifdef __LINUX__
    const bool gnu_prefix = (*which == '-') || (*which == '+');
#endif
	for (int i = 1; i < TotalArgs && i < MAX_ARGS; i++)
	{
        const char* query = which;
        const char* candidate = GameArgs[i];
#ifdef __LINUX__
        if (gnu_prefix) {
            if (candidate[0] != '-' || candidate[1] != '-') continue;
            ++query; candidate += 2;
        }
#endif
		if (strcasecmp(query, candidate) == 0)
		{
			mprintf((0, "FindArg: Found [%s] at argument index (%d).",
				which, i));
			return (i);
		} // if
	} // for

	mprintf((0, "FindArg: Did not find [%s] on command line.\n", which));
	return (0);
} // FindArg



// Returns index of argument sought, or 0 if not found
int FindArgChar(char* which, char singleCharArg)
{
#ifdef __LINUX__
	for (int i = 1; i < TotalArgs && i < MAX_ARGS; i++)
	{
		char* str = GameArgs[i];
		if ((str[0] == '-') && (str[1] == singleCharArg) && (str[2] == '\0'))
		{
			mprintf((0, "FindArg: Found [-%c] at argument index (%d).",
				singleCharArg, i));
			return (i);
		} // if
	} // for
#endif

	return (FindArg(which));
} // FindArgChar
