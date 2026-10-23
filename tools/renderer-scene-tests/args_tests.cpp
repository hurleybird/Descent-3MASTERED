// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/args.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

extern int TotalArgs;
static unsigned Allocations = 0;
void* operator new(std::size_t size) {
    ++Allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int main() {
    TotalArgs = MAX_ARGS;
    for (int i = 0; i < MAX_ARGS; ++i) std::strcpy(GameArgs[i],"unused");
#ifdef __LINUX__
    std::strcpy(GameArgs[1],"--first"); std::strcpy(GameArgs[2],"--FIRST");
    std::strcpy(GameArgs[MAX_ARGS-1],"--last");
#else
    std::strcpy(GameArgs[1],"-first"); std::strcpy(GameArgs[2],"-FIRST");
    std::strcpy(GameArgs[MAX_ARGS-1],"-last");
#endif
    char first[] = "-FiRsT", last[] = "-last", absent[] = "-missing", empty[] = "";
#ifdef __LINUX__
    std::strcpy(GameArgs[3],"---double"); std::strcpy(GameArgs[4],"-q");
    char plus[] = "+first", doubled[] = "--double";
    if (FindArg(plus) != 1 || FindArg(doubled) != 3 || FindArgChar(absent,'q') != 4) return 5;
#endif
    char long_query[1024]; std::memset(long_query,'x',sizeof(long_query)); long_query[1023] = 0;
    const auto before = Allocations;
    for (int i = 0; i < 10000; ++i)
        if (FindArg(first) != 1 || FindArg(last) != MAX_ARGS-1 || FindArg(absent) || FindArg(nullptr) ||
            FindArg(empty) || FindArg(long_query)) return 1;
    if (Allocations != before) return 2;
    // The module ABI permits edits: lookup must observe them, not stale cache.
    std::strcpy(GameArgs[1],"changed");
    if (FindArg(first) != 2 || GetArg(-1) || GetArg(0) || GetArg(MAX_ARGS)) return 3;
    TotalArgs = 2;
    if (FindArg(first) != 0 || GetArg(2)) return 4;
    TotalArgs = 0;
    if (FindArg(first) || GetArg(1)) return 6;
    const auto parser_allocations = Allocations;
    char quoted[] = "  -first \"two words\"\t\"\" -tail  value  ";
    if (!GatherArgs(quoted) || TotalArgs != 6 || std::strcmp(GetArg(2),"two words") ||
        std::strcmp(GetArg(3),"") || std::strcmp(GetArg(5),"value") || FindArg(empty) || ArgumentErrorMessage()) return 7;
    char again[] = "-single";
    if (!GatherArgs(again) || TotalArgs != 2 || GetArg(2) || std::strcmp(GetArg(1),"-single")) return 8;
    char boundary[MAX_CHARS_PER_ARG+1]; std::memset(boundary,'x',sizeof(boundary));
    boundary[MAX_CHARS_PER_ARG-1] = 0;
    char line[MAX_CHARS_PER_ARG+40];
    std::snprintf(line,sizeof(line),"-path \"%s\" -next ok",boundary);
    if (!GatherArgs(line) || std::strlen(GetArg(2)) != MAX_CHARS_PER_ARG-1 || std::strcmp(GetArg(4),"ok")) return 9;
    boundary[MAX_CHARS_PER_ARG-1] = 'x'; boundary[MAX_CHARS_PER_ARG] = 0;
    std::snprintf(line,sizeof(line),"-path \"%s\" -next ok",boundary);
    if (GatherArgs(line) || TotalArgs || !ArgumentErrorMessage() || GetArg(1) || GameArgs[3][0]) return 10;
    char broken[] = "-path \"unterminated";
    if (GatherArgs(broken) || TotalArgs || !ArgumentErrorMessage()) return 11;
    char program[400]; std::memset(program,'p',sizeof(program)); program[399] = 0;
    char value[] = "value", option[] = "-one";
    char* argv[] = {program,option,value,nullptr};
    if (!GatherArgs(argv) || TotalArgs != 3 || GameArgs[0][MAX_CHARS_PER_ARG-1] || std::strcmp(GetArg(2),value)) return 12;
    argv[2] = boundary;
    if (GatherArgs(argv) || TotalArgs || !ArgumentErrorMessage()) return 13;
    char* many[MAX_ARGS+2];
    for (int i = 0; i <= MAX_ARGS; ++i) many[i] = value;
    many[MAX_ARGS+1] = nullptr;
    if (GatherArgs(many) || TotalArgs || GameArgs[MAX_ARGS-1][0]) return 14;
    many[MAX_ARGS] = nullptr;
    if (!GatherArgs(many) || TotalArgs != MAX_ARGS || std::strcmp(GetArg(MAX_ARGS-1),value)) return 15;
    char tokens[MAX_ARGS*2+1];
    for (int i = 0; i < MAX_ARGS; ++i) { tokens[i*2] = 'x'; tokens[i*2+1] = ' '; }
    tokens[MAX_ARGS*2] = 0;
    if (GatherArgs(tokens) || TotalArgs || !ArgumentErrorMessage()) return 16;
    tokens[(MAX_ARGS-1)*2] = 0;
    if (!GatherArgs(tokens) || TotalArgs != MAX_ARGS || ArgumentErrorMessage()) return 17;
    char no_arguments[] = " \t ";
    if (!GatherArgs(no_arguments) || TotalArgs != 1 || GetArg(1) || Allocations != parser_allocations) return 18;
    std::puts("PASS: bounded argument parsing, mutable ABI and zero-allocation lookup");
}
