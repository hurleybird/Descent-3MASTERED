// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "gl_mesh.h"
#include "SceneView.h"
#include "SceneGeometry.h"
#include "SceneImages.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include "SceneMotion.h"
#include "gameloop.h"
#include "args.h"
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
extern int Last_texel_unit_set;

namespace {
static_assert(static_cast<int>(d3scene::OcclusionClass::Default) == RENDERER_AO_CLASS_DEFAULT &&
    static_cast<int>(d3scene::OcclusionClass::Terrain) == RENDERER_AO_CLASS_TERRAIN &&
    static_cast<int>(d3scene::OcclusionClass::Model) == RENDERER_AO_CLASS_POLYOBJECT &&
    static_cast<int>(d3scene::OcclusionClass::MineRock) == RENDERER_AO_CLASS_MINE_ROCK &&
    static_cast<int>(d3scene::OcclusionClass::Mine) == RENDERER_AO_CLASS_MINE,"Canvas AO class adapter differs from scene contract");
struct SceneVertex { d3scene::Vec3 position; float u, v, lu, lv, alpha; d3scene::Vec3 normal, color; d3scene::Vec2 raster_offset; };
const char* SceneLightingShader = R"GLSL(#version 330 core
uniform samplerBuffer scene_lights;
uniform isamplerBuffer scene_influences;
uniform int influence_offset, influence_count;
uniform int lighting_per_pixel;
float sceneEmitterAttenuation(int light,vec3 world_position,out vec3 incident) {
    vec4 position_radius = texelFetch(scene_lights,light);
    vec4 color_falloff = texelFetch(scene_lights,light+1);
    vec4 direction_cone = texelFetch(scene_lights,light+2);
    vec4 flags = texelFetch(scene_lights,light+3);
    vec3 delta = world_position-position_radius.xyz;
    float distance = length(delta);
    incident = distance > .000001 ? delta/distance : vec3(0);
    if (distance >= position_radius.w) return 0;
    float attenuation = pow(1.0-distance/position_radius.w,color_falloff.w);
    if (flags.x != 0) {
        float cone = distance > .000001 ? dot(incident,direction_cone.xyz) : 1.0;
        attenuation *= max(0.0,(cone-direction_cone.w)/max(.000001,1.0-direction_cone.w));
    }
    return attenuation;
}
vec3 sceneDynamicLighting(vec3 world_position) {
    vec3 dynamic_color = vec3(0);
    for (int index = 0; index < influence_count; ++index) {
        int light = texelFetch(scene_influences,influence_offset+index).r*4;
        vec4 color_falloff = texelFetch(scene_lights,light+1);
        vec3 incident;
        float attenuation = sceneEmitterAttenuation(light,world_position,incident);
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
layout(location=6) in vec2 raster_offset;
uniform vec2 raster_pixel_scale;
uniform samplerBuffer instance_uvs;
uniform int use_instance_uvs, instance_uv_offset;
uniform int use_texture_uvs, texture_uv_offset;
uniform samplerBuffer instance_positions;
uniform int use_instance_positions, instance_position_offset;
uniform samplerBuffer scene_reflections;
uniform int reflection_normal_offset, use_reflection_normals;
uniform int reflection_field_offset, use_reflection_field;
out vec3 reflection_normal;
out vec3 field_centers[4], field_colors[4];
out vec3 field_modulation;
uniform vec3 ambient_color, ambient_direction;
uniform int directional_ambient;
uniform vec3 eye, camera_right, camera_up, camera_forward;
uniform vec4 world_clip_plane;
uniform vec3 model_position, model_right, model_up, model_forward;
uniform vec3 previous_position, previous_right, previous_up, previous_forward;
uniform mat4 previous_view_projection;
out vec4 current_clip, previous_clip;
uniform vec4 projection;
uniform float near_distance, effect_depth_offset;
uniform float orthographic_depth_scale;
out vec2 texcoord, lightcoord;
out float vertex_alpha;
out vec3 world_position;
out vec3 ambient;
out vec3 vertex_dynamic;
out vec3 authored_color;
void main() {
    int position_record = (gl_VertexID+instance_position_offset)*2;
    vec3 prepared = use_instance_positions != 0 ? texelFetch(instance_positions,position_record).rgb : position;
    vec3 previous_prepared = use_instance_positions != 0 ? texelFetch(instance_positions,position_record+1).rgb : position;
    vec3 world = model_position + model_right*prepared.x + model_up*prepared.y + model_forward*prepared.z;
    vec3 relative = world-eye;
    world_position = world;
    gl_ClipDistance[0] = dot(vec4(world,1),world_clip_plane);
    authored_color = vertex_color;
    vertex_dynamic = lighting_per_pixel != 0 ? vec3(0) : sceneDynamicLighting(world);
    float z = dot(relative,camera_forward);
    gl_Position = vec4(dot(relative,camera_right)*projection.x + z*projection.z,
                       dot(relative,camera_up)*projection.y + z*projection.w, z-2.0*near_distance,z);
    if (orthographic_depth_scale > 0.0)
        gl_Position = vec4(dot(relative,camera_right)*projection.x+projection.z,
            dot(relative,camera_up)*projection.y+projection.w,(z-near_distance)*orthographic_depth_scale-1.0,1.0);
    gl_Position.xy += raster_offset*raster_pixel_scale*gl_Position.w;
    if (effect_depth_offset != 0.0)
        gl_Position.z = orthographic_depth_scale > 0.0 ? (z+effect_depth_offset-near_distance)*orthographic_depth_scale-1.0 :
            (1.0-2.0*near_distance/max(near_distance,z+effect_depth_offset))*gl_Position.w;
    current_clip = gl_Position;
    vec3 previous_world = previous_position+previous_right*previous_prepared.x+
        previous_up*previous_prepared.y+previous_forward*previous_prepared.z;
    previous_clip = previous_view_projection*vec4(previous_world,1);
    texcoord=uv; lightcoord=light_uv; vertex_alpha=alpha;
    if (use_instance_uvs != 0) lightcoord = texelFetch(instance_uvs,gl_VertexID+instance_uv_offset).rg;
    if (use_texture_uvs != 0) texcoord = texelFetch(instance_uvs,gl_VertexID+texture_uv_offset).rg;
    vec3 world_normal = model_right*normal.x + model_up*normal.y + model_forward*normal.z;
    reflection_normal = use_reflection_normals != 0 ? texelFetch(scene_reflections,gl_VertexID+reflection_normal_offset).rgb : world_normal;
    field_modulation = vec3(1);
    for (int lobe = 0; lobe < 4; ++lobe) { field_centers[lobe] = vec3(0); field_colors[lobe] = vec3(0); }
    if (use_reflection_field != 0) {
        int record = reflection_field_offset+gl_VertexID*9;
        for (int lobe = 0; lobe < 4; ++lobe) {
            field_centers[lobe] = texelFetch(scene_reflections,record+lobe*2).rgb;
            field_colors[lobe] = texelFetch(scene_reflections,record+lobe*2+1).rgb;
        }
        field_modulation = texelFetch(scene_reflections,record+8).rgb;
    }
    float shade = directional_ambient != 0 && lighting_per_pixel == 0 ? clamp((1.0-dot(world_normal,ambient_direction))*0.5,0.0,1.0) : 1.0;
    ambient = ambient_color*shade;
}
)GLSL";
const std::string SceneFragmentShader = std::string(SceneLightingShader) + R"GLSL(
uniform sampler2D colortexture, lightmaptexture;
uniform int use_lightmap, use_texture_alpha;
uniform int use_texture;
uniform int unlit;
uniform float occlusion_class;
uniform uint motion_identity;
uniform int motion_valid;
in vec4 current_clip, previous_clip;
layout(location=1) out vec4 surface_motion; // XY displacement, Z history validity, W surface class
layout(location=4) out uint surface_identity;
uniform samplerBuffer scene_fog;
uniform int fog_volume_count, fog_enabled, fog_additive, fog_exterior;
uniform int multiply_material;
uniform sampler2D opaque_depth;
uniform float soft_depth_range, near_distance, orthographic_depth_scale;
uniform ivec2 depth_snapshot_origin;
uniform vec3 eye, camera_forward, exterior_fog_color;
uniform vec2 exterior_fog_range;
uniform vec3 base_color;
uniform vec3 color_modulation;
uniform vec2 light_scale, texture_offset, texture_scale;
uniform vec3 raster_stripe;
uniform float material_alpha;
uniform float alpha_cutoff;
uniform float lighting_modulation;
uniform samplerBuffer scene_reflections;
uniform sampler2D specular_texture;
uniform int reflection_offset, reflection_count;
uniform vec4 specular_response; // strength, exponent, lightmap mix, mask strength
uniform float static_specular_strength;
uniform vec3 ambient_direction;
uniform int directional_ambient;
in vec3 reflection_normal;
uniform int use_reflection_field;
in vec3 field_centers[4], field_colors[4];
in vec3 field_modulation;
in vec3 world_position;
in vec3 ambient;
in vec3 vertex_dynamic;
in vec3 authored_color;
in vec2 texcoord, lightcoord;
in float vertex_alpha;
layout(location=0) out vec4 color;
// G retains the current bloom-protection contract. Surface class is part of
// the depth-selected surface record, never this averaged coverage product.
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
    vec2 material_uv=texcoord*texture_scale+texture_offset;
    vec4 base=use_texture != 0 ? texture(colortexture,material_uv) : vec4(base_color,1);
    base.a=(use_texture_alpha != 0 ? base.a : 1.0)*vertex_alpha*material_alpha;
    if (raster_stripe.x > 0.0) {
        float row = mod(floor(gl_FragCoord.y+raster_stripe.y),raster_stripe.x);
        base.a *= step(row/raster_stripe.x,raster_stripe.z);
    }
    if (soft_depth_range > 0.0) {
        ivec2 size = textureSize(opaque_depth,0);
        ivec2 pixel = clamp(ivec2(gl_FragCoord.xy)-depth_snapshot_origin,ivec2(0),size-ivec2(1));
        float depth = texelFetch(opaque_depth,pixel,0).r;
        if (depth < 1.0) {
            float surface_depth = orthographic_depth_scale > 0.0 ? near_distance+2.0*depth/orthographic_depth_scale :
                near_distance/max(1.e-8,1.0-depth);
            float particle_depth = dot(world_position-eye,camera_forward);
            base.a *= clamp((surface_depth-particle_depth)/soft_depth_range,0.0,1.0);
        }
    }
    if(base.a < alpha_cutoff) discard;
    vec3 baked = use_lightmap != 0 ? texture(lightmaptexture,lightcoord*light_scale).rgb : vec3(1);
    vec3 illumination = use_lightmap != 0 ? baked : ambient;
    if (use_lightmap == 0 && directional_ambient != 0 && lighting_per_pixel != 0) {
        vec3 n = reflection_normal/max(length(reflection_normal),1.e-8);
        vec3 direction = ambient_direction/max(length(ambient_direction),1.e-8);
        illumination *= clamp((1.0-dot(n,direction))*.5,0.0,1.0);
    }
    vec3 dynamic_color = lighting_per_pixel != 0 ? sceneDynamicLighting(world_position) : vertex_dynamic;
    illumination = clamp(illumination+dynamic_color,vec3(0),vec3(1));
    if (unlit != 0) illumination = vec3(1);
    color=vec4(base.rgb*color_modulation*authored_color*illumination*lighting_modulation,base.a);
    if (specular_response.x > 0 && unlit == 0 && dot(reflection_normal,reflection_normal) > 1.e-6) {
        vec3 n = reflection_normal/max(length(reflection_normal),1.e-8);
        vec3 view_delta = eye-world_position;
        vec3 v = view_delta/max(length(view_delta),1.e-8);
        vec3 reflected = vec3(0);
        const float weights[4] = float[4](1.0,.66,.33,.25);
        if (use_reflection_field != 0) for (int lobe = 0; lobe < 4; ++lobe) {
            vec3 delta = field_centers[lobe]-world_position;
            vec3 direction = delta/max(length(delta),1.e-8);
            reflected += field_colors[lobe]*pow(max(0.0,dot(v,direction)),specular_response.y);
        }
        // Four ranked authored lobes are a content convention, not a limit on
        // the shared dynamic emitter list.
        for (int i = 0; use_reflection_field == 0 && i < min(reflection_count,4); ++i) {
            vec3 incident = world_position-texelFetch(scene_reflections,reflection_offset+i*2).rgb;
            float distance = length(incident);
            if (distance <= 1.e-8) continue;
            float response = pow(max(0.0,dot(v,reflect(incident/distance,n))),specular_response.y);
            reflected += texelFetch(scene_reflections,reflection_offset+i*2+1).rgb*response*weights[i];
        }
        reflected *= static_specular_strength;
        for (int index = 0; index < influence_count; ++index) {
            int light = texelFetch(scene_influences,influence_offset+index).r*4;
            vec3 incident;
            float attenuation = sceneEmitterAttenuation(light,world_position,incident);
            float response = pow(max(0.0,dot(v,reflect(incident,n))),specular_response.y);
            reflected += texelFetch(scene_lights,light+1).rgb*attenuation*response*texelFetch(scene_lights,light+3).z;
        }
        reflected *= mix(vec3(1),clamp(use_reflection_field != 0 ? field_modulation : baked,vec3(0),vec3(1)),specular_response.z);
        float mask = texture(specular_texture,material_uv).a;
        color.rgb += clamp(reflected*specular_response.x,vec3(0),vec3(1))*
            clamp(mask*specular_response.w*vertex_alpha,0.0,1.0);
    }
    if (multiply_material != 0) color.rgb = mix(vec3(1),color.rgb,color.a);
    vec3 surface_radiance = unlit != 0 ? vec3(0) : color.rgb;
    float transmission;
    color.rgb = applySceneFog(color.rgb,transmission);
    ao_radiance = vec4(multiply_material != 0 ? color.rgb : surface_radiance*transmission,base.a);
    post_coverage = vec4(0,1.0-transmission,0,0);
    bool valid = motion_valid != 0 && current_clip.w > 1.e-6 && previous_clip.w > 1.e-6;
    surface_motion = valid ? vec4((current_clip.xy/current_clip.w-previous_clip.xy/previous_clip.w)*.5,1,0) : vec4(0);
    surface_motion.w = occlusion_class/255.0;
    surface_identity = motion_identity;
}
)GLSL";
}

