// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "gl_mesh.h"
#include "SceneView.h"
#include "SceneGeometry.h"
#include "SceneImages.h"
#include <cstring>
#include <cmath>
extern int Last_texel_unit_set;

namespace {
struct SceneVertex { d3scene::Vec3 position; float u, v, lu, lv, alpha; d3scene::Vec3 normal; };
const char* SceneVertexShader = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec2 light_uv;
layout(location=3) in float alpha;
layout(location=4) in vec3 normal;
uniform samplerBuffer instance_uvs;
uniform int use_instance_uvs, instance_uv_offset;
uniform samplerBuffer instance_positions;
uniform int use_instance_positions, instance_position_offset;
uniform vec3 ambient_color, ambient_direction;
uniform int directional_ambient;
uniform vec3 eye, camera_right, camera_up, camera_forward;
uniform vec3 model_position, model_right, model_up, model_forward;
uniform vec4 projection;
uniform float near_distance;
out vec2 texcoord, lightcoord;
out float vertex_alpha;
out vec3 world_position;
out vec3 ambient;
void main() {
    vec3 prepared = use_instance_positions != 0 ? texelFetch(instance_positions,gl_VertexID+instance_position_offset).rgb : position;
    vec3 world = model_position + model_right*prepared.x + model_up*prepared.y + model_forward*prepared.z;
    vec3 relative = world-eye;
    world_position = world;
    float z = dot(relative,camera_forward);
    gl_Position = vec4(dot(relative,camera_right)*projection.x + z*projection.z,
                       dot(relative,camera_up)*projection.y + z*projection.w, z-2.0*near_distance,z);
    texcoord=uv; lightcoord=light_uv; vertex_alpha=alpha;
    if (use_instance_uvs != 0) lightcoord = texelFetch(instance_uvs,gl_VertexID+instance_uv_offset).rg;
    vec3 world_normal = model_right*normal.x + model_up*normal.y + model_forward*normal.z;
    float shade = directional_ambient != 0 ? clamp((1.0-dot(world_normal,ambient_direction))*0.5,0.0,1.0) : 1.0;
    ambient = ambient_color*shade;
}
)GLSL";
const char* SceneFragmentShader = R"GLSL(#version 330 core
uniform sampler2D colortexture, lightmaptexture;
uniform int use_lightmap, use_texture_alpha;
uniform int use_texture;
uniform vec3 base_color;
uniform vec3 color_modulation;
uniform vec2 light_scale, texture_offset;
uniform float material_alpha;
uniform float alpha_cutoff;
uniform float lighting_modulation;
uniform samplerBuffer scene_lights;
uniform isamplerBuffer scene_influences;
uniform int influence_offset, influence_count;
in vec3 world_position;
in vec3 ambient;
in vec2 texcoord, lightcoord;
in float vertex_alpha;
layout(location=0) out vec4 color;
void main() {
    vec4 base=use_texture != 0 ? texture(colortexture,texcoord+texture_offset) : vec4(base_color,1);
    base.a=(use_texture_alpha != 0 ? base.a : 1.0)*vertex_alpha*material_alpha;
    if(base.a < alpha_cutoff) discard;
    vec3 illumination = use_lightmap != 0 ? texture(lightmaptexture,lightcoord*light_scale).rgb : ambient;
    vec3 dynamic_color = vec3(0);
    for (int index = 0; index < influence_count; ++index) {
        int light = texelFetch(scene_influences,influence_offset+index).r*4;
        vec4 position_radius = texelFetch(scene_lights,light);
        vec4 color_falloff = texelFetch(scene_lights,light+1);
        vec4 direction_cone = texelFetch(scene_lights,light+2);
        vec4 flags = texelFetch(scene_lights,light+3);
        vec3 delta = world_position-position_radius.xyz;
        float distance = length(delta);
        if (distance >= position_radius.w) continue;
        float attenuation = pow(1.0-distance/position_radius.w,color_falloff.w);
        if (flags.x != 0) {
            float cone = distance > 0.000001 ? dot(delta/distance,direction_cone.xyz) : 1.0;
            attenuation *= max(0.0,(cone-direction_cone.w)/max(0.000001,1.0-direction_cone.w));
        }
        dynamic_color += color_falloff.rgb*attenuation;
    }
    illumination = clamp(illumination+dynamic_color,vec3(0),vec3(1));
    color=vec4(base.rgb*color_modulation*illumination*lighting_modulation,base.a);
}
)GLSL";
}

