// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace d3scene {
// Vertex ranges, independent of GL objects. Pages never move live allocations;
// retired assets return ranges for reuse rather than growing an append-only heap.
class SceneGeometryAllocator {
public:
    struct Allocation { uint32_t page = UINT32_MAX, offset = 0, count = 0; };
private:
    struct Range { uint32_t offset, count; };
    struct Page { uint32_t capacity; std::vector<Range> free; };
    std::vector<Page> pages_;
    uint32_t page_size_;
public:
    explicit SceneGeometryAllocator(uint32_t page_size = 16384) : page_size_(page_size) {
        if (!page_size) throw std::invalid_argument("Empty geometry page");
    }
    Allocation allocate(uint32_t count) {
        if (!count) return {};
        for (size_t p = 0; p < pages_.size(); ++p) {
            auto& free = pages_[p].free;
            for (size_t r = 0; r < free.size(); ++r) if (free[r].count >= count) {
                const Allocation result{uint32_t(p),free[r].offset,count};
                free[r].offset += count; free[r].count -= count;
                if (!free[r].count) free.erase(free.begin()+r);
                return result;
            }
        }
        if (pages_.size() == UINT32_MAX) throw std::overflow_error("Too many geometry pages");
        // Size classes also let a growing oversized mesh reuse retired pages;
        // exact-size pages would accumulate for every small geometry revision.
        auto capacity = page_size_;
        while (capacity < count) {
            if (capacity > UINT32_MAX/2) { capacity = count; break; }
            capacity *= 2;
        }
        Page page{capacity,{}};
        if (capacity > count) page.free.push_back({count,capacity-count});
        pages_.push_back(std::move(page));
        return {uint32_t(pages_.size()-1),0,count};
    }
    void release(Allocation allocation) {
        if (!allocation.count) return;
        if (allocation.page >= pages_.size() || uint64_t(allocation.offset)+allocation.count > pages_[allocation.page].capacity)
            throw std::invalid_argument("Geometry allocation outside page");
        auto& free = pages_[allocation.page].free;
        auto at = std::lower_bound(free.begin(),free.end(),allocation.offset,
            [](const Range& range,uint32_t offset) { return range.offset < offset; });
        if ((at != free.end() && uint64_t(allocation.offset)+allocation.count > at->offset) ||
            (at != free.begin() && uint64_t((at-1)->offset)+(at-1)->count > allocation.offset))
            throw std::invalid_argument("Overlapping geometry retirement");
        at = free.insert(at,{allocation.offset,allocation.count});
        if (at != free.begin() && (at-1)->offset+(at-1)->count == at->offset) {
            (at-1)->count += at->count; at = free.erase(at)-1;
        }
        if (at+1 != free.end() && at->offset+at->count == (at+1)->offset) {
            at->count += (at+1)->count; free.erase(at+1);
        }
    }
    uint32_t capacity(uint32_t page) const { return pages_.at(page).capacity; }
    size_t pageCount() const { return pages_.size(); }
    void clear() { pages_.clear(); }
};
}