void GL4Renderer::SceneDepthSnapshot::Destroy() {
    if (texture) glDeleteTextures(1,&texture);
    if (framebuffer) glDeleteFramebuffers(1,&framebuffer);
    texture = framebuffer = 0; width = height = 0;
}
void GL4Renderer::DestroySceneResources() {
    for (auto& image : scene_view_images) { image.second.buffer.Destroy(); image.second.depth.Destroy(); }
    scene_view_images.clear();
    scene_view_history.clear();
    scene_motion = {}; completed_scene_motion = {};
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
    for (auto& depth : scene_depth_snapshots) depth.second.Destroy();
    scene_depth_snapshots.clear();
    if (scene_light_buffer) glDeleteBuffers(1,&scene_light_buffer);
    if (scene_influence_buffer) glDeleteBuffers(1,&scene_influence_buffer);
    if (scene_light_texture) glDeleteTextures(1,&scene_light_texture);
    if (scene_influence_texture) glDeleteTextures(1,&scene_influence_texture);
    if (scene_uv_buffer) glDeleteBuffers(1,&scene_uv_buffer);
    if (scene_uv_texture) glDeleteTextures(1,&scene_uv_texture);
    scene_uv_buffer = scene_uv_texture = 0;
    scene_uv_ranges.clear();
    scene_texture_uv_ranges.clear();
    if (scene_position_buffer) glDeleteBuffers(1,&scene_position_buffer);
    if (scene_position_texture) glDeleteTextures(1,&scene_position_texture);
    scene_position_buffer = scene_position_texture = 0;
    scene_position_ranges.clear();
    if (scene_reflection_buffer) glDeleteBuffers(1,&scene_reflection_buffer);
    if (scene_reflection_texture) glDeleteTextures(1,&scene_reflection_texture);
    scene_reflection_buffer = scene_reflection_texture = 0;
    scene_reflection_ranges.clear(); scene_normal_ranges.clear();
    scene_field_ranges.clear();
    scene_reflection_assets.clear(); scene_reflection_revisions.clear();
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
        glGenBuffers(1,&scene_reflection_buffer); glGenTextures(1,&scene_reflection_texture);
    }
    if (scene_frame_sequence != frame.data().sequence) {
        std::vector<float> lights;
        for (const auto& light : frame.data().lights) {
            const float record[16] = {light.position.x,light.position.y,light.position.z,light.radius,
                light.color.x,light.color.y,light.color.z,light.falloff,
                light.direction.x,light.direction.y,light.direction.z,light.cone,
                light.directional ? 1.f : 0.f,light.headlight ? 1.f : 0.f,light.specular_strength,0};
            lights.insert(lights.end(),record,record+16);
        }
        if (lights.empty()) lights.resize(16,0);
        std::vector<int32_t> indices;
        std::vector<d3scene::Vec2> uvs;
        std::vector<d3scene::Vec3> reflections;
        std::vector<uint64_t> reflection_revisions{0}; // also initializes the empty GPU buffer once
        const auto collect = [&](const d3scene::SurfaceBinding& surface) {
            if (surface.reflection && surface.specular_strength > 0) reflection_revisions.push_back(surface.reflection->id().revision);
        };
        for (const auto& room : frame.data().rooms) for (const auto& surface : room.surfaces) collect(surface);
        for (const auto& patch : frame.data().terrain.patches) for (const auto& surface : patch) collect(surface);
        for (const auto& mesh : frame.data().meshes) collect(mesh.surface);
        for (const auto& object : frame.data().model_instances) for (const auto& model : object.models) for (const auto& part : model.parts)
            for (const auto& surface : part.surfaces) collect(surface);
        std::sort(reflection_revisions.begin(),reflection_revisions.end());
        reflection_revisions.erase(std::unique(reflection_revisions.begin(),reflection_revisions.end()),reflection_revisions.end());
        const bool rebuild_reflections = reflection_revisions != scene_reflection_revisions;
        if (rebuild_reflections) scene_reflection_assets.clear();
        scene_reflection_ranges.clear(); scene_normal_ranges.clear();
        scene_field_ranges.clear();
        scene_light_ranges.clear();
        scene_uv_ranges.clear();
        scene_texture_uv_ranges.clear();
        const auto add = [&](const d3scene::SurfaceBinding& surface) {
            SceneReflectionRanges reflection_ranges{};
            if (surface.reflection && surface.specular_strength > 0) {
            const auto revision = surface.reflection->id().revision;
            const auto cached = scene_reflection_assets.find(revision);
            if (cached != scene_reflection_assets.end()) reflection_ranges = cached->second;
            else {
            const uint32_t reflection_start = static_cast<uint32_t>(reflections.size());
            if (surface.reflection) for (const auto& source : surface.reflection->data().sources) {
                if (source.color.x == 0 && source.color.y == 0 && source.color.z == 0) continue;
                reflections.push_back(source.position); reflections.push_back(source.color);
            }
            reflection_ranges.sources = ElementRange(reflection_start,static_cast<uint32_t>(reflections.size()-reflection_start)/2);
            const uint32_t normal_start = static_cast<uint32_t>(reflections.size());
            if (surface.reflection) {
                const auto& normals = surface.reflection->data().corner_normals;
                d3scene::forEachTriangleCorner(normals.size(),[&](size_t corner) { reflections.push_back(normals[corner]); });
            }
            reflection_ranges.normals = ElementRange(normal_start,static_cast<uint32_t>(reflections.size())-normal_start);
            const uint32_t field_start = static_cast<uint32_t>(reflections.size());
            if (surface.reflection) {
                const auto& corners = surface.reflection->data().field_corners;
                d3scene::forEachTriangleCorner(corners.size(),[&](size_t corner) {
                    for (const auto& lobe : corners[corner].lobes) { reflections.push_back(lobe.position); reflections.push_back(lobe.color); }
                    reflections.push_back(corners[corner].baked_modulation);
                });
            }
            reflection_ranges.field = ElementRange(field_start,static_cast<uint32_t>(reflections.size())-field_start);
            scene_reflection_assets.emplace(revision,reflection_ranges);
            }
            }
            scene_reflection_ranges.emplace(&surface,reflection_ranges.sources);
            scene_normal_ranges.emplace(&surface,reflection_ranges.normals);
            scene_field_ranges.emplace(&surface,reflection_ranges.field);
            scene_light_ranges.emplace(&surface,ElementRange(static_cast<uint32_t>(indices.size()),static_cast<uint32_t>(surface.light_indices.size())));
            for (uint32_t index : surface.light_indices) indices.push_back(static_cast<int32_t>(index));
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            d3scene::forEachTriangleCorner(surface.baked_uvs.size(),[&](size_t corner) { uvs.push_back(surface.baked_uvs[corner]); });
            scene_uv_ranges.emplace(&surface,ElementRange(start,static_cast<uint32_t>(uvs.size())-start));
        };
        for (const auto& room : frame.data().rooms) for (const auto& surface : room.surfaces) add(surface);
        for (const auto& patch : frame.data().terrain.patches) for (const auto& surface : patch) add(surface);
        for (const auto& mesh : frame.data().meshes) {
            add(mesh.surface);
            const auto coordinates = d3scene::preparedTextureCoordinates(mesh);
            if (coordinates.empty()) continue;
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            uvs.insert(uvs.end(),coordinates.begin(),coordinates.end());
            scene_texture_uv_ranges.emplace(&mesh.surface,ElementRange(start,static_cast<uint32_t>(uvs.size())-start));
        }
        for (const auto& object : frame.data().model_instances) for (const auto& model : object.models) for (const auto& part : model.parts)
            for (const auto& surface : part.surfaces) add(surface);
        std::vector<d3scene::PositionPair> positions;
        scene_position_ranges.clear();
        for (const auto& mesh : frame.data().meshes) {
            // Transient meshes have no declared temporal identity yet. Publish
            // current/current positions without inventing valid object motion.
            const auto pairs = d3scene::preparedPositionPairs(mesh.asset->geometry(),mesh.vertices,mesh.vertices);
            if (pairs.empty()) continue;
            const uint32_t start = static_cast<uint32_t>(positions.size());
            positions.insert(positions.end(),pairs.begin(),pairs.end());
            scene_position_ranges.emplace(&mesh,ElementRange(start,static_cast<uint32_t>(positions.size())-start));
        }
        for (const auto& object : frame.data().model_instances) for (const auto& model : object.models) for (size_t i = 0; i < model.parts.size(); ++i) {
            const auto& part = model.parts[i];
            const auto pairs = d3scene::modelPositionPairs(model.asset->geometry().parts[i].geometry,part);
            if (pairs.empty()) continue;
            const uint32_t start = static_cast<uint32_t>(positions.size());
            positions.insert(positions.end(),pairs.begin(),pairs.end());
            scene_position_ranges.emplace(&part,ElementRange(start,static_cast<uint32_t>(positions.size())-start));
        }
        if (positions.empty()) positions.push_back({});
        if (rebuild_reflections) {
            if (reflections.empty()) reflections.push_back({});
            glBindBuffer(GL_TEXTURE_BUFFER,scene_reflection_buffer);
            glBufferData(GL_TEXTURE_BUFFER,reflections.size()*sizeof(d3scene::Vec3),reflections.data(),GL_STATIC_DRAW);
            scene_reflection_revisions = std::move(reflection_revisions);
        }
        glBindBuffer(GL_TEXTURE_BUFFER,scene_position_buffer);
        glBufferData(GL_TEXTURE_BUFFER,positions.size()*sizeof(d3scene::PositionPair),positions.data(),GL_STREAM_DRAW);
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
    glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_BUFFER,scene_reflection_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGB32F,scene_reflection_buffer);
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
                    geometry.vertex_colors.empty() ? d3scene::Vec3{1,1,1} : geometry.vertex_colors[c.vertex],
                    geometry.raster_offsets.empty() ? d3scene::Vec2{} : geometry.raster_offsets[c.vertex]});
        });
        gpu.polygons.emplace_back(first,static_cast<uint32_t>(vertices.size())-first);
    }
    glGenVertexArrays(1,&gpu.vao); glGenBuffers(1,&gpu.buffer);
    glBindVertexArray(gpu.vao); glBindBuffer(GL_ARRAY_BUFFER,gpu.buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(SceneVertex),vertices.data(),GL_STATIC_DRAW);
    const int counts[7] = {3,2,2,1,3,3,2};
    const size_t offsets[7] = {offsetof(SceneVertex,position),offsetof(SceneVertex,u),offsetof(SceneVertex,lu),offsetof(SceneVertex,alpha),offsetof(SceneVertex,normal),offsetof(SceneVertex,color),offsetof(SceneVertex,raster_offset)};
    for (GLuint attribute = 0; attribute < 7; ++attribute) {
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

void GL4Renderer::CompositeSceneRadiance(bool paused) {
    // Capture-only timestamps separate AO estimation from SSAA-scaled image
    // traffic. Synchronous collection is restricted to the screenshot frame.
    const bool measure = AutomatedCapturePending();
    GLuint timings[4] = {};
    if (measure) { glGenQueries(4,timings); glQueryCounter(timings[0],GL_TIMESTAMP); }
    GLint previous_read, previous_draw, viewport[4];
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_draw);
    glGetIntegerv(GL_VIEWPORT,viewport);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    // AO is a world-light operation. Apply it to full raster-resolution
    // radiance before nonlinear display resampling or later canvas layers.
    // Those layers therefore need no inferred alpha/protection correction.
    if (!opaque_scene.ready || !scene_motion.valid ||
        (OpenGL_preferred_state.bloom_enabled && !opaque_scene.has_color))
        Error("World occlusion requires the scheduled opaque scene product");
    const auto source = framebuffers[framebuffer_current_draw].Handle();
    const auto records = surface_records.TextureForRead(source);
    const auto identities = surface_records.ObjectIdTextureForRead(source);
    const auto& camera = scene_motion.camera;
    const bool temporal = OpenGL_preferred_state.ao_temporal_blend > 0 || OpenGL_preferred_state.ao_temporal_debug_preview;
    AOInputs inputs;
    inputs.depth = opaque_scene.images.DepthTextureForRead();
    inputs.highlight_color = OpenGL_preferred_state.bloom_enabled ? opaque_scene.images.ColorTextureForRead() : 0;
    inputs.surface_records = records; inputs.object_ids = identities;
    inputs.width = opaque_scene.images.Width(); inputs.height = opaque_scene.images.Height();
    inputs.raster_width = surface_records.width; inputs.raster_height = surface_records.height;
    inputs.raster_scale = SupersamplingFactor();
    inputs.viewport = {framebuffer_logical_offset_x,
        framebuffer_logical_height-OpenGL_state.screen_height-framebuffer_logical_offset_y,
        OpenGL_state.screen_width,OpenGL_state.screen_height};
    inputs.current = camera.current; inputs.previous = camera.previous;
    inputs.previous_valid = camera.previous_valid;
    inputs.moving = temporal && !paused; inputs.reset_history = scene_temporal_reset;
    if (measure) glQueryCounter(timings[1],GL_TIMESTAMP);
    const auto result = ao.Evaluate(inputs,OpenGL_preferred_state);
    if (!result.ao_texture) Error("Scheduled world occlusion did not produce its required image");
    if (measure) glQueryCounter(timings[2],GL_TIMESTAMP);
    ao_compositeshader.Use();
    glUniform1f(ao_composite_intensity,result.debug_display ? 1.f :
        (std::max)(0.f,(std::min)(4.f,OpenGL_preferred_state.ao_intensity)));
    glUniform1i(ao_composite_has_suppression_mask,result.suppression_texture != 0);
    glUniform1i(ao_composite_debug_display,result.debug_display);
    glUniform1i(ao_composite_debug_channel,result.debug_channel);
    glUniform2f(ao_composite_visible_origin,float(viewport[0]),float(viewport[1]));
    glUniform2f(ao_composite_visible_size,float(viewport[2]),float(viewport[3]));
    glUniform1i(ao_composite_use_visible_rect,1);
    glUniform1i(ao_compositeshader.FindUniform("source_samples"),scene_ao_samples);
    rend_ClearBoundTextures();
    const GLenum radiance_type = scene_ao_samples >= 2 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,source);
    // Remove the sampled image from the framebuffer entirely: no read/write
    // feedback and no radiance-copy allocation, even for single-sample scenes.
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT3,radiance_type,0,0);
    if (scene_ao_samples >= 2) {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,scene_ao_radiance_texture);
    } else GL_BindFramebufferTexture(scene_ao_radiance_texture,1,GL_NEAREST);
    GL_BindFramebufferTexture(result.ao_texture,2,GL_LINEAR);
    if (result.suppression_texture) GL_BindFramebufferTexture(result.suppression_texture,3,GL_LINEAR);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,source);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,scene_ao_width,scene_ao_height);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if (result.debug_display) glDisablei(GL_BLEND,0);
    else { glEnablei(GL_BLEND,0); glBlendEquationi(0,GL_FUNC_ADD); glBlendFuncSeparatei(0,GL_ONE,GL_ONE,GL_ZERO,GL_ONE); }
    glBindVertexArray(GL_GetFramebufferVAO());
    glDrawArrays(GL_TRIANGLES,0,3); rend_RecordDrawCall(RENDERER_DRAW_CALL_POSTPROCESS);
    glDisablei(GL_BLEND,0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,0);
    if (measure) {
        glQueryCounter(timings[3],GL_TIMESTAMP);
        GLuint64 ticks[4] = {};
        for (int i = 0; i < 4; ++i) glGetQueryObjectui64v(timings[i],GL_QUERY_RESULT,&ticks[i]);
        AutomatedCaptureLog("scene AO GPU diagnostic raster=%ux%u estimate=%ux%u samples=%u resolve_ms=%.6f estimate_ms=%.6f composite_ms=%.6f",
            scene_ao_width,scene_ao_height,ao.ao_framebuffer.Width(),ao.ao_framebuffer.Height(),scene_ao_samples,
            double(ticks[1]-ticks[0])*1.e-6,double(ticks[2]-ticks[1])*1.e-6,double(ticks[3]-ticks[2])*1.e-6);
        glDeleteQueries(4,timings);
    }
    framebuffers[framebuffer_current_draw].MarkColorDirty();
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_draw);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    // Completed world color is now independent of later canvas/cockpit alpha.
    framebuffers[framebuffer_current_draw].ClearAlphaToZero();
    UseSceneDrawBuffers();
}