void GL4Renderer::DestroySceneResources() {
    for (auto& image : scene_images) if (image.second.texture) glDeleteTextures(1,&image.second.texture);
    scene_images.clear();
    for (auto& room : scene_rooms) {
        if (room.vao) glDeleteVertexArrays(1, &room.vao);
        if (room.buffer) glDeleteBuffers(1, &room.buffer);
    }
    scene_rooms.clear();
    for (auto& model : scene_models) for (auto& mesh : model) {
        if (mesh.vao) glDeleteVertexArrays(1,&mesh.vao);
        if (mesh.buffer) glDeleteBuffers(1,&mesh.buffer);
    }
    scene_models.clear();
    if (scene_light_buffer) glDeleteBuffers(1,&scene_light_buffer);
    if (scene_influence_buffer) glDeleteBuffers(1,&scene_influence_buffer);
    if (scene_light_texture) glDeleteTextures(1,&scene_light_texture);
    if (scene_influence_texture) glDeleteTextures(1,&scene_influence_texture);
    if (scene_uv_buffer) glDeleteBuffers(1,&scene_uv_buffer);
    if (scene_uv_texture) glDeleteTextures(1,&scene_uv_texture);
    scene_uv_buffer = scene_uv_texture = 0;
    scene_uv_ranges.clear();
    if (scene_position_buffer) glDeleteBuffers(1,&scene_position_buffer);
    if (scene_position_texture) glDeleteTextures(1,&scene_position_texture);
    scene_position_buffer = scene_position_texture = 0;
    scene_position_ranges.clear();
    scene_light_buffer = scene_influence_buffer = scene_light_texture = scene_influence_texture = 0;
    scene_frame_sequence = 0;
    scene_light_ranges.clear();
    scene_asset_lifetime = 0;
    scene_shader.Destroy();
}

void GL4Renderer::BindSceneImage(const d3scene::ImageAsset& image,int unit,bool wrap,bool baked) {
    auto& gpu = scene_images[image.id().slot];
    glActiveTexture(GL_TEXTURE0+unit);
    if (gpu.id != image.id()) {
        if (gpu.texture) glDeleteTextures(1,&gpu.texture);
        glGenTextures(1,&gpu.texture); gpu.id = image.id();
        glBindTexture(GL_TEXTURE_2D,gpu.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        for (size_t level = 0; level < image.mips().size(); ++level) {
            const auto& mip = image.mips()[level];
            const uint32_t width = image.paddedExtent() ? image.paddedExtent() : mip.width;
            const uint32_t height = image.paddedExtent() ? image.paddedExtent() : mip.height;
            if (image.format() == d3scene::ImageFormat::RGBA8) {
                std::vector<uint8_t> padded;
                const auto* pixels = mip.pixels.data();
                if (image.paddedExtent()) {
                    padded.resize(size_t(width)*height*4,0);
                    for (uint32_t y = 0; y < mip.height; ++y)
                        std::memcpy(padded.data()+size_t(y)*width*4,pixels+size_t(y)*mip.width*4,size_t(mip.width)*4);
                    pixels = padded.data();
                }
                glTexImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            } else {
                std::vector<uint8_t> pixels(size_t(width)*height*4,0);
                for (uint32_t y = 0; y < mip.height; ++y) for (uint32_t x = 0; x < mip.width; ++x) {
                    uint16_t pixel; std::memcpy(&pixel,mip.pixels.data()+(size_t(y)*mip.width+x)*2,2);
                    const auto rgba = d3scene::imageRgbaPixel(image.format(),pixel);
                    std::memcpy(pixels.data()+(size_t(y)*width+x)*4,rgba.data(),4);
                }
                glTexImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            }
        }
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,image.generatedLevels() ? image.generatedLevels()-1 : static_cast<GLint>(image.mips().size())-1);
        if (image.generatedLevels()) glGenerateMipmap(GL_TEXTURE_2D);
        glPixelStorei(GL_UNPACK_ALIGNMENT,2);
    } else glBindTexture(GL_TEXTURE_2D,gpu.texture);
    const bool filter = baked || OpenGL_preferred_state.filtering;
    const bool mip = !baked && OpenGL_preferred_state.mipping && (image.generatedLevels() > 1 || image.mips().size() > 1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mip ? (filter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (filter ? GL_LINEAR : GL_NEAREST));
    if (OpenGL_max_anisotropy > 1)
        glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,mip ? std::min(OpenGL_max_anisotropy,std::max(1.f,static_cast<float>(OpenGL_preferred_state.anisotropy))) : 1);
}

