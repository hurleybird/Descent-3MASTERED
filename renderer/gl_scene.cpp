// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "gl_mesh.h"
#include "SceneView.h"
#include "SceneGeometry.h"
#include "SceneImages.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include <cstring>
#include <cmath>
#include <string>
extern int Last_texel_unit_set;

namespace {
struct SceneVertex { d3scene::Vec3 position; float u, v, lu, lv, alpha; d3scene::Vec3 normal, color; };
const char* SceneLightingShader = R"GLSL(#version 330 core
uniform samplerBuffer scene_lights;
uniform isamplerBuffer scene_influences;
uniform int influence_offset, influence_count;
uniform int lighting_per_pixel;
vec3 sceneDynamicLighting(vec3 world_position) {
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
    return dynamic_color;
}
)GLSL";
const std::string SceneVertexShader = std::string(SceneLightingShader) + R"GLSL(
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec2 light_uv;
layout(location=3) in float alpha;
layout(location=4) in vec3 normal;
layout(location=5) in vec3 vertex_color;
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
out vec3 vertex_dynamic;
out vec3 authored_color;
void main() {
    vec3 prepared = use_instance_positions != 0 ? texelFetch(instance_positions,gl_VertexID+instance_position_offset).rgb : position;
    vec3 world = model_position + model_right*prepared.x + model_up*prepared.y + model_forward*prepared.z;
    vec3 relative = world-eye;
    world_position = world;
    authored_color = vertex_color;
    vertex_dynamic = lighting_per_pixel != 0 ? vec3(0) : sceneDynamicLighting(world);
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
const std::string SceneFragmentShader = std::string(SceneLightingShader) + R"GLSL(
uniform sampler2D colortexture, lightmaptexture;
uniform int use_lightmap, use_texture_alpha;
uniform int use_texture;
uniform int unlit;
uniform float occlusion_class;
uniform samplerBuffer scene_fog;
uniform int fog_volume_count, fog_enabled, fog_additive, fog_exterior;
uniform int multiply_material;
uniform sampler2D opaque_depth;
uniform float soft_depth_range, near_distance;
uniform ivec2 depth_snapshot_origin;
uniform vec3 eye, camera_forward, exterior_fog_color;
uniform vec2 exterior_fog_range;
uniform vec3 base_color;
uniform vec3 color_modulation;
uniform vec2 light_scale, texture_offset;
uniform float material_alpha;
uniform float alpha_cutoff;
uniform float lighting_modulation;
in vec3 world_position;
in vec3 ambient;
in vec3 vertex_dynamic;
in vec3 authored_color;
in vec2 texcoord, lightcoord;
in float vertex_alpha;
layout(location=0) out vec4 color;
// Material metadata: G retains the current bloom-protection contract and A
// identifies the opaque depth owner's AO class. AO uses eligible RGB below,
// not an estimated fog/brightness suppression mask.
layout(location=2) out vec4 post_coverage;
layout(location=3) out vec4 ao_radiance;
bool fogHit(vec3 a,vec3 b,vec3 c,vec3 direction,out float distance) {
    vec3 n = cross(b-a,c-a);
    float denominator = dot(n,direction);
    if (abs(denominator) < 1.e-12) return false;
    distance = dot(n,a-eye)/denominator;
    if (distance < 0.0) return false;
    vec3 p = eye+direction*distance;
    vec3 magnitude = abs(n);
    int drop = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0 : (magnitude.y >= magnitude.z ? 1 : 2);
    int x = (drop+1)%3, y = (drop+2)%3;
    vec3 corners[3] = vec3[3](a,b,c);
    float orientation = n[drop] < 0.0 ? -1.0 : 1.0;
    for (int i = 0; i < 3; ++i) {
        vec3 start = corners[i], finish = corners[(i+1)%3];
        vec2 edge = vec2(finish[x]-start[x],finish[y]-start[y])*orientation;
        float side = edge.x*(p[y]-start[y])-edge.y*(p[x]-start[x]);
        if (side < 0.0 || (side == 0.0 && !(edge.y > 0.0 || (edge.y == 0.0 && edge.x < 0.0)))) return false;
    }
    return true;
}
bool fogBounds(vec3 low,vec3 high,vec3 direction,float length) {
    float entry = 0.0, exit = length;
    for (int axis = 0; axis < 3; ++axis) {
        if (abs(direction[axis]) < 1.e-8) {
            if (eye[axis] < low[axis] || eye[axis] > high[axis]) return false;
        } else {
            float a = (low[axis]-eye[axis])/direction[axis], b = (high[axis]-eye[axis])/direction[axis];
            entry = max(entry,min(a,b)); exit = min(exit,max(a,b));
            if (entry > exit) return false;
        }
    }
    return true;
}
vec3 applySceneFog(vec3 radiance,out float transmission) {
    transmission = 1.0;
    if (fog_enabled == 0) return radiance;
    vec3 delta = world_position-eye;
    float distance = length(delta);
    if (distance < 1.e-6) return radiance;
    vec3 direction = delta/distance;
    float forward_scale = max(0.0,dot(direction,camera_forward));
    if (fog_exterior != 0) {
        float amount = clamp((distance*forward_scale-exterior_fog_range.x)/(exterior_fog_range.y-exterior_fog_range.x),0.0,1.0);
        transmission *= 1.0-amount;
        radiance = mix(radiance,multiply_material != 0 ? vec3(1) : (fog_additive != 0 ? vec3(0) : exterior_fog_color),amount);
    }
    float optical_depth = 0.0;
    vec3 weighted_color = vec3(0);
    for (int volume = 0; volume < fog_volume_count; ++volume) {
        vec4 appearance = texelFetch(scene_fog,volume*4);
        vec4 low = texelFetch(scene_fog,volume*4+1), high = texelFetch(scene_fog,volume*4+2);
        if (!fogBounds(low.xyz,high.xyz,direction,distance)) continue;
        vec4 state = texelFetch(scene_fog,volume*4+3);
        float path = state.x*distance;
        int node = 0;
        while (node < int(state.y)) {
            int record = int(low.w)+node*3;
            vec4 node_low = texelFetch(scene_fog,record), node_high = texelFetch(scene_fog,record+1);
            if (!fogBounds(node_low.xyz,node_high.xyz,direction,distance)) { node = int(node_high.w); continue; }
            int count = int(texelFetch(scene_fog,record+2).x);
            for (int triangle = 0; triangle < count; ++triangle) {
                int offset = int(high.w)+(int(node_low.w)+triangle)*4;
                float crossing;
                if (fogHit(texelFetch(scene_fog,offset).xyz,texelFetch(scene_fog,offset+1).xyz,texelFetch(scene_fog,offset+2).xyz,direction,crossing) &&
                    crossing > 1.e-5 && crossing < distance)
                    path += (distance-crossing)*(dot(texelFetch(scene_fog,offset+3).xyz,direction) > 0.0 ? 1.0 : -1.0);
            }
            ++node;
        }
        // Preserve the authored linear camera-forward depth law, while measuring
        // only the part of the ray actually inside connected fog space.
        float amount = clamp(path,0.0,distance)*forward_scale/appearance.w;
        optical_depth += amount; weighted_color += appearance.rgb*amount;
    }
    if (optical_depth > 0.0) {
        radiance = mix(radiance,multiply_material != 0 ? vec3(1) : (fog_additive != 0 ? vec3(0) : weighted_color/optical_depth),clamp(optical_depth,0.0,1.0));
        transmission *= 1.0-clamp(optical_depth,0.0,1.0);
    }
    return radiance;
}
void main() {
    vec4 base=use_texture != 0 ? texture(colortexture,texcoord+texture_offset) : vec4(base_color,1);
    base.a=(use_texture_alpha != 0 ? base.a : 1.0)*vertex_alpha*material_alpha;
    if (soft_depth_range > 0.0) {
        ivec2 size = textureSize(opaque_depth,0);
        ivec2 pixel = clamp(ivec2(gl_FragCoord.xy)-depth_snapshot_origin,ivec2(0),size-ivec2(1));
        float depth = texelFetch(opaque_depth,pixel,0).r;
        if (depth < 1.0) {
            float surface_depth = near_distance/max(1.e-8,1.0-depth);
            float particle_depth = dot(world_position-eye,camera_forward);
            base.a *= clamp((surface_depth-particle_depth)/soft_depth_range,0.0,1.0);
        }
    }
    if(base.a < alpha_cutoff) discard;
    vec3 illumination = use_lightmap != 0 ? texture(lightmaptexture,lightcoord*light_scale).rgb : ambient;
    vec3 dynamic_color = lighting_per_pixel != 0 ? sceneDynamicLighting(world_position) : vertex_dynamic;
    illumination = clamp(illumination+dynamic_color,vec3(0),vec3(1));
    if (unlit != 0) illumination = vec3(1);
    color=vec4(base.rgb*color_modulation*authored_color*illumination*lighting_modulation,base.a);
    if (multiply_material != 0) color.rgb = mix(vec3(1),color.rgb,color.a);
    vec3 surface_radiance = unlit != 0 ? vec3(0) : color.rgb;
    float transmission;
    color.rgb = applySceneFog(color.rgb,transmission);
    ao_radiance = vec4(multiply_material != 0 ? color.rgb : surface_radiance*transmission,base.a);
    post_coverage = vec4(0,1.0-transmission,0,occlusion_class/255.0);
}
)GLSL";
}

