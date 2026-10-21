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
    std::puts("PASS: argument lookup semantics and zero allocations");
}
