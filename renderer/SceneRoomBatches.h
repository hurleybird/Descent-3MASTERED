// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d3scene {
constexpr size_t RoomDrawWords=100;
// The only draw constants allowed to vary without a new material publication:
// light list, illumination, soft depth, exterior fog, reflection source range,
// deformation-history validity, and inline light IDs. Vertex stream addresses
// and animated UV transforms live in the separate face table.
constexpr std::array<size_t,12> RoomViewFields{{62,63,65,67,72,77,78,93,96,97,98,99}};
using RoomViewPatch=std::array<uint32_t,RoomViewFields.size()>;
inline RoomViewPatch roomViewPatch(const float* record) {
    RoomViewPatch patch{};
    for (size_t i=0;i<patch.size();++i) std::memcpy(&patch[i],record+RoomViewFields[i],4);
    return patch;
}
inline void applyRoomViewPatch(float* record,const RoomViewPatch& patch) {
    for (size_t i=0;i<patch.size();++i) std::memcpy(record+RoomViewFields[i],&patch[i],4);
}

// Owned by one retained geometry/material publication, not by a frame. Hidden
// faces compile lazily on first demand. Unchanged publications never rebuild
// their material constants or compare their wide records again.
class RoomMaterialTemplates {
    std::vector<uint32_t> faces_;
    std::vector<std::array<float,RoomDrawWords>> records_;
public:
    void reset(size_t faces) { faces_.assign(faces,UINT32_MAX); records_.clear(); }
    uint32_t find(size_t face) const { return faces_.at(face); }
    const auto& record(uint32_t id) const { return records_.at(id); }
    uint32_t intern(size_t face,const float* source) {
        std::array<float,RoomDrawWords> record;
        std::memcpy(record.data(),source,sizeof(record));
        for (auto field : RoomViewFields) record[field]=0;
        uint32_t id=0;
        for (;id<records_.size();++id)
            if (!std::memcmp(records_[id].data(),record.data(),sizeof(record))) break;
        if (id==records_.size()) records_.push_back(record);
        faces_.at(face)=id;
        return id;
    }
};

struct RoomBatchKey {
    uint32_t room=0,material=0;
    RoomViewPatch patch{};
    bool operator==(const RoomBatchKey& other) const {
        return room==other.room && material==other.material && patch==other.patch;
    }
};
// Reused open-addressed storage avoids per-frame map nodes/allocations. A view
// epoch invalidates entries without clearing capacity or visiting the scene.
class RoomViewBatches {
    struct Slot { RoomBatchKey key; uint32_t record=0,epoch=0; };
    std::vector<Slot> slots_;
    uint32_t epoch_=0;
    size_t count_=0;
public:
    void begin(size_t maximum) {
        if (maximum>size_t(std::numeric_limits<uint32_t>::max()/2)) throw std::length_error("Too many room batches");
        size_t capacity=8;
        while (capacity<maximum*2) capacity*=2;
        if (slots_.size()<capacity) { slots_.assign(capacity,{}); epoch_=0; }
        if (++epoch_==0) { for (auto& slot : slots_) slot.epoch=0; epoch_=1; }
        count_=0;
    }
    struct Lookup { uint32_t record; bool inserted; };
    Lookup resolve(const RoomBatchKey& key,uint32_t proposed_record) {
        if (slots_.empty()) throw std::logic_error("Room batch table was not initialized");
        uint32_t hash=2166136261u;
        const auto add=[&](uint32_t word){ hash=(hash^word)*16777619u; };
        add(key.room); add(key.material);
        for (auto word : key.patch) add(word);
        hash^=hash>>16;
        size_t at=hash&(slots_.size()-1);
        for (;;) {
            auto& slot=slots_[at];
            if (slot.epoch!=epoch_) {
                if (count_>=slots_.size()/2) throw std::logic_error("Room batch table capacity exhausted");
                slot={key,proposed_record,epoch_}; ++count_;
                return {proposed_record,true};
            }
            if (slot.key==key) return {slot.record,false};
            at=(at+1)&(slots_.size()-1);
        }
    }
};
}
