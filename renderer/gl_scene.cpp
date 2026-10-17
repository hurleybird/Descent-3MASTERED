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
layout(std140) uniform SceneDrawBlock { vec4 draw_params[21]; uvec4 draw_identity; };
#define model_position (draw_params[0].xyz)
#define effect_depth_offset (draw_params[0].w)
#define model_right (draw_params[1].xyz)
#define use_instance_positions (int(draw_params[1].w))
#define model_up (draw_params[2].xyz)
#define instance_position_offset (int(draw_params[2].w))
#define model_forward (draw_params[3].xyz)
#define use_instance_uvs (int(draw_params[3].w))
#define previous_position (draw_params[4].xyz)
#define instance_uv_offset (int(draw_params[4].w))
#define previous_right (draw_params[5].xyz)
#define use_texture_uvs (int(draw_params[5].w))
#define previous_up (draw_params[6].xyz)
#define texture_uv_offset (int(draw_params[6].w))
#define previous_forward (draw_params[7].xyz)
#define use_reflection_normals (int(draw_params[7].w))
#define ambient_color (draw_params[8].xyz)
#define reflection_normal_offset (int(draw_params[8].w))
#define ambient_direction (draw_params[9].xyz)
#define use_reflection_field (int(draw_params[9].w))
#define base_color (draw_params[10].xyz)
#define reflection_field_offset (int(draw_params[10].w))
#define color_modulation (draw_params[11].xyz)
#define directional_ambient (int(draw_params[11].w))
#define raster_stripe (draw_params[12].xyz)
#define material_alpha (draw_params[12].w)
#define specular_response (draw_params[13].xyzw)
#define texture_scale (draw_params[14].xy)
#define texture_offset (draw_params[14].zw)
#define light_scale (draw_params[15].xy)
#define influence_offset (int(draw_params[15].z))
#define influence_count (int(draw_params[15].w))
#define static_specular_strength (draw_params[16].x)
#define lighting_modulation (draw_params[16].y)
#define occlusion_class (draw_params[16].z)
#define soft_depth_range (draw_params[16].w)
#define unlit (int(draw_params[17].x))
#define fog_enabled (int(draw_params[17].y))
#define fog_additive (int(draw_params[17].z))
#define multiply_material (int(draw_params[17].w))
#define fog_exterior (int(draw_params[18].x))
#define use_texture (int(draw_params[18].y))
#define use_lightmap (int(draw_params[18].z))
#define use_texture_alpha (int(draw_params[18].w))
#define alpha_cutoff (draw_params[19].x)
#define reflection_offset (int(draw_params[19].y))
#define reflection_count (int(draw_params[19].z))
#define planar_material (int(draw_params[19].w))
#define planar_image (int(draw_params[20].x))
#define motion_identity (draw_identity.x)
#define motion_valid (int(draw_identity.y))
uniform samplerBuffer scene_lights;
uniform isamplerBuffer scene_influences;

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


uniform samplerBuffer instance_positions;

uniform samplerBuffer scene_reflections;


out vec3 reflection_normal;
out vec3 field_centers[4], field_colors[4];
out vec3 field_modulation;


uniform vec3 eye, camera_right, camera_up, camera_forward;
uniform vec4 world_clip_plane;


uniform mat4 previous_view_projection;
out vec4 current_clip, previous_clip;
uniform vec4 projection;
uniform float near_distance;
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
uniform sampler2D planar_texture;
uniform mat4 planar_projection;







in vec4 current_clip, previous_clip;
layout(location=1) out vec4 surface_motion; // XY displacement, Z history validity, W surface class
layout(location=4) out uint surface_identity;
uniform samplerBuffer scene_fog;
uniform int fog_volume_count;
uniform vec4 fog_path_plane; // zero: camera origin; otherwise retained optical half-space

uniform sampler2D opaque_depth;
uniform float near_distance, orthographic_depth_scale;
uniform ivec2 depth_snapshot_origin;
uniform vec3 eye, camera_forward, exterior_fog_color;
uniform vec2 exterior_fog_range;







uniform samplerBuffer scene_reflections;
uniform sampler2D specular_texture;

 // strength, exponent, lightmap mix, mask strength