void GL4Renderer::UploadSceneFrame(const d3scene::Presentation& frame) {
    if (!scene_light_buffer) {
        glGenBuffers(1,&scene_light_buffer); glGenTextures(1,&scene_light_texture);
        glGenBuffers(1,&scene_influence_buffer); glGenTextures(1,&scene_influence_texture);
        glGenBuffers(1,&scene_uv_buffer); glGenTextures(1,&scene_uv_texture);
        glGenBuffers(1,&scene_position_buffer); glGenTextures(1,&scene_position_texture);
    }
    if (scene_frame_sequence != frame.data().sequence) {
        std::vector<float> lights;
        for (const auto& light : frame.data().lights) {
            const float record[16] = {light.position.x,light.position.y,light.position.z,light.radius,
                light.color.x,light.color.y,light.color.z,light.falloff,
                light.direction.x,light.direction.y,light.direction.z,light.cone,
                light.directional ? 1.f : 0.f,light.headlight ? 1.f : 0.f,0,0};
            lights.insert(lights.end(),record,record+16);
        }
        if (lights.empty()) lights.resize(16,0);
        std::vector<int32_t> indices;
        std::vector<d3scene::Vec2> uvs;
        scene_light_ranges.clear();
        scene_uv_ranges.clear();
        const auto add = [&](const d3scene::SurfaceBinding& surface) {
            scene_light_ranges.emplace(&surface,ElementRange(static_cast<uint32_t>(indices.size()),static_cast<uint32_t>(surface.light_indices.size())));
            for (uint32_t index : surface.light_indices) indices.push_back(static_cast<int32_t>(index));
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            d3scene::forEachTriangleCorner(surface.baked_uvs.size(),[&](size_t corner) { uvs.push_back(surface.baked_uvs[corner]); });
            scene_uv_ranges.emplace(&surface,ElementRange(start,static_cast<uint32_t>(uvs.size())-start));
        };
        for (const auto& room : frame.data().rooms) for (const auto& surface : room.surfaces) add(surface);
        for (const auto& object : frame.data().objects) for (const auto& part : object.parts)
            for (const auto& surface : part.surfaces) add(surface);
        std::vector<d3scene::Vec3> positions;
        scene_position_ranges.clear();
        for (const auto& object : frame.data().objects) for (size_t i = 0; i < object.parts.size(); ++i) {
            const auto& part = object.parts[i];
            if (part.vertices.empty()) continue;
            const uint32_t start = static_cast<uint32_t>(positions.size());
            for (const auto& polygon : object.model->geometry().parts[i].geometry.polygons)
                d3scene::forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
                    positions.push_back(part.vertices[polygon.corners[corner].vertex]);
                });
            scene_position_ranges.emplace(&part,ElementRange(start,static_cast<uint32_t>(positions.size())-start));
        }
        if (positions.empty()) positions.push_back({0,0,0});
        glBindBuffer(GL_TEXTURE_BUFFER,scene_position_buffer);
        glBufferData(GL_TEXTURE_BUFFER,positions.size()*sizeof(d3scene::Vec3),positions.data(),GL_STREAM_DRAW);
        if (indices.empty()) indices.push_back(0);
        if (uvs.empty()) uvs.push_back({0,0});
        glBindBuffer(GL_TEXTURE_BUFFER,scene_uv_buffer);
        glBufferData(GL_TEXTURE_BUFFER,uvs.size()*sizeof(d3scene::Vec2),uvs.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_TEXTURE_BUFFER,scene_light_buffer);
        glBufferData(GL_TEXTURE_BUFFER,lights.size()*sizeof(float),lights.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_TEXTURE_BUFFER,scene_influence_buffer);
        glBufferData(GL_TEXTURE_BUFFER,indices.size()*sizeof(int32_t),indices.data(),GL_STREAM_DRAW);
        scene_frame_sequence = frame.data().sequence;
    }
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER,scene_light_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,scene_light_buffer);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_BUFFER,scene_influence_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_R32I,scene_influence_buffer);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_BUFFER,scene_uv_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RG32F,scene_uv_buffer);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_BUFFER,scene_position_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGB32F,scene_position_buffer);
    glActiveTexture(GL_TEXTURE0); Last_texel_unit_set = 0;
}