void GL4Renderer::DestroySceneResources() {
    scene_view_history.clear();
    scene_temporal_reset = true;
    if (scene_ao_radiance_texture) {
        GLint target; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
        for (const auto& framebuffer : framebuffers) if (framebuffer.Handle()) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer.Handle());
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT3,0,0);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
        glDeleteTextures(1,&scene_ao_radiance_texture);
    }
    scene_ao_radiance_texture = 0;
    scene_ao_width = scene_ao_height = scene_ao_samples = 0;
    scene_ao_resolved.Destroy(); scene_ao_product.Destroy();
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
    for (auto& patch : scene_terrain) for (auto& mesh : patch) {
        if (mesh.vao) glDeleteVertexArrays(1,&mesh.vao);
        if (mesh.buffer) glDeleteBuffers(1,&mesh.buffer);
    }
    scene_terrain.clear();
    for (auto& mesh : scene_meshes) {
        if (mesh.vao) glDeleteVertexArrays(1,&mesh.vao);
        if (mesh.buffer) glDeleteBuffers(1,&mesh.buffer);
    }
    scene_meshes.clear();
    if (scene_fog_buffer) glDeleteBuffers(1,&scene_fog_buffer);
    if (scene_fog_texture) glDeleteTextures(1,&scene_fog_texture);
    scene_fog_buffer = scene_fog_texture = 0;
    scene_fog_id = {};
    if (scene_depth_snapshot) glDeleteTextures(1,&scene_depth_snapshot);
    if (scene_depth_framebuffer) glDeleteFramebuffers(1,&scene_depth_framebuffer);
    scene_depth_snapshot = scene_depth_framebuffer = 0;
    scene_depth_width = scene_depth_height = 0;
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
        for (const auto& patch : frame.data().terrain.patches) for (const auto& surface : patch) add(surface);
        for (const auto& mesh : frame.data().meshes) add(mesh.surface);
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
                    geometry.vertex_normals.empty() ? polygon.normal : geometry.vertex_normals[c.vertex],
                    geometry.vertex_colors.empty() ? d3scene::Vec3{1,1,1} : geometry.vertex_colors[c.vertex]});
        });
        gpu.polygons.emplace_back(first,static_cast<uint32_t>(vertices.size())-first);
    }
    glGenVertexArrays(1,&gpu.vao); glGenBuffers(1,&gpu.buffer);
    glBindVertexArray(gpu.vao); glBindBuffer(GL_ARRAY_BUFFER,gpu.buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(SceneVertex),vertices.data(),GL_STATIC_DRAW);
    const int counts[6] = {3,2,2,1,3,3};
    const size_t offsets[6] = {offsetof(SceneVertex,position),offsetof(SceneVertex,u),offsetof(SceneVertex,lu),offsetof(SceneVertex,alpha),offsetof(SceneVertex,normal),offsetof(SceneVertex,color)};
    for (GLuint attribute = 0; attribute < 6; ++attribute) {
        glEnableVertexAttribArray(attribute);
        glVertexAttribPointer(attribute,counts[attribute],GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsets[attribute]));
    }
}

