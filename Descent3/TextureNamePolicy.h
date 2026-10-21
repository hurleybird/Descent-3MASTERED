// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstring>
#include <cstddef>

// Names can be replaced through the legacy page/editor ABI without a setter.
// Retain classification against the exact fixed-size name, not a slot lifetime
// or hash. No allocation, and no stale policy after a rename or slot reuse.
template<std::size_t Capacity> class TextureNamePolicy {
    char name_[Capacity]{};
    bool valid_ = false, value_ = false;
public:
    template<class Classify> bool resolve(const char (&name)[Capacity],Classify classify) {
        if (!valid_ || std::strncmp(name_,name,Capacity) != 0) {
            value_ = classify(name);
            std::memcpy(name_,name,Capacity);
            valid_ = true;
        }
        return value_;
    }
};
