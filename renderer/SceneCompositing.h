// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
#include <stdexcept>

namespace d3scene {
inline void validateForeground(const ForegroundLayer& layer) {
    if (layer.scene_views.empty() && layer.display_views.empty())
        throw std::invalid_argument("Empty foreground layer");
    if (layer.composition != ForegroundComposition::Isolated && layer.composition != ForegroundComposition::InScene)
        throw std::invalid_argument("Unknown foreground composition");
    if (layer.composition == ForegroundComposition::Isolated)
        for (const auto& view : layer.scene_views) if (view.motion_composition == View::MotionComposition::Stationary)
            throw std::invalid_argument("Stationary scene motion cannot target an isolated foreground");
    if (layer.composition == ForegroundComposition::InScene) {
        if (!layer.display_views.empty()) throw std::invalid_argument("In-scene canvas cannot complete display presentation");
        for (const auto& view : layer.scene_views) if (view.ordering != ViewOrdering::Compositing || view.clear_depth)
            throw std::invalid_argument("In-scene foreground must be a depth-independent canvas");
    }
    const auto validate = [](const View& view, ViewTarget target) {
        validateView(view);
        if (view.target != target || view.kind == ViewKind::Main || view.feedback != NoFeedback)
            throw std::invalid_argument("Foreground views cannot own world products or gameplay feedback");
    };
    for (const auto& view : layer.scene_views) validate(view,ViewTarget::Scene);
    for (const auto& view : layer.display_views) validate(view,ViewTarget::Display);
}
struct ImageReduction { uint32_t source_scale, destination_scale; };
// One reduction law for world and overlay layers. Each kernel reduces by two;
// cropping remains expressed in logical coordinates until the final consumer.
class ImageReductionSteps {
    std::array<ImageReduction,2> steps_{};
    size_t count_ = 0;
public:
    explicit ImageReductionSteps(uint32_t scale) {
        if (scale != 1 && scale != 2 && scale != 4) throw std::invalid_argument("Unsupported image reduction scale");
        for (; scale > 1; scale /= 2) steps_[count_++] = {scale,scale/2};
    }
    const ImageReduction* begin() const { return steps_.data(); }
    const ImageReduction* end() const { return begin()+count_; }
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const ImageReduction& operator[](size_t i) const { return steps_[i]; }
};
inline ImageReductionSteps imageReductionSteps(uint32_t scale) {
    return ImageReductionSteps(scale);
}
// Reference algebra for the scene MRT contract. `eligible` contains only the
// opaque surface's AO-eligible radiance after fog transmission and subsequent
// compositing. It is not alpha, fog coverage, or the final pixel brightness.
struct RadianceSample { Vec3 color, eligible; };
inline RadianceSample composeRadiance(RadianceSample destination, RadianceSample source,
    BlendMode blend, float alpha) {
    if (blend == BlendMode::Solid) return source;
    const auto channel = [&](float& color,float& eligible,float incoming) {
        if (blend == BlendMode::Alpha) { color = color*(1-alpha)+incoming*alpha; eligible *= 1-alpha; }
        else if (blend == BlendMode::Additive) color += incoming*alpha;
        else { color *= incoming; eligible *= incoming; } // source is the final fogged multiplier
    };
    channel(destination.color.x,destination.eligible.x,source.color.x);
    channel(destination.color.y,destination.eligible.y,source.color.y);
    channel(destination.color.z,destination.eligible.z,source.color.z);
    return destination;
}
inline Vec3 applyOcclusion(RadianceSample sample,float factor) {
    return {sample.color.x+(factor-1)*sample.eligible.x,
        sample.color.y+(factor-1)*sample.eligible.y,sample.color.z+(factor-1)*sample.eligible.z};
}
}