void GL4Renderer::BeginSceneRadianceProduct() {
    const auto& target = framebuffers[framebuffer_current_draw];
    const auto samples = target.Samples();
    const GLenum type = samples >= 2 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    if (!scene_ao_radiance_texture || scene_ao_width != target.Width() ||
        scene_ao_height != target.Height() || scene_ao_samples != samples) {
        if (scene_ao_radiance_texture) glDeleteTextures(1,&scene_ao_radiance_texture);
        glGenTextures(1,&scene_ao_radiance_texture);
        glBindTexture(type,scene_ao_radiance_texture);
        if (samples >= 2) glTexImage2DMultisample(type,samples,GL_RGBA16F,target.Width(),target.Height(),GL_TRUE);
        else {
            glTexImage2D(type,0,GL_RGBA16F,target.Width(),target.Height(),0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(type,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(type,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        scene_ao_width = target.Width(); scene_ao_height = target.Height(); scene_ao_samples = samples;
    }
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT3,type,scene_ao_radiance_texture,0);
    const GLenum targets[4] = {GL_COLOR_ATTACHMENT0,GL_NONE,
        static_cast<GLenum>(post_protection_mask.mask_texture ? GL_COLOR_ATTACHMENT2 : GL_NONE),GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4,targets);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        Error("Scene radiance outputs are incomplete");
    glColorMaski(3,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const GLfloat zero[4] = {};
    glClearBufferfv(GL_COLOR,3,zero);
}

void GL4Renderer::FinishSceneRadianceProduct() {
    GLint previous_read, previous_draw;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_draw);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    scene_ao_resolved.Update(scene_ao_width,scene_ao_height,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    scene_ao_product.Update(framebuffer_logical_width,framebuffer_logical_height,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffers[framebuffer_current_draw].Handle());
    glReadBuffer(GL_COLOR_ATTACHMENT3);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,scene_ao_resolved.Handle());
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,scene_ao_width,scene_ao_height,0,0,scene_ao_width,scene_ao_height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,scene_ao_resolved.Handle());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,scene_ao_product.Handle());
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,scene_ao_width,scene_ao_height,0,0,framebuffer_logical_width,framebuffer_logical_height,
        GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_draw);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    // World translucency is already incorporated in eligible RGB. Alpha now
    // belongs only to later canvas/cockpit layers, avoiding double attenuation.
    framebuffers[framebuffer_current_draw].ClearAlphaToZero();
    UseSceneDrawBuffers();
}

