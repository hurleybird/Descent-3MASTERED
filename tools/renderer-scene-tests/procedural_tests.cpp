// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneProcedural.h"
#include "../../renderer/SceneAnimation.h"
#include "../../lib/ImmutablePixels.h"
#include <stdexcept>
#include <climits>
#include <vector>
int runProceduralTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Procedural clock/random contract"); };
    {
        std::array<uint8_t,769> indices;
        std::array<uint16_t,256> palette;
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint8_t(i*17);
        for (size_t i = 0; i < palette.size(); ++i) palette[i] = uint16_t(i*251);
        const auto source = indices; const auto colors = palette;
        IndexedPixels16 pixels(indices.data(),indices.size(),palette.data());
        indices.fill(0); palette.fill(0); // retained image owns BOTH inputs
        check(pixels.size() == source.size()*2);
        const auto& result = pixels.bytes();
        check(&result == &pixels.bytes());
        for (size_t i = 0; i < source.size(); ++i) {
            uint16_t color; std::memcpy(&color,result.data()+i*2,2);
            check(color == colors[source[i]]);
        }
        bool rejected = false;
        try { IndexedPixels16 invalid(nullptr,1,colors.data()); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
    }
    for (int heat = 0; heat < 256; ++heat) {
        std::vector<uint8_t> pixels(258,99);
        for (int i = 0; i < 256; ++i) pixels[i+1] = uint8_t(i);
        fadeProcedural(pixels.data()+1,256,uint8_t(heat));
        const int fade = ((255-heat)>>3)+1;
        for (int i = 0; i < 256; ++i) check(pixels[i+1] == (i > fade ? i-fade : 0));
        check(pixels.front() == 99 && pixels.back() == 99);
    }
    for (size_t width : {size_t(1),size_t(2),size_t(15),size_t(16),size_t(17),size_t(18),size_t(31),size_t(32),size_t(33),size_t(127),size_t(128),size_t(129)})
    for (size_t height : {size_t(1),size_t(2),size_t(3),size_t(128)}) {
        std::vector<uint8_t> pixels(width*height+2,73),result(pixels.size(),91);
        ProceduralRandom random(uint32_t(width*height));
        for (size_t i = 0; i < width*height; ++i) pixels[i+1] = uint8_t(random());
        const auto* source = pixels.data()+1;
        blendProcedural(source,result.data()+1,width,height);
        for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x) {
            const int expected = (int(source[y*width+x])+source[y*width+(x+1)%width]+
                source[y*width+(x+width-1)%width]+source[((y+1)%height)*width+x])>>2;
            check(result[y*width+x+1] == expected);
        }
        check(result.front() == 91 && result.back() == 91);
    }
    for (size_t count : {size_t(0),size_t(1),size_t(15),size_t(17),size_t(255)}) {
        std::vector<uint8_t> pixels(count+2,91);
        fadeProcedural(pixels.data()+1,count,255);
        for (size_t i = 0; i < count; ++i) check(pixels[i+1] == 90);
        check(pixels.front() == 91 && pixels.back() == 91);
    }
    for (size_t width : {size_t(1),size_t(2),size_t(7),size_t(8),size_t(9),size_t(10),size_t(15),size_t(16),size_t(17),size_t(127),size_t(128),size_t(129)})
    for (size_t height : {size_t(1),size_t(2),size_t(3),size_t(128)})
    for (unsigned density : {0u,1u,3u,8u,15u,31u,32u,255u}) {
        std::vector<int16_t> pixels(width*height+2,73),result(pixels.size(),91);
        ProceduralRandom random(uint32_t(width*height));
        for (size_t i = 0; i < width*height; ++i) {
            pixels[i+1] = i%4 == 0 ? INT16_MIN : i%4 == 1 ? INT16_MAX : int16_t(random()*2);
            result[i+1] = int16_t(random()*2);
        }
        const auto prior = result;
        const auto* source = pixels.data()+1;
        stepProceduralWater(source,result.data()+1,width,height,density);
        for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x) {
            const int value = ((int(source[((y+1)%height)*width+x])+source[((y+height-1)%height)*width+x]+
                source[y*width+(x+1)%width]+source[y*width+(x+width-1)%width])>>1)-prior[y*width+x+1];
            check(result[y*width+x+1] == int16_t(value-(value>>(density&31))));
        }
        check(result.front() == 91 && result.back() == 91);
    }
    check(visualAngle(1,160,5000) == 14600);
    check(weaponSpriteAngle(10,0) == 0);
    check(weaponSpriteAngle(1.25,65536) == 16384);
    check(weaponSpriteAngle(1.25,32768) == 16384);
    check(weaponSpriteAngle(-.25,65536) == 49152);
    check(visualAngle(-1,160) == 55936);
    check(visualAngle(65536,1) == 0);
    const auto first = animationFrames(10,0,false), halfway = animationFrames(10,.25f,false);
    check(first.frame0 == 0 && !first.has_frame1);
    check(halfway.frame0 == 2 && halfway.frame1 == 3 && halfway.frame1_weight == .5f && halfway.has_frame1);
    check(!animationFrames(10,.99f,false).has_frame1);
    check(animationFrames(10,.99f,true).frame1 == 0 && animationFrames(10,.99f,true).has_frame1);
    check(animationFrames(10,-1,true).frame0 == 0 && animationFrames(10,2,false).frame0 == 9);
    check(!animationFrames(1,.3f,true).has_frame1 && !animationFrames(0,.3f,true).has_frame1);
    check(proceduralSteps(-1, 700, true) == 1);
    check(proceduralSteps(-1, 0, false) == 1);
    check(proceduralSteps(10, 10, true) == 0);
    check(proceduralSteps(10, 10, false) == 0);
    check(proceduralSteps(10, 18, true) == 8);
    check(proceduralSteps(10, 19, true) == 1);
    check(proceduralSteps(10, 609, false) == 0);
    check(proceduralSteps(10, 610, false) == 1);
    check(proceduralSteps(700, 10, false) == 1);
    check(proceduralSteps(0, INT_MAX, true) == 1);
    ProceduralRandom reference(1);
    check(reference() == 41); check(reference() == 18467); check(reference() == 6334);
    ProceduralRandom a(42, 100), repeat(42, 100), other(43, 100), next(42, 101);
    bool other_differs = false, next_differs = false;
    for (int i = 0; i < 1000; ++i) {
        const int value = a();
        other_differs |= value != other(); next_differs |= value != next();
        check(value == repeat() && value >= 0 && value <= 32767);
    }
    check(other_differs && next_differs);
    return checks;
}