in vec3 reflection_normal;

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
uniform int surface_prepass, scene_occlusion_enabled;
uniform sampler2D scene_occlusion;
uniform float scene_occlusion_intensity, scene_highlight_gamma, scene_highlight_threshold;
uniform int scene_highlight_suppression;
uniform vec2 scene_raster_extent;
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
    float start = 0.0;
    float origin_side = dot(vec4(eye,1),fog_path_plane);
    if (origin_side < 0.0) {
        float advance = dot(direction,fog_path_plane.xyz);
        start = advance > 0.0 ? min(distance,-origin_side/advance) : distance;
    }
    float interval = distance-start;
    float forward_scale = max(0.0,dot(direction,camera_forward));
    if (fog_exterior != 0) {
        float amount = clamp((interval*forward_scale-exterior_fog_range.x)/(exterior_fog_range.y-exterior_fog_range.x),0.0,1.0);
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
        float path = state.x*interval;
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
                    path += (distance-max(start,crossing))*(dot(texelFetch(scene_fog,offset+3).xyz,direction) > 0.0 ? 1.0 : -1.0);
            }
            ++node;
        }
        // Preserve the authored linear camera-forward depth law, while measuring
        // only the part of the ray actually inside connected fog space.
        float amount = clamp(path,0.0,interval)*forward_scale/appearance.w;
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
    bool valid = motion_valid != 0 && current_clip.w > 1.e-6 && previous_clip.w > 1.e-6;
    surface_motion = valid ? vec4((current_clip.xy/current_clip.w-previous_clip.xy/previous_clip.w)*.5,1,0) : vec4(0);
    surface_motion.w = occlusion_class/255.0;
    surface_identity = motion_identity;
    if (surface_prepass != 0) return;
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
    if (planar_material != 0) {
        if (planar_image != 0) {
            vec4 projected = planar_projection*vec4(world_position,1);
            vec2 uv = projected.xy/projected.w*.5+.5;
            vec3 reflected = texture(planar_texture,uv).rgb;
            color.rgb = color.rgb*base.a+reflected*(1.0-base.a);
            // Only the physical film receives this camera's surface AO. The
            // reflected radiance is not illumination on the mirror polygon.
            surface_radiance *= base.a;
        }
        color.a = 1.0;
    }
    float transmission;
    color.rgb = applySceneFog(color.rgb,transmission);
    if (scene_occlusion_enabled != 0) {
        vec4 estimate = texture(scene_occlusion,gl_FragCoord.xy/scene_raster_extent);
        float ao = clamp(estimate.x,0,1);
        if (scene_highlight_suppression != 0) {
            vec3 display_color = pow(max(color.rgb,vec3(0)),vec3(scene_highlight_gamma));
            float brightness = max(max(display_color.r,display_color.g),display_color.b);
            ao = mix(ao,1,clamp((brightness-scene_highlight_threshold)/max(1-scene_highlight_threshold,.0001),0,1));
        }
        if (multiply_material == 0)
            color.rgb += surface_radiance*transmission*(pow(ao,scene_occlusion_intensity)-1);
    }
    post_coverage = vec4(0,1.0-transmission,0,0);
}
)GLSL";
}

void GL4Renderer::SceneDepthSnapshot::Destroy() {
    if (texture) glDeleteTextures(1,&texture);
    if (framebuffer) glDeleteFramebuffers(1,&framebuffer);
    texture = framebuffer = 0; width = height = 0;
}
void GL4Renderer::DestroySceneResources() {
    scene_ao_debug_shader.Destroy();
    for (auto& image : scene_view_images) { image.second.buffer.Destroy(); image.second.depth.Destroy(); }
    scene_view_images.clear();
    scene_view_history.clear();
    scene_motion = {}; completed_scene_motion = {};
    scene_temporal_reset = true;
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
    if (scene_draw_buffer) glDeleteBuffers(1,&scene_draw_buffer);
    scene_draw_buffer = 0;
    scene_draw_packets.clear(); scene_draw_records.clear(); scene_room_bindings.clear();
    scene_surface_ranges.clear();
    scene_model_influences.clear();
    if (scene_position_buffer) glDeleteBuffers(1,&scene_position_buffer);
    if (scene_position_texture) glDeleteTextures(1,&scene_position_texture);
    scene_position_buffer = scene_position_texture = 0;
    scene_position_ranges.clear();
    if (scene_reflection_buffer) glDeleteBuffers(1,&scene_reflection_buffer);
    if (scene_reflection_texture) glDeleteTextures(1,&scene_reflection_texture);
    scene_reflection_buffer = scene_reflection_texture = 0;
    scene_reflection_assets.clear(); scene_reflection_revisions.clear();
    scene_light_buffer = scene_influence_buffer = scene_light_texture = scene_influence_texture = 0;
    scene_frame_sequence = 0;
    scene_asset_lifetime = 0;
    scene_shader.Destroy();
}