d3scene::VisibilityReport GL4Renderer::RenderSceneView(const d3scene::Presentation& presentation,
    const d3scene::View& view) {
    if (!view.geometry_inspection)
        Error("Scene executor migration incomplete: only explicit geometry inspection is currently supported.");
    const auto visibility = d3scene::queryView(presentation,view);
    const auto check_scene_error = [](const char* stage) { const auto error = glGetError(); if (error != GL_NO_ERROR) Error("Scene GL error 0x%x at %s",error,stage); };
    check_scene_error("entry");
    FlushFontBatch();
    check_scene_error("font flush");
    if (scene_asset_lifetime != presentation.data().scene_lifetime) {
        DestroySceneResources();
        scene_asset_lifetime = presentation.data().scene_lifetime;
    }
    const auto history = scene_view_history.prepare(presentation.data(),view,visibility.camera);
    if (view.kind == d3scene::ViewKind::Main) {
        scene_temporal_reset = !history.previous_valid;
        have_previous_view_projection = history.previous_valid;
        std::memcpy(previous_view_projection,history.previous.view_projection.data(),sizeof(previous_view_projection));
    }
    if (!scene_shader.Handle()) scene_shader.AttachSource(SceneVertexShader.c_str(), SceneFragmentShader.c_str());
    UploadSceneFrame(presentation);
    check_scene_error("frame upload");

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
    post_protection_mask.UseSceneDrawBuffers(framebuffers[framebuffer_current_draw].Handle(),false,false);
    const d3scene::ViewProducts products{view.kind == d3scene::ViewKind::Main,
        view.kind == d3scene::ViewKind::Main && OpenGL_preferred_state.ao_enabled && framebuffer_ok};
    if (products.occlusion_radiance) BeginSceneRadianceProduct();
    post_protection_mask.resolve_valid = false;
    if (visibility.exterior_visible && presentation.data().sky_enabled) {
        // Clear only this view's color attachment. Background geometry never
        // tests or writes world depth, including in auxiliary viewports.
        GLint viewport[4], previous_scissor[4]; GLfloat previous_color[4];
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_SCISSOR_BOX,previous_scissor);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,previous_color);
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewport[0],viewport[1],viewport[2],viewport[3]);
        const auto color = presentation.data().sky_color;
        const GLfloat clear_color[4] = {color.x,color.y,color.z,1};
        glClearBufferfv(GL_COLOR,0,clear_color);
        glClearColor(previous_color[0],previous_color[1],previous_color[2],previous_color[3]);
        glScissor(previous_scissor[0],previous_scissor[1],previous_scissor[2],previous_scissor[3]);
        if (!scissor) glDisable(GL_SCISSOR_TEST);
    }
    scene_shader.Use();
    auto vec = [&](const char* name, d3scene::Vec3 v) { glUniform3f(scene_shader.FindUniform(name),v.x,v.y,v.z); };
    const auto& camera = visibility.camera.transform;
    vec("eye",camera.position); vec("camera_right",camera.right);
    vec("camera_up",camera.up); vec("camera_forward",camera.forward);
    const auto& projection = visibility.camera.projection;
    glUniform4f(scene_shader.FindUniform("projection"),projection.x_scale,projection.y_scale,
        projection.x_offset,projection.y_offset);
    glUniform1f(scene_shader.FindUniform("near_distance"),projection.near_distance);
    glUniform1i(scene_shader.FindUniform("lighting_per_pixel"),presentation.data().lighting_quality == d3scene::LightingQuality::Pixel);
    glUniform1i(scene_shader.FindUniform("colortexture"),0);
    glUniform1i(scene_shader.FindUniform("lightmaptexture"),1);
    glUniform1i(scene_shader.FindUniform("scene_lights"),2);
    glUniform1i(scene_shader.FindUniform("scene_influences"),3);
    glUniform1i(scene_shader.FindUniform("instance_uvs"),4);
    glUniform1i(scene_shader.FindUniform("instance_positions"),5);
    glUniform1i(scene_shader.FindUniform("scene_fog"),6);
    const auto& fog_volumes = d3scene::fogVolumes(presentation.data());
    const auto fog_id = presentation.data().fog ? presentation.data().fog->id() : d3scene::AssetId{};
    const bool upload_fog = !scene_fog_buffer || scene_fog_id != fog_id;
    if (!scene_fog_buffer) { glGenBuffers(1,&scene_fog_buffer); glGenTextures(1,&scene_fog_texture); }
    glBindBuffer(GL_TEXTURE_BUFFER,scene_fog_buffer);
    if (upload_fog) {
    std::vector<float> fog(fog_volumes.size()*16,0);
    for (size_t index = 0; index < fog_volumes.size(); ++index) {
        const auto& volume = fog_volumes[index];
        const auto& low = volume.bounds.minimum; const auto& high = volume.bounds.maximum;
        const float record[16] = {volume.color.x,volume.color.y,volume.color.z,volume.depth,
            low.x,low.y,low.z,float(fog.size()/4),high.x,high.y,high.z,float(fog.size()/4+volume.nodes.size()*3),
            0,float(volume.nodes.size()),0,0};
        std::copy(record,record+16,fog.begin()+index*16);
        for (const auto& node : volume.nodes) {
            const auto& a = node.bounds.minimum; const auto& b = node.bounds.maximum;
            fog.insert(fog.end(),{a.x,a.y,a.z,float(node.first),b.x,b.y,b.z,float(node.escape),float(node.count),0,0,0});
        }
        for (const auto& triangle : volume.boundary) for (const auto& point : {triangle.a,triangle.b,triangle.c,triangle.inward})
            fog.insert(fog.end(),{point.x,point.y,point.z,0});
    }
    if (fog.empty()) fog.resize(4,0);
    glBufferData(GL_TEXTURE_BUFFER,fog.size()*sizeof(float),fog.data(),GL_DYNAMIC_DRAW);
    scene_fog_id = fog_id;
    }
    // Only containment depends on the view. Geometry and the traversal index
    // stay resident until their scene asset revision changes.
    for (size_t index = 0; index < fog_volumes.size(); ++index) {
        const float inside = d3scene::fogContains(fog_volumes[index],camera.position) ? 1.f : 0.f;
        glBufferSubData(GL_TEXTURE_BUFFER,(index*16+12)*sizeof(float),sizeof(float),&inside);
    }
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,scene_fog_buffer);
    glUniform1i(scene_shader.FindUniform("fog_volume_count"),static_cast<GLint>(fog_volumes.size()));
    const auto& exterior_fog = presentation.data().exterior_fog;
    vec("exterior_fog_color",exterior_fog.color);
    glUniform2f(scene_shader.FindUniform("exterior_fog_range"),exterior_fog.start,exterior_fog.end);
    const auto exteriorRoom = [&](int room) {
        if (room < 0) return true;
        for (const auto& state : presentation.data().rooms) if (state.number == room) return state.exterior;
        return true;
    };

    const auto passes = d3scene::buildViewPasses(presentation,visibility,products);
    check_scene_error("pass setup");
    for (const auto& pass : passes) {
      if (pass.kind == d3scene::ViewPassKind::ExportRadiance) {
        FinishSceneRadianceProduct();
        continue;
      }
      if (pass.kind == d3scene::ViewPassKind::ExportOpaqueScene) {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        ExportOpaqueScene(visibility.camera);
        const GLenum targets[4] = {GL_COLOR_ATTACHMENT0,GL_NONE,
            static_cast<GLenum>(post_protection_mask.mask_texture ? GL_COLOR_ATTACHMENT2 : GL_NONE),
            static_cast<GLenum>(products.occlusion_radiance ? GL_COLOR_ATTACHMENT3 : GL_NONE)};
        glDrawBuffers(4,targets);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        // Image-space resolve owns its own bindings. Resume the scene pipeline
        // explicitly, including buffer textures used by both shader stages.
        UploadSceneFrame(presentation);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,scene_depth_snapshot);
        scene_shader.Use();
        glDepthFunc(GL_LEQUAL);
        continue;
      }
      if (pass.kind == d3scene::ViewPassKind::SnapshotOpaqueDepth) {
        const auto incoming_error = glGetError();
        if (incoming_error != GL_NO_ERROR) Error("Scene reached opaque-depth snapshot with prior GL error 0x%x",incoming_error);
        GLint viewport[4], previous_read, previous_draw;
        glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_draw);
        if (!scene_depth_snapshot) glGenTextures(1,&scene_depth_snapshot);
        if (!scene_depth_framebuffer) glGenFramebuffers(1,&scene_depth_framebuffer);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,scene_depth_snapshot);
        if (scene_depth_width != viewport[2] || scene_depth_height != viewport[3]) {
            scene_depth_width = viewport[2]; scene_depth_height = viewport[3];
            // Scene render targets use 32-bit float depth, including MSAA. The
            // snapshot has identical pixel extent and resolves depth only.
            glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,viewport[2],viewport[3],0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,scene_depth_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,scene_depth_snapshot,0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Error("Scene opaque-depth snapshot is incomplete");
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_draw);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],0,0,viewport[2],viewport[3],GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_draw);
        const auto snapshot_error = glGetError();
        if (snapshot_error != GL_NO_ERROR) Error("Scene opaque-depth snapshot failed: GL 0x%x source=%d viewport=%d,%d,%d,%d",
            snapshot_error,previous_draw,viewport[0],viewport[1],viewport[2],viewport[3]);
        glUniform1i(scene_shader.FindUniform("opaque_depth"),7);
        glUniform2i(scene_shader.FindUniform("depth_snapshot_origin"),viewport[0],viewport[1]);
        continue;
      }
      for (uint32_t item_index = pass.begin; item_index < pass.end; ++item_index) {
        const auto& item = visibility.surfaces[item_index];
        const d3scene::SurfaceBinding* binding;
        SceneMeshGpu* mesh;
        ElementRange prepared_positions(0,0);
        if (item.mesh) {
            const auto& instance = presentation.data().meshes[item.mesh_index];
            const auto id = instance.asset->id();
            if (scene_meshes.size() <= id.slot) scene_meshes.resize(id.slot+1);
            mesh = &scene_meshes[id.slot]; UploadSceneMesh(*mesh,id,instance.asset->geometry());
            binding = &instance.surface;
        } else if (item.terrain) {
            const auto& terrain = presentation.data().terrain;
            if (scene_terrain.size() <= item.terrain_patch) scene_terrain.resize(item.terrain_patch+1);
            auto& batches = scene_terrain[item.terrain_patch];
            if (batches.size() <= item.terrain_batch) batches.resize(item.terrain_batch+1);
            mesh = &batches[item.terrain_batch];
            UploadSceneMesh(*mesh,terrain.asset->id(),terrain.asset->patches()[item.terrain_patch].batches[item.terrain_batch].geometry);
            binding = &terrain.patches[item.terrain_patch][item.terrain_batch];
        } else if (item.model) {
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
        if (surface.color_image) BindSceneImage(*surface.color_image,0,surface.texture_wrap,false);
        if (surface.baked_image) BindSceneImage(*surface.baked_image,1,surface.lightmap_wrap,true);
        scene_shader.Use();
        const auto influences = scene_light_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("influence_offset"),influences.offset);
        glUniform1i(scene_shader.FindUniform("influence_count"),view.dynamic_lighting && !surface.unlit ? influences.count : 0);
        glUniform1i(scene_shader.FindUniform("unlit"),surface.unlit);
        glUniform1f(scene_shader.FindUniform("occlusion_class"),item.terrain ? RENDERER_AO_CLASS_TERRAIN :
            (item.model ? RENDERER_AO_CLASS_POLYOBJECT : RENDERER_AO_CLASS_MINE));
        glUniform1f(scene_shader.FindUniform("soft_depth_range"),surface.material.layer == d3scene::RenderLayer::World ? surface.soft_depth_range : 0);
        glUniform1i(scene_shader.FindUniform("fog_enabled"),surface.material.layer == d3scene::RenderLayer::World);
        glUniform1i(scene_shader.FindUniform("fog_additive"),surface.material.blend == d3scene::BlendMode::Additive);
        glUniform1i(scene_shader.FindUniform("multiply_material"),surface.material.blend == d3scene::BlendMode::Multiply);
        const bool exterior = item.terrain || (item.model ? exteriorRoom(presentation.data().objects[item.object_index].room) :
            (item.mesh ? exteriorRoom(presentation.data().meshes[item.mesh_index].room) : presentation.data().rooms[item.room_index].exterior));
        glUniform1i(scene_shader.FindUniform("fog_exterior"),exterior && exterior_fog.enabled);
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
            glEnablei(GL_BLEND,0);
            if (surface.material.blend == d3scene::BlendMode::Multiply) glBlendFuncSeparatei(0,GL_DST_COLOR,GL_ZERO,GL_ZERO,GL_ONE);
            else if (surface.material.blend == d3scene::BlendMode::Additive) glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_ONE);
            else glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        } else glDisablei(GL_BLEND,0);
        // Metadata belongs to the solid depth sample. Blended radiance changes
        // color/remaining opaque coverage, never the hidden surface's class.
        const GLboolean solid = surface.material.writesSolidDepth() ? GL_TRUE : GL_FALSE;
        glDisablei(GL_BLEND,2);
        glColorMaski(2,solid,solid,solid,solid);
        // The eligible-radiance image follows the same RGB compositing law.
        // Translucent surfaces do not have their own depth AO, so only transmit
        // the already-visible opaque radiance; additive light leaves it alone.
        glColorMaski(3,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        if (solid || surface.material.layer == d3scene::RenderLayer::Background) glDisablei(GL_BLEND,3);
        else {
            glEnablei(GL_BLEND,3);
            if (surface.material.blend == d3scene::BlendMode::Multiply) glBlendFunci(3,GL_DST_COLOR,GL_ZERO);
            else if (surface.material.blend == d3scene::BlendMode::Additive) glBlendFunci(3,GL_ZERO,GL_ONE);
            else glBlendFunci(3,GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);
        }
        glDepthMask(surface.material.writesSolidDepth() ? GL_TRUE : GL_FALSE);
        if (surface.material.testsDepth()) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glBindVertexArray(gpu.vao);
        const auto range = item.terrain || item.mesh ? ElementRange(0,gpu.polygons.back().offset+gpu.polygons.back().count) : gpu.polygons[item.polygon_index];
        const auto uv_range = scene_uv_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("use_instance_uvs"),uv_range.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_uv_offset"),static_cast<int>(uv_range.offset)-static_cast<int>(range.offset));
        glUniform1i(scene_shader.FindUniform("use_instance_positions"),prepared_positions.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_position_offset"),prepared_positions.offset);
        if (item.terrain || (item.mesh && !surface.two_sided)) { glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(GL_BACK); }
        else glDisable(GL_CULL_FACE);
        glDrawArrays(GL_TRIANGLES,range.offset,range.count);
        rend_RecordDrawCall(RENDERER_DRAW_CALL_3D);
      }
    }
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    framebuffers[framebuffer_current_draw].MarkAllDirty();
    post_protection_mask_dirty = true;
    post_protection_mask.resolve_valid = false;
    ShaderProgram::ClearBinding();
    // The remaining canvas adapter maintains its own texture binding cache.
    // Invalidate that cache rather than lying about scene-owned GL names.
    ClearBoundTextures();
    glActiveTexture(GL_TEXTURE0); Last_texel_unit_set = 0;
    rendTEMP_UnbindVertexBuffer();
    rend_NotifyDepthBufferWrite();
    scene_view_history.commit(history);
    // Broad-phase geometry inspection is NOT gameplay visibility. Do not mark
    // every submitted room explored merely because it was depth-tested.
    d3scene::VisibilityReport result;
    result.frame_sequence=presentation.data().sequence;
    result.scene_lifetime=presentation.data().scene_lifetime;
    result.view_history_key=view.history_key;
    return result;
}
