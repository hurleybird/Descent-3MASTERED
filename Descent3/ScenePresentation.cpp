// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "SceneReflectionBake.h"
#include "SceneCanvas.h"
#include "SceneModuleContext.h"
#include "SceneFeedback.h"
#include "../renderer/SceneGeometry.h"
#include "../renderer/SceneDependencies.h"
#include "../renderer/SceneView.h"
#include "grtext.h"
#include "gamefont.h"
#include "../renderer/SceneLighting.h"
#include "../renderer/SceneImages.h"
#include "../renderer/SceneTerrain.h"
#include "../renderer/SceneFog.h"
#include "config.h"
#include "descent.h"
#include "bitmap.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include "game.h"
#include "gameloop.h"
#include "gametexture.h"
#include "room.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "args.h"
#include "renderer.h"
#include "pserror.h"
#include "polymodel.h"
#include "door.h"
#include "doorway.h"
#include "psrand.h"
#include "damage.h"
#include "player.h"
#include "special_face.h"
#include "terrain.h"

bool SceneGeometryInspection() { return FindArg("-scene-inspection") != 0; }


d3scene::OcclusionClass SceneRoomOcclusionClass(int texture, bool exterior) {
    if (exterior) return d3scene::OcclusionClass::Default;
    // Compatibility interpretation of legacy content, resolved once upstream.
    // The GPU receives a semantic class and never guesses from geometry type.
    if (texture >= 0 && texture < MAX_TEXTURES && GameTextures[texture].used) {
        const auto& source = GameTextures[texture];
        if (source.flags & TF_RUBBLE) return d3scene::OcclusionClass::MineRock;
        for (const char* name = source.name; *name; ++name)
            if (!strnicmp(name,"rock",4) || !strnicmp(name,"rubble",6))
                return d3scene::OcclusionClass::MineRock;
    }
    return d3scene::OcclusionClass::Mine;
}

