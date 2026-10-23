// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
// Mutable producer caches belong to the same explicit lifetime as their assets.
// A handle retains nothing. Publication copies immutable values/references out;
// neither rendering nor source-cache destruction may invoke game callbacks.
template<class T> class SceneCache {
    uint64_t lifetime_ = 0;
    std::weak_ptr<T> state_;
public:
    std::shared_ptr<T> acquire(SceneAssets& owner) {
        if (lifetime_ == owner.lifetime()) if (auto state = state_.lock()) return state;
        const auto lifetime = owner.lifetime();
        auto state = std::make_shared<T>();
        owner.retainProducerState(lifetime,state);
        lifetime_ = lifetime; state_ = state;
        return state;
    }
};
}
