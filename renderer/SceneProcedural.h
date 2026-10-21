// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define D3_PROCEDURAL_SSE2
#endif

namespace d3scene {
// Constant-height impulse span, with authored wrapping (not saturation).
inline void addProceduralSpan(int16_t* pixels,size_t count,uint8_t height) {
    size_t i = 0;
#ifdef D3_PROCEDURAL_SSE2
    const auto addition = _mm_set1_epi16(height);
    for (; i+8 <= count; i += 8) {
        const auto values = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels+i));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i),_mm_add_epi16(values,addition));
    }
#endif
    for (; i < count; ++i) pixels[i] = int16_t(pixels[i]+height);
}
// Exact byte kernels: saturating decay and the authored four-tap wrapped
// diffusion. Source/destination diffusion buffers must not overlap. SSE2 uses
// widened integer sums, not rounded byte averages, so every texel is unchanged.
inline void fadeProcedural(uint8_t* pixels,size_t count,uint8_t heat) {
    const int fade = ((255-int(heat))>>3)+1;
    size_t i = 0;
#ifdef D3_PROCEDURAL_SSE2
    const auto amount = _mm_set1_epi8(static_cast<char>(fade));
    for (; i+16 <= count; i += 16) {
        const auto values = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels+i));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i),_mm_subs_epu8(values,amount));
    }
#endif
    for (; i < count; ++i) pixels[i] = pixels[i] > fade ? uint8_t(pixels[i]-fade) : 0;
}
inline void blendProcedural(const uint8_t* source,uint8_t* destination,size_t width,size_t height) {
    if (!width || !height) return;
    for (size_t y = 0; y < height; ++y) {
        const auto* row = source+y*width;
        const auto* below = source+(y+1 == height ? 0 : y+1)*width;
        auto* output = destination+y*width;
        output[0] = uint8_t((int(row[0])+row[width == 1 ? 0 : 1]+row[width-1]+below[0])>>2);
        size_t x = 1;
#ifdef D3_PROCEDURAL_SSE2
        const auto zero = _mm_setzero_si128();
        for (; x+16 < width; x += 16) {
            const auto center = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x));
            const auto left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x-1));
            const auto right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x+1));
            const auto down = _mm_loadu_si128(reinterpret_cast<const __m128i*>(below+x));
            const auto lo = _mm_add_epi16(_mm_add_epi16(_mm_unpacklo_epi8(center,zero),_mm_unpacklo_epi8(left,zero)),
                _mm_add_epi16(_mm_unpacklo_epi8(right,zero),_mm_unpacklo_epi8(down,zero)));
            const auto hi = _mm_add_epi16(_mm_add_epi16(_mm_unpackhi_epi8(center,zero),_mm_unpackhi_epi8(left,zero)),
                _mm_add_epi16(_mm_unpackhi_epi8(right,zero),_mm_unpackhi_epi8(down,zero)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output+x),_mm_packus_epi16(_mm_srli_epi16(lo,2),_mm_srli_epi16(hi,2)));
        }
#endif
        for (; x+1 < width; ++x) output[x] = uint8_t((int(row[x])+row[x-1]+row[x+1]+below[x])>>2);
        if (width > 1) output[width-1] = uint8_t((int(row[width-1])+row[width-2]+row[0]+below[width-1])>>2);
    }
}
// Wrapped four-neighbor wave update. Preserve signed 32-bit intermediate
// arithmetic and truncation (not saturation) into the authored 16-bit grid.
inline void stepProceduralWater(const int16_t* source,int16_t* destination,size_t width,size_t height,unsigned density) {
    if (!width || !height) return;
    density &= 31; // legacy x86 variable-shift semantics, also defined for corrupt input
    for (size_t y = 0; y < height; ++y) {
        const auto* row = source+y*width;
        const auto* above = source+(y ? y-1 : height-1)*width;
        const auto* below = source+(y+1 == height ? 0 : y+1)*width;
        auto* output = destination+y*width;
        const auto scalar = [&](size_t x,size_t left,size_t right) {
            const int value = ((int(above[x])+below[x]+row[left]+row[right])>>1)-output[x];
            output[x] = int16_t(value-(value>>density));
        };
        scalar(0,width-1,width == 1 ? 0 : 1);
        size_t x = 1;
#ifdef D3_PROCEDURAL_SSE2
        const auto shift = _mm_cvtsi32_si128(int(density));
        const auto low = [](__m128i v) { return _mm_unpacklo_epi16(v,_mm_srai_epi16(v,15)); };
        const auto high = [](__m128i v) { return _mm_unpackhi_epi16(v,_mm_srai_epi16(v,15)); };
        const auto damp = [&](const __m128i sum,const __m128i previous) {
            const auto value = _mm_sub_epi32(_mm_srai_epi32(sum,1),previous);
            const auto result = _mm_sub_epi32(value,_mm_sra_epi32(value,shift));
            return _mm_srai_epi32(_mm_slli_epi32(result,16),16);
        };
        for (; x+8 < width; x += 8) {
            const auto up = _mm_loadu_si128(reinterpret_cast<const __m128i*>(above+x));
            const auto down = _mm_loadu_si128(reinterpret_cast<const __m128i*>(below+x));
            const auto left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x-1));
            const auto right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x+1));
            const auto previous = _mm_loadu_si128(reinterpret_cast<const __m128i*>(output+x));
            const auto lo = _mm_add_epi32(_mm_add_epi32(low(up),low(down)),_mm_add_epi32(low(left),low(right)));
            const auto hi = _mm_add_epi32(_mm_add_epi32(high(up),high(down)),_mm_add_epi32(high(left),high(right)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output+x),_mm_packs_epi32(damp(lo,low(previous)),damp(hi,high(previous))));
        }
#endif
        for (; x+1 < width; ++x) scalar(x,x-1,x+1);
        if (width > 1) scalar(width-1,width-2,0);
    }
}
// Procedural images advance on presentation time, never wall time or draw count.
// A long absence advances one step rather than replaying unbounded hidden work.
inline int proceduralSteps(int previous, int current, bool enabled) {
    if (previous == current) return 0;
    const int64_t elapsed = int64_t(current) - previous;
    if (previous >= 0 && elapsed > 0 && !enabled && elapsed < 600) return 0;
    return previous < 0 || elapsed < 1 || elapsed > 8 ? 1 : int(elapsed);
}
#ifdef D3_PROCEDURAL_SSE2
#undef D3_PROCEDURAL_SSE2
#endif

// Local LCG retains the procedural kernels' 15-bit random range. Each image/tick
// has its own stream: extra views or another texture cannot consume its entropy.
class ProceduralRandom {
    uint32_t state_;
public:
    explicit ProceduralRandom(uint32_t seed) : state_(seed) {}
    ProceduralRandom(uint32_t image, int tick) {
        uint32_t seed = image * 0x9e3779b9u ^ uint32_t(tick) ^ 0x70726f63u;
        seed ^= seed >> 16; seed *= 0x7feb352du;
        seed ^= seed >> 15; seed *= 0x846ca68bu;
        state_ = seed ^ (seed >> 16);
    }
    int operator()() {
        state_ = state_ * 214013u + 2531011u;
        return int((state_ >> 16) & 0x7fffu);
    }
};
}
