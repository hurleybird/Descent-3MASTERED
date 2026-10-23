// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include <cstdlib>
#include <new>
#include <stdexcept>

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
    if (!retained || Allocations) throw std::runtime_error("Warm abandoned frame builders allocated or discarded capacity");
    return 256;
}