void GL4Renderer::RenderViewImage(const d3scene::Presentation& presentation,const d3scene::ViewImageRequest& request) {
    d3scene::validateViewImage(request);
    if (!framebuffer_ok) Error("Independent view image requires an initialized scene context");
    FlushFontBatch(); // drain the caller's canvas before any target allocation
    EnsureSceneLifetime(presentation);
    const auto sequence = presentation.data().sequence;
    for (auto it = scene_view_images.begin(); it != scene_view_images.end();) {
        if (it->second.sequence < sequence && sequence-it->second.sequence > 1) {
            it->second.buffer.Destroy(); it->second.depth.Destroy(); it = scene_view_images.erase(it);
        } else ++it;
    }
    auto& image = scene_view_images[request.image];
    if (image.sequence == sequence) throw std::logic_error("View image has more than one producer in a presentation");
    image.sequence = 0; // failed/in-flight products are never readable
    GLint draw, read, viewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    glGetIntegerv(GL_VIEWPORT,viewport);
    const auto restore = [&] {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    };
    try {
        if (image.buffer.Width() != uint32_t(request.view.viewport.width)*request.scale ||
            image.buffer.Height() != uint32_t(request.view.viewport.height)*request.scale ||
            image.buffer.RequestedSamples() != request.samples) image.history.clear();
        image.buffer.Update(request.view.viewport.width*request.scale,request.view.viewport.height*request.scale,request.samples);
        image.buffer.ClearAll();
        const auto raster = d3scene::rasterViewport(request.view.viewport,image.buffer.Height(),request.scale);
        ExecuteSceneView(presentation,request.view,{image.buffer,raster,SceneOutputRole::Image,image.depth,image.history});
        image.sequence = sequence;
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene view image key=%llu size=%ux%u samples=%u sequence=%llu",
                static_cast<unsigned long long>(request.image),image.buffer.Width(),image.buffer.Height(),image.buffer.Samples(),
                static_cast<unsigned long long>(sequence));
    } catch (...) { restore(); throw; }
    restore();
}
d3scene::VisibilityReport GL4Renderer::RenderSceneView(const d3scene::Presentation& presentation,
    const d3scene::View& view) {
    EnsureSceneLifetime(presentation);
    const bool display_target = view.target == d3scene::ViewTarget::Display;
    if (!framebuffer_ok || modal_ui_frame_active ||
        (display_target && (!post_present_pending_swap || cockpit_scene_frame_active)) ||
        (!display_target && post_present_pending_swap && !cockpit_scene_frame_active) ||
        (view.kind == d3scene::ViewKind::Main && cockpit_scene_frame_active))
        Error("Scene view target is unavailable in the current composition phase");
    auto& output = display_target ? post_present_framebuffer : framebuffers[framebuffer_current_draw];
    const auto raster = d3scene::rasterViewport(view.viewport,output.Height(),
        display_target ? 1 : SupersamplingFactor(),display_target ? 0 : framebuffer_logical_offset_x,
        display_target ? 0 : framebuffer_logical_offset_y);
    return ExecuteSceneView(presentation,view,{output,raster,display_target ? SceneOutputRole::Display :
        (view.kind == d3scene::ViewKind::Main ? SceneOutputRole::MainWorld : SceneOutputRole::SceneLayer),
        scene_depth_snapshots[{view.kind,view.history_key}],scene_view_history});
}
void GL4Renderer::EnsureSceneLifetime(const d3scene::Presentation& presentation) {
    if (scene_asset_lifetime != presentation.data().scene_lifetime) {
        DestroySceneResources();
        scene_asset_lifetime = presentation.data().scene_lifetime;
    }
}
d3scene::VisibilityReport GL4Renderer::ExecuteSceneView(const d3scene::Presentation& presentation,
    const d3scene::View& view,const SceneRasterTarget& target) {
    if (!view.geometry_inspection)
        Error("Scene executor migration incomplete: only explicit geometry inspection is currently supported.");
    if (scene_asset_lifetime != presentation.data().scene_lifetime)
        Error("Scene target was selected before establishing its scene lifetime");
    auto& output = target.framebuffer;
    auto& depth = target.depth;
    const auto raster = target.raster;
    const bool owns_world_products = target.role == SceneOutputRole::MainWorld;
    const bool writes_protection = owns_world_products || target.role == SceneOutputRole::SceneLayer;
    const auto visibility = d3scene::queryView(presentation,view);
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene view collection=%llu target=%u projection=%u surfaces=%u",
            static_cast<unsigned long long>(view.collection),static_cast<unsigned>(view.target),
            static_cast<unsigned>(view.projection),static_cast<unsigned>(visibility.surfaces.size()));
    if (FindArg("-scene-lod-probe") && (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)) {
        std::vector<unsigned> polygons(presentation.data().model_instances.size(),0);
        for (const auto& item : visibility.surfaces) if (item.model) ++polygons[item.model_instance_index];
        for (size_t i = 0; i < polygons.size(); ++i) if (polygons[i]) {
            const auto& object = presentation.data().model_instances[i];
            const auto variant = d3scene::selectModelVariant(object,visibility.camera);
            AutomatedCaptureLog("scene LOD visible owner=%u variant=%u asset=%u polygons=%u",
                object.owner,variant,object.models[variant].asset->id().slot,polygons[i]);
        }
    }
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0) {
        unsigned receivers = 0;
        for (const auto& item : visibility.surfaces)
            if (item.mesh && presentation.data().meshes[item.mesh_index].receiver_polygon >= 0) ++receivers;
        if (receivers) AutomatedCaptureLog("scene receiver meshes visible=%u",receivers);
    }
    const auto check_scene_error = [](const char* stage) { const auto error = glGetError(); if (error != GL_NO_ERROR) Error("Scene GL error 0x%x at %s",error,stage); };
    check_scene_error("entry");
    FlushFontBatch();
    const GLboolean inherited_scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    check_scene_error("font flush");
    if (!scene_sample_resolve_verified) {
        GL_VerifySceneSampleResolve();
        scene_sample_resolve_verified = true;
        check_scene_error("sample resolve self-test");
    }
    const auto history = target.history.prepare(presentation.data(),view,visibility.camera);
    if (owns_world_products) {
        scene_motion = d3scene::prepareImageMotion(presentation.data(),history,completed_scene_motion);
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
    if (writes_protection) post_protection_mask.UseSceneDrawBuffers(output.Handle(),false,false);
    const d3scene::ViewProducts products{owns_world_products,
        owns_world_products && OpenGL_preferred_state.ao_enabled && framebuffer_ok};
    if (products.occlusion_radiance) BeginSceneRadianceProduct();
    const bool writes_motion = products.opaque_scene && SceneSurfaceTargetEnabled();
    const auto bind_outputs = [&] {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.Handle());
        const GLenum targets[5] = {GL_COLOR_ATTACHMENT0,
            static_cast<GLenum>(writes_motion ? GL_COLOR_ATTACHMENT1 : GL_NONE),
            static_cast<GLenum>(writes_protection && post_protection_mask.mask_texture ? GL_COLOR_ATTACHMENT2 : GL_NONE),
            static_cast<GLenum>(products.occlusion_radiance ? GL_COLOR_ATTACHMENT3 : GL_NONE),
            static_cast<GLenum>(writes_motion ? GL_COLOR_ATTACHMENT4 : GL_NONE)};
        glDrawBuffers(5,targets);
        glDisablei(GL_BLEND,1); glDisablei(GL_BLEND,4);
    };
    bind_outputs();
    output.MarkAllDirty();
    glViewport(raster.x,raster.y,raster.width,raster.height);
    if (output.Samples() >= 2) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
    if (view.clear_depth) {
        GLint previous_scissor[4]; glGetIntegerv(GL_SCISSOR_BOX,previous_scissor);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glEnable(GL_SCISSOR_TEST); glScissor(raster.x,raster.y,raster.width,raster.height);
        const GLfloat depth = 1; glClearBufferfv(GL_DEPTH,0,&depth);
        glScissor(previous_scissor[0],previous_scissor[1],previous_scissor[2],previous_scissor[3]);
        if (!scissor) glDisable(GL_SCISSOR_TEST);
    }
    if (writes_motion) {
        // This product belongs to the main scene, not StartFrame's legacy
        // canvas state. Clear background identity/velocity on every execution.
        surface_records.ClearAttached(output.Handle());
        bind_outputs();
        surface_records.MarkDirty(); motion_vectors_dirty = true;
    }
    if (writes_protection) post_protection_mask.resolve_valid = false;
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
    const auto clip = view.has_clip_plane ? view.clip_plane : d3scene::Plane{};
    glUniform4f(scene_shader.FindUniform("world_clip_plane"),clip.normal.x,clip.normal.y,clip.normal.z,clip.constant);
    const auto& projection = visibility.camera.projection;
    glUniform4f(scene_shader.FindUniform("projection"),projection.x_scale,projection.y_scale,
        projection.x_offset,projection.y_offset);
    glUniform1f(scene_shader.FindUniform("near_distance"),projection.near_distance);
    glUniform1f(scene_shader.FindUniform("orthographic_depth_scale"),projection.orthographic_depth_scale);
    glUniform2f(scene_shader.FindUniform("raster_pixel_scale"),2.f/view.viewport.width,2.f/view.viewport.height);
    glUniformMatrix4fv(scene_shader.FindUniform("previous_view_projection"),1,GL_FALSE,history.previous.view_projection.data());
    glUniform1i(scene_shader.FindUniform("lighting_per_pixel"),presentation.data().lighting_quality == d3scene::LightingQuality::Pixel);
    glUniform1i(scene_shader.FindUniform("colortexture"),0);
    glUniform1i(scene_shader.FindUniform("lightmaptexture"),1);
    glUniform1i(scene_shader.FindUniform("scene_lights"),2);
    glUniform1i(scene_shader.FindUniform("scene_influences"),3);
    glUniform1i(scene_shader.FindUniform("instance_uvs"),4);
    glUniform1i(scene_shader.FindUniform("instance_positions"),5);
    glUniform1i(scene_shader.FindUniform("scene_reflections"),9);
    glUniform1i(scene_shader.FindUniform("specular_texture"),8);
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
        if (view.collection != 0) break;
        const float inside = d3scene::fogContains(fog_volumes[index],camera.position) ? 1.f : 0.f;
        glBufferSubData(GL_TEXTURE_BUFFER,(index*16+12)*sizeof(float),sizeof(float),&inside);
    }
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,scene_fog_buffer);
    glUniform1i(scene_shader.FindUniform("fog_volume_count"),view.collection == 0 ? static_cast<GLint>(fog_volumes.size()) : 0);
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
      if (pass.kind == d3scene::ViewPassKind::CompositeWorldOcclusion) {
        CompositeSceneRadiance(presentation.data().paused);
        continue;
      }
      if (pass.kind == d3scene::ViewPassKind::ExportOpaqueScene) {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        ExportOpaqueScene();
        // Read the actual scene outputs at a few capture boundaries, including
        // the history fixture's cut. This never runs in interactive rendering.
        const int capture_frame = AutomatedCaptureFrameIndex();
        if (writes_motion &&
            (capture_frame == 0 || capture_frame == 30 || capture_frame == 31 || capture_frame == 59 ||
                (FindArg("-scene-resource-probe") && capture_frame >= 10 && capture_frame <= 51 && capture_frame % 10 <= 1))) {
            const size_t pixels = size_t(surface_records.width)*surface_records.height;
            std::vector<float> vectors(pixels*4);
            std::vector<GLuint> identities(pixels);
            const auto source = output.Handle();
            const auto velocity_texture = surface_records.TextureForRead(source);
            const auto identity_texture = surface_records.ObjectIdTextureForRead(source);
            GLint texture_binding; glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture_binding);
            glBindTexture(GL_TEXTURE_2D,velocity_texture);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,vectors.data());
            glBindTexture(GL_TEXTURE_2D,identity_texture);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RED_INTEGER,GL_UNSIGNED_INT,identities.data());
            glBindTexture(GL_TEXTURE_2D,texture_binding);
            size_t model_pixels = 0, valid_pixels = 0, moving_pixels = 0;
            float maximum = 0;
            for (size_t pixel = 0; pixel < pixels; ++pixel) if (identities[pixel]) {
                ++model_pixels;
                const float x = vectors[pixel*4], y = vectors[pixel*4+1], valid = vectors[pixel*4+2];
                if (!std::isfinite(x) || !std::isfinite(y) || (valid != 0 && valid != 1))
                    Error("Invalid scene motion record at pixel %u",static_cast<unsigned>(pixel));
                if (valid != 0) ++valid_pixels;
                const float magnitude = std::sqrt(x*x+y*y);
                if (magnitude > 1.e-6f) ++moving_pixels;
                maximum = (std::max)(maximum,magnitude);
            }
            if (!history.previous_valid && valid_pixels)
                Error("Scene motion retained valid pixels across an invalid view history");
            AutomatedCaptureLog("scene motion frame=%d camera_history=%d model_pixels=%u valid=%u moving=%u max_uv=%.9g",
                capture_frame,history.previous_valid,static_cast<unsigned>(model_pixels),
                static_cast<unsigned>(valid_pixels),static_cast<unsigned>(moving_pixels),maximum);
            check_scene_error("motion record readback");
        }
        bind_outputs();
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        // Image-space resolve owns its own bindings. Resume the scene pipeline
        // explicitly, including buffer textures used by both shader stages.
        UploadSceneFrame(presentation);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth.texture);
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
        if (!depth.texture) glGenTextures(1,&depth.texture);
        if (!depth.framebuffer) glGenFramebuffers(1,&depth.framebuffer);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth.texture);
        if (depth.width != viewport[2] || depth.height != viewport[3]) {
            depth.width = viewport[2]; depth.height = viewport[3];
            // Scene render targets use 32-bit float depth, including MSAA. The
            // snapshot has identical pixel extent and resolves depth only.
            glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,viewport[2],viewport[3],0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,depth.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth.texture,0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Error("Scene opaque-depth snapshot is incomplete");
        // All depth consumers share nearest-surface sample selection. A raw
        // MSAA depth blit can choose a different sample from motion/identity.
        output.DepthTextureForRead();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,depth.framebuffer);
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
      // Image consumers do not write clip distances; never leak this state.
      if (view.has_clip_plane) glEnable(GL_CLIP_DISTANCE0);
      else glDisable(GL_CLIP_DISTANCE0);
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
            const auto positions = scene_position_ranges.find(&instance);
            if (positions != scene_position_ranges.end()) prepared_positions = positions->second;
        } else if (item.terrain) {
            const auto& terrain = presentation.data().terrain;
            if (scene_terrain.size() <= item.terrain_patch) scene_terrain.resize(item.terrain_patch+1);
            auto& batches = scene_terrain[item.terrain_patch];
            if (batches.size() <= item.terrain_batch) batches.resize(item.terrain_batch+1);
            mesh = &batches[item.terrain_batch];
            UploadSceneMesh(*mesh,terrain.asset->id(),terrain.asset->patches()[item.terrain_patch].batches[item.terrain_batch].geometry);
            binding = &terrain.patches[item.terrain_patch][item.terrain_batch];
        } else if (item.model) {
            const auto& object = presentation.data().model_instances[item.model_instance_index];
            const auto& model = object.models[item.model_variant];
            const auto id = model.asset->id();
            if (scene_models.size() <= id.slot) scene_models.resize(static_cast<size_t>(id.slot)+1);
            auto& parts = scene_models[id.slot];
            if (parts.size() <= item.part_index) parts.resize(static_cast<size_t>(item.part_index)+1);
            mesh = &parts[item.part_index];
            UploadSceneMesh(*mesh,id,model.asset->geometry().parts[item.part_index].geometry);
            binding = &model.parts[item.part_index].surfaces[item.polygon_index];
            const auto positions = scene_position_ranges.find(&model.parts[item.part_index]);
            if (positions != scene_position_ranges.end()) prepared_positions = positions->second;
        } else {
            const auto& room = presentation.data().rooms[item.room_index];
            if (scene_rooms.size() <= static_cast<size_t>(room.number)) scene_rooms.resize(room.number+1);
            mesh = &scene_rooms[room.number];
            UploadSceneMesh(*mesh,room.asset->id(),room.asset->geometry());
            binding = &room.surfaces[item.polygon_index];
        }
        const auto& surface = *binding;
        if ((surface.bitmap >= 0 && !surface.color_image) || (surface.lightmap_bitmap >= 0 && !surface.baked_image) ||
            (surface.specular_bitmap >= 0 && !surface.specular_image))
            Error("Scene surface reached execution without prepared image assets");
        const auto& gpu = *mesh;
        if (surface.color_image) BindSceneImage(*surface.color_image,0,surface.texture_wrap,false);
        if (surface.view_image_slot) {
            const auto found = scene_view_images.find(d3scene::viewImageSource(view,surface.view_image_slot));
            if (found == scene_view_images.end() || found->second.sequence != presentation.data().sequence)
                throw std::logic_error("Surface requested a missing, stale or in-flight view image");
            if (found->second.buffer.Handle() == output.Handle())
                throw std::logic_error("View image cannot sample its own render target");
            GLint draw, read;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
            const auto texture = found->second.buffer.ColorTextureForRead();
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        if (surface.baked_image) BindSceneImage(*surface.baked_image,1,surface.lightmap_wrap,true);
        if (surface.specular_image) BindSceneImage(*surface.specular_image,8,surface.texture_wrap,false);
        scene_shader.Use();
        const auto reflections = scene_reflection_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("reflection_offset"),reflections.offset);
        glUniform1i(scene_shader.FindUniform("reflection_count"),surface.specular_image ? reflections.count : 0);
        glUniform4f(scene_shader.FindUniform("specular_response"),surface.specular_image ? surface.specular_strength : 0,surface.specular_exponent,
            surface.specular_lightmap_mix,surface.specular_alpha_strength);
        glUniform1f(scene_shader.FindUniform("static_specular_strength"),surface.static_specular_strength);
        const auto influences = scene_light_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("influence_offset"),influences.offset);
        glUniform1i(scene_shader.FindUniform("influence_count"),view.dynamic_lighting && !surface.unlit ? influences.count : 0);
        glUniform1i(scene_shader.FindUniform("unlit"),surface.unlit);
        glUniform1f(scene_shader.FindUniform("occlusion_class"),static_cast<unsigned>(surface.occlusion_class));
        glUniform1f(scene_shader.FindUniform("soft_depth_range"),surface.material.layer == d3scene::RenderLayer::World ? surface.soft_depth_range : 0);
        glUniform1i(scene_shader.FindUniform("fog_enabled"),surface.material.layer == d3scene::RenderLayer::World);
        glUniform1i(scene_shader.FindUniform("fog_additive"),surface.material.blend == d3scene::BlendMode::Additive);
        glUniform1i(scene_shader.FindUniform("multiply_material"),surface.material.blend == d3scene::BlendMode::Multiply);
        const bool exterior = item.terrain || (item.model ? exteriorRoom(presentation.data().model_instances[item.model_instance_index].room) :
            (item.mesh ? exteriorRoom(presentation.data().meshes[item.mesh_index].room) : presentation.data().rooms[item.room_index].exterior));
        glUniform1i(scene_shader.FindUniform("fog_exterior"),view.collection == 0 && exterior && exterior_fog.enabled);
        vec("model_position",item.world.position); vec("model_right",item.world.right);
        vec("model_up",item.world.up); vec("model_forward",item.world.forward);
        const auto motion = d3scene::surfaceMotion(presentation,item,history.previous_valid);
        vec("previous_position",motion.previous.position); vec("previous_right",motion.previous.right);
        vec("previous_up",motion.previous.up); vec("previous_forward",motion.previous.forward);
        glUniform1ui(scene_shader.FindUniform("motion_identity"),motion.identity);
        glUniform1i(scene_shader.FindUniform("motion_valid"),motion.valid);
        vec("base_color",surface.base_color);
        vec("color_modulation",surface.color_modulation);
        vec("ambient_color",surface.ambient_color); vec("ambient_direction",surface.ambient_direction);
        glUniform1i(scene_shader.FindUniform("directional_ambient"),surface.directional_ambient);
        glUniform1f(scene_shader.FindUniform("lighting_modulation"),surface.lighting_modulation);
        glUniform1i(scene_shader.FindUniform("use_texture"),surface.color_image != nullptr || surface.view_image_slot != 0);
        glUniform1i(scene_shader.FindUniform("use_lightmap"),surface.baked_image != nullptr);
        glUniform1i(scene_shader.FindUniform("use_texture_alpha"),surface.material.texture_alpha != d3scene::TextureAlpha::Ignore);
        glUniform1f(scene_shader.FindUniform("alpha_cutoff"),surface.material.alpha_cutoff);
        glUniform2f(scene_shader.FindUniform("light_scale"),surface.lightmap_u_scale,surface.lightmap_v_scale);
        glUniform2f(scene_shader.FindUniform("texture_offset"),surface.u_offset,surface.v_offset);
        glUniform2f(scene_shader.FindUniform("texture_scale"),surface.texture_scale.x,surface.texture_scale.y);
        vec("raster_stripe",surface.raster_stripe);
        glUniform1f(scene_shader.FindUniform("material_alpha"),surface.alpha);
        glUniform1f(scene_shader.FindUniform("effect_depth_offset"),surface.effect_depth_offset);
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
        const GLboolean motion_write = solid && writes_motion ? GL_TRUE : GL_FALSE;
        glColorMaski(1,motion_write,motion_write,motion_write,motion_write);
        glColorMaski(4,motion_write,motion_write,motion_write,motion_write);
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
        glDepthMask(surface.material.writesDepth() ? GL_TRUE : GL_FALSE);
        if (surface.material.testsDepth()) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glBindVertexArray(gpu.vao);
        const auto range = item.terrain || item.mesh ? ElementRange(0,gpu.polygons.back().offset+gpu.polygons.back().count) : gpu.polygons[item.polygon_index];
        const auto uv_range = scene_uv_ranges.at(binding);
        const auto texture_uv_found = scene_texture_uv_ranges.find(binding);
        const auto texture_uv_range = texture_uv_found == scene_texture_uv_ranges.end() ? ElementRange() : texture_uv_found->second;
        const auto normals = scene_normal_ranges.at(binding);
        const auto field = scene_field_ranges.at(binding);
        glUniform1i(scene_shader.FindUniform("use_reflection_field"),field.count != 0);
        glUniform1i(scene_shader.FindUniform("reflection_field_offset"),static_cast<int>(field.offset)-static_cast<int>(range.offset)*9);
        glUniform1i(scene_shader.FindUniform("use_reflection_normals"),normals.count != 0);
        glUniform1i(scene_shader.FindUniform("reflection_normal_offset"),static_cast<int>(normals.offset)-static_cast<int>(range.offset));
        glUniform1i(scene_shader.FindUniform("use_instance_uvs"),uv_range.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_uv_offset"),static_cast<int>(uv_range.offset)-static_cast<int>(range.offset));
        glUniform1i(scene_shader.FindUniform("use_texture_uvs"),texture_uv_range.count != 0);
        glUniform1i(scene_shader.FindUniform("texture_uv_offset"),static_cast<int>(texture_uv_range.offset)-static_cast<int>(range.offset));
        glUniform1i(scene_shader.FindUniform("use_instance_positions"),prepared_positions.count != 0);
        glUniform1i(scene_shader.FindUniform("instance_position_offset"),prepared_positions.offset);
        if (item.terrain || (item.mesh && !surface.two_sided)) {
            glEnable(GL_CULL_FACE);
            glFrontFace(d3scene::reversesWinding(camera) != d3scene::reversesWinding(item.world) ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        else glDisable(GL_CULL_FACE);
        glDrawArrays(GL_TRIANGLES,range.offset,range.count);
        rend_RecordDrawCall(RENDERER_DRAW_CALL_3D);
      }
      glDisable(GL_CLIP_DISTANCE0);
    }
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    if (inherited_scissor) glEnable(GL_SCISSOR_TEST);
    output.MarkAllDirty();
    if (writes_protection) {
        post_protection_mask_dirty = true;
        post_protection_mask.resolve_valid = false;
    }
    ShaderProgram::ClearBinding();
    // The remaining canvas adapter maintains its own texture binding cache.
    // Invalidate that cache rather than lying about scene-owned GL names.
    ClearBoundTextures();
    glActiveTexture(GL_TEXTURE0); Last_texel_unit_set = 0;
    rendTEMP_UnbindVertexBuffer();
    if (target.role != SceneOutputRole::Image) rend_NotifyDepthBufferWrite();
    target.history.commit(history);
    // Broad-phase geometry inspection is NOT gameplay visibility. Do not mark
    // every submitted room explored merely because it was depth-tested.
    d3scene::VisibilityReport result;
    result.frame_sequence=presentation.data().sequence;
    result.scene_lifetime=presentation.data().scene_lifetime;
    result.view_history_key=view.history_key;
    return result;
}
