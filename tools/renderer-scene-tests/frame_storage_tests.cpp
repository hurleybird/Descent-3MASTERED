// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include "../../renderer/SceneCache.h"
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <cstdio>

namespace {
thread_local bool CountAllocations = false;
thread_local size_t Allocations = 0;
}
void* operator new(std::size_t size) {
    if (CountAllocations) ++Allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int runFrameStorageTests() {
    using namespace d3scene;
    SceneAssets owner;
    SceneCache<std::array<int,256>> cache;
    const auto warm_state = cache.acquire(owner);
    Allocations = 0; CountAllocations = true;
    bool same_state = true;
    for (int i = 0; i < 256; ++i) same_state = same_state && cache.acquire(owner) == warm_state;
    CountAllocations = false;
    if (!same_state || Allocations || owner.retainedProducerStates() != 1)
        throw std::runtime_error("Warm producer cache acquisition allocated or duplicated ownership");
    // Warm the exact acquire/abandon path, including the thread-local pool.
    { auto builder = acquireFrameData(); builder.objects.reserve(4096); }
    bool retained = true;
    Allocations = 0; CountAllocations = true;
    for (int frame = 0; frame < 256; ++frame) {
        auto builder = acquireFrameData();
        retained = retained && builder.objects.capacity() >= 4096 && builder.objects.empty();
        builder.objects.resize(128);
    }
    CountAllocations = false;
    if (!retained) throw std::runtime_error("Warm abandoned frame builders discarded capacity");
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
    // MSVC's checked STL allocates a proxy for even an empty vector. Retain
    // those iterator checks in Debug; the shipping allocation gate is Release.
    std::printf("Frame storage: retained capacity with debug STL tracking (%zu allocations)\n",Allocations);
#else
    if (Allocations) throw std::runtime_error("Warm abandoned frame builders allocated");
#endif
    return 256;
}
