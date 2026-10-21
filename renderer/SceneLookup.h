// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace d3scene {
// Frame-owned identity lookup. One contiguous allocation, no node per entity.
// All integral keys (including zero, signed rooms and generation bits) are valid.
// Pointers returned by find/emplace remain valid until reserve/growth or clear.
template<class Key,class Value> class SceneLookup {
    static_assert(std::is_integral<Key>::value,"Scene identities must be integral");
    using Entry = std::pair<Key,Value>;
    struct Slot { Entry entry{}; bool occupied = false; };
    std::vector<Slot> slots_;
    size_t count_ = 0;
    static size_t hash(Key key) {
        uint64_t x = static_cast<uint64_t>(key);
        x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
        x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
        return size_t(x ^ (x >> 31));
    }
    size_t position(Key key) const {
        size_t index = hash(key)&(slots_.size()-1);
        while (slots_[index].occupied && slots_[index].entry.first != key)
            index = (index+1)&(slots_.size()-1);
        return index;
    }
public:
    void clear() {
        for (auto& slot : slots_) slot = Slot{};
        count_ = 0;
    }
    void reserve(size_t entries) {
        if (entries <= slots_.size()/2) return;
        if (entries > (std::numeric_limits<size_t>::max)()/4) throw std::length_error("Scene lookup overflow");
        size_t capacity = 2;
        while (capacity < entries*2) capacity *= 2;
        auto previous = std::move(slots_);
        slots_.resize(capacity);
        for (auto& old : previous) if (old.occupied) {
            auto& slot = slots_[position(old.entry.first)];
            slot.entry = std::move(old.entry); slot.occupied = true;
        }
    }
    std::pair<const Entry*,bool> emplace(Key key,Value value) {
        reserve(count_+1);
        auto& slot = slots_[position(key)];
        if (slot.occupied) return {&slot.entry,false};
        slot.entry = {key,std::move(value)}; slot.occupied = true; ++count_;
        return {&slot.entry,true};
    }
    const Entry* find(Key key) const {
        if (slots_.empty()) return nullptr;
        const auto& slot = slots_[position(key)];
        return slot.occupied ? &slot.entry : nullptr;
    }
    const Entry* end() const { return nullptr; }
    size_t count(Key key) const { return find(key) ? 1 : 0; }
    size_t size() const { return count_; }
    const Value& at(Key key) const {
        const auto* entry = find(key);
        if (!entry) throw std::out_of_range("Missing scene identity");
        return entry->second;
    }
};
}
