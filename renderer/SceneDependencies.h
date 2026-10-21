// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace d3scene {
// Retain the union of immutable dependency lists. A stable inventory performs
// pointer/value comparisons only; edited inventories rebuild a sorted union.
// Owning list references prevents address reuse from impersonating old content.
template<class T,class Less = std::less<T>> class RetainedDependencies {
    using List = std::shared_ptr<const std::vector<T>>;
    std::vector<List> owners_;
    std::vector<T> direct_,values_,scratch_;
    size_t owner_cursor_ = 0,direct_cursor_ = 0;
    bool edited_ = false;
    Less less_;
    bool same(const T& a,const T& b) const { return !less_(a,b) && !less_(b,a); }
public:
    explicit RetainedDependencies(Less less = Less{}) : less_(std::move(less)) {}
    void clear() {
        owners_.clear(); direct_.clear(); values_.clear(); scratch_.clear();
        begin();
    }
    void begin() { owner_cursor_ = direct_cursor_ = 0; edited_ = false; }
    void add(const List& list) {
        if (!list || list->empty()) return;
        if (owner_cursor_ == owners_.size()) { owners_.push_back(list); edited_ = true; }
        else if (owners_[owner_cursor_].get() != list.get()) { owners_[owner_cursor_] = list; edited_ = true; }
        ++owner_cursor_;
    }
    void addValue(T value) {
        if (direct_cursor_ == direct_.size()) { direct_.push_back(std::move(value)); edited_ = true; }
        else if (!same(direct_[direct_cursor_],value)) { direct_[direct_cursor_] = std::move(value); edited_ = true; }
        ++direct_cursor_;
    }
    bool layoutChanged() const {
        return edited_ || owner_cursor_ != owners_.size() || direct_cursor_ != direct_.size();
    }
    // True means the union changed, not merely its owning tables or ordering.
    bool finish() {
        if (!layoutChanged()) return false;
        edited_ = true;
        owners_.resize(owner_cursor_); direct_.resize(direct_cursor_);
        scratch_.assign(direct_.begin(),direct_.end());
        for (const auto& owner : owners_) scratch_.insert(scratch_.end(),owner->begin(),owner->end());
        std::sort(scratch_.begin(),scratch_.end(),less_);
        scratch_.erase(std::unique(scratch_.begin(),scratch_.end(),[&](const T& a,const T& b) { return same(a,b); }),scratch_.end());
        const bool changed = values_.size() != scratch_.size() ||
            !std::equal(values_.begin(),values_.end(),scratch_.begin(),[&](const T& a,const T& b) { return same(a,b); });
        if (changed) values_.swap(scratch_);
        return changed;
    }
    const std::vector<T>& values() const { return values_; }
};
}
