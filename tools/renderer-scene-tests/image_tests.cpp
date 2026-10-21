// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneImages.h"
#include "../../Descent3/TextureNamePolicy.h"
#include <stdexcept>
namespace {
int checks;
void check(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Invalid image accepted");
}
}
int runSceneImageTests() {
    using namespace d3scene;
    checks = 0;
    {
    TextureNamePolicy<16> policy;
    char name[16] = "monitor";
    int classifications = 0;
    const auto classify = [&](const char* value) { ++classifications; return std::strcmp(value,"monitor") == 0; };
    check(policy.resolve(name,classify) && policy.resolve(name,classify) && classifications == 1,
        "Stable texture name was repeatedly classified");
    std::strcpy(name,"wall");
    check(!policy.resolve(name,classify) && classifications == 2,"Renamed texture retained old policy");
    std::strcpy(name,"monitor");
    check(policy.resolve(name,classify) && classifications == 3,"Reused texture slot retained old policy");
    name[10] = 'x';
    check(policy.resolve(name,classify) && classifications == 3,"Unused name padding changed policy identity");
    }
    bool packed_matches = true;
    for (uint32_t pixel = 0; pixel < 65536; ++pixel) {
        const auto expand = [](uint32_t v,float maximum) { return static_cast<uint8_t>(255*(v/maximum)); };
        const std::array<uint8_t,4> rgba1555 = (pixel & 0x8000) ? std::array<uint8_t,4>{{expand((pixel>>10)&31,31),expand((pixel>>5)&31,31),expand(pixel&31,31),255}} : std::array<uint8_t,4>{{0,0,0,0}};
        const std::array<uint8_t,4> rgba4444{{expand((pixel>>8)&15,15),expand((pixel>>4)&15,15),expand(pixel&15,15),expand(pixel>>12,15)}};
        packed_matches = packed_matches && imageRgbaPixel(ImageFormat::ARGB1555,static_cast<uint16_t>(pixel)) == rgba1555 &&
            imageRgbaPixel(ImageFormat::ARGB4444,static_cast<uint16_t>(pixel)) == rgba4444;
    }
    check(packed_matches,"Image conversion changed authored color/alpha masks");
    SceneAssets assets;
    uint16_t pixels[] = {0x8000,0xffff,0xf800,0x83e0};
    ImageSource source; source.mips = {{2,2,reinterpret_cast<const uint8_t*>(pixels),sizeof(pixels)}};
    auto image = assets.publishImage(42,source);
    check(assets.publishImage(42,source) == image,"Unchanged pixels created new image revision");
    check(!assets.image(42,1),"Unversioned image satisfied a producer revision");
    source.content_revision = 1;
    check(assets.publishImage(42,source) == image && assets.image(42,1) == image,
        "Producer revision did not retain unchanged immutable pixels");
    check(!assets.image(42,2) && !assets.image(42,0),"Mismatched producer revision reused an image");
    source.content_revision = 2;
    check(assets.publishImage(42,source) == image && assets.image(42,2) == image && !assets.image(42,1),
        "Unchanged content did not advance producer revision independently of asset revision");
    pixels[0] = 0xffff;
    source.content_revision = 3;
    auto changed = assets.publishImage(42,source);
    check(changed->id() != image->id() && image->mips()[0].pixels[0] == 0,"Held image changed with mutable source");
    uint16_t mip = 0xffff;
    source.mips.push_back({1,1,reinterpret_cast<const uint8_t*>(&mip),2});
    check(assets.publishImage(42,source)->mips().size() == 2,"Authored mip chain lost");
    source.mips[1].width = 2;
    rejects([&] { assets.publishImage(42,source); });
    source.mips.resize(1); source.mips[0].bytes = 6;
    rejects([&] { assets.publishImage(42,source); });
    source.mips[0].bytes = 8; source.padded_extent = 1;
    rejects([&] { assets.publishImage(42,source); });
    source.padded_extent = 4;
    check(assets.publishImage(42,source)->paddedExtent() == 4,"Lightmap storage extent lost");
    source.padded_extent = 0; source.generated_levels = 5;
    rejects([&] { assets.publishImage(42,source); });
    uint8_t rgba[] = {7,23,129,42};
    source.format = ImageFormat::RGBA8; source.mips = {{1,1,rgba,4}};
    auto truecolor = assets.publishImage(43,source);
    check(truecolor->mips()[0].pixels[2] == 129 && truecolor->generatedLevels() == 5,"Truecolor image was quantized");
    SurfaceBinding surface;
    MeshPolygon polygon; polygon.corners.resize(3);
    auto policy = compileMaterialPolicy(surface,polygon);
    check(!policy.blended() && policy.writesSolidDepth() && !policy.allowsBehind(),"Opaque material policy");
    surface.texture_alpha = true; surface.color_image = image->id().slot+1; surface.color_format = image->format();
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.texture_alpha == TextureAlpha::Cutout && policy.writesSolidDepth() && policy.allowsBehind(),"Cutout lost solid-depth participation");
    surface.color_image = truecolor->id().slot+1; surface.color_format = truecolor->format();
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.texture_alpha == TextureAlpha::Continuous && policy.blend == BlendMode::Alpha && !policy.writesSolidDepth(),"Fractional texture alpha classified opaque");
    surface.texture_alpha = false;
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.texture_alpha == TextureAlpha::Ignore && !policy.blended(),"Ignored texture alpha changed pass");
    surface.blend = BlendMode::Additive;
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.blend == BlendMode::Additive && policy.texture_alpha == TextureAlpha::Continuous && !policy.writesSolidDepth(),"Additive texture alpha/depth policy");
    surface.blend = BlendMode::Multiply;
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.blend == BlendMode::Multiply && policy.blended() && !policy.writesSolidDepth(),"Multiply material lost compositing/depth semantics");
    surface.blend = BlendMode::Alpha; surface.color_image = 0;
    check(compileMaterialPolicy(surface,polygon).blended(),"Explicit alpha compositor became a depth-writing opaque material");
    surface.blend = BlendMode::Solid; surface.alpha = .9999f;
    check(compileMaterialPolicy(surface,polygon).blended(),"Almost-opaque alpha incorrectly wrote solid depth");
    surface.alpha = 1; polygon.corners[0].alpha = 254;
    check(compileMaterialPolicy(surface,polygon).blended(),"Vertex alpha absent from material policy");
    surface.depth = DepthMode::ReadWrite;
    policy = compileMaterialPolicy(surface,polygon);
    check(policy.blended() && policy.testsDepth() && policy.writesDepth() && !policy.writesSolidDepth() && policy.requiresLatePass(),
        "Late transparent depth writes contaminated solid depth or lost self-occlusion");
    polygon.corners[0].alpha = 255; surface.depth = DepthMode::ReadOnly;
    policy = compileMaterialPolicy(surface,polygon);
    check(!policy.blended() && policy.testsDepth() && !policy.writesDepth() && policy.requiresLatePass(),
        "Read-only opaque overlay was scheduled as an opaque depth writer");
    surface.depth = DepthMode::Disabled;
    policy = compileMaterialPolicy(surface,polygon);
    check(!policy.blended() && !policy.testsDepth() && !policy.writesDepth() && policy.requiresLatePass(),
        "Foreground no-depth policy was inferred from color blending");
    // Pixel revisions belong to frames, not to retained surface storage.
    FrameData frame;
    frame.sequence = 1; frame.scene_lifetime = assets.lifetime(); frame.images = {{44,truecolor}};
    MeshGeometry triangle;
    triangle.vertices = {{0,0,0},{1,0,0},{0,1,0}};
    MeshPolygon face; face.normal = {0,0,1}; face.corners = {{0},{1},{2}};
    triangle.polygons.push_back(face);
    RoomState room; room.number = 0; room.asset = assets.publishRoom(0,std::move(triangle));
    room.surfaces.resize(1); room.surfaces[0].color_image = 44;
    room.surfaces[0].color_format = truecolor->format(); room.surfaces[0].bitmap = 43;
    frame.rooms.push_back(std::move(room));
    const auto first = Presentation::freeze(frame);
    rgba[0] = 99;
    const auto updated_pixels = assets.publishImage(43,source);
    auto next = first->data(); next.sequence = 2; next.images = {{44,updated_pixels}};
    const auto second = Presentation::publish(std::move(next));
    check(&first->data().rooms[0].surfaces.read() == &second->data().rooms[0].surfaces.read(),
        "Pixel update detached stable surface storage");
    check(first->image(44).mips()[0].pixels[0] == 7 && second->image(44).mips()[0].pixels[0] == 99,
        "Frame image revisions did not preserve held-frame isolation");
    auto missing = second->data(); missing.images.clear();
    rejects([&] { Presentation::freeze(missing); });
    auto duplicate = second->data(); duplicate.images.push_back({44,updated_pixels});
    rejects([&] { Presentation::freeze(duplicate); });
    rejects([&] { first->image(999); });
    FrameData sparse; sparse.sequence = 1; sparse.scene_lifetime = assets.lifetime();
    const auto empty_frame = Presentation::freeze(sparse);
    rejects([&] { empty_frame->image(1); });
    rejects([&] { empty_frame->image(0); });
    for (uint32_t i = 1; i <= 256; ++i) sparse.images.push_back({i*104729u,updated_pixels});
    sparse.images.push_back({UINT32_MAX,truecolor});
    const auto sparse_frame = Presentation::freeze(sparse);
    for (const auto& binding : sparse.images)
        check(&sparse_frame->image(binding.binding) == binding.asset.get(),"Sparse frame image lookup lost a collision or high binding");
    rejects([&] { sparse_frame->image(2); });
    auto animated = second->data();
    const auto next_image_slot = assets.publishImage(45,source);
    animated.images = {{44,next_image_slot}};
    const auto animation_frame = Presentation::freeze(animated);
    check(animation_frame->image(44).id().slot == 45 &&
        &animation_frame->data().rooms[0].surfaces.read() == &second->data().rooms[0].surfaces.read(),
        "Image sequence advancement changed stable material storage");
    auto wrong_format = animated;
    wrong_format.rooms[0].surfaces[0].color_format = ImageFormat::ARGB4444;
    rejects([&] { Presentation::freeze(wrong_format); });
    assets.reset();
    auto stale = second->data(); stale.scene_lifetime = assets.lifetime();
    rejects([&] { Presentation::freeze(stale); });
    check(!assets.image(42) && image->mips()[0].width == 2,"Scene reset invalidated held image");
    check(assets.publishImage(43,source)->id().lifetime != truecolor->id().lifetime,"Image slot reuse inherited old lifetime");
    return checks;
}
