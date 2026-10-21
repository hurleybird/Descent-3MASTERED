// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
namespace d3scene {
// CPU publication revisions, not reusable source slots, identify GPU storage.
// A weak owner permits retirement without extending CPU asset lifetimes. The
// caller performs GL destruction on the owning context at a safe boundary.
template<class Asset,class Gpu> class SceneGpuCache {
    struct Key {
        uint64_t lifetime, revision;
        bool operator==(Key other) const { return lifetime == other.lifetime && revision == other.revision; }
    };
    struct Hash {
        size_t operator()(Key key) const {
            return std::hash<uint64_t>{}(key.revision) ^ (std::hash<uint64_t>{}(key.lifetime)<<1);
        }
    };
    struct Entry { std::weak_ptr<const Asset> owner; Gpu gpu; };
    std::unordered_map<Key,Entry,Hash> entries_;
public:
    Gpu& get(const std::shared_ptr<const Asset>& asset) {
        const auto id = asset->id();
        const Key key{id.lifetime,id.revision};
        const auto found = entries_.find(key);
        if (found != entries_.end()) return found->second.gpu;
        return entries_.emplace(key,Entry{asset,Gpu{}}).first->second.gpu;
    }
    template<class Destroy> void releaseExpired(Destroy destroy) {
        for (auto at = entries_.begin(); at != entries_.end();) {
            if (!at->second.owner.expired()) { ++at; continue; }
            destroy(at->second.gpu); at = entries_.erase(at);
        }
    }
    template<class Destroy> void clear(Destroy destroy) {
        for (auto& entry : entries_) destroy(entry.second.gpu);
        entries_.clear();
    }
    size_t size() const { return entries_.size(); }
};
}
