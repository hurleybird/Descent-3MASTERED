// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneImages.h"
#include "../../renderer/SceneDependencies.h"
#include "../../renderer/SceneGpuCache.h"
#include "../../renderer/SceneWaterImage.h"
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
        struct CountedPixels final : ImmutablePixels {
            std::shared_ptr<int> calls;
            explicit CountedPixels(std::shared_ptr<int> count) : ImmutablePixels(2),calls(std::move(count)) {}
            std::vector<uint8_t> evaluate() const override { ++*calls; return {17,128}; }
        };
        auto calls = std::make_shared<int>(0);
        ImageSource source; source.content_revision = 1;
        source.mips = {{1,1,nullptr,2}}; source.deferred = std::make_shared<CountedPixels>(calls);
        SceneAssets assets;
        const auto held = assets.publishImage(0,source);
        check(*calls == 0 && assets.publishImage(0,source) == held,"Image publication evaluated deferred pixels");
        source.deferred = std::make_shared<CountedPixels>(calls); source.content_revision = 2;
        const auto next = assets.publishImage(0,source);
        check(next != held && *calls == 0,"New recipe aliased old pixels or evaluated during publication");
        check(next->mips()[0].width == 1 && *calls == 0,"Layout inspection evaluated deferred pixels");
        check(&next->pixels(0) == &source.deferred->bytes(),"Image copied recipe-owned pixels");
        const auto raw = next->pixels(0);
        source.deferred.reset(); source.mips[0].pixels = raw.data();
        check(assets.publishImage(0,source) == next,"Reading recipe through bitmap storage changed its identity");
        *calls = 0;
        assets.reset(); source.deferred.reset();
        check(held->pixels(0) == std::vector<uint8_t>({17,128}) && *calls == 1,"Held recipe lost its owned input");
        check(held->pixels(0)[0] == 17 && *calls == 1,"Pixel recipe evaluated more than once");
        const auto bytes = next->pixels(0);
        source.mips[0].pixels = bytes.data();
        check(assets.publishImage(0,source)->pixels(0) == bytes,"Materialized recipe changed bytes");
        source.deferred = std::make_shared<CountedPixels>(calls); source.mips[0].bytes = 4;
        rejects([&] { assets.publishImage(1,source); });
        source.mips[0].bytes = 2; source.format = ImageFormat::RGBA8;
        rejects([&] { assets.publishImage(1,source); });
    }
    {
        // Independent legacy scalar reference, including wrapped edge samples,
        // signed 32-bit gradients, shade clamping and both packed lookup halves.
        constexpr int side = 128;
        std::vector<int16_t> heights(side*side);
        std::vector<uint16_t> base(side*side);
        uint32_t random = 76543;
        const auto next = [&] { random = random*1664525u+1013904223u; return random; };
        for (size_t i = 0; i < heights.size(); ++i) { heights[i] = int16_t(next()>>16); base[i] = uint16_t(next()>>16); }
        auto shading = std::make_shared<WaterShading>();
        for (auto& row : shading->high) for (auto& value : row) value = uint16_t(next());
        for (auto& row : shading->low) for (auto& value : row) value = uint8_t(next());
        auto pixels = std::make_shared<CopiedPixels>(reinterpret_cast<const uint8_t*>(base.data()),base.size()*2);
        for (int light : {-1,0,1,4,8,15,31}) {
            WaterImage image(side,side,heights.data(),pixels,light,shading);
            std::vector<uint16_t> expected(base.size());
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                const int offset = y*side+x;
                int dx,dy;
                if (light < 0) {
                    dx = heights[offset]-heights[y*side+(x+1)%side];
                    dy = heights[offset]-heights[((y+1)%side)*side+x];
                    if (dx < 0) dx = 0;
                    if (dy < 0) dy = 0;
                } else {
                    dx = heights[y*side+(x+side-1)%side]-heights[y*side+(x+1)%side];
                    dy = heights[((y+side-1)%side)*side+x]-heights[((y+1)%side)*side+x];
                }
                auto color = base[((y+(dy>>3))&(side-1))*side+((x+(dx>>3))&(side-1))];
                if (light >= 0) {
                    const int shade = std::max(0,std::min(63,32-(dx>>light)));
                    color &= 0x7fff; color = shading->high[shade][color>>8]+shading->low[shade][color&255];
                }
                expected[offset] = color;
            }
            check(image.bytes().size() == expected.size()*2 &&
                std::memcmp(image.bytes().data(),expected.data(),image.bytes().size()) == 0,"Deferred water differs from legacy pixels");
        }
        WaterImage held(side,side,heights.data(),pixels,-1,{});
        WaterImage control(side,side,heights.data(),pixels,-1,{});
        const auto frozen = control.bytes();
        std::fill(heights.begin(),heights.end(),0); std::fill(base.begin(),base.end(),0); pixels.reset();
        check(held.bytes() == frozen,"Water image borrowed mutable source storage");
        rejects([&] { WaterImage bad(3,3,heights.data(),{},0,{}); });
    }
    {
        SceneGpuCache<ImageAsset,unsigned> gpu;
        SceneAssets source,other_source;
        uint8_t pixels[] = {1,2,3,255};
        ImageSource image; image.format = ImageFormat::RGBA8; image.mips = {{1,1,pixels,4}};
        auto first = source.publishImage(0,image);
        gpu.get(first) = 11;
        pixels[0] = 9;
        auto second = source.publishImage(0,image);
        auto other = other_source.publishImage(0,image);
        gpu.get(second) = 22; gpu.get(other) = 33;
        check(gpu.size() == 3 && gpu.get(first) == 11 && gpu.get(second) == 22 && gpu.get(other) == 33,
            "GPU cache aliased held revisions or different scene lifetimes");
        unsigned retired = 0;
        gpu.releaseExpired([&](unsigned value) { retired += value; });
        check(retired == 0,"GPU cache retired an owned asset");
        first.reset();
        gpu.releaseExpired([&](unsigned value) { retired += value; });
        check(retired == 11 && gpu.size() == 2,"GPU cache retained an unowned revision");
        gpu.clear([&](unsigned value) { retired += value; });
        check(retired == 66 && gpu.size() == 0 && gpu.get(second) == 0,
            "GPU context reset retained storage or failed to destroy each resource once");
    }
    {
        RetainedDependencies<uint64_t> dependencies;
        auto a = std::make_shared<const std::vector<uint64_t>>(std::initializer_list<uint64_t>{3,1,3});
        auto b = std::make_shared<const std::vector<uint64_t>>(std::initializer_list<uint64_t>{2,3});
        dependencies.begin(); dependencies.add(a); dependencies.add(b); dependencies.addValue(5);
        check(dependencies.finish() && dependencies.values() == std::vector<uint64_t>({1,2,3,5}),
            "Dependency union lost or duplicated entries");
        const auto* stable = dependencies.values().data();
        dependencies.begin(); dependencies.add(a); dependencies.add(b); dependencies.addValue(5);
        check(!dependencies.layoutChanged() && !dependencies.finish() && dependencies.values().data() == stable,
            "Unchanged dependency inventory rebuilt its union");
        std::weak_ptr<const std::vector<uint64_t>> held = a;
        a.reset();
        check(!held.expired(),"Dependency owner identity was not retained");
        a = std::make_shared<const std::vector<uint64_t>>(std::initializer_list<uint64_t>{1,3});
        dependencies.begin(); dependencies.add(b); dependencies.add(a); dependencies.addValue(5);
        check(dependencies.layoutChanged() && !dependencies.finish() && dependencies.values().data() == stable,
            "Equivalent replacement/reordered lists changed the dependency union");
        check(held.expired(),"Retired dependency owner leaked");
        dependencies.begin(); dependencies.add(b);
        check(dependencies.finish() && dependencies.values() == std::vector<uint64_t>({2,3}),
            "Removed table/direct dependency remained resident");
        dependencies.begin(); dependencies.add({});
        check(dependencies.finish() && dependencies.values().empty(),"Empty inventory retained dependencies");
        dependencies.clear(); dependencies.begin(); dependencies.addValue(0);
        check(dependencies.finish() && dependencies.values() == std::vector<uint64_t>({0}),
            "Dependency reset lost the empty-buffer sentinel");
        dependencies.begin(); dependencies.add(a); dependencies.addValue(7);
        // Simulate validation throwing between collection and finish.
        dependencies.begin(); dependencies.add(a); dependencies.addValue(7);
        check(dependencies.finish() && dependencies.values() == std::vector<uint64_t>({1,3,7}),
            "Interrupted dependency collection lost pending edits");
    }
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
    for (const auto format : {ImageFormat::ARGB1555,ImageFormat::ARGB4444}) {
        std::vector<uint8_t> packed(65536*2+2,0x67), expanded(65536*4+2,0x91);
        for (uint32_t i = 0; i < 65536; ++i) {
            const uint16_t pixel = uint16_t(i); std::memcpy(packed.data()+1+i*2,&pixel,2);
        }
        expandImageRgba(format,packed.data()+1,65536,expanded.data()+1);
        bool exact = expanded.front() == 0x91 && expanded.back() == 0x91;
        for (uint32_t i = 0; i < 65536; ++i) {
            const auto expected = imageRgbaPixel(format,uint16_t(i));
            exact = exact && std::memcmp(expanded.data()+1+i*4,expected.data(),4) == 0;
        }
        check(exact,"Bulk unaligned image expansion differs from scalar or overwrites guards");
    }
    {
        const uint8_t source[] = {1,2,3,4,5,6,7,8};
        uint8_t output[10] = {91,0,0,0,0,0,0,0,0,92};
        expandImageRgba(ImageFormat::RGBA8,source,2,output+1);
        check(std::memcmp(source,output+1,8) == 0 && output[0] == 91 && output[9] == 92,"Truecolor expansion changed bytes");
        expandImageRgba(ImageFormat::RGBA8,nullptr,0,nullptr);
        expandImageRgba(ImageFormat::ARGB1555,nullptr,0,nullptr);
        rejects([&] { expandImageRgba(static_cast<ImageFormat>(255),nullptr,0,nullptr); });
    }
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
    check(changed->id() != image->id() && image->pixels(0)[0] == 0,"Held image changed with mutable source");
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
    check(truecolor->pixels(0)[2] == 129 && truecolor->generatedLevels() == 5,"Truecolor image was quantized");
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
    {
        SceneAssets mesh_assets;
        MeshGeometry geometry;
        geometry.vertices = {{0,0,0},{1,0,0},{0,1,0}};
        MeshPolygon face; face.normal = {0,0,1}; face.corners = {{0},{1},{2}};
        geometry.polygons = {face,face};
        const auto opaque = mesh_assets.publishMesh(0,geometry);
        geometry.polygons.back().corners.back().alpha = 254;
        const auto translucent = mesh_assets.publishMesh(0,geometry);
        check(!opaque->hasVertexAlpha() && translucent->hasVertexAlpha(),"Mesh alpha summary lost immutable revision ownership");
        for (const auto& mesh : {opaque,translucent})
        for (int blend = 0; blend < 4; ++blend)
        for (int depth = 0; depth < 4; ++depth)
        for (int layer = 0; layer < 2; ++layer)
        for (int alpha = 0; alpha < 2; ++alpha) {
            if (layer == int(RenderLayer::Background) && depth != int(DepthMode::Automatic) &&
                depth != int(DepthMode::Disabled)) continue;
            PreparedMesh instance; instance.asset = mesh;
            instance.surface.blend = static_cast<BlendMode>(blend);
            instance.surface.depth = static_cast<DepthMode>(depth);
            instance.surface.layer = static_cast<RenderLayer>(layer);
            instance.surface.alpha = alpha ? .5f : 1.f;
            auto expected = compileMaterialPolicy(instance.surface,MeshPolygon{});
            for (const auto& polygon : mesh->geometry().polygons) {
                const auto policy = compileMaterialPolicy(instance.surface,polygon);
                if (policy.blended()) expected = policy;
            }
            FrameData input; input.sequence = 1; input.scene_lifetime = mesh_assets.lifetime();
            input.meshes.push_back(instance);
            const auto result = Presentation::publish(std::move(input));
            const auto& actual = result->data().meshes[0].surface.material;
            check(actual.blend == expected.blend && actual.depth == expected.depth &&
                actual.layer == expected.layer && actual.texture_alpha == expected.texture_alpha,
                "Asset alpha summary changed compiled material policy");
        }
    }
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
    check(first->image(44).pixels(0)[0] == 7 && second->image(44).pixels(0)[0] == 99,
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
    std::reverse(sparse.images.begin(),sparse.images.end());
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
    {
        ModelGeometry geometry; geometry.parts.resize(1);
        auto& part = geometry.parts[0];
        part.geometry = second->data().rooms[0].asset->geometry();
        part.geometry.vertex_normals.assign(part.geometry.vertices.size(),{0,0,1});
        part.polygon_materials.assign(part.geometry.polygons.size(),-1);
        part.polygon_colors.assign(part.geometry.polygons.size(),{1,1,1});
        PreparedModel model; model.asset = assets.publishModel(7,std::move(geometry));
        model.parts.resize(1); model.parts[0].surfaces = second->data().rooms[0].surfaces;
        auto model_frame = second->data(); model_frame.rooms.clear();
        ModelInstance instance; instance.identity = 7;
        model_frame.appendModel(instance,std::move(model)); model_frame.model_instances.push_back(std::move(instance));
        const auto sealed = Presentation::publish(std::move(model_frame));
        check(sealed->data().model_variants[0].parts.summary()->images->size() == 1,
            "Model summary lost its image requirements");
        auto missing_model_image = sealed->data(); missing_model_image.images.clear();
        rejects([&] { Presentation::freeze(missing_model_image); });
        auto changed_model_format = sealed->data(); changed_model_format.images = {{44,image}};
        rejects([&] { Presentation::freeze(changed_model_format); });
        const auto replay = Presentation::freeze(sealed->data());
        check(replay->image(44).id() == sealed->image(44).id(),
            "Rejected publication poisoned retained model requirements");
    }
    assets.reset();
    auto stale = second->data(); stale.scene_lifetime = assets.lifetime();
    rejects([&] { Presentation::freeze(stale); });
    check(!assets.image(42) && image->mips()[0].width == 2,"Scene reset invalidated held image");
    check(assets.publishImage(43,source)->id().lifetime != truecolor->id().lifetime,"Image slot reuse inherited old lifetime");
    return checks;
}
