// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/gamesave.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

int runSaveContractTests() {
    struct Guarded { char before = 42; char field[GAMESAVE_DESCLEN+1]; char after = 43; } output;
    std::fill(std::begin(output.field),std::end(output.field),'!');
    const auto check = [](bool valid) { if (!valid) throw std::runtime_error("Native save description contract failed"); };
    check(!EncodeGameSaveDescription(output.field,nullptr));
    check(!EncodeGameSaveDescription(output.field,"12345678901234567890123456789012"));
    check(!EncodeGameSaveDescription(output.field,"Renderer interleaved publication check"));
    check(std::all_of(std::begin(output.field),std::end(output.field),[](char c) {return c == '!';}));
    check(EncodeGameSaveDescription(output.field,"1234567890123456789012345678901"));
    check(std::strlen(output.field) == 31 && output.before == 42 && output.after == 43);
    check(EncodeGameSaveDescription(output.field,"Renderer export check"));
    const auto length = std::strlen(output.field);
    check(std::all_of(output.field+length,std::end(output.field),[](char c) {return c == 0;}));
    check(EncodeGameSaveDescription(output.field,"") && output.field[0] == 0);
    check(output.before == 42 && output.after == 43);
    return 10;
}