namespace {
bool roomMaterialInputsUnchanged() {
    static std::vector<texture> previous;
    static std::array<float,15> settings{};
    static uint64_t reflection_revision = 0;
    static std::array<int,MAX_BITMAPS> bitmap_formats{};
    static uint64_t bitmap_material_revision = 0;
    const std::array<float,15> current{{float(Detail_settings.Specular_lighting),float(Detail_settings.Mirrored_surfaces),
        float(Render_preferred_state.per_pixel_lighting),float(Render_split_specular_textures),float(Render_per_pixel_field_static_specular),
        Render_per_pixel_specular_sharpness,Render_per_pixel_specular_strength,Render_per_pixel_static_specular_strength,
        Render_per_pixel_specular_lightmap_mix,Render_per_pixel_specular_alpha_strength,float(UseHardware),float(Render_hires_textures),
        Terrain_sky.satellite_vectors[0].x,Terrain_sky.satellite_vectors[0].y,Terrain_sky.satellite_vectors[0].z}};
    bool same = settings == current && previous.size() == MAX_TEXTURES;
    const auto revision = SceneReflectionBakeRevision();
    same = same && reflection_revision == revision;
    reflection_revision = revision;
    // Pixel revisions are resolved by prepareImages. A format change also
    // changes alpha/specular material policy, so it must recompile appearance.
    const auto bitmap_revision = bm_MaterialRevision();
    const bool bitmap_metadata_current = bitmap_material_revision == bitmap_revision;
    const bool audit_bitmaps = FindArg("-scene-image-revision-audit") != 0;
    bool bitmap_metadata_equal = true;
    if (!bitmap_metadata_current || audit_bitmaps) for (int i = 0; i < MAX_BITMAPS; ++i) {
        const int format = GameBitmaps[i].used ? int(GameBitmaps[i].format)+1 : 0;
        if (bitmap_formats[i] != format) { bitmap_metadata_equal = false; bitmap_formats[i] = format; }
    }
    if (bitmap_metadata_current && !bitmap_metadata_equal && audit_bitmaps)
        throw std::logic_error("Bitmap format/lifetime changed without producer invalidation");
    bitmap_material_revision = bitmap_revision;
    same = same && bitmap_metadata_equal;
    if (previous.size() != MAX_TEXTURES) previous.resize(MAX_TEXTURES);
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        const auto& a = GameTextures[i]; auto& b = previous[i];
        const bool equal = a.used == b.used && (!a.used ||
            (std::tie(a.flags,a.alpha,a.reflectivity,a.slide_u,a.slide_v,a.destroy_handle,a.bm_handle,a.speed) ==
             std::tie(b.flags,b.alpha,b.reflectivity,b.slide_u,b.slide_v,b.destroy_handle,b.bm_handle,b.speed) &&
             std::memcmp(a.name,b.name,sizeof(a.name)) == 0));
        same = same && equal;
        if (!equal) b = a;
    }
    settings = current;
    struct ReflectionInput { unsigned flags=0,type=0,used=0; std::vector<specular_instance> sources; std::vector<vector> normals; };
    static std::vector<ReflectionInput> reflection_inputs(MAX_SPECIAL_FACES);
    static std::vector<int> lightmap_handles(MAX_LIGHTMAP_INFOS,-2);
    static uint64_t special_revision = 0, lightmap_revision = 0, geometry_revision = 0, metadata_lifetime = 0;
    const bool metadata_current = special_revision == SpecialFaceSourceRevision() &&
        lightmap_revision == LightmapInfoSourceRevision() && geometry_revision == GameSceneGeometryRevision() &&
        metadata_lifetime == d3scene::assets().lifetime();
    const bool audit_metadata = FindArg("-scene-geometry-revision-audit") != 0;
    bool metadata_equal = true;
    if (!metadata_current || audit_metadata) {
    for (int room = 0; room < MAX_ROOMS; ++room) if (Rooms[room].used) {
        const auto& source_room = Rooms[room];
        for (int face = 0; face < source_room.num_faces; ++face) {
            const auto& source_face = source_room.faces[face];
            if ((source_face.flags & FF_LIGHTMAP) && source_face.lmi_handle >= 0 && source_face.lmi_handle < MAX_LIGHTMAP_INFOS) {
                auto& previous_handle = lightmap_handles[source_face.lmi_handle];
                const int current_handle = LightmapInfo[source_face.lmi_handle].lm_handle;
                if (previous_handle != current_handle) { metadata_equal = false; previous_handle = current_handle; }
            }
            const int index = source_face.special_handle;
            if (index < 0 || index >= MAX_SPECIAL_FACES) continue;
            const auto& input = SpecialFaces[index]; auto& old = reflection_inputs[index];
            const size_t count = input.used && input.spec_instance ? input.num : 0;
            const size_t normals = input.used && (input.flags & SFF_SPEC_SMOOTH) && input.vertnorms ? source_face.num_verts : 0;
            const unsigned authored_flags = input.flags & SFF_SPEC_SMOOTH;
            bool equal = old.flags == authored_flags && old.type == input.type && old.used == input.used &&
                old.sources.size() == count && old.normals.size() == normals;
            for (size_t i = 0; equal && i < count; ++i) {
                const auto& a = input.spec_instance[i]; const auto& b = old.sources[i];
                equal = std::tie(a.bright_center.x,a.bright_center.y,a.bright_center.z,a.bright_color) ==
                    std::tie(b.bright_center.x,b.bright_center.y,b.bright_center.z,b.bright_color);
            }
            for (size_t i = 0; equal && i < normals; ++i) {
                const auto& a = input.vertnorms[i]; const auto& b = old.normals[i];
                equal = a.x == b.x && a.y == b.y && a.z == b.z;
            }
            if (!equal) {
                metadata_equal = false; old.flags = authored_flags; old.type = input.type; old.used = input.used;
                old.sources.resize(count); old.normals.resize(normals);
                for (size_t i = 0; i < count; ++i) old.sources[i] = input.spec_instance[i];
                for (size_t i = 0; i < normals; ++i) old.normals[i] = input.vertnorms[i];
            }
        }
    }
    if (metadata_current && !metadata_equal && audit_metadata)
        throw std::logic_error("Reflection/lightmap metadata changed without producer invalidation");
    special_revision = SpecialFaceSourceRevision(); lightmap_revision = LightmapInfoSourceRevision();
    geometry_revision = GameSceneGeometryRevision(); metadata_lifetime = d3scene::assets().lifetime();
    }
    same = same && metadata_equal;
    return same;
}
// Texture animation and procedural evaluation are presentation inputs, not
// per-face work. Resolve once per texture for this preparation transaction.
// Nothing survives the transaction, so edits/reloads/settings cannot go stale.
struct MaterialResolution {
    explicit MaterialResolution(float value) : time(value) { bitmaps.fill(-2); occlusion.fill(-1); }
    float time;
    std::array<int,MAX_TEXTURES> bitmaps, occlusion;
    int bitmap(int texture) {
        auto& result = bitmaps[texture];
        if (result == -2) {
            if (GameTextures[texture].flags & TF_PROCEDURAL) {
                PERF_MARKER_SCOPE("Scene.Resources.Procedural");
                result = GetTextureBitmapAtTime(texture,0,time);
            } else result = GetTextureBitmapAtTime(texture,0,time);
        }
        return result;
    }
    d3scene::OcclusionClass roomOcclusion(int texture,bool exterior) {
        if (exterior || texture < 0 || texture >= MAX_TEXTURES) return SceneRoomOcclusionClass(texture,exterior);
        auto& result = occlusion[texture];
        if (result < 0) result = static_cast<int>(SceneRoomOcclusionClass(texture,false));
        return static_cast<d3scene::OcclusionClass>(result);
    }
};
void resolveMaterial(d3scene::SurfaceBinding&,MaterialResolution&,bool model,bool exterior);
void prepareImages(d3scene::FrameData& data,MaterialResolution& resolution) {
    // Source handles and resource names occupy bounded registries. Reuse index
    // scratch, not thousands of per-frame hash nodes/reference-counted entries.
    static thread_local std::array<int,MAX_BITMAPS+MAX_LIGHTMAPS> pixels;
    static thread_local std::array<int,MAX_BITMAPS+MAX_LIGHTMAPS+MAX_TEXTURES+1> bindings;
    static thread_local size_t previous_binding_count = 0;
    pixels.fill(-1); bindings.fill(-1);
    data.images.reserve(previous_binding_count);
    const bool audit_images = FindArg("-scene-image-revision-audit") != 0;
    size_t image_misses = 0, image_requests = 0;
    const auto image = [&](int handle,bool baked) -> d3scene::ImageAssetRef {
        ++image_requests;
        const int slot = handle+(baked ? MAX_BITMAPS : 0);
        const auto revision = baked ? lm_ContentRevision(handle) : bm_ContentRevision(handle);
        const auto resident = d3scene::assets().image(slot,revision);
        if (resident && !audit_images) {
            return resident;
        }
        PERF_MARKER_SCOPE("Scene.Resources.PublishPixels");
        if (!resident) ++image_misses;
        d3scene::ImageSource source;
        source.content_revision = revision;
        if (baked) {
            const auto& lightmap = GameLightmaps[handle];
            source.padded_extent = lightmap.square_res;
            source.mips.push_back({lightmap.width,lightmap.height,reinterpret_cast<const uint8_t*>(lm_data(handle)),
                size_t(lightmap.width)*lightmap.height*2});
        } else {
            const int width = bm_w(handle,0), height = bm_h(handle,0); // make resident before borrowing data
            source.deferred = bm_DeferredPixels(handle);
            const auto* rgba = bm_data_truecolor(handle);
            if (source.deferred) {
                source.format = bm_format(handle) == BITMAP_FORMAT_4444 ? d3scene::ImageFormat::ARGB4444 : d3scene::ImageFormat::ARGB1555;
                source.mips.push_back({uint32_t(width),uint32_t(height),nullptr,size_t(width)*height*2});
            } else if (rgba) {
                source.format = d3scene::ImageFormat::RGBA8;
                source.mips.push_back({static_cast<uint32_t>(width),static_cast<uint32_t>(height),rgba,size_t(width)*height*4});
                if (bm_mipped(handle)) source.generated_levels = NUM_MIP_LEVELS;
            } else {
                source.format = bm_format(handle) == BITMAP_FORMAT_4444 ? d3scene::ImageFormat::ARGB4444 : d3scene::ImageFormat::ARGB1555;
                const int levels = bm_mipped(handle) ? NUM_MIP_LEVELS : 1;
                for (int mip = 0; mip < levels; ++mip) {
                    const int w = bm_w(handle,mip), h = bm_h(handle,mip);
                    if (w < 1 || h < 1) break;
                    source.mips.push_back({static_cast<uint32_t>(w),static_cast<uint32_t>(h),
                        reinterpret_cast<const uint8_t*>(bm_data(handle,mip)),size_t(w)*h*2});
                }
            }
        }
        auto asset = d3scene::assets().publishImage(slot,source);
        if (resident && asset != resident)
            Error("Image content changed without a producer revision: slot %d",slot);
        return asset;
    };
    // The frame owns images; dependency lookups borrow the immutable asset.
    // Avoid an atomic reference-count pair for every face-table dependency.
    struct ImageBindingView { uint32_t binding = 0; const d3scene::ImageAsset* asset = nullptr; };
    const auto sample = [&](int handle,bool baked,int texture = -1) -> ImageBindingView {
        const uint32_t key = texture >= 0 ? uint32_t(MAX_BITMAPS+MAX_LIGHTMAPS+texture+1) : uint32_t(handle+(baked ? MAX_BITMAPS : 0)+1);
        if (key >= bindings.size()) Error("Image resource outside source registry");
        if (bindings[key] >= 0) return {key,data.images[bindings[key]].asset.get()};
        const int bitmap = texture >= 0 ? resolution.bitmap(texture) : handle;
        const int pixel_slot = bitmap+(baked ? MAX_BITMAPS : 0);
        if (bitmap < 0 || pixel_slot >= int(pixels.size())) Error("Image pixels outside source registry");
        auto asset = pixels[pixel_slot] >= 0 ? data.images[pixels[pixel_slot]].asset : image(bitmap,baked);
        bindings[key] = pixels[pixel_slot] = int(data.images.size()); data.images.push_back({key,std::move(asset)});
        return {key,data.images.back().asset.get()};
    };
    const auto prepare = [&](d3scene::SurfaceBinding& surface) {
        if (surface.bitmap >= 0) {
            const auto resource = sample(surface.bitmap,false,surface.color_texture);
            surface.color_image = resource.binding; surface.color_format = resource.asset->format();
        }
        if (surface.specular_bitmap >= 0) {
            const auto resource = sample(surface.specular_bitmap,false,surface.specular_texture);
            surface.specular_image = resource.binding; surface.specular_format = resource.asset->format();
        }
        if (surface.lightmap_bitmap >= 0) {
            surface.baked_image = sample(surface.lightmap_bitmap,true).binding;
            surface.lightmap_wrap = (GameLightmaps[surface.lightmap_bitmap].flags & LF_WRAP) != 0;
        }
    };
    const auto dependencyUnchanged = [&](const d3scene::RoomSurfaces::ImageDependency& dependency) {
                const bool wrap = dependency.baked && (GameLightmaps[dependency.handle].flags & LF_WRAP);
                const auto resource = sample(dependency.handle,dependency.baked,dependency.texture);
                if (resource.binding != dependency.binding || wrap != dependency.wrap ||
                    (dependency.classify_alpha && resource.asset->format() != dependency.format)) return false;
                if (dependency.baked) {
                    const auto& lm = GameLightmaps[dependency.handle];
                    if (lm.square_res && (dependency.u_scale != float(lm.width)/lm.square_res ||
                        dependency.v_scale != float(lm.height)/lm.square_res)) return false;
                }
        return true;
    };
    const auto tableUnchanged = [&](const d3scene::RoomSurfaces& table) {
        if (!table.published()) return false;
        bool unchanged = true;
        for (const auto& dependency : table.images()) unchanged = dependencyUnchanged(dependency) && unchanged;
        return unchanged;
    };
    const auto prepareTable = [&](d3scene::RoomSurfaces& table,bool model,bool exterior) {
        // Pixel/frame changes never detach storage. A format transition may
        // change cutout/specular policy, so it explicitly recompiles appearance.
        for (size_t face = 0; face < table.size(); ++face) {
            const auto& input = table.read()[face];
            if ((input.bitmap >= 0 && input.color_texture >= 0 &&
                sample(input.bitmap,false,input.color_texture).asset->format() != input.color_format) ||
                (input.specular_bitmap >= 0 && input.specular_texture >= 0 &&
                sample(input.specular_bitmap,false,input.specular_texture).asset->format() != input.specular_format)) {
                auto& changed = table[face];
                changed.alpha = changed.authored_alpha;
                changed.specular_strength = 0; changed.specular_bitmap = -1; changed.specular_texture = -1;
                resolveMaterial(changed,resolution,model,exterior);
            }
            const auto& surface = table.read()[face];
            const auto color = surface.bitmap >= 0 ? sample(surface.bitmap,false,surface.color_texture) : ImageBindingView{};
            const auto specular = surface.specular_bitmap >= 0 ? sample(surface.specular_bitmap,false,surface.specular_texture) : ImageBindingView{};
            const auto baked = surface.lightmap_bitmap >= 0 ? sample(surface.lightmap_bitmap,true) : ImageBindingView{};
            const bool wrap = surface.lightmap_bitmap >= 0 && (GameLightmaps[surface.lightmap_bitmap].flags & LF_WRAP);
            const uint32_t color_slot = color.binding;
            const uint32_t specular_slot = specular.binding;
            const uint32_t baked_slot = baked.binding;
            const auto format = color.asset ? color.asset->format() : d3scene::ImageFormat::ARGB1555;
            const auto specular_format = specular.asset ? specular.asset->format() : d3scene::ImageFormat::ARGB1555;
            const auto* lm = surface.lightmap_bitmap >= 0 ? &GameLightmaps[surface.lightmap_bitmap] : nullptr;
            const bool resized = lm && lm->square_res && (surface.lightmap_u_scale != float(lm->width)/lm->square_res ||
                surface.lightmap_v_scale != float(lm->height)/lm->square_res);
            if (surface.color_image != color_slot || surface.specular_image != specular_slot || surface.baked_image != baked_slot ||
                surface.color_format != format || surface.specular_format != specular_format || surface.lightmap_wrap != wrap || resized) {
                auto& changed = table[face]; changed.color_image = color_slot; changed.specular_image = specular_slot;
                changed.baked_image = baked_slot; changed.color_format = format; changed.lightmap_wrap = wrap;
                changed.specular_format = specular_format;
                if (changed.lightmap_bitmap >= 0 && GameLightmaps[changed.lightmap_bitmap].square_res) {
                    const auto& lightmap = GameLightmaps[changed.lightmap_bitmap];
                    changed.lightmap_u_scale = float(lightmap.width)/lightmap.square_res;
                    changed.lightmap_v_scale = float(lightmap.height)/lightmap.square_res;
                }
            }
        }
    };
    // Retain the union of immutable dependency lists, not last frame's images.
    // Owning the lists prevents address reuse from masquerading as identity.
    // Unpublished/edited tables are prepared independently below.
    using Dependency = d3scene::RoomSurfaces::ImageDependency;
    const auto order = [](const Dependency& a,const Dependency& b) {
        const auto key = [](const Dependency& d) {
            return std::make_tuple(d.binding,d.handle,d.texture,d.baked,d.format,d.wrap,d.u_scale,d.v_scale,d.classify_alpha);
        };
        return key(a)<key(b);
    };
    static thread_local d3scene::RetainedDependencies<Dependency,decltype(order)> dependency_set(order);
    static thread_local uint64_t dependency_lifetime = 0;
    size_t table_count = 0;
    if (dependency_lifetime != data.scene_lifetime) { dependency_set.clear(); dependency_lifetime = data.scene_lifetime; }
    dependency_set.begin();
    const auto add_table = [&](const d3scene::RoomSurfaces& table) {
        if (table.published()) dependency_set.add(table.imageDependencies());
        ++table_count;
    };
    for (const auto& room : data.rooms) add_table(room.surfaces);
    for (const auto& model : data.model_variants) {
        if (const auto* summary = model.parts.summary()) {
            dependency_set.add(summary->images); table_count += model.parts.size();
        } else for (const auto& part : model.parts) add_table(part.surfaces);
    }
    const bool layout_matches = !dependency_set.layoutChanged();
    bool dependencies_current = true;
    { PERF_MARKER_SCOPE("Scene.Resources.DependencyLayout");
    if (!layout_matches) {
        PERF_MARKER_SCOPE("Scene.Resources.RebuildDependencies");
        dependency_set.finish();
    }
    for (const auto& dependency : dependency_set.values()) dependencies_current = dependencyUnchanged(dependency) && dependencies_current;
    }
    { PERF_MARKER_SCOPE("Scene.Resources.RoomImages");
    for (auto& room : data.rooms) if (!room.surfaces.published() || (!dependencies_current && !tableUnchanged(room.surfaces)))
        prepareTable(room.surfaces,false,room.exterior);
    }
    { PERF_MARKER_SCOPE("Scene.Resources.ModelImages");
    for (auto& model : data.model_variants) {
      if (dependencies_current && model.parts.summary()) continue;
      for (size_t p = 0; p < model.parts.size(); ++p)
        if (!model.parts.read()[p].surfaces.published() || (!dependencies_current && !tableUnchanged(model.parts.read()[p].surfaces)))
            prepareTable(model.parts[p].surfaces,true,false);
    }
    }
    { PERF_MARKER_SCOPE("Scene.Resources.OtherImages");
    for (auto& surface : data.terrain.materials) prepare(surface);
    for (auto& mesh : data.meshes) prepare(mesh.surface);
    }
    previous_binding_count = data.images.size();
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene frame resources bindings=%u pixel_sources=%u revised_sources=%u dependency_tables=%u unique_dependencies=%u layout_rebuilt=%d",
            unsigned(data.images.size()),unsigned(image_requests),unsigned(image_misses),unsigned(table_count),unsigned(dependency_set.values().size()),!layout_matches);
}
// Field-wise fingerprints diagnose state-vs-pixel variation; never hash padding
// or use diagnostic hashes as asset identities.
void logPreparedModels(const d3scene::Presentation& frame) {
    const auto& data = frame.data();
    if (!AutomatedCapturePending() && AutomatedCaptureFrameIndex() != 0) return;
    AutomatedCaptureLog("scene probe frame=%d time=%.9g",AutomatedCaptureFrameIndex(),data.simulation_time);
    size_t room_surfaces = 0, model_surfaces = 0, terrain_surfaces = 0, retained_surfaces = 0;
    for (const auto& room : data.rooms) { room_surfaces += room.surfaces.size();
        if (room.surfaces.publishedFor(room.asset->id())) retained_surfaces += room.surfaces.size(); }
    for (const auto& model : data.model_variants)
        for (const auto& part : model.parts) model_surfaces += part.surfaces.size();
    if (data.terrain.asset) for (const auto& patch : data.terrain.asset->patches()) terrain_surfaces += patch.batches.size();
    AutomatedCaptureLog("scene inventory rooms=%u models=%u terrain=%u meshes=%u",unsigned(room_surfaces),unsigned(model_surfaces),unsigned(terrain_surfaces),unsigned(data.meshes.size()));
    AutomatedCaptureLog("scene retained room surfaces=%u",unsigned(retained_surfaces));
    std::unordered_set<const d3scene::ModelParts::Summary*> material_tables;
    size_t unique_model_surfaces = 0;
    for (const auto& model : data.model_variants)
        if (const auto* table = model.parts.summary())
            if (material_tables.insert(table).second) unique_model_surfaces += table->surface_count;
    AutomatedCaptureLog("scene model materials variants=%u unique_tables=%u unique_surfaces=%u",
        unsigned(data.model_variants.size()),unsigned(material_tables.size()),unsigned(unique_model_surfaces));
    unsigned reflection_surfaces = 0, reflection_sources = 0, smooth_reflections = 0;
    for (const auto& room : data.rooms) for (const auto& surface : room.surfaces) if (surface.specular_strength > 0 && surface.reflection) {
        ++reflection_surfaces;
        reflection_sources += static_cast<unsigned>(surface.reflection->data().sources.size());
        smooth_reflections += !surface.reflection->data().corner_normals.empty();
    }
    AutomatedCaptureLog("scene authored reflections surfaces=%u sources=%u smooth=%u",reflection_surfaces,reflection_sources,smooth_reflections);
    size_t fog_triangles = 0;
    for (const auto& volume : d3scene::fogVolumes(data)) fog_triangles += volume.boundary.size();
    AutomatedCaptureLog("scene fog volumes=%u triangles=%u exterior=%d",static_cast<unsigned>(d3scene::fogVolumes(data).size()),
        static_cast<unsigned>(fog_triangles),data.exterior_fog.enabled ? 1 : 0);
    for (const auto& effect : data.camera_perturbations)
        AutomatedCaptureLog("scene camera perturbation owner=%u forward=%.9g,%.9g,%.9g",effect.owner,
            effect.rotation.forward.x,effect.rotation.forward.y,effect.rotation.forward.z);
    std::unordered_set<int> textures;
    for (const auto& instance : data.model_instances) for (const auto& source : data.variants(instance)) {
        const auto& model = frame.resolvedModel(size_t(&instance-data.model_instances.data()),size_t(&source-data.variants(instance).data()));
        uint64_t hash = 14695981039346656037ull;
        const auto number = [&](float value) {
            uint32_t bits; std::memcpy(&bits,&value,sizeof(bits));
            for (int byte = 0; byte < 4; ++byte) { hash ^= (bits>>(byte*8))&255; hash *= 1099511628211ull; }
        };
        const auto vector = [&](d3scene::Vec3 v) { number(v.x); number(v.y); number(v.z); };
        const auto transform = [&](const d3scene::Transform& t) { vector(t.position); vector(t.right); vector(t.up); vector(t.forward); };
        transform(instance.current);
        for (const auto& part : model.parts) {
            transform(part.current);
            for (const auto& surface : part.surfaces) if (surface.texture >= 0) textures.insert(surface.texture);
        }
        const auto owner = std::find_if(data.objects.begin(),data.objects.end(),
            [&](const d3scene::ObjectState& object) { return instance.has_owner && object.handle == instance.owner; });
        const unsigned type = owner == data.objects.end() ? OBJ_NONE : owner->type;
        AutomatedCaptureLog("scene pose object=%u model=%u name=%s type=%u hash=%llu position=%.9g,%.9g,%.9g",
            instance.owner,model.asset->id().slot,Poly_models[model.asset->id().slot].name,type,hash,
            instance.current.position.x,instance.current.position.y,instance.current.position.z);
        if (type == OBJ_PLAYER)
            AutomatedCaptureLog("scene player direction=%.9g,%.9g,%.9g",
                instance.current.forward.x,instance.current.forward.y,instance.current.forward.z);
    }
    for (int texture : textures) {
        // Resolve was already performed. Reading the source bitmap here must not
        // call GetTextureBitmap again (which can advance procedural state).
        const d3scene::SurfaceBinding* binding = nullptr;
        for (const auto& model : data.model_variants) for (const auto& part : model.parts)
            for (const auto& surface : part.surfaces) if (surface.texture == texture) binding = &surface;
        if (!binding || binding->bitmap < 0) continue;
        const int bitmap = binding->bitmap;
        const auto* pixels = bm_data(bitmap,0);
        uint64_t hash = 14695981039346656037ull;
        const int count = bm_w(bitmap,0)*bm_h(bitmap,0);
        for (int i = 0; i < count; ++i) { hash ^= pixels[i]; hash *= 1099511628211ull; }
        AutomatedCaptureLog("scene texture id=%d name=%s bitmap=%d flags=%u pixels=%llu",
            texture,GameTextures[texture].name,bitmap,GameTextures[texture].flags,hash);
    }
}
// One material resolver for all prepared mesh surfaces. Source-domain rules
// are explicit authoring conventions, not state inferred from a draw call.
void resolveMaterial(d3scene::SurfaceBinding& surface, MaterialResolution& resolution, bool model, bool exterior = false) {
    surface.authored_alpha = surface.alpha;
    if (surface.texture >= 0) { // flat/baked-only surfaces are not tied to a source geometry domain
    if (surface.texture >= MAX_TEXTURES || !GameTextures[surface.texture].used)
        Error("Scene material references missing texture %d", surface.texture);
    int base_texture = surface.texture;
    const auto& authored = GameTextures[surface.texture];
    const auto pair = SplitSpecularTexturePathEnabled() ? FindSplitSpecularTexturePair(surface.texture) : SplitSpecularTexturePair{};
    if (!model && (surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
        base_texture = authored.destroy_handle;
    else if (pair.valid) base_texture = pair.base_tmap;
    if (base_texture < 0 || base_texture >= MAX_TEXTURES || !GameTextures[base_texture].used)
        Error("Scene material references missing base texture %d", base_texture);
    const auto& texture = GameTextures[base_texture];
    surface.bitmap = resolution.bitmap(base_texture);
    surface.color_texture = base_texture;
    surface.scroll_period = {authored.slide_u,authored.slide_v};
    surface.u_offset = surface.v_offset = 0;
    surface.alpha *= texture.alpha;
    surface.blend = (texture.flags & TF_SATURATE) ? d3scene::BlendMode::Additive : d3scene::BlendMode::Solid;
    // Room 4444 textures historically carry specular masks, while model alpha
    // can be authored in that format. Keep the distinction at compilation.
    surface.texture_alpha = (texture.flags & TF_TMAP2) != 0 &&
        (model || bm_format(surface.bitmap) != BITMAP_FORMAT_4444);
    if (!model && Detail_settings.Specular_lighting) {
        int specular_texture = surface.texture;
        int specular_bitmap = -1;
        if ((surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
            specular_texture = authored.destroy_handle;
        else if (pair.valid) { specular_texture = pair.spec_tmap; specular_bitmap = pair.spec_bm; }
        if (specular_texture >= 0 && specular_texture < MAX_TEXTURES && GameTextures[specular_texture].used) {
            const auto& specular = GameTextures[specular_texture];
            if (specular_bitmap < 0) specular_bitmap = resolution.bitmap(specular_texture);
            const bool opaque_mask = bm_format(specular_bitmap) == BITMAP_FORMAT_4444 && texture.alpha >= 1 &&
                surface.blend == d3scene::BlendMode::Solid && surface.lightmap >= 0 && (surface.authored_flags & FF_LIGHTMAP);
            const bool authored_path = (authored.flags & TF_SPECULAR) && (surface.special >= 0 || exterior);
            const bool generated_path = !exterior && opaque_mask && (pair.valid ||
                (Render_preferred_state.per_pixel_lighting && Render_per_pixel_field_static_specular));
            if (authored_path || generated_path) {
            surface.specular_bitmap = specular_bitmap;
            surface.specular_texture = specular_texture;
            const float exponent = (specular.flags & TF_PLASTIC) ? 14.f : ((specular.flags & TF_MARBLE) ? 4.f : 6.f);
            surface.specular_exponent = (std::max)(1.f,(std::min)(128.f,std::floor(exponent*ConfigNormalizePerPixelSpecularSharpness(Render_per_pixel_specular_sharpness)+.5f)));
            surface.specular_strength = Render_per_pixel_specular_strength*specular.reflectivity*1.5f;
            surface.static_specular_strength = Render_per_pixel_static_specular_strength;
            surface.specular_lightmap_mix = Render_per_pixel_specular_lightmap_mix;
            surface.specular_alpha_strength = Render_per_pixel_specular_alpha_strength;
            }
        }
    }
    }
    if (surface.lightmap >= 0) {
        const int handle = LightmapInfo[surface.lightmap].lm_handle;
        if (handle >= 0 && handle < MAX_LIGHTMAPS && GameLightmaps[handle].square_res > 0) {
            surface.lightmap_bitmap = handle;
            surface.lightmap_u_scale = static_cast<float>(GameLightmaps[handle].width) / GameLightmaps[handle].square_res;
            surface.lightmap_v_scale = static_cast<float>(GameLightmaps[handle].height) / GameLightmaps[handle].square_res;
        }
    }
}
}

void PrepareGameSceneResources(d3scene::FrameData& data) {
    PERF_MARKER_SCOPE("Scene.PrepareResources");
    MaterialResolution resolution(static_cast<float>(data.simulation_time));
    { PERF_MARKER_SCOPE("Scene.Resources.Materials");
    for (auto& surface : data.terrain.materials) resolveMaterial(surface,resolution,false,true);
    for (auto& model : data.model_variants) {
      if (model.parts.publishedFor(model.asset->id())) continue;
      for (size_t p = 0; p < model.parts.size(); ++p) {
        const auto& input = model.parts.read()[p];
        if (input.surfaces.publishedFor(model.asset->id(),uint32_t(p),input.material_palette)) continue;
        auto& part = model.parts[p];
        for (auto& surface : part.surfaces) resolveMaterial(surface,resolution,true);
      }
    }
    }
    prepareImages(data,resolution);
    d3scene::prepareTerrainPatches(data.terrain);
}

void PrepareGameInspectionBanner(d3scene::FrameData& data) {
    if (!SceneGeometryInspection()) return;
    SceneTextStyle style; style.font = HUD_FONT; style.color = {1,220/255.f,80/255.f}; style.additive = true;
    int order = 0;
    for (int pass = 0; pass < 2; ++pass) PrepareSceneText(data,SceneInspectionBanner,order,12,12,
        "RENDERER MIGRATION TEST",style);
    for (int pass = 0; pass < 2; ++pass) PrepareSceneText(data,SceneInspectionBanner,order,12,28,
        "Incomplete - not yet qualified for gameplay",style);
    d3scene::PreparedForeground foreground; foreground.identity = SceneInspectionBanner;
    foreground.phase = d3scene::ForegroundPhase::FinalOverlay;
    foreground.layer.display_views.push_back(MakeSceneCanvasView(SceneInspectionBanner,{0,0,Max_window_w,Max_window_h},
        d3scene::ViewTarget::Display,true));
    data.foregrounds.push_back(std::move(foreground));
}

d3scene::PresentationRef PrepareGamePresentation(bool advance) {
    PERF_MARKER_SCOPE("Scene.PreparePresentation");
    static d3scene::PresentationRef previous_models;
    const bool retain_rooms = [&] { PERF_MARKER_SCOPE("Scene.MaterialInputs"); return roomMaterialInputsUnchanged(); }();
    if (!retain_rooms) ResetSplitSpecularTexturePairCache();
    const unsigned int simulation_random = ps_rand_get_state();
    auto data = [&] { PERF_MARKER_SCOPE("Scene.Extract"); return ExtractGameFrame(Gametime, advance ? Frametime : 0, Game_paused,advance,
        retain_rooms ? previous_models.get() : nullptr,FindArg("-scene-geometry-revision-audit") != 0); }();
    const auto control_delta = GetPlayerControlledTranslationDelta();
    data.player_control_delta = {control_delta.x,control_delta.y,control_delta.z};
    data.afterburner_factor = (std::max)(0.f,(std::min)(1.f,Render_afterburner_visual_factor));
    data.lighting_quality = Render_preferred_state.per_pixel_lighting ?
        d3scene::LightingQuality::Pixel : d3scene::LightingQuality::Vertex;
    if (Player_object) {
        const auto rotation = PlayerCameraShakeRotation(data.simulation_time,Player_object->handle,data.paused);
        const auto vector = [](const ::vector& v) { return d3scene::Vec3{v.x,v.y,v.z}; };
        data.camera_perturbations.push_back({static_cast<uint32_t>(Player_object->handle),
            {{},vector(rotation.rvec),vector(rotation.uvec),vector(rotation.fvec)}});
    }
    static uint64_t material_lifetime = 0;
    if (material_lifetime != data.scene_lifetime) {
        ResetSplitSpecularTexturePairCache();
        material_lifetime = data.scene_lifetime;
    }
    // Capture mutable authoring tables at the game/renderer boundary. Retained
    // presentations keep their own revision even if a face is edited or freed.
    std::unordered_map<int,d3scene::ReflectionAssetRef> reflections;
    // Exterior content historically reflects the primary celestial anchor in
    // white, independently of the satellite's visible tint/halo. Compile that
    // appearance convention once; execution consumes the same reflection asset
    // contract as interior materials, never the mutable terrain sky table.
    d3scene::ReflectionData exterior_reflection_data;
    const auto& primary_anchor = Terrain_sky.satellite_vectors[0];
    exterior_reflection_data.sources.push_back({{primary_anchor.x,primary_anchor.y,primary_anchor.z},{1,1,1}});
    const auto exterior_reflection = d3scene::assets().publishReflection(UINT32_MAX,std::move(exterior_reflection_data));
    uint32_t reflection_slot = MAX_SPECIAL_FACES;
    { PERF_MARKER_SCOPE("Scene.RoomMaterials");
    MaterialResolution resolution(static_cast<float>(data.simulation_time));
    for (auto& room : data.rooms) {
      room.exterior = (room.authored_flags & RF_EXTERNAL) != 0;
      room.fog_enabled = Detail_settings.Fog_enabled && (room.authored_flags & RF_FOG) && GetFunctionMode() != EDITOR_MODE;
      const auto& source = Rooms[room.number];
      room.blocks_portals = (source.flags & RF_DOOR) && source.doorway_data &&
          DoorwayGetPosition(&Rooms[room.number]) == 0 && !(Doors[source.doorway_data->doornum].flags & DF_SEETHROUGH);
      room.light_scalar = d3scene::roomLightScalar(static_cast<float>(data.simulation_time),room.pulse_time,
          room.pulse_offset,room.number,(room.authored_flags & RF_STROBE) != 0,
          (room.authored_flags & RF_FLICKER) != 0,GetFunctionMode() == EDITOR_MODE);
      const bool retained_appearance = room.uses_room_illumination && room.surfaces.publishedFor(room.asset->id());
      room.uses_room_illumination = true;
      if (retained_appearance) {
          reflection_slot += uint32_t(room.surfaces.size());
          continue;
      }
      for (auto& surface : room.surfaces) {
        const uint32_t baked_reflection_slot = reflection_slot++;
        if (surface.special >= 0 && surface.special < MAX_SPECIAL_FACES) {
            const auto& authored = SpecialFaces[surface.special];
            if (authored.used && authored.type == SFT_SPECULAR) {
                auto found = reflections.find(surface.special);
                if (found == reflections.end()) {
                    d3scene::ReflectionData reflection;
                    if (authored.num && !authored.spec_instance)
                        Error("Scene reflection %d has no sources",surface.special);
                    for (unsigned i = 0; i < authored.num; ++i) {
                        const auto& source = authored.spec_instance[i];
                        const auto color = source.bright_color;
                        reflection.sources.push_back({{source.bright_center.x,source.bright_center.y,source.bright_center.z},
                            {float((color >> 10)&31)/31.f,float((color >> 5)&31)/31.f,float(color&31)/31.f}});
                    }
                    if ((authored.flags & SFF_SPEC_SMOOTH) && authored.vertnorms) {
                        const size_t face = &surface-room.surfaces.data();
                        const size_t count = room.asset->geometry().polygons[face].corners.size();
                        for (size_t i = 0; i < count; ++i) {
                            const auto& n = authored.vertnorms[i];
                            reflection.corner_normals.push_back({n.x,n.y,n.z});
                        }
                    }
                    found = reflections.emplace(surface.special,d3scene::assets().publishReflection(surface.special,std::move(reflection))).first;
                }
                surface.reflection = found->second;
            }
        }
        if (!room.exterior && data.lighting_quality == d3scene::LightingQuality::Pixel) {
            const auto baked = PublishRoomReflectionBake(room.number,static_cast<int>(&surface-room.surfaces.data()),
                baked_reflection_slot,Render_per_pixel_field_static_specular,Render_split_specular_textures);
            if (baked) surface.reflection = baked;
        }
        surface.occlusion_class = resolution.roomOcclusion(surface.texture,room.exterior);
        resolveMaterial(surface,resolution,false,room.exterior);
        if (room.exterior && surface.specular_strength > 0) surface.reflection = exterior_reflection;
        surface.lighting_modulation = 1;
        // Authored missing textures suppress game room faces at publication.
        // The geometry executor also supports flat and view-image surfaces;
        // it must not infer eligibility from a legacy bitmap sentinel.
        surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG) && surface.bitmap >= 0;
        if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
            surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
      }
      if (Detail_settings.Mirrored_surfaces && source.mirror_face >= 0 && source.mirror_face < source.num_faces) {
          const int texture = source.faces[source.mirror_face].tmap;
          if (texture >= 0 && texture < MAX_TEXTURES && GameTextures[texture].used && (GameTextures[texture].flags & TF_ALPHA)) {
              std::vector<uint32_t> polygons;
              for (size_t face = 0; face < room.surfaces.size(); ++face)
                  if (room.surfaces[face].texture == texture && room.surfaces[face].draw_surface)
                      polygons.push_back(static_cast<uint32_t>(face));
              room.mirrors = d3scene::compilePlanarSurfaces(room.asset->geometry(),std::move(polygons));
              if (!room.mirrors.empty() && (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0))
                  AutomatedCaptureLog("scene mirror surfaces room=%d groups=%u",room.number,(unsigned)room.mirrors.size());
          }
      }
    }
    }
    { PERF_MARKER_SCOPE("Scene.PrepareTerrain"); PrepareGameTerrain(data); }
    { PERF_MARKER_SCOPE("Scene.PrepareSky"); PrepareGameSky(data); }
    { PERF_MARKER_SCOPE("Scene.PrepareEffects"); PrepareGameEffects(data); }
    { PERF_MARKER_SCOPE("Scene.PrepareModels"); PrepareGameModelInstances(data, previous_models.get(),retain_rooms); }
    { PERF_MARKER_SCOPE("Scene.PrepareObjectEffects"); PrepareGameObjectEffects(data); }
    { PERF_MARKER_SCOPE("Scene.PrepareScorches"); PrepareGameScorches(data); }
    if (SceneGeometryInspection() && FindArg("-scene-cockpit-probe")) {
        PERF_MARKER_SCOPE("Scene.PrepareCockpit");
        PrepareGameCockpit(data,advance,previous_models ? &previous_models->data() : nullptr,retain_rooms);
        PrepareCockpitComposition(data,{Game_window_x,Game_window_y,Game_window_w,Game_window_h},SceneCockpitZoom);
    }
    if (SceneGeometryInspection() && (FindArg("-scene-reticle-probe") || FindArg("-scene-hud-probe"))) {
        const auto layout = CaptureGameHUDLayout();
        if (FindArg("-scene-reticle-probe")) PrepareGameReticle(data,layout);
        if (FindArg("-scene-hud-probe")) PrepareGameHUD(data,layout,advance);
    }
    PrepareGameInspectionBanner(data);
    if (SceneGeometryInspection() && FindArg("-scene-view-image-probe")) {
        int order = 0; SceneQuadStyle style; style.uv_min = {0,1}; style.uv_max = {1,0};
        PrepareSceneQuad(data,SceneViewImageProbe,order,{16,72,320,180},style);
        data.meshes.back().surface.view_image_slot = 1;
        d3scene::PreparedForeground foreground; foreground.identity = SceneViewImageProbe; foreground.order = 30;
        foreground.layer.display_views.push_back(MakeSceneCanvasView(SceneViewImageProbe,{0,0,Max_window_w,Max_window_h},
            d3scene::ViewTarget::Display,true));
        foreground.layer.display_views.back().image_inputs.push_back({1,SceneViewImageProbe+1});
        data.foregrounds.push_back(std::move(foreground));
        PrepareSceneQuad(data,SceneViewImageProbe+1,order,{0,0,320,180},style);
        data.meshes.back().surface.view_image_slot = 1;
    }
    if (SceneGeometryInspection() && FindArg("-scene-standalone-model-probe") && Player_object) {
        const auto owner = uint32_t(Player_object->handle);
        unsigned detached = 0;
        for (auto& instance : data.model_instances) if (instance.has_owner && instance.owner == owner) {
            instance.has_owner = false; ++detached;
            if (FindArg("-scene-model-collection-probe")) instance.collection = 1;
            for (auto& mesh : data.meshes) if (mesh.model_instance == instance.identity) {
                mesh.has_owner = false; mesh.collection = instance.collection;
            }
        }
        if (!detached) Error("Standalone model fixture found no player model");
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene standalone model fixture instances=%u",detached);
    }
    PrepareGameSceneResources(data);
    { PERF_MARKER_SCOPE("Scene.PrepareLights"); PrepareGameSceneLights(data); }
    for (auto& room : data.rooms) {
      if (room.portals.publishedFor(room.surfaces)) continue;
      for (size_t p = 0; p < room.portals.size(); ++p) {
        const auto& portal = room.portals.read()[p];
        const auto& surface = room.surfaces.read()[portal.polygon];
        // Procedural opacity is not a stable aperture blocker. Otherwise the
        // renderer compiles traversal from the final material (including mirrors).
        const bool controlled = (portal.flags & PF_RENDER_FACES) && !(GameTextures[surface.texture].flags & TF_PROCEDURAL);
        if (portal.surface_controls_visibility != controlled || (!controlled && !portal.traversable)) {
            auto& changed = room.portals[p]; changed.surface_controls_visibility = controlled;
            if (!controlled) changed.traversable = true;
        }
      }
    }
    if (SceneGeometryInspection()) {
        // Explicit authoring preview: copy optical properties before compilation,
        // not a view-dependent patch to the finished image or mutable level data.
        const int option = FindArg("-scene-fog-room-source");
        if (option) {
            const auto roomNumber = [&](int offset) {
                const char* text = GetArg(option+offset);
                char* end = nullptr;
                const long number = text ? std::strtol(text,&end,10) : -1;
                if (!text || end == text || *end || number < 0 || number >= MAX_ROOMS)
                    throw std::invalid_argument("Fog room preview requires target and source room numbers");
                return int(number);
            };
            const int target_number = roomNumber(1), source_number = roomNumber(2);
            auto target = std::find_if(data.rooms.begin(),data.rooms.end(),
                [&](const d3scene::RoomState& room) { return room.number == target_number; });
            const auto source = std::find_if(data.rooms.begin(),data.rooms.end(),
                [&](const d3scene::RoomState& room) { return room.number == source_number; });
            if (target == data.rooms.end() || source == data.rooms.end() || !(source->fog_depth > 0))
                throw std::invalid_argument("Fog room preview references missing room or invalid source depth");
            target->fog_enabled = source->fog_enabled;
            target->fog_depth = source->fog_depth;
            target->fog_color = source->fog_color;
            if (AutomatedCapturePending())
                AutomatedCaptureLog("scene fog authoring preview target=%d source=%d enabled=%d depth=%.9g color=%.9g,%.9g,%.9g",
                    target_number,source_number,target->fog_enabled,target->fog_depth,
                    target->fog_color.x,target->fog_color.y,target->fog_color.z);
        }
    }
    { PERF_MARKER_SCOPE("Scene.PublishFog"); data.fog = d3scene::assets().publishFog(data.rooms); }
    if (SceneGeometryInspection() && FindArg("-scene-lod-probe") && Player_object) {
        // Isolated authored-model inspection: keep every player variant and
        // its prepared attachments, but no room that could occlude a far camera.
        const auto owner = uint32_t(Player_object->handle);
        data.objects.erase(std::remove_if(data.objects.begin(),data.objects.end(),
            [&](const d3scene::ObjectState& object) { return object.handle != owner; }),data.objects.end());
        data.model_instances.erase(std::remove_if(data.model_instances.begin(),data.model_instances.end(),
            [&](const d3scene::ModelInstance& instance) { return !instance.has_owner || instance.owner != owner; }),data.model_instances.end());
        data.compactModels();
        data.meshes.erase(std::remove_if(data.meshes.begin(),data.meshes.end(),
            [&](const d3scene::PreparedMesh& mesh) { return mesh.collection == 0 && (!mesh.has_owner || mesh.owner != owner); }),data.meshes.end());
        data.rooms.clear(); data.terrain = {}; data.fog.reset(); data.exterior_fog = {};
        data.sky_enabled = false;
    }
    // No domain is declared complete by a diagnostic subset of its consumers.
    auto prepared = [&] { PERF_MARKER_SCOPE("Scene.Freeze"); return d3scene::Presentation::publish(std::move(data)); }();
    if (FindArg("-scene-geometry-revision-audit")) AuditGameModelPoses(*prepared);
    logPreparedModels(*prepared);
    if (ps_rand_get_state() != simulation_random)
        Error("Presentation preparation consumed simulation randomness");
    if (advance) previous_models = prepared;
    return prepared;
}

d3scene::VisibilityReport RenderGameSceneView(const d3scene::Presentation& frame, d3scene::View view) {
    if (!SceneGeometryInspection())
        Error("Renderer migration is incomplete. This test build requires -scene-inspection; use the preserved Candidate for gameplay.");
    view.geometry_inspection = true;
    view.feedback = d3scene::NoFeedback;
    if (FindArg("-scene-automap-probe") && Player_object) {
        const auto& p = Player_object->pos;
        const auto& o = Player_object->orient;
        view.camera.position = {p.x-o.fvec.x*70+o.uvec.x*30,
            p.y-o.fvec.y*70+o.uvec.y*30,p.z-o.fvec.z*70+o.uvec.z*30};
        vector toward{p.x-view.camera.position.x,p.y-view.camera.position.y,p.z-view.camera.position.z};
        vector up = o.uvec;
        matrix facing; vm_VectorToMatrix(&facing,&toward,&up,nullptr);
        view.camera.right = {facing.rvec.x,facing.rvec.y,facing.rvec.z};
        view.camera.up = {facing.uvec.x,facing.uvec.y,facing.uvec.z};
        view.camera.forward = {facing.fvec.x,facing.fvec.y,facing.fvec.z};
        view.attached_camera = false; view.exclude_camera_rig = false; view.excluded_objects.clear();
        view.portal_visibility = false; view.dynamic_lighting = false; view.room = -1;
        view.exterior_origin = false;
        if (FindArg("-scene-automap-terrain-probe") && frame.data().terrain.asset) {
            const auto& grid = frame.data().terrain.asset->grid();
            const uint32_t x = grid.width/2,z = grid.depth/4;
            view.camera.position = {x*grid.spacing,grid.heights[z*grid.width+x]+120,z*grid.spacing};
            view.camera.right = {1,0,0}; view.camera.up = {0,.8f,.6f}; view.camera.forward = {0,-.6f,.8f};
            view.exterior_origin = true;
        }
    }
    const int heading_arg = FindArg("-scene-camera-heading");
    if (heading_arg && AutomatedCaptureFrameIndex() >= 0 && GetArg(heading_arg+1)) {
        const float angle = float(std::atof(GetArg(heading_arg+1)))*.01745329252f;
        const float c = std::cos(angle),s = std::sin(angle);
        const auto right = view.camera.right,forward = view.camera.forward;
        view.camera.right = {right.x*c-forward.x*s,right.y*c-forward.y*s,right.z*c-forward.z*s};
        view.camera.forward = {forward.x*c+right.x*s,forward.y*c+right.y*s,forward.z*c+right.z*s};
    }
    const int pitch_arg = FindArg("-scene-camera-pitch");
    if (pitch_arg && AutomatedCaptureFrameIndex() >= 0 && GetArg(pitch_arg+1)) {
        const float angle = float(std::atof(GetArg(pitch_arg+1)))*.01745329252f;
        const float c = std::cos(angle),s = std::sin(angle);
        const auto up = view.camera.up,forward = view.camera.forward;
        view.camera.up = {up.x*c-forward.x*s,up.y*c-forward.y*s,up.z*c-forward.z*s};
        view.camera.forward = {forward.x*c+up.x*s,forward.y*c+up.y*s,forward.z*c+up.z*s};
    }
    if (FindArg("-scene-history-probe")) {
        const int index = AutomatedCaptureFrameIndex();
        const float angle = .006f*index+(index >= 30 ? .35f : 0.f);
        const float c = std::cos(angle), s = std::sin(angle);
        const auto right = view.camera.right, forward = view.camera.forward;
        view.camera.right = {right.x*c-forward.x*s,right.y*c-forward.y*s,right.z*c-forward.z*s};
        view.camera.forward = {forward.x*c+right.x*s,forward.y*c+right.y*s,forward.z*c+right.z*s};
        view.camera_variant = index >= 30 ? 2 : 0;
        view.camera_cut = index == 30;
        AutomatedCaptureLog("scene history fixture frame=%d cut=%d variant=%u",index,view.camera_cut,view.camera_variant);
    }
    if ((FindArg("-scene-terrain-probe") || FindArg("-scene-sky-probe") || FindArg("-scene-satellite-probe")) && frame.data().terrain.asset) {
        const auto& grid = frame.data().terrain.asset->grid();
        const uint32_t x = grid.width/2, z = grid.depth/4;
        view.camera.position = {x*grid.spacing,grid.heights[z*grid.width+x]+120,z*grid.spacing};
        view.camera.right = {1,0,0}; view.camera.up = {0,.8f,.6f}; view.camera.forward = {0,-.6f,.8f};
        view.attached_camera = false; view.room = -1; view.exterior_origin = true;
        if (FindArg("-scene-sky-probe")) {
            view.camera.position.y += 180;
            view.camera.up = {0,.9797959f,-.2f}; view.camera.forward = {0,.2f,.9797959f};
        }
    }
    if (FindArg("-scene-satellite-probe")) {
        for (const auto& mesh : frame.data().meshes) if (mesh.eye_distance > 0 && mesh.alignment == d3scene::MeshAlignment::FacingEye) {
            vector direction{mesh.world.position.x-view.camera.position.x,mesh.world.position.y-view.camera.position.y,
                mesh.world.position.z-view.camera.position.z};
            if (vm_GetMagnitude(&direction) <= .0001f) continue;
            matrix orientation; vm_VectorToMatrix(&orientation,&direction,nullptr,nullptr);
            view.camera.right = {orientation.rvec.x,orientation.rvec.y,orientation.rvec.z};
            view.camera.up = {orientation.uvec.x,orientation.uvec.y,orientation.uvec.z};
            view.camera.forward = {orientation.fvec.x,orientation.fvec.y,orientation.fvec.z};
            break;
        }
    }
    const int lod_probe = FindArg("-scene-lod-probe");
    if ((FindArg("-scene-model-effects-probe") || lod_probe) && Player_object) {
        for (const auto& object : frame.data().model_instances) if (object.identity == uint64_t(uint32_t(Player_object->handle))+1) {
            const auto& pose = object.current;
            float distance = Player_object->size*4+2;
            if (lod_probe) {
                const auto* argument = GetArg(lod_probe+1);
                if (!argument || argument[0] < '0' || argument[0] > '2' || argument[1])
                    Error("LOD fixture requires tier 0, 1 or 2");
                const int tier = argument[0]-'0';
                const float close_depth = distance*std::sqrt(1.f+.45f*.45f);
                const float depth = tier == 0 ? (std::min)(close_depth,object.lod_distances.x*.5f) :
                    tier == 1 ? (object.lod_distances.x+object.lod_distances.y)*.5f :
                    (std::max)(object.lod_distances.x,object.lod_distances.y)*1.1f;
                if (!(depth > 0) || !object.model_count) Error("LOD fixture player has no authored LOD range");
                distance = depth/std::sqrt(1.f+.45f*.45f);
                view.zoom *= .4f*close_depth/depth;
                AutomatedCaptureLog("scene LOD fixture tier=%d variants=%u depth=%.6g thresholds=%.6g,%.6g",
                    tier,unsigned(object.model_count),depth,object.lod_distances.x,object.lod_distances.y);
            }
            view.camera = pose;
            view.camera.position = {pose.position.x-pose.forward.x*distance+pose.right.x*distance*.45f,
                pose.position.y-pose.forward.y*distance+pose.right.y*distance*.45f,
                pose.position.z-pose.forward.z*distance+pose.right.z*distance*.45f};
            vector toward{pose.position.x-view.camera.position.x,pose.position.y-view.camera.position.y,
                pose.position.z-view.camera.position.z};
            vector up{pose.up.x,pose.up.y,pose.up.z};
            matrix facing; vm_VectorToMatrix(&facing,&toward,&up,nullptr);
            view.camera.right = {facing.rvec.x,facing.rvec.y,facing.rvec.z};
            view.camera.up = {facing.uvec.x,facing.uvec.y,facing.uvec.z};
            view.camera.forward = {facing.fvec.x,facing.fvec.y,facing.fvec.z};
            view.room = object.room; view.attached_camera = false; view.exclude_camera_rig = false; view.excluded_objects.clear();
            view.exterior_origin = object.room != -1 && ROOMNUM_OUTSIDE(object.room);
            break;
        }
    }
    if (FindArg("-scene-scorch-view-probe")) {
        for (const auto& mesh : frame.data().meshes) if (mesh.receiver_polygon >= 0) {
            for (const auto& room : frame.data().rooms) if (room.number == mesh.room) {
                const auto normal = room.asset->geometry().polygons[mesh.receiver_polygon].normal;
                vector forward{-normal.x,-normal.y,-normal.z};
                matrix facing; vm_VectorToMatrix(&facing,&forward,nullptr,nullptr);
                view.camera.position = {mesh.world.position.x+normal.x*12,mesh.world.position.y+normal.y*12,
                    mesh.world.position.z+normal.z*12};
                view.camera.right = {facing.rvec.x,facing.rvec.y,facing.rvec.z};
                view.camera.up = {facing.uvec.x,facing.uvec.y,facing.uvec.z};
                view.camera.forward = {facing.fvec.x,facing.fvec.y,facing.fvec.z};
                view.room = room.number; view.exterior_origin = false; view.attached_camera = false;
                break;
            }
            break;
        }
    }
    if ((FindArg("-scene-mirror-camera-probe") || FindArg("-scene-mirror-surface-probe")) && view.kind == d3scene::ViewKind::Main) {
        bool found = false;
        for (const auto& room : frame.data().rooms) {
            const auto& geometry = room.asset->geometry();
            if (room.mirrors.empty()) continue;
            const auto& mirror = room.mirrors.front();
            const auto polygon = mirror.polygons.front();
            const auto& face = geometry.polygons[polygon];
            if (face.corners.empty()) continue;
            d3scene::Vec3 center;
            for (const auto& corner : face.corners) {
                const auto& p = geometry.vertices[corner.vertex];
                center.x += p.x/face.corners.size(); center.y += p.y/face.corners.size(); center.z += p.z/face.corners.size();
            }
            const auto n = mirror.plane.normal;
            vector forward{-n.x,-n.y,-n.z}; matrix basis;
            vm_VectorToMatrix(&basis,&forward,nullptr,nullptr);
            view.camera.position = {center.x+n.x*20,center.y+n.y*20,center.z+n.z*20};
            view.camera.right = {basis.rvec.x,basis.rvec.y,basis.rvec.z};
            view.camera.up = {basis.uvec.x,basis.uvec.y,basis.uvec.z};
            view.camera.forward = {basis.fvec.x,basis.fvec.y,basis.fvec.z};
            view.attached_camera = false; view.exclude_camera_rig = false;
            view.room = room.number; view.exterior_origin = false;
            if (FindArg("-scene-mirror-surface-probe")) {
                view.history_key = 0x4d4952524f525002ull;
                if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
                    AutomatedCaptureLog("scene mirror surface camera room=%d polygon=%u",room.number,polygon);
                found = true; break;
            }
            view = d3scene::reflectedView(frame,view,mirror.plane,0x4d4952524f525001ull,room.number);
            for (const auto face : mirror.polygons) view.excluded_surfaces.push_back({room.asset->id(),face});
            // Inspect the reflected image with main-view image products. This
            // fixture is NOT mirror-surface composition or gameplay feedback.
            view.kind = d3scene::ViewKind::Main;
            if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
                AutomatedCaptureLog("scene mirror camera room=%d polygon=%u groups=%u reversed=%d clip=1",
                    room.number,polygon,(unsigned)room.mirrors.size(),d3scene::reversesWinding(view.camera));
            found = true; break;
        }
        if (!found) Error("Mirror camera fixture requires an authored mirror");
    }
    // Diagnostic views do not mark rooms explored or tell AI it was observed.
    // The gameplay feedback compiler barrier remains until the normal executor
    // and its visibility policy have been migrated.
    auto planar = [&] { PERF_MARKER_SCOPE("Scene.PrepareView");
        return d3scene::planPlanarViews(frame,view,Render_preferred_state.msaa_samples); }();
    view = planar.consumer.view();
    if (FindArg("-scene-view-image-probe") && view.kind == d3scene::ViewKind::Main) {
        d3scene::ViewImageRequest image;
        image.image = SceneViewImageProbe; image.view = view;
        image.view.kind = d3scene::ViewKind::Preview; image.view.feedback = d3scene::NoFeedback;
        image.view.history_key = SceneViewImageProbe;
        image.view.viewport = image.view.projection_anchor = {0,0,320,180};
        image.samples = 4;
        d3scene::ViewImageRequest copy;
        copy.image = SceneViewImageProbe+1;
        copy.view = MakeSceneCanvasView(SceneViewImageProbe+1,{0,0,320,180},d3scene::ViewTarget::Scene,true);
        copy.view.image_inputs.push_back({1,image.image});
        // Deliberately reversed producer order: the graph, not caller order,
        // owns the dependency from this canvas image to the scene image.
        planar.images.push_back(std::move(copy)); planar.images.push_back(std::move(image));
    }
    d3scene::renderViewImages(frame,planar.images);
    auto observations = d3scene::renderView(planar.consumer);
    if (FindArg("-scene-observation-probe") &&
        (view.kind == d3scene::ViewKind::Main || view.kind == d3scene::ViewKind::Auxiliary)) {
        auto inspection = view;
        inspection.feedback = d3scene::Automap | d3scene::ObjectVisibility | d3scene::EffectEligibility;
        const auto candidate = InspectSceneObservations(frame,inspection);
        observations = candidate.report; // collected for inspection only; never committed
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene observation candidate rooms=%u objects=%u exterior_pending=%d mirrors_pending=%d",
                (unsigned)observations.rooms.size(),(unsigned)observations.objects.size(),
                candidate.exterior_pending,candidate.mirrors_pending);
    }
    if (view.kind == d3scene::ViewKind::Main && FindArg("-scene-model-collection-probe")) {
        // Exercise a second content collection in the same frozen frame. No
        // second preparation or temporary simulation model/object is involved.
        auto preview = view;
        preview.kind = d3scene::ViewKind::Preview; preview.collection = 1;
        preview.history_key = 0x434f4c4c454354ull;
        if (FindArg("-scene-display-view-probe")) {
            if (!rend_BeginPostPresentFrame()) Error("Display view fixture could not complete the world");
            preview.target = d3scene::ViewTarget::Display;
            preview.clear_depth = true;
            preview.viewport = {view.projection_anchor.width/2,view.projection_anchor.height/2,
                view.projection_anchor.width/2,view.projection_anchor.height/2};
            preview.projection_anchor = {0,0,preview.viewport.width,preview.viewport.height};
        }
        d3scene::renderView(frame,preview);
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene model collection fixture world=0 preview=1");
        if (FindArg("-scene-display-view-probe") && (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0))
            AutomatedCaptureLog("scene display view fixture target=display viewport=bottom_right");
    }
    return observations;
}

void RenderGameForegrounds(const d3scene::Presentation& frame,d3scene::ForegroundPhase phase) {
    if (!SceneGeometryInspection()) Error("Foreground migration has not passed gameplay acceptance");
    for (const auto& foreground : frame.data().foregrounds) if (foreground.phase == phase) {
        // The retained comparison oracle is diagnostic-only, not a fallback.
        if (foreground.identity == SceneReticle && FindArg("-scene-reticle-reference"))
            RenderPreparedReticle(frame);
        else d3scene::renderForeground(frame,foreground.layer);
    }
}
void PrepareCockpitComposition(d3scene::FrameData& frame,d3scene::Rect viewport,float zoom) {
    if (!SceneGeometryInspection()) Error("Cockpit migration has not passed gameplay acceptance");
    bool base = false, layered = false, late = false, gauges = false, monitors = false, text = false;
    for (const auto& instance : frame.model_instances) {
        base = base || instance.collection == SceneCockpitBase;
        layered = layered || instance.collection == SceneCockpitAfterGauges;
        late = late || instance.collection == SceneCockpitLate;
    }
    for (const auto& mesh : frame.meshes) {
        gauges = gauges || mesh.collection == SceneCockpitGauges;
        monitors = monitors || mesh.collection == SceneCockpitMonitorBacking;
        text = text || mesh.collection == SceneCockpitText;
    }
    if (!base && !layered && !late) Error("Cockpit fixture has no prepared cockpit model");
    d3scene::View view;
    view.geometry_inspection = true; view.kind = d3scene::ViewKind::Preview;
    view.viewport = viewport; view.projection_anchor = {0,0,viewport.width,viewport.height};
    view.zoom = zoom; view.dynamic_lighting = false; view.portal_visibility = false;
    d3scene::ForegroundLayer layer;
    if (monitors) {
        view.collection = view.history_key = SceneCockpitMonitorBacking;
        view.ordering = d3scene::ViewOrdering::Compositing;
        layer.scene_views.push_back(view);
        view.ordering = d3scene::ViewOrdering::Depth;
    }
    if (base) {
        view.collection = view.history_key = SceneCockpitBase;
        layer.scene_views.push_back(view);
    }
    if (gauges) {
        view.collection = view.history_key = SceneCockpitGauges;
        view.ordering = d3scene::ViewOrdering::Compositing;
        layer.scene_views.push_back(view);
        view.ordering = d3scene::ViewOrdering::Depth;
    }
    if (text) {
        auto canvas = MakeSceneCanvasView(SceneCockpitText,viewport,d3scene::ViewTarget::Scene,true);
        layer.scene_views.push_back(canvas);
    }
    if (layered) {
        view.collection = view.history_key = SceneCockpitAfterGauges;
        layer.scene_views.push_back(view);
    }
    if (late) {
        view.collection = view.history_key = SceneCockpitLate;
        view.target = d3scene::ViewTarget::Display;
        layer.display_views.push_back(view);
    }
    frame.foregrounds.push_back({SceneCockpitBase,std::move(layer),40});
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene cockpit composed base=%d late=%d gauges=%d text=%d",base,late,gauges,text);
}

void RenderModuleSceneView(object* viewer, vector* eye, int room, matrix* orientation,
    float zoom, bool rear, int x, int y, int w, int h) {
    if (!eye || !orientation) Error("Module scene view has no camera");
    int width, height;
    rend_GetProjectionParameters(&width, &height);
    if (w <= 0 || h <= 0) { x = y = 0; w = width; h = height; }
    const auto frame = ModuleScenePresentation();
    const auto view = MakeGameSceneView(d3scene::ViewKind::Preview, 0x100000000ull,
        viewer, *eye, room, *orientation, zoom, rear, {0, 0, width, height}, {x, y, w, h});
    RenderGameSceneView(*frame, view);
}