void GL4Renderer::UploadSceneMesh(SceneMeshGpu& gpu, d3scene::AssetId id, const d3scene::MeshGeometry& geometry) {
    if (gpu.id == id) return;
    if (gpu.vao) glDeleteVertexArrays(1,&gpu.vao);
    if (gpu.buffer) glDeleteBuffers(1,&gpu.buffer);
    gpu = {};
    gpu.id = id;
    std::vector<SceneVertex> vertices;
    for (const auto& polygon : geometry.polygons) {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        d3scene::forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
                const auto& c = polygon.corners[corner];
                vertices.push_back({geometry.vertices[c.vertex],c.u,c.v,c.light_u,c.light_v,c.alpha/255.f,
                    geometry.vertex_normals.empty() ? polygon.normal : geometry.vertex_normals[c.vertex]});
        });
        gpu.polygons.emplace_back(first,static_cast<uint32_t>(vertices.size())-first);
    }
    glGenVertexArrays(1,&gpu.vao); glGenBuffers(1,&gpu.buffer);
    glBindVertexArray(gpu.vao); glBindBuffer(GL_ARRAY_BUFFER,gpu.buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(SceneVertex),vertices.data(),GL_STATIC_DRAW);
    const int counts[5] = {3,2,2,1,3};
    const size_t offsets[5] = {offsetof(SceneVertex,position),offsetof(SceneVertex,u),offsetof(SceneVertex,lu),offsetof(SceneVertex,alpha),offsetof(SceneVertex,normal)};
    for (GLuint attribute = 0; attribute < 5; ++attribute) {
        glEnableVertexAttribArray(attribute);
        glVertexAttribPointer(attribute,counts[attribute],GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsets[attribute]));
    }
}