void GL4Renderer::BindSceneImage(const d3scene::ImageAsset& image,int unit,bool wrap,bool baked) {
    auto& gpu = scene_images[image.id().slot];
    glActiveTexture(GL_TEXTURE0+unit);
    if (gpu.id != image.id()) {
        if (gpu.texture) glDeleteTextures(1,&gpu.texture);
        glGenTextures(1,&gpu.texture); gpu.id = image.id(); gpu.sampling = -1;
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
    const int sampling = int(wrap)|(int(filter)<<1)|(int(mip)<<2);
    const float anisotropy = mip ? std::min(OpenGL_max_anisotropy,std::max(1.f,static_cast<float>(OpenGL_preferred_state.anisotropy))) : 1;
    if (gpu.sampling == sampling && gpu.anisotropy == anisotropy) return;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mip ? (filter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (filter ? GL_LINEAR : GL_NEAREST));
    if (OpenGL_max_anisotropy > 1)
        glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
    gpu.sampling = sampling; gpu.anisotropy = anisotropy;
}

void GL4Renderer::UploadSceneFrame(const d3scene::Presentation& frame) {
    PERF_MARKER_SCOPE("Scene.UploadFrame");
    if (!scene_light_buffer) {
        glGenBuffers(1,&scene_light_buffer); glGenTextures(1,&scene_light_texture);
        glGenBuffers(1,&scene_influence_buffer); glGenTextures(1,&scene_influence_texture);
        glGenBuffers(1,&scene_uv_buffer); glGenTextures(1,&scene_uv_texture);
        glGenBuffers(1,&scene_position_buffer); glGenTextures(1,&scene_position_texture);
        glGenBuffers(1,&scene_reflection_buffer); glGenTextures(1,&scene_reflection_texture);
    }
    if (scene_frame_sequence != frame.data().sequence) {
        PerfMarkersBegin("Scene.Upload.Tables");
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
        const auto collectTable = [&](const d3scene::RoomSurfaces& table) {
            if (table.published()) reflection_revisions.insert(reflection_revisions.end(),table.reflections().begin(),table.reflections().end());
            else for (const auto& surface : table) collect(surface);
        };
        for (const auto& room : frame.data().rooms) collectTable(room.surfaces);
        for (const auto& patch : frame.data().terrain.patches) for (const auto& surface : patch) collect(surface);
        for (const auto& mesh : frame.data().meshes) collect(mesh.surface);
        for (const auto& object : frame.data().model_instances) for (const auto& model : object.models) for (const auto& part : model.parts)
            collectTable(part.surfaces);
        std::sort(reflection_revisions.begin(),reflection_revisions.end());
        reflection_revisions.erase(std::unique(reflection_revisions.begin(),reflection_revisions.end()),reflection_revisions.end());
        const bool rebuild_reflections = reflection_revisions != scene_reflection_revisions;
        if (rebuild_reflections) { scene_reflection_assets.clear(); scene_room_bindings.clear(); }
        scene_surface_ranges.assign(frame.data().compiled_surface_count,SceneSurfaceRanges{});
        const auto add = [&](const d3scene::SurfaceBinding& surface,uint32_t index,const std::vector<uint32_t>& lights) {
            auto& ranges = scene_surface_ranges[index];
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
            ranges.reflections = reflection_ranges.sources;
            ranges.normals = reflection_ranges.normals;
            ranges.field = reflection_ranges.field;
            ranges.lights = ElementRange(static_cast<uint32_t>(indices.size()),static_cast<uint32_t>(lights.size()));
            for (uint32_t index : lights) indices.push_back(static_cast<int32_t>(index));
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            d3scene::forEachTriangleCorner(surface.baked_uvs.size(),[&](size_t corner) { uvs.push_back(surface.baked_uvs[corner]); });
            ranges.uv = ElementRange(start,static_cast<uint32_t>(uvs.size())-start);
        };
        const std::vector<uint32_t> no_lights;
        for (const auto& room : frame.data().rooms) {
            if (scene_room_bindings.size() <= size_t(room.number)) scene_room_bindings.resize(size_t(room.number)+1);
            auto& cached = scene_room_bindings[room.number];
            const bool reuse = cached.reusable && &cached.surfaces.read() == &room.surfaces.read();
            if (reuse) std::copy(cached.ranges.begin(),cached.ranges.end(),scene_surface_ranges.begin()+room.surface_base);
            else {
            bool reusable = room.surfaces.publishedFor(room.asset->id()) && room.light_influences_prepared;
            for (size_t face = 0; face < room.surfaces.size(); ++face) {
            const auto& surface = room.surfaces[face];
            reusable = reusable && surface.baked_uvs.empty();
            add(surface,room.surface_base+uint32_t(face),room.light_influences_prepared ? no_lights : surface.light_indices);
            }
            cached.reusable = reusable;
            if (reusable) {
                cached.surfaces = room.surfaces;
                cached.ranges.assign(scene_surface_ranges.begin()+room.surface_base,
                    scene_surface_ranges.begin()+room.surface_base+room.surfaces.size());
            }
            }
            for (size_t face = 0; face < room.light_influences.size(); ++face) {
                const auto& lights = room.light_influences[face];
                auto& range = scene_surface_ranges[room.surface_base+face].lights;
                range = ElementRange(uint32_t(indices.size()),uint32_t(lights.size()));
                for (auto light : lights) indices.push_back(int32_t(light));
            }
        }
        for (const auto& patch : frame.data().terrain.patches) for (const auto& surface : patch) add(surface,surface.compiled_index,surface.light_indices);
        for (const auto& mesh : frame.data().meshes) {
            add(mesh.surface,mesh.surface.compiled_index,mesh.surface.light_indices);
            const auto coordinates = d3scene::preparedTextureCoordinates(mesh);
            if (coordinates.empty()) continue;
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            uvs.insert(uvs.end(),coordinates.begin(),coordinates.end());
            scene_surface_ranges[mesh.surface.compiled_index].texture_uv = ElementRange(start,static_cast<uint32_t>(uvs.size())-start);
        }
        scene_model_influences.clear();
        for (const auto& object : frame.data().model_instances) for (const auto& model : object.models) for (const auto& part : model.parts) {
            for (size_t surface = 0; surface < part.surfaces.size(); ++surface)
                add(part.surfaces[surface],part.surface_base+uint32_t(surface),part.surfaces[surface].light_indices);
            if (!part.polygon_lights.empty()) {
                auto& ranges = scene_model_influences[&part]; ranges.reserve(part.polygon_lights.size());
                for (const auto& lights : part.polygon_lights) {
                    ranges.emplace_back(uint32_t(indices.size()),uint32_t(lights.size()));
                    for (auto light : lights) indices.push_back(int32_t(light));
                }
            }
        }
        PerfMarkersEnd("Scene.Upload.Tables");
        PerfMarkersBegin("Scene.Upload.Positions");
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
        PerfMarkersEnd("Scene.Upload.Positions");
        PerfMarkersBegin("Scene.Upload.GLBuffers");
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
        PerfMarkersEnd("Scene.Upload.GLBuffers");
        if (AutomatedCapturePending()) AutomatedCaptureLog("profile upload bindings=%u positions=%u uvs=%u influences=%u reflection_rebuild=%d",
            unsigned(scene_surface_ranges.size()),unsigned(positions.size()),unsigned(uvs.size()),unsigned(indices.size()),rebuild_reflections);
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
        image.matrices = d3scene::viewMatrices(d3scene::prepareCamera(d3scene::resolveView(presentation,request.view)));
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
    const d3scene::View& request,const SceneRasterTarget& target) {
    PERF_MARKER_SCOPE("Scene.ExecuteView");
    const auto view = d3scene::resolveView(presentation,request);
    if (!view.geometry_inspection)
        Error("Scene executor migration incomplete: only explicit geometry inspection is currently supported.");
    if (scene_asset_lifetime != presentation.data().scene_lifetime)
        Error("Scene target was selected before establishing its scene lifetime");
    auto& output = target.framebuffer;
    auto& depth = target.depth;
    const auto raster = target.raster;
    const bool owns_world_products = target.role == SceneOutputRole::MainWorld;
    const bool writes_protection = owns_world_products || target.role == SceneOutputRole::SceneLayer;
    const auto visibility = [&] { PERF_MARKER_SCOPE("Scene.QueryView"); return d3scene::queryView(presentation,view); }();
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
    GLint inherited_viewport[4]; glGetIntegerv(GL_VIEWPORT,inherited_viewport);
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
    if (!scene_shader.Handle()) {
        scene_shader.AttachSource(SceneVertexShader.c_str(), SceneFragmentShader.c_str());
        glUniformBlockBinding(scene_shader.Handle(),glGetUniformBlockIndex(scene_shader.Handle(),"SceneDrawBlock"),7);
    }
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
    const bool writes_motion = products.opaque_scene && SceneSurfaceTargetEnabled();
    bool depth_prepass = false;
    const auto bind_outputs = [&] {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,depth_prepass ? surface_records.surface_framebuffer : output.Handle());
        const bool record_output = writes_motion && (!products.occlusion || depth_prepass);
        const GLenum targets[5] = {static_cast<GLenum>(depth_prepass ? GL_NONE : GL_COLOR_ATTACHMENT0),
            static_cast<GLenum>(record_output ? GL_COLOR_ATTACHMENT1 : GL_NONE),
            static_cast<GLenum>(!depth_prepass && writes_protection && post_protection_mask.mask_texture ? GL_COLOR_ATTACHMENT2 : GL_NONE),
            GL_NONE,
            static_cast<GLenum>(record_output ? GL_COLOR_ATTACHMENT4 : GL_NONE)};
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
        if (products.occlusion) {
            opaque_scene.images.Update(framebuffer_logical_width,framebuffer_logical_height);
            surface_records.PrepareSurfaceTarget(opaque_scene.images.DepthTextureRaw());
        } else surface_records.ClearAttached(output.Handle());
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
    const auto fog_clip = view.fog_path_origin == d3scene::FogPathOrigin::ClipPlane ? clip : d3scene::Plane{};
    glUniform4f(scene_shader.FindUniform("fog_path_plane"),fog_clip.normal.x,fog_clip.normal.y,fog_clip.normal.z,fog_clip.constant);
    const auto& projection = visibility.camera.projection;
    glUniform4f(scene_shader.FindUniform("projection"),projection.x_scale,projection.y_scale,
        projection.x_offset,projection.y_offset);
    glUniform1f(scene_shader.FindUniform("near_distance"),projection.near_distance);
    glUniform1f(scene_shader.FindUniform("orthographic_depth_scale"),projection.orthographic_depth_scale);
    glUniform2f(scene_shader.FindUniform("raster_pixel_scale"),2.f/view.viewport.width,2.f/view.viewport.height);
    glUniformMatrix4fv(scene_shader.FindUniform("previous_view_projection"),1,GL_FALSE,history.previous.view_projection.data());
    glUniform1i(scene_shader.FindUniform("lighting_per_pixel"),presentation.data().lighting_quality == d3scene::LightingQuality::Pixel);
    glUniform1i(scene_shader.FindUniform("colortexture"),0);
    glUniform1i(scene_shader.FindUniform("planar_texture"),10);
    glUniform1i(scene_shader.FindUniform("lightmaptexture"),1);
    glUniform1i(scene_shader.FindUniform("scene_lights"),2);
    glUniform1i(scene_shader.FindUniform("scene_influences"),3);
    glUniform1i(scene_shader.FindUniform("instance_uvs"),4);
    glUniform1i(scene_shader.FindUniform("instance_positions"),5);
    glUniform1i(scene_shader.FindUniform("scene_reflections"),9);
    glUniform1i(scene_shader.FindUniform("specular_texture"),8);
    glUniform1i(scene_shader.FindUniform("scene_fog"),6);
    glUniform1i(scene_shader.FindUniform("surface_prepass"),0);
    glUniform1i(scene_shader.FindUniform("scene_occlusion_enabled"),0);
    glUniform1i(scene_shader.FindUniform("scene_occlusion"),11);
    glUniform2f(scene_shader.FindUniform("scene_raster_extent"),float(output.Width()),float(output.Height()));
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

    // Build compact GPU draw records once, then submit by buffer offset. The
    // program no longer receives dozens of individually named uniforms per face.
    auto& packets = scene_draw_packets; packets.resize(visibility.surfaces.size());
    GLint record_alignment = 16;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&record_alignment);
    const size_t record_bytes = ((22*16+size_t(record_alignment)-1)/size_t(record_alignment))*size_t(record_alignment);
    auto& records = scene_draw_records; records.assign(visibility.surfaces.size()*(record_bytes/sizeof(float)),0);
    {
    PERF_MARKER_SCOPE("Scene.DrawRecords");
    for (size_t item_index = 0; item_index < visibility.surfaces.size(); ++item_index) {
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
            binding = &model.parts[item.part_index].surface(model.asset->geometry().parts[item.part_index],item.polygon_index);
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

        const auto& gpu = *mesh;
        const bool planar_image = surface.material.planar_slot && view.reflections == d3scene::ReflectionPolicy::Images;
        auto* record = records.data()+item_index*(record_bytes/sizeof(float));
        record[79] = float(surface.material.planar_slot != 0);
        record[80] = float(planar_image);
        uint32_t surface_index = binding->compiled_index;
        if (item.model) {
            const auto& model = presentation.data().model_instances[item.model_instance_index].models[item.model_variant];
            const auto& part = model.parts[item.part_index];
            surface_index = part.surface_base+(part.material_palette ?
                model.asset->geometry().parts[item.part_index].surface_for_polygon[item.polygon_index] : item.polygon_index);
        } else if (!item.mesh && !item.terrain) surface_index = presentation.data().rooms[item.room_index].surface_base+item.polygon_index;
        const auto& ranges = scene_surface_ranges[surface_index];
        const auto reflections = ranges.reflections;
        record[77] = float(reflections.offset);
        record[78] = float(surface.specular_image ? reflections.count : 0);
        record[52] = float(surface.specular_image ? surface.specular_strength : 0); record[53] = float(surface.specular_exponent); record[54] = float(surface.specular_lightmap_mix); record[55] = float(surface.specular_alpha_strength);
        record[64] = float(surface.static_specular_strength);
        auto influences = ranges.lights;
        if (item.model) {
            const auto& part = presentation.data().model_instances[item.model_instance_index].models[item.model_variant].parts[item.part_index];
            if (part.light_influences_prepared) {
                const auto found = scene_model_influences.find(&part);
                influences = found == scene_model_influences.end() ? ElementRange() : found->second[item.polygon_index];
            }
        }
        record[62] = float(influences.offset);
        record[63] = float(view.dynamic_lighting && !surface.unlit ? influences.count : 0);
        record[68] = float(surface.unlit);
        record[66] = float(static_cast<unsigned>(surface.occlusion_class));
        record[67] = float(surface.material.layer == d3scene::RenderLayer::World ? surface.soft_depth_range : 0);
        record[69] = float(surface.material.layer == d3scene::RenderLayer::World);
        record[70] = float(surface.material.blend == d3scene::BlendMode::Additive);
        record[71] = float(surface.material.blend == d3scene::BlendMode::Multiply);
        const bool exterior = item.terrain || (item.model ? exteriorRoom(presentation.data().model_instances[item.model_instance_index].room) :
            (item.mesh ? exteriorRoom(presentation.data().meshes[item.mesh_index].room) : presentation.data().rooms[item.room_index].exterior));
        record[72] = float(view.collection == 0 && exterior && exterior_fog.enabled);
        { const auto value = item.world.position; record[0] = value.x; record[1] = value.y; record[2] = value.z; } { const auto value = item.world.right; record[4] = value.x; record[5] = value.y; record[6] = value.z; }
        { const auto value = item.world.up; record[8] = value.x; record[9] = value.y; record[10] = value.z; } { const auto value = item.world.forward; record[12] = value.x; record[13] = value.y; record[14] = value.z; }
        const auto motion = d3scene::surfaceMotion(presentation,item,history.previous_valid);
        { const auto value = motion.previous.position; record[16] = value.x; record[17] = value.y; record[18] = value.z; } { const auto value = motion.previous.right; record[20] = value.x; record[21] = value.y; record[22] = value.z; }
        { const auto value = motion.previous.up; record[24] = value.x; record[25] = value.y; record[26] = value.z; } { const auto value = motion.previous.forward; record[28] = value.x; record[29] = value.y; record[30] = value.z; }
        { const uint32_t value = uint32_t(motion.identity); std::memcpy(record+84,&value,sizeof(value)); }
        { const uint32_t value = uint32_t(motion.valid); std::memcpy(record+85,&value,sizeof(value)); }
        { const auto value = surface.base_color; record[40] = value.x; record[41] = value.y; record[42] = value.z; }
        { const auto value = surface.color_modulation; record[44] = value.x; record[45] = value.y; record[46] = value.z; }
        { const auto value = surface.ambient_color; record[32] = value.x; record[33] = value.y; record[34] = value.z; } { const auto value = surface.ambient_direction; record[36] = value.x; record[37] = value.y; record[38] = value.z; }
        record[47] = float(surface.directional_ambient);
        record[65] = float(!item.model && !item.mesh && !item.terrain &&
            presentation.data().rooms[item.room_index].uses_room_illumination ? presentation.data().rooms[item.room_index].light_scalar : surface.lighting_modulation);
        record[73] = float(surface.color_image != nullptr || surface.view_image_slot != 0);
        record[74] = float(surface.baked_image != nullptr);
        record[75] = float(surface.material.texture_alpha != d3scene::TextureAlpha::Ignore);
        record[76] = float(surface.material.alpha_cutoff);
        record[60] = float(surface.lightmap_u_scale); record[61] = float(surface.lightmap_v_scale);
        record[58] = float(surface.u_offset); record[59] = float(surface.v_offset);
        record[56] = float(surface.texture_scale.x); record[57] = float(surface.texture_scale.y);
        { const auto value = surface.raster_stripe; record[48] = value.x; record[49] = value.y; record[50] = value.z; }
        record[51] = float(surface.alpha);
        record[3] = float(surface.effect_depth_offset);
        const auto range = item.terrain || item.mesh ? ElementRange(0,gpu.polygons.back().offset+gpu.polygons.back().count) : gpu.polygons[item.polygon_index];
        const auto uv_range = ranges.uv;
        const auto texture_uv_range = ranges.texture_uv;
        const auto normals = ranges.normals;
        const auto field = ranges.field;
        record[39] = float(field.count != 0);
        record[43] = float(static_cast<int>(field.offset)-static_cast<int>(range.offset)*9);
        record[31] = float(normals.count != 0);
        record[35] = float(static_cast<int>(normals.offset)-static_cast<int>(range.offset));
        record[15] = float(uv_range.count != 0);
        record[19] = float(static_cast<int>(uv_range.offset)-static_cast<int>(range.offset));
        record[23] = float(texture_uv_range.count != 0);
        record[27] = float(static_cast<int>(texture_uv_range.offset)-static_cast<int>(range.offset));
        record[7] = float(prepared_positions.count != 0);
        record[11] = float(prepared_positions.offset);

        packets[item_index] = {binding,gpu.vao,range};
    }
    if (!scene_draw_buffer) glGenBuffers(1,&scene_draw_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER,scene_draw_buffer);
    glBufferData(GL_UNIFORM_BUFFER,records.size()*sizeof(float),records.empty() ? nullptr : records.data(),GL_STREAM_DRAW);
    }

    PERF_MARKER_SCOPE("Scene.Submission");
    // Diagnostic timestamps are confined to the requested screenshot frame.
    // Queue every marker before reading results so individual passes do not
    // acquire CPU/GPU synchronization points during execution.
    const bool measure_passes = AutomatedCapturePending();
    std::vector<GLuint> pass_queries(measure_passes ? passes.size()+1 : 0);
    if (measure_passes) glGenQueries(static_cast<GLsizei>(pass_queries.size()),pass_queries.data());
    size_t pass_query_index = 0;
    bool debug_occlusion = false;
    for (const auto& pass : passes) {
      if (measure_passes) glQueryCounter(pass_queries[pass_query_index++],GL_TIMESTAMP);
      if (debug_occlusion && (pass.kind == d3scene::ViewPassKind::OpaqueWorld ||
          pass.kind == d3scene::ViewPassKind::LateWorld || pass.kind == d3scene::ViewPassKind::SnapshotOpaqueDepth)) continue;
      if (pass.kind == d3scene::ViewPassKind::EvaluateWorldOcclusion) {
        AOInputs inputs;
        inputs.depth = opaque_scene.images.DepthTextureForRead();
        inputs.surface_records = surface_records.TextureForRead(output.Handle());
        inputs.object_ids = surface_records.ObjectIdTextureForRead(output.Handle());
        inputs.width = opaque_scene.images.Width(); inputs.height = opaque_scene.images.Height();
        inputs.raster_width = surface_records.ReadWidth(); inputs.raster_height = surface_records.ReadHeight();
        inputs.raster_scale = surface_records.ReadWidth()/opaque_scene.images.Width();
        inputs.viewport = {framebuffer_logical_offset_x,framebuffer_logical_height-OpenGL_state.screen_height-framebuffer_logical_offset_y,
            OpenGL_state.screen_width,OpenGL_state.screen_height};
        inputs.current = scene_motion.camera.current; inputs.previous = scene_motion.camera.previous;
        inputs.previous_valid = scene_motion.camera.previous_valid;
        inputs.moving = !presentation.data().paused;
        inputs.reset_history = scene_temporal_reset;
        // Capture-only diagnostics must not mutate the user's saved preferences.
        auto ao_preferences = OpenGL_preferred_state;
        if (AutomatedCaptureFrameIndex() >= 0 && FindArg("-scene-ao-debug-probe"))
            ao_preferences.ao_debug_preview = true;
        const auto estimate = ao.Evaluate(inputs,ao_preferences);
        if (!estimate.ao_texture) Error("Scheduled occlusion estimate is missing");
        UploadSceneFrame(presentation);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth.texture);
        GL_BindFramebufferTexture(estimate.ao_texture,11,GL_LINEAR);
        scene_shader.Use();
        glUniform1i(scene_shader.FindUniform("scene_occlusion_enabled"),1);
        glUniform1f(scene_shader.FindUniform("scene_occlusion_intensity"),estimate.debug_display ? 1.f :
            (std::max)(0.f,(std::min)(4.f,OpenGL_preferred_state.ao_intensity)));
        glUniform1i(scene_shader.FindUniform("scene_highlight_suppression"),OpenGL_preferred_state.bloom_enabled && !estimate.debug_display);
        glUniform1f(scene_shader.FindUniform("scene_highlight_gamma"),OpenGL_preferred_state.gamma != 0 ? 1.f/OpenGL_preferred_state.gamma : 1.f);
        glUniform1f(scene_shader.FindUniform("scene_highlight_threshold"),OpenGL_preferred_state.bloom_threshold);
        glDepthFunc(GL_LEQUAL);
        bind_outputs();
        glViewport(raster.x,raster.y,raster.width,raster.height);
        debug_occlusion = estimate.debug_display;
        if (debug_occlusion) {
            if (!scene_ao_debug_shader.Handle()) {
                extern const char* blitVertexSrc;
                scene_ao_debug_shader.AttachSource(blitVertexSrc,R"GLSL(#version 450 core
uniform sampler2D estimate;
uniform int channel;
uniform vec2 extent;
uniform vec4 rectangle;
out vec4 color;
void main() {
    vec2 local = gl_FragCoord.xy-rectangle.xy;
    if (any(lessThan(local,vec2(0))) || any(greaterThanEqual(local,rectangle.zw))) discard;
    vec4 value = texture(estimate,gl_FragCoord.xy/extent);
    color = vec4(vec3(clamp(channel == 2 ? value.z : value.x,0,1)),0);
}
)GLSL");
            }
            scene_ao_debug_shader.Use();
            glUniform1i(scene_ao_debug_shader.FindUniform("estimate"),11);
            glUniform1i(scene_ao_debug_shader.FindUniform("channel"),estimate.debug_channel);
            glUniform2f(scene_ao_debug_shader.FindUniform("extent"),float(output.Width()),float(output.Height()));
            glUniform4f(scene_ao_debug_shader.FindUniform("rectangle"),float(raster.x),float(raster.y),float(raster.width),float(raster.height));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.Handle()); glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glViewport(0,0,output.Width(),output.Height());
            glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glBindVertexArray(GL_GetFramebufferVAO());
            glDrawArrays(GL_TRIANGLES,0,3); rend_RecordDrawCall(RENDERER_DRAW_CALL_POSTPROCESS);
        }
        continue;
      }
      if (pass.kind == d3scene::ViewPassKind::ExportOpaqueScene) {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        ExportOpaqueScene(products.occlusion);
        // Read the actual scene outputs at a few capture boundaries, including
        // the history fixture's cut. This never runs in interactive rendering.
        const int capture_frame = AutomatedCaptureFrameIndex();
        if (writes_motion &&
            (capture_frame == 0 || capture_frame == 30 || capture_frame == 31 || capture_frame == 59 ||
                (FindArg("-scene-resource-probe") && capture_frame >= 10 && capture_frame <= 51 && capture_frame % 10 <= 1))) {
            const size_t pixels = size_t(surface_records.ReadWidth())*surface_records.ReadHeight();
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
      depth_prepass = pass.kind == d3scene::ViewPassKind::DepthSurface;
      bind_outputs();
      if (depth_prepass) {
          glViewport(raster.x/SupersamplingFactor(),raster.y/SupersamplingFactor(),
              raster.width/SupersamplingFactor(),raster.height/SupersamplingFactor());
          glDisable(GL_MULTISAMPLE);
      } else {
          glViewport(raster.x,raster.y,raster.width,raster.height);
          if (output.Samples() >= 2) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
      }
      glUniform1i(scene_shader.FindUniform("surface_prepass"),depth_prepass);
      if (view.has_clip_plane) glEnable(GL_CLIP_DISTANCE0);
      else glDisable(GL_CLIP_DISTANCE0);
      for (uint32_t item_index = pass.begin; item_index < pass.end; ++item_index) {
        const auto& item = visibility.surfaces[item_index];
        const auto& packet = packets[item_index];
        const auto* binding = packet.binding;
        const auto& surface = *binding;
        const auto range = packet.range;
        if ((surface.bitmap >= 0 && !surface.color_image) || (surface.lightmap_bitmap >= 0 && !surface.baked_image) ||
            (surface.specular_bitmap >= 0 && !surface.specular_image))
            Error("Scene surface reached execution without prepared image assets");
        if (surface.color_image) BindSceneImage(*surface.color_image,0,surface.texture_wrap,false);
        const auto bind_view_image = [&](uint64_t slot,int unit) -> const SceneViewImageGpu& {
            const auto found = scene_view_images.find(d3scene::viewImageSource(view,slot));
            if (found == scene_view_images.end() || found->second.sequence != presentation.data().sequence)
                throw std::logic_error("Surface requested a missing, stale or in-flight view image");
            if (found->second.buffer.Handle() == output.Handle())
                throw std::logic_error("View image cannot sample its own render target");
            GLint draw, read;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
            const auto texture = found->second.buffer.ColorTextureForRead();
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
            glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            return found->second;
        };
        if (surface.view_image_slot) bind_view_image(surface.view_image_slot,0);
        const bool planar_image = surface.material.planar_slot && view.reflections == d3scene::ReflectionPolicy::Images;
        if (planar_image) {
            const auto& image = bind_view_image(surface.material.planar_slot,10);
            scene_shader.Use();
            glUniformMatrix4fv(scene_shader.FindUniform("planar_projection"),1,GL_FALSE,image.matrices.view_projection.data());
        }
        if (surface.baked_image) BindSceneImage(*surface.baked_image,1,surface.lightmap_wrap,true);
        if (surface.specular_image) BindSceneImage(*surface.specular_image,8,surface.texture_wrap,false);
        scene_shader.Use();
        glBindBufferRange(GL_UNIFORM_BUFFER,7,scene_draw_buffer,item_index*record_bytes,22*16);
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
        glBindVertexArray(packet.vao);
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
    if (measure_passes) {
        glQueryCounter(pass_queries.back(),GL_TIMESTAMP);
        GLuint64 previous = 0;
        glGetQueryObjectui64v(pass_queries[0],GL_QUERY_RESULT,&previous);
        for (size_t i = 0; i < passes.size(); ++i) {
            GLuint64 current = 0;
            glGetQueryObjectui64v(pass_queries[i+1],GL_QUERY_RESULT,&current);
            AutomatedCaptureLog("scene pass GPU kind=%d items=%u ms=%.6f",int(passes[i].kind),
                passes[i].end-passes[i].begin,double(current-previous)*1.e-6);
            previous = current;
        }
        glDeleteQueries(static_cast<GLsizei>(pass_queries.size()),pass_queries.data());
    }
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    // A view owns its raster rectangle only during execution. The surrounding
    // canvas may continue drawing without another legacy StartFrame call.
    glViewport(inherited_viewport[0],inherited_viewport[1],inherited_viewport[2],inherited_viewport[3]);
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