d3scene::VisibilityReport GL4Renderer::RenderSceneView(const d3scene::Presentation& presentation,
    const d3scene::View& view) {
    if (!view.geometry_inspection)
        Error("Scene executor migration incomplete: only explicit geometry inspection is currently supported.");
    const auto visibility = d3scene::queryView(presentation,view);
    FlushFontBatch();
    if (scene_asset_lifetime != presentation.data().scene_lifetime) {
        DestroySceneResources();
        scene_asset_lifetime = presentation.data().scene_lifetime;
    }
    if (!scene_shader.Handle()) scene_shader.AttachSource(SceneVertexShader, SceneFragmentShader);
    UploadSceneFrame(presentation);

    // The pass owns depth/blend/culling policy. No game draw-state capture and
    // no invocation of RenderMine/RenderTerrain/RenderObject occurs here.
    SetZBufferState(1);
    SetZBufferWriteMask(1);
    SetAlphaType(AT_ALWAYS);
    SetWrapType(WT_WRAP);
    SetFiltering(1);
    SetFogState(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    scene_shader.Use();
    auto vec = [&](const char* name, d3scene::Vec3 v) { glUniform3f(scene_shader.FindUniform(name),v.x,v.y,v.z); };
    vec("eye",view.camera.position); vec("camera_right",view.camera.right);
    vec("camera_up",view.camera.up); vec("camera_forward",view.camera.forward);
    const auto& projection = visibility.camera.projection;
    glUniform4f(scene_shader.FindUniform("projection"),projection.x_scale,projection.y_scale,
        projection.x_offset,projection.y_offset);
    glUniform1f(scene_shader.FindUniform("near_distance"),projection.near_distance);
    glUniform1i(scene_shader.FindUniform("colortexture"),0);
    glUniform1i(scene_shader.FindUniform("lightmaptexture"),1);
    glUniform1i(scene_shader.FindUniform("scene_lights"),2);
    glUniform1i(scene_shader.FindUniform("scene_influences"),3);
    glUniform1i(scene_shader.FindUniform("instance_uvs"),4);
    glUniform1i(scene_shader.FindUniform("instance_positions"),5);

    for (const auto& item : visibility.surfaces) {
        const d3scene::SurfaceBinding* binding;
        SceneMeshGpu* mesh;
        ElementRange prepared_positions(0,0);
        if (item.model) {
            const auto& object = presentation.data().objects[item.object_index];
            const auto id = object.model->id();
            if (scene_models.size() <= id.slot) scene_models.resize(static_cast<size_t>(id.slot)+1);
            auto& parts = scene_models[id.slot];
            if (parts.size() <= item.part_index) parts.resize(static_cast<size_t>(item.part_index)+1);
            mesh = &parts[item.part_index];
            UploadSceneMesh(*mesh,id,object.model->geometry().parts[item.part_index].geometry);
            binding = &object.parts[item.part_index].surfaces[item.polygon_index];
            const auto positions = scene_position_ranges.find(&object.parts[item.part_index]);
            if (positions != scene_position_ranges.end()) prepared_positions = positions->second;
        } else {
            const auto& room = presentation.data().rooms[item.room_index];
            if (scene_rooms.size() <= static_cast<size_t>(room.number)) scene_rooms.resize(room.number+1);
            mesh = &scene_rooms[room.number];
            UploadSceneMesh(*mesh,room.asset->id(),room.asset->geometry());
            binding = &room.surfaces[item.polygon_index];
        }
        const auto& surface = *binding;
        if ((surface.bitmap >= 0 && !surface.color_image) || (surface.lightmap_bitmap >= 0 && !surface.baked_image))
            Error("Scene surface reached execution without prepared image assets");
        const auto& gpu = *mesh;
        if (surface.color_image) BindSceneImage(*surface.color_image,0,true,false);
        if (surface.baked_image) BindSceneImage(*surface.baked_image,1,surface.lightmap_wrap,true);
        scene_shader.Use();
        const auto influences = scene_light_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("influence_offset"),influences.offset);
        glUniform1i(scene_shader.FindUniform("influence_count"),view.dynamic_lighting ? influences.count : 0);
        vec("model_position",item.world.position); vec("model_right",item.world.right);
        vec("model_up",item.world.up); vec("model_forward",item.world.forward);
        vec("base_color",surface.base_color);
        vec("color_modulation",surface.color_modulation);
        vec("ambient_color",surface.ambient_color); vec("ambient_direction",surface.ambient_direction);
        glUniform1i(scene_shader.FindUniform("directional_ambient"),surface.directional_ambient);
        glUniform1f(scene_shader.FindUniform("lighting_modulation"),surface.lighting_modulation);
        glUniform1i(scene_shader.FindUniform("use_texture"),surface.color_image != nullptr);
        glUniform1i(scene_shader.FindUniform("use_lightmap"),surface.baked_image != nullptr);
        glUniform1i(scene_shader.FindUniform("use_texture_alpha"),surface.material.texture_alpha != d3scene::TextureAlpha::Ignore);
        glUniform1f(scene_shader.FindUniform("alpha_cutoff"),surface.material.alpha_cutoff);
        glUniform2f(scene_shader.FindUniform("light_scale"),surface.lightmap_u_scale,surface.lightmap_v_scale);
        glUniform2f(scene_shader.FindUniform("texture_offset"),surface.u_offset,surface.v_offset);
        glUniform1f(scene_shader.FindUniform("material_alpha"),surface.alpha);
        if(surface.material.blended()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,surface.material.blend == d3scene::BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        } else glDisable(GL_BLEND);
        glDepthMask(surface.material.writesSolidDepth() ? GL_TRUE : GL_FALSE);
        glBindVertexArray(gpu.vao);
        const auto range=gpu.polygons[item.polygon_index];
        const auto uv_range = scene_uv_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("use_instance_uvs"),uv_range.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_uv_offset"),static_cast<int>(uv_range.offset)-static_cast<int>(range.offset));
        glUniform1i(scene_shader.FindUniform("use_instance_positions"),prepared_positions.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_position_offset"),prepared_positions.offset);
        glDrawArrays(GL_TRIANGLES,range.offset,range.count);
        rend_RecordDrawCall(RENDERER_DRAW_CALL_3D);
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    ShaderProgram::ClearBinding();
    // The remaining canvas adapter maintains its own texture binding cache.
    // Invalidate that cache rather than lying about scene-owned GL names.
    ClearBoundTextures();
    glActiveTexture(GL_TEXTURE0); Last_texel_unit_set = 0;
    rendTEMP_UnbindVertexBuffer();
    rend_NotifyDepthBufferWrite();
    // Broad-phase geometry inspection is NOT gameplay visibility. Do not mark
    // every submitted room explored merely because it was depth-tested.
    d3scene::VisibilityReport result;
    result.frame_sequence=presentation.data().sequence;
    result.scene_lifetime=presentation.data().scene_lifetime;
    result.view_history_key=view.history_key;
    return result;
}
