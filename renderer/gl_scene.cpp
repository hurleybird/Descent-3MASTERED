// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "gl_mesh.h"
#include "SceneView.h"
#include "SceneGeometry.h"
#include "SceneLighting.h"
#include "SceneImages.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include "SceneFogShaders.h"
#include "SceneMotion.h"
#include "gameloop.h"
#include "args.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>
#include <tuple>
extern int Last_texel_unit_set;

namespace {
static_assert(static_cast<int>(d3scene::OcclusionClass::Default) == RENDERER_AO_CLASS_DEFAULT &&
    static_cast<int>(d3scene::OcclusionClass::Terrain) == RENDERER_AO_CLASS_TERRAIN &&
    static_cast<int>(d3scene::OcclusionClass::Model) == RENDERER_AO_CLASS_POLYOBJECT &&
    static_cast<int>(d3scene::OcclusionClass::MineRock) == RENDERER_AO_CLASS_MINE_ROCK &&
    static_cast<int>(d3scene::OcclusionClass::Mine) == RENDERER_AO_CLASS_MINE,"Canvas AO class adapter differs from scene contract");
struct SceneVertex { d3scene::Vec3 position; float u, v, lu, lv, alpha; d3scene::Vec3 normal, color; d3scene::Vec2 raster_offset; uint32_t polygon; };
const char* SceneLightingShader = R"GLSL(#version 330 core
layout(std140) uniform SceneViewBlock { vec4 view_params[14]; ivec4 view_options; };
#define eye (view_params[0].xyz)
#define near_distance (view_params[0].w)
#define camera_right (view_params[1].xyz)
#define orthographic_depth_scale (view_params[1].w)
#define camera_up (view_params[2].xyz)
#define camera_forward (view_params[3].xyz)
#define world_clip_plane (view_params[4])
#define fog_path_plane (view_params[5])
#define projection (view_params[6])
#define raster_pixel_scale (view_params[7].xy)
#define scene_raster_extent (view_params[7].zw)
#define previous_view_projection mat4(view_params[8],view_params[9],view_params[10],view_params[11])
#define exterior_fog_color (view_params[12].xyz)
#define exterior_fog_range (view_params[13].xy)
#define lighting_per_pixel (view_options.x)
#define fog_volume_count (view_options.y)
#define fog_volume_offset (view_options.z)
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
#define polygon_light_table (int(draw_identity.z))
uniform samplerBuffer scene_lights;
uniform isamplerBuffer scene_influences;

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
vec3 sceneDynamicLighting(vec3 world_position,ivec2 influences) {
    vec3 dynamic_color = vec3(0);
    for (int index = 0; index < influences.y; ++index) {
        int light = texelFetch(scene_influences,influences.x+index).r*4;
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
layout(location=7) in uint polygon;
uniform samplerBuffer instance_uvs;


uniform samplerBuffer instance_positions;

uniform samplerBuffer scene_reflections;


// Explicit interface budget: 14 smooth vec4 slots + one flat integer slot.
// Reserve the sixteenth interpolator slot for gl_ClipDistance[0]. Keep integer
// indices flat; packing must not change any value's interpolation qualifier.
out vec4 normal_alpha;
// Seven full vectors carry the same 27 scalar values as nine vec3 outputs.
// Centers occupy XYZ; color R fills W, with G/B pairs packed afterward.
out vec4 reflection_payload[7];




out vec4 current_clip, previous_clip;
out vec4 texture_coordinates;
// XYZ world position + ambient RGB + dynamic RGB + authored RGB (12 scalars).
out vec4 surface_payload[3];
flat out ivec2 primitive_influences;
void main() {
    primitive_influences = ivec2(influence_offset,influence_count);
    if (polygon_light_table != 0) {
        int entry = polygon_light_table-1+int(polygon)*2;
        primitive_influences = ivec2(texelFetch(scene_influences,entry).r,texelFetch(scene_influences,entry+1).r);
    }
    int position_record = (gl_VertexID+instance_position_offset)*2;
    vec3 prepared = use_instance_positions != 0 ? texelFetch(instance_positions,position_record).rgb : position;
    vec3 previous_prepared = use_instance_positions != 0 ? texelFetch(instance_positions,position_record+1).rgb : position;
    vec3 world = model_position + model_right*prepared.x + model_up*prepared.y + model_forward*prepared.z;
    vec3 relative = world-eye;
    gl_ClipDistance[0] = dot(vec4(world,1),world_clip_plane);
    vec3 vertex_dynamic = lighting_per_pixel != 0 ? vec3(0) : sceneDynamicLighting(world,primitive_influences);
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
    texture_coordinates = vec4(uv,light_uv); normal_alpha.w = alpha;
    if (use_instance_uvs != 0) texture_coordinates.zw = texelFetch(instance_uvs,gl_VertexID+instance_uv_offset).rg;
    if (use_texture_uvs != 0) texture_coordinates.xy = texelFetch(instance_uvs,gl_VertexID+texture_uv_offset).rg;
    vec3 world_normal = model_right*normal.x + model_up*normal.y + model_forward*normal.z;
    normal_alpha.xyz = use_reflection_normals != 0 ? texelFetch(scene_reflections,gl_VertexID+reflection_normal_offset).rgb : world_normal;
    vec3 field_centers[4], field_colors[4];
    vec3 field_modulation = vec3(1);
    for (int lobe = 0; lobe < 4; ++lobe) { field_centers[lobe] = vec3(0); field_colors[lobe] = vec3(0); }
    if (use_reflection_field != 0) {
        int record = reflection_field_offset+gl_VertexID*9;
        for (int lobe = 0; lobe < 4; ++lobe) {
            field_centers[lobe] = texelFetch(scene_reflections,record+lobe*2).rgb;
            field_colors[lobe] = texelFetch(scene_reflections,record+lobe*2+1).rgb;
        }
        field_modulation = texelFetch(scene_reflections,record+8).rgb;
    }
    for (int lobe = 0; lobe < 4; ++lobe)
        reflection_payload[lobe] = vec4(field_centers[lobe],field_colors[lobe].r);
    reflection_payload[4] = vec4(field_colors[0].gb,field_colors[1].gb);
    reflection_payload[5] = vec4(field_colors[2].gb,field_colors[3].gb);
    reflection_payload[6] = vec4(field_modulation,0);
    float shade = directional_ambient != 0 && lighting_per_pixel == 0 ? clamp((1.0-dot(world_normal,ambient_direction))*0.5,0.0,1.0) : 1.0;
    vec3 ambient = ambient_color*shade;
    surface_payload[0] = vec4(world,ambient.x);
    surface_payload[1] = vec4(ambient.yz,vertex_dynamic.xy);
    surface_payload[2] = vec4(vertex_dynamic.z,vertex_color);
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
uniform samplerBuffer scene_fog_nodes;
uniform sampler3D scene_fog_field;
uniform vec4 fragment_mapping; // raster pixel -> owning scene pixel (scale, offset)
uniform float fog_field_range;
uniform vec4 fog_field_color; // W selects scalar optical depth; RGB is shared fog color.
uniform int fog_debug;

uniform sampler2D opaque_depth;
uniform ivec2 depth_snapshot_origin;







uniform samplerBuffer scene_reflections;
uniform sampler2D specular_texture;

 // strength, exponent, lightmap mix, mask strength



in vec4 normal_alpha;
#define reflection_normal (normal_alpha.xyz)
#define vertex_alpha (normal_alpha.w)

in vec4 reflection_payload[7];
in vec4 surface_payload[3];
#define world_position (surface_payload[0].xyz)
#define ambient vec3(surface_payload[0].w,surface_payload[1].xy)
#define vertex_dynamic vec3(surface_payload[1].zw,surface_payload[2].x)
#define authored_color (surface_payload[2].yzw)
flat in ivec2 primitive_influences;
in vec4 texture_coordinates;
#define texcoord (texture_coordinates.xy)
#define lightcoord (texture_coordinates.zw)
layout(location=0) out vec4 color;
// G retains the current bloom-protection contract. Surface class is part of
// the depth-selected surface record, never this averaged coverage product.
layout(location=2) out vec4 post_coverage;
uniform int surface_prepass, scene_occlusion_enabled;
uniform sampler2D scene_occlusion;
uniform float scene_occlusion_intensity, scene_highlight_gamma, scene_highlight_threshold;
uniform int scene_highlight_suppression;
)GLSL" + SceneFogIntersectionShader + R"GLSL(
vec3 applySceneFog(vec3 radiance,out float transmission) {
    transmission = 1.0;
    // No medium can contribute: preserve transmission without constructing
    // a ray or clip-plane path for every fragment in a clear view.
    if (fog_enabled == 0 || (fog_volume_count == 0 && fog_exterior == 0)) return radiance;
    vec3 delta = world_position-eye;
    float distance = length(delta);
    if (distance < 1.e-6) return radiance;
    vec3 direction = delta/distance;
    // One ray setup for every boundary node; parallel axes never divide by zero.
    bvec3 parallel = lessThan(abs(direction),vec3(1.e-8));
    vec3 inverse_direction = 1.0/vec3(parallel.x ? 1.0 : direction.x,
        parallel.y ? 1.0 : direction.y,parallel.z ? 1.0 : direction.z);
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
    bool refine_fog = true;
#if SCENE_FOG_MODE == 2
    {
        float z = max(0.0,dot(delta,camera_forward));
        vec2 uv = vec2(dot(delta,camera_right),dot(delta,camera_up))/max(z,1.e-6)*projection.xy+projection.zw;
        float slices = float(textureSize(scene_fog_field,0).z-1);
        float sample_index = min(slices-1.0,floor(sqrt(clamp(z/fog_field_range,0.0,1.0))*slices));
        float low_z = sample_index*sample_index/(slices*slices)*fog_field_range;
        float high_z = (sample_index+1.0)*(sample_index+1.0)/(slices*slices)*fog_field_range;
        float fraction = clamp((z-low_z)/(high_z-low_z),0.0,1.0);
        vec2 dimensions = vec2(textureSize(scene_fog_field,0).xy);
        vec2 location = (uv*0.5+0.5)*dimensions-0.5;
        vec2 base = floor(location), blend = fract(location);
        float layer = (sample_index+fraction+0.5)/(slices+1.0);
        vec4 integrated = vec4(0), minimum = vec4(1.e30), maximum = vec4(-1.e30);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            vec4 column = texture(scene_fog_field,vec3((base+vec2(x,y)+0.5)/dimensions,layer));
            if (fog_field_color.w != 0.0) column = vec4(fog_field_color.rgb,1.0)*column.r;
            integrated += column*(x == 0 ? 1.0-blend.x : blend.x)*(y == 0 ? 1.0-blend.y : blend.y);
            vec4 visible = column/max(1.0,column.a);
            minimum = min(minimum,visible); maximum = max(maximum,visible);
        }
        // Refine strong relative transitions, not ordinary smooth gradients.
        // This is a contrast heuristic, not a bound on unsampled thin features.
        // The absolute floor avoids chasing sub-code-value quantization noise.
        refine_fog = any(greaterThan(maximum-minimum,max(vec4(1.0/255.0),maximum*0.25)));
        optical_depth = integrated.a; weighted_color = integrated.rgb;
    }
#endif
#if SCENE_FOG_MODE != 0
    if (refine_fog) {
    optical_depth = 0.0; weighted_color = vec3(0);
    for (int candidate = 0; candidate < fog_volume_count; ++candidate) {
        int entry = fog_volume_offset+candidate*4;
        int volume = texelFetch(scene_influences,entry).r;
        int view_nodes = texelFetch(scene_influences,entry+1).r;
        int view_count = texelFetch(scene_influences,entry+2).r;
        vec4 appearance = texelFetch(scene_fog,volume*3);
        vec4 low = texelFetch(scene_fog,volume*3+1), high = texelFetch(scene_fog,volume*3+2);
        if (!fogBounds(low.xyz,high.xyz,inverse_direction,parallel,distance)) continue;
        int winding = texelFetch(scene_influences,entry+3).r;
        float path = winding > 0 ? interval : 0.0;
        int node = 0;
        while (node < view_count) {
            int record = view_nodes+node*2;
            vec4 node_low = texelFetch(scene_fog_nodes,record), node_high = texelFetch(scene_fog_nodes,record+1);
            int count = int(node_high.w);
            if (!fogBounds(node_low.xyz,node_high.xyz,inverse_direction,parallel,distance)) {
                node = count > 0 ? node+1 : int(node_low.w); continue;
            }
            for (int triangle = 0; triangle < count; ++triangle) {
                int offset = int(high.w)+(int(node_low.w)+triangle)*4;
                float crossing;
                if (fogHit(texelFetch(scene_fog,offset),texelFetch(scene_fog,offset+1),texelFetch(scene_fog,offset+2),direction,crossing) &&
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
    }
#endif
    if (optical_depth > 0.0) {
        radiance = mix(radiance,multiply_material != 0 ? vec3(1) : (fog_additive != 0 ? vec3(0) : weighted_color/optical_depth),clamp(optical_depth,0.0,1.0));
        transmission *= 1.0-clamp(optical_depth,0.0,1.0);
    }
    return radiance;
}
void main() {
    vec2 material_uv=texcoord*texture_scale+texture_offset;
    vec2 scene_pixel = gl_FragCoord.xy*fragment_mapping.xy+fragment_mapping.zw;
    vec4 base=use_texture != 0 ? texture(colortexture,material_uv) : vec4(base_color,1);
    base.a=(use_texture_alpha != 0 ? base.a : 1.0)*vertex_alpha*material_alpha;
    if (raster_stripe.x > 0.0) {
        float row = mod(floor(gl_FragCoord.y+raster_stripe.y),raster_stripe.x);
        base.a *= step(row/raster_stripe.x,raster_stripe.z);
    }
    if (soft_depth_range > 0.0) {
        ivec2 size = textureSize(opaque_depth,0);
        ivec2 pixel = clamp(ivec2(scene_pixel)-depth_snapshot_origin,ivec2(0),size-ivec2(1));
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
    vec3 dynamic_color = lighting_per_pixel != 0 ? sceneDynamicLighting(world_position,primitive_influences) : vertex_dynamic;
    illumination = clamp(illumination+dynamic_color,vec3(0),vec3(1));
    if (unlit != 0) illumination = vec3(1);
    color=vec4(base.rgb*color_modulation*authored_color*illumination*lighting_modulation,base.a);
    if (specular_response.x > 0 && unlit == 0 && dot(reflection_normal,reflection_normal) > 1.e-6) {
        vec3 n = reflection_normal/max(length(reflection_normal),1.e-8);
        vec3 view_delta = eye-world_position;
        vec3 v = view_delta/max(length(view_delta),1.e-8);
        vec3 reflected = vec3(0);
        const float weights[4] = float[4](1.0,.66,.33,.25);
        if (use_reflection_field != 0) {
        vec3 field_colors[4] = vec3[4](
            vec3(reflection_payload[0].w,reflection_payload[4].xy),
            vec3(reflection_payload[1].w,reflection_payload[4].zw),
            vec3(reflection_payload[2].w,reflection_payload[5].xy),
            vec3(reflection_payload[3].w,reflection_payload[5].zw));
        for (int lobe = 0; lobe < 4; ++lobe) {
            vec3 delta = reflection_payload[lobe].xyz-world_position;
            vec3 direction = delta/max(length(delta),1.e-8);
            reflected += field_colors[lobe]*pow(max(0.0,dot(v,direction)),specular_response.y);
        }
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
        for (int index = 0; index < primitive_influences.y; ++index) {
            int light = texelFetch(scene_influences,primitive_influences.x+index).r*4;
            vec3 incident;
            float attenuation = sceneEmitterAttenuation(light,world_position,incident);
            float response = pow(max(0.0,dot(v,reflect(incident,n))),specular_response.y);
            reflected += texelFetch(scene_lights,light+1).rgb*attenuation*response*texelFetch(scene_lights,light+3).z;
        }
        reflected *= mix(vec3(1),clamp(use_reflection_field != 0 ? reflection_payload[6].xyz : baked,vec3(0),vec3(1)),specular_response.z);
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
    if (fog_debug != 0 && fog_enabled != 0) color.rgb = vec3(1.0-transmission);
    if (scene_occlusion_enabled != 0) {
        vec4 estimate = texture(scene_occlusion,scene_pixel/scene_raster_extent);
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

// Capture-only numerical audit: compare actual GPU texels with the independent
// double-precision CPU boundary integrator. Never part of a timing capture.
static void AuditSceneFogField(const d3scene::View& view,const d3scene::Camera& camera,
    const std::vector<d3scene::FogVolume>& volumes,const std::vector<uint32_t>& candidates,
    int width,int height,float range,bool scalar_field,const d3scene::Vec3& color) {
    std::vector<float> texels(size_t(width)*height*(SceneFogFieldSlices+1)*4);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
    glGetTexImage(GL_TEXTURE_3D,0,GL_RGBA,GL_FLOAT,texels.data());
    if (scalar_field) for (size_t i = 0; i < texels.size(); i += 4) {
        const float depth = texels[i];
        texels[i] = depth*color.x; texels[i+1] = depth*color.y;
        texels[i+2] = depth*color.z; texels[i+3] = depth;
    }
    std::vector<int> winding;
    for (auto index : candidates) winding.push_back(d3scene::fogWinding(volumes[index],camera.transform.position));
    uint32_t random = 12345;
    double maximum_error = 0,maximum_union_error = 0,maximum_reference_disagreement = 0; unsigned samples = 0;
    int worst_x = 0,worst_y = 0,worst_slice = 0,worst_channel = 0;
    double worst_expected = 0,worst_actual = 0;
    d3scene::Vec3 worst_endpoint;
    float worst_start = 0;
    for (int column = 0; column < 64; ++column) {
        random = random*1664525u+1013904223u; const int x = (random>>8)%width;
        random = random*1664525u+1013904223u; const int y = (random>>8)%height;
        const auto& p = camera.projection; const auto& c = camera.transform;
        const float rx = ((x+.5f)/width*2-1-p.x_offset)/p.x_scale;
        const float ry = ((y+.5f)/height*2-1-p.y_offset)/p.y_scale;
        const d3scene::Vec3 ray{c.forward.x+c.right.x*rx+c.up.x*ry,
            c.forward.y+c.right.y*rx+c.up.y*ry,c.forward.z+c.right.z*rx+c.up.z*ry};
        const float ray_length = std::sqrt(ray.x*ray.x+ray.y*ray.y+ray.z*ray.z);
        const float forward = ray.x*c.forward.x+ray.y*c.forward.y+ray.z*c.forward.z;
        for (int probe : {0,1,4,12,24,40,55,64}) {
            const int slice = probe*SceneFogFieldSlices/64;
            const float z = range*float(slice*slice)/float(SceneFogFieldSlices*SceneFogFieldSlices);
            const d3scene::Vec3 endpoint{c.position.x+ray.x*z/forward,c.position.y+ray.y*z/forward,c.position.z+ray.z*z/forward};
            const float start = d3scene::fogRayStart(view,endpoint);
            double expected[4] = {};
            double occupied_expected[4] = {};
            for (size_t i = 0; i < candidates.size(); ++i) {
                const auto& volume = volumes[candidates[i]];
                const double amount = d3scene::fogSegmentLength(volume,c.position,endpoint,winding[i]>0,start)*forward/ray_length/volume.depth;
                expected[0] += amount*volume.color.x; expected[1] += amount*volume.color.y;
                expected[2] += amount*volume.color.z; expected[3] += amount;
                const double occupied = d3scene::fogOccupiedLength(volume,c.position,endpoint,winding[i],start)*forward/ray_length/volume.depth;
                occupied_expected[0] += occupied*volume.color.x; occupied_expected[1] += occupied*volume.color.y;
                occupied_expected[2] += occupied*volume.color.z; occupied_expected[3] += occupied;
            }
            const size_t offset = ((size_t(slice)*height+y)*width+x)*4;
            for (int channel = 0; channel < 4; ++channel) {
                const float actual = texels[offset+channel];
                if (!std::isfinite(actual)) Error("Non-finite fog field texel");
                const double error = std::abs(actual-expected[channel]);
                maximum_union_error = (std::max)(maximum_union_error,std::abs(actual-occupied_expected[channel]));
                maximum_reference_disagreement = (std::max)(maximum_reference_disagreement,std::abs(expected[channel]-occupied_expected[channel]));
                if (error > maximum_error) {
                    maximum_error = error; worst_x = x; worst_y = y; worst_slice = slice; worst_channel = channel;
                    worst_expected = expected[channel]; worst_actual = actual;
                    worst_endpoint = endpoint; worst_start = start;
                }
            }
            ++samples;
        }
    }
    AutomatedCaptureLog("scene fog field audit samples=%u max_optical_error=%.9g",samples,maximum_error);
    AutomatedCaptureLog("scene fog field audit worst=(%d,%d,%d,%d) expected=%.9g actual=%.9g",worst_x,worst_y,worst_slice,worst_channel,worst_expected,worst_actual);
    AutomatedCaptureLog("scene fog occupancy audit max_field_error=%.9g max_signed_reference_disagreement=%.9g",maximum_union_error,maximum_reference_disagreement);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& volume = volumes[candidates[i]];
        const float signed_length = d3scene::fogSegmentLength(volume,camera.transform.position,worst_endpoint,winding[i]>0,worst_start);
        const float occupied_length = d3scene::fogOccupiedLength(volume,camera.transform.position,worst_endpoint,winding[i],worst_start);
        if (signed_length != 0 || occupied_length != 0)
            AutomatedCaptureLog("scene fog audit contribution volume=%u winding=%d signed_length=%.9g occupied_length=%.9g endpoint=%.9g,%.9g,%.9g",
                candidates[i],winding[i],signed_length,occupied_length,worst_endpoint.x,worst_endpoint.y,worst_endpoint.z);
    }
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
    for (auto& sampler : scene_samplers) {
        if (sampler.sampler) glDeleteSamplers(1,&sampler.sampler);
        sampler = {};
    }
    scene_rooms.clear();
    for (auto& model : scene_models) for (auto& part : model)
        if (part.material_indices) glDeleteBuffers(1,&part.material_indices);
    scene_models.clear();
    scene_terrain.clear();
    scene_meshes.clear([](SceneMeshGpu&) {});
    for (auto& page : scene_geometry_pages) {
        if (page.vao) glDeleteVertexArrays(1,&page.vao);
        if (page.buffer) glDeleteBuffers(1,&page.buffer);
    }
    scene_geometry_pages.clear(); scene_geometry_allocator.clear();
    if (scene_fog_buffer) glDeleteBuffers(1,&scene_fog_buffer);
    if (scene_fog_texture) glDeleteTextures(1,&scene_fog_texture);
    scene_fog_buffer = scene_fog_texture = 0;
    if (scene_fog_nodes_texture) glDeleteTextures(1,&scene_fog_nodes_texture);
    scene_fog_nodes_texture = 0;
    if (scene_fog_field_texture) glDeleteTextures(1,&scene_fog_field_texture);
    scene_fog_field_texture = 0;
    scene_fog_field_width = scene_fog_field_height = 0;
    for (auto& program : scene_fog_field_programs) program.shader.Destroy();
    scene_fog_id = {};
    scene_view_fog = {}; scene_view_fog_volumes.clear();
    for (auto& depth : scene_depth_snapshots) depth.second.Destroy();
    scene_depth_snapshots.clear();
    if (scene_volume_texture) glDeleteTextures(1,&scene_volume_texture);
    if (scene_volume_framebuffer) glDeleteFramebuffers(1,&scene_volume_framebuffer);
    scene_volume_texture = scene_volume_framebuffer = 0;
    scene_volume_width = scene_volume_height = 0;
    scene_volume_composite.Destroy();
    scene_volume_rectangle = -1;
    if (scene_light_buffer) glDeleteBuffers(1,&scene_light_buffer);
    if (scene_influence_buffer) glDeleteBuffers(1,&scene_influence_buffer);
    if (scene_light_texture) glDeleteTextures(1,&scene_light_texture);
    if (scene_influence_texture) glDeleteTextures(1,&scene_influence_texture);
    if (scene_uv_buffer) glDeleteBuffers(1,&scene_uv_buffer);
    if (scene_uv_texture) glDeleteTextures(1,&scene_uv_texture);
    scene_uv_buffer = scene_uv_texture = 0;
    if (scene_draw_buffer) glDeleteBuffers(1,&scene_draw_buffer);
    scene_draw_buffer = 0;
    if (scene_view_buffer) glDeleteBuffers(1,&scene_view_buffer);
    scene_view_buffer = 0;
    scene_draw_packets.clear(); scene_draw_records.clear(); scene_room_bindings.clear();
    scene_surface_ranges.clear();
    scene_part_ranges.clear();
    if (scene_position_buffer) glDeleteBuffers(1,&scene_position_buffer);
    if (scene_position_texture) glDeleteTextures(1,&scene_position_texture);
    scene_position_buffer = scene_position_texture = 0;
    scene_mesh_position_ranges.clear();
    if (scene_reflection_buffer) glDeleteBuffers(1,&scene_reflection_buffer);
    if (scene_reflection_texture) glDeleteTextures(1,&scene_reflection_texture);
    scene_reflection_buffer = scene_reflection_texture = 0;
    scene_reflection_assets.clear(); scene_reflection_dependencies.clear();
    scene_light_buffer = scene_influence_buffer = scene_light_texture = scene_influence_texture = 0;
    scene_frame_owner.reset();
    scene_draw_record_bytes = 0;
    scene_asset_lifetime = 0;
    for (auto& program : scene_programs) program.shader.Destroy();
}

void GL4Renderer::BindSceneImage(const d3scene::ImageAsset& image,int unit,bool wrap,bool baked) {
    auto& gpu = scene_images[image.id().slot];
    glActiveTexture(GL_TEXTURE0+unit);
    if (gpu.id != image.id()) {
        PERF_MARKER_SCOPE("Scene.UploadImage");
        if (!gpu.texture) glGenTextures(1,&gpu.texture);
        glBindTexture(GL_TEXTURE_2D,gpu.texture);
        const auto width = image.paddedExtent() ? image.paddedExtent() : image.extent(0).width;
        const auto height = image.paddedExtent() ? image.paddedExtent() : image.extent(0).height;
        const auto levels = image.generatedLevels() ? image.generatedLevels() : uint32_t(image.mipCount());
        const bool same_storage = gpu.width == width && gpu.height == height && gpu.levels == levels;
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        for (size_t level = 0; level < image.mipCount(); ++level) {
            const auto mip = image.extent(level);
            const uint32_t width = image.paddedExtent() ? image.paddedExtent() : mip.width;
            const uint32_t height = image.paddedExtent() ? image.paddedExtent() : mip.height;
            const auto* pixels = image.pixels(level).data();
            if (image.format() != d3scene::ImageFormat::RGBA8 || image.paddedExtent()) {
                scene_image_upload.resize(size_t(width)*height*4);
                if (image.paddedExtent()) std::fill(scene_image_upload.begin(),scene_image_upload.end(),0);
                const size_t source_bytes = image.format() == d3scene::ImageFormat::RGBA8 ? 4 : 2;
                for (uint32_t y = 0; y < mip.height; ++y)
                    d3scene::expandImageRgba(image.format(),pixels+size_t(y)*mip.width*source_bytes,
                        mip.width,scene_image_upload.data()+size_t(y)*width*4);
                pixels = scene_image_upload.data();
            }
            if (same_storage)
                glTexSubImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            else
                glTexImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        }
        if (!same_storage) glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,levels-1);
        if (image.generatedLevels()) glGenerateMipmap(GL_TEXTURE_2D);
        glPixelStorei(GL_UNPACK_ALIGNMENT,2);
        gpu.id = image.id(); gpu.width = width; gpu.height = height; gpu.levels = levels;
    } else glBindTexture(GL_TEXTURE_2D,gpu.texture);
    const bool filter = baked || OpenGL_preferred_state.filtering;
    const bool mip = !baked && OpenGL_preferred_state.mipping && (image.generatedLevels() > 1 || image.mipCount() > 1);
    const int sampling = int(wrap)|(int(filter)<<1)|(int(mip)<<2);
    const float anisotropy = mip ? std::min(OpenGL_max_anisotropy,std::max(1.f,static_cast<float>(OpenGL_preferred_state.anisotropy))) : 1;
    // Sampling belongs to a binding, not to image storage: one image may be
    // read with different policies on multiple units in the same draw.
    auto& sampler = scene_samplers[sampling];
    if (!sampler.sampler) {
        glGenSamplers(1,&sampler.sampler);
        glSamplerParameteri(sampler.sampler,GL_TEXTURE_WRAP_S,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler.sampler,GL_TEXTURE_WRAP_T,wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler.sampler,GL_TEXTURE_MAG_FILTER,filter ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(sampler.sampler,GL_TEXTURE_MIN_FILTER,mip ? (filter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (filter ? GL_LINEAR : GL_NEAREST));
    }
    if (sampler.anisotropy != anisotropy) {
        if (OpenGL_max_anisotropy > 1)
            glSamplerParameterf(sampler.sampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
        sampler.anisotropy = anisotropy;
    }
    glBindSampler(unit,sampler.sampler);
}

void GL4Renderer::PrepareSceneViewTables(const d3scene::PreparedView& prepared) {
    PERF_MARKER_SCOPE("Scene.UploadFrame");
    const auto& frame = prepared.frame();
    const auto& demand = prepared.visibility();
    const bool dynamic_lighting = prepared.view().dynamic_lighting;
    const auto resident = scene_frame_owner.lock();
    const bool new_frame = resident.get() != &frame;
    if (!scene_light_buffer) {
        const auto create = [](GLuint& buffer,GLuint& texture,GLenum format,GLuint unit) {
            glGenBuffers(1,&buffer); glBindBuffer(GL_TEXTURE_BUFFER,buffer);
            glBufferData(GL_TEXTURE_BUFFER,16,nullptr,GL_STREAM_DRAW);
            glGenTextures(1,&texture); glActiveTexture(GL_TEXTURE0+unit);
            glBindTexture(GL_TEXTURE_BUFFER,texture); glTexBuffer(GL_TEXTURE_BUFFER,format,buffer);
        };
        // A texture-buffer association follows the buffer object across data
        // store replacements. Establish it once; later views only bind it.
        create(scene_light_buffer,scene_light_texture,GL_RGBA32F,2);
        create(scene_influence_buffer,scene_influence_texture,GL_R32I,3);
        create(scene_uv_buffer,scene_uv_texture,GL_RG32F,4);
        create(scene_position_buffer,scene_position_texture,GL_RGB32F,5);
        create(scene_reflection_buffer,scene_reflection_texture,GL_RGB32F,9);
    }
    // Every view owns its submission tables. Binding restores below do not
    // rebuild them; auxiliary views supply their own visibility, never the main view's.
    {
        static thread_local std::vector<uint8_t> surfaces, parts;
        const auto& visible_rooms = demand.geometry.rooms;
        const auto& visible_meshes = demand.geometry.meshes;
        const auto& visible_terrain = demand.geometry.terrain;
        const auto& visible_models = demand.geometry.models;
        if (FindArg("-scene-demand-audit")) {
            d3scene::ViewGeometryDemand reference;
            for (const auto& item : demand.surfaces) {
                if (item.model) {
                    const auto& instance = frame.data().model_instances[item.model_instance_index];
                    const auto& model = frame.data().model_variants[instance.model_begin+item.model_variant];
                    reference.models.emplace_back(item.model_instance_index,item.model_variant);
                    reference.parts.push_back(model.part_base+item.part_index);
                } else if (item.mesh) reference.meshes.push_back(item.mesh_index);
                else if (item.terrain) reference.terrain.emplace_back(item.terrain_patch,item.terrain_batch);
                else reference.rooms.push_back(item.room_index);
            }
            const auto canonicalize = [](auto& values) {
                std::sort(values.begin(),values.end());
                values.erase(std::unique(values.begin(),values.end()),values.end());
            };
            canonicalize(reference.rooms); canonicalize(reference.meshes); canonicalize(reference.parts);
            canonicalize(reference.models); canonicalize(reference.terrain);
            if (reference.rooms != demand.geometry.rooms || reference.meshes != demand.geometry.meshes ||
                reference.parts != demand.geometry.parts || reference.models != demand.geometry.models ||
                reference.terrain != demand.geometry.terrain) Error("Prepared geometry demand differs from draw references");
        }
        surfaces.assign(frame.data().compiled_surface_count,0);
        parts.assign(frame.data().compiled_part_count,0);
        for (auto part : demand.geometry.parts) parts[part] = 1;
        bool optical_frustum = prepared.view().projection == d3scene::ViewProjection::Perspective;
        for (const auto& item : demand.surfaces) if (!item.model && !item.mesh && !item.terrain)
            surfaces[frame.data().rooms[item.room_index].surface_base+item.polygon_index] = 1;
        // Pixel-expanded geometry can shade an off-frustum world point.
        for (auto mesh : visible_meshes)
            if (!frame.data().meshes[mesh].asset->geometry().raster_offsets.empty()) optical_frustum = false;
        const auto wantsPart = [&](const d3scene::PreparedModelPart& part) {
            return parts[part.compiled_index] != 0;
        };
        PerfMarkersBegin("Scene.Upload.Tables");
        static thread_local std::vector<float> lights;
        if (new_frame) {
        lights.clear();
        for (const auto& light : frame.data().lights) {
            const float record[16] = {light.position.x,light.position.y,light.position.z,light.radius,
                light.color.x,light.color.y,light.color.z,light.falloff,
                light.direction.x,light.direction.y,light.direction.z,light.cone,
                light.directional ? 1.f : 0.f,light.headlight ? 1.f : 0.f,light.specular_strength,0};
            lights.insert(lights.end(),record,record+16);
        }
        if (lights.empty()) lights.resize(16,0);
        }
        static thread_local std::vector<int32_t> indices;
        static thread_local std::vector<d3scene::Vec2> uvs;
        static thread_local std::vector<d3scene::Vec3> reflections;
        indices.clear(); uvs.clear(); reflections.clear();
        // Equal lists have equal meaning, not a different address per face.
        // Reuse the preceding nonempty list without hashing or reordering;
        // this also lets equal draw records share the existing ordered batch.
        ElementRange previous_lights;
        const auto finish_lights = [&](uint32_t start) {
            const uint32_t count = uint32_t(indices.size())-start;
            if (!count) return ElementRange();
            if (count == previous_lights.count && std::equal(indices.begin()+start,indices.end(),
                    indices.begin()+previous_lights.offset)) {
                indices.resize(start);
                return previous_lights;
            }
            previous_lights = ElementRange(start,count);
            return previous_lights;
        };
        const auto collect = [&](const d3scene::SurfaceBinding& surface) {
            if (surface.reflection && surface.specular_strength > 0) scene_reflection_dependencies.addValue(surface.reflection->id().revision);
        };
        const auto collectTable = [&](const d3scene::RoomSurfaces& table) {
            if (table.published()) scene_reflection_dependencies.add(table.reflectionDependencies());
            else for (const auto& surface : table) collect(surface);
        };
        bool rebuild_reflections = false;
        if (new_frame) {
        scene_meshes.releaseExpired([&](SceneMeshGpu& mesh) { ReleaseSceneMesh(mesh); });
        scene_reflection_dependencies.begin();
        scene_reflection_dependencies.addValue(0); // initialize the empty GPU buffer once
        for (const auto& room : frame.data().rooms) collectTable(room.surfaces);
        for (const auto& surface : frame.data().terrain.materials) collect(surface);
        for (const auto& mesh : frame.data().meshes) collect(mesh.surface);
        for (const auto& model : frame.data().model_variants)
            scene_reflection_dependencies.add(model.parts.summary()->reflections);
        rebuild_reflections = scene_reflection_dependencies.finish();
        }
        if (rebuild_reflections) { scene_reflection_assets.clear(); scene_room_bindings.clear(); }
        // Stable frame addresses do not require clearing the whole level for
        // every view. Every demanded entry is fully assigned below (or copied
        // from a complete retained room table); unseen entries are never read.
        scene_surface_ranges.resize(frame.data().compiled_surface_count);
        const auto reflection = [&](const d3scene::SurfaceBinding& surface) {
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
            return reflection_ranges;
        };
        // Reflection data is asset-owned, not view-owned. A HUD or mirror view
        // must not evict the main view's immutable fields every frame.
        if (rebuild_reflections) {
            for (const auto& room : frame.data().rooms) for (const auto& surface : room.surfaces) reflection(surface);
            for (const auto& surface : frame.data().terrain.materials) reflection(surface);
            for (const auto& mesh : frame.data().meshes) reflection(mesh.surface);
            for (const auto& model : frame.data().model_variants)
                for (const auto& part : model.parts) for (const auto& surface : part.surfaces) reflection(surface);
        }
        const auto add = [&](const d3scene::SurfaceBinding& surface,uint32_t index,const auto& lights) {
            auto& ranges = scene_surface_ranges[index];
            ranges = {};
            const auto reflection_ranges = reflection(surface);
            ranges.reflections = reflection_ranges.sources;
            ranges.normals = reflection_ranges.normals;
            ranges.field = reflection_ranges.field;
            if (dynamic_lighting && !surface.unlit) {
                const uint32_t start = uint32_t(indices.size());
                for (uint32_t index : lights) indices.push_back(static_cast<int32_t>(index));
                ranges.lights = finish_lights(start);
            }
            const uint32_t start = static_cast<uint32_t>(uvs.size());
            d3scene::forEachTriangleCorner(surface.baked_uvs.size(),[&](size_t corner) { uvs.push_back(surface.baked_uvs[corner]); });
            ranges.uv = ElementRange(start,static_cast<uint32_t>(uvs.size())-start);
        };
        const std::vector<uint32_t> no_lights;
        for (const auto room_index : visible_rooms) {
            const auto& room = frame.data().rooms[room_index];
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
            if (dynamic_lighting && room.light_influences_prepared) for (size_t face = 0; face < room.surfaces.size(); ++face) {
                if (!surfaces[room.surface_base+face]) continue;
                if (room.surfaces[face].unlit) continue;
                const uint32_t start = uint32_t(indices.size());
                const auto& geometry = room.asset->geometry();
                d3scene::appendSurfaceLightIndices(frame.data(),room.light_candidates,
                    d3scene::MeshView(geometry),geometry.polygons[face],{},indices);
                scene_surface_ranges[room.surface_base+face].lights = finish_lights(start);
            }
        }
        const auto& terrain = frame.data().terrain;
        for (const auto& key : visible_terrain) {
            const auto p = key.first,b = key.second;
            const auto& patch = terrain.patches[p];
            const auto index = patch.surface_base+uint32_t(b);
            const auto& surface = terrain.surface(p,b);
            if (terrain.light_influences_prepared) add(surface,index,frame.data().lightCandidates(patch.light_candidates));
            else add(surface,index,surface.light_indices);
        }
        d3scene::LightCandidateRange last_mesh_candidates;
        ElementRange last_mesh_lights;
        bool mesh_candidates_uploaded = false;
        for (const auto mesh_index : visible_meshes) {
            const auto& mesh = frame.data().meshes[mesh_index];
            add(mesh.surface,mesh.surface.compiled_index,mesh.light_influences_prepared ? no_lights : mesh.surface.light_indices);
            if (dynamic_lighting && !mesh.surface.unlit && mesh.light_influences_prepared && !mesh.light_candidates.empty()) {
                const auto range = mesh.light_candidates;
                if (!mesh_candidates_uploaded || range.offset != last_mesh_candidates.offset || range.count != last_mesh_candidates.count) {
                    const uint32_t start = uint32_t(indices.size());
                    for (const auto light : frame.data().lightCandidates(range)) indices.push_back(int32_t(light));
                    last_mesh_lights = finish_lights(start);
                    last_mesh_candidates = range; mesh_candidates_uploaded = true;
                }
                scene_surface_ranges[mesh.surface.compiled_index].lights = last_mesh_lights;
            }
            if (!mesh.texture_uvs.empty()) {
                const uint32_t start = static_cast<uint32_t>(uvs.size());
                d3scene::appendPreparedTextureCoordinates(mesh,uvs);
                scene_surface_ranges[mesh.surface.compiled_index].texture_uv = ElementRange(start,static_cast<uint32_t>(uvs.size())-start);
            }
        }
        scene_part_ranges.resize(frame.data().compiled_part_count);
        for (const auto& key : visible_models) {
          const auto& object = frame.data().model_instances[key.first];
          const auto& model = frame.resolvedModel(key.first,key.second);
          for (const auto& part : model.parts) {
            if (!wantsPart(part)) continue;
            auto& part_ranges = scene_part_ranges[part.compiled_index];
            part_ranges = {};
            const auto motion = d3scene::modelPartMotion(object,part,true);
            part_ranges.previous_world = motion.previous;
            part_ranges.motion_identity = motion.identity; part_ranges.motion_valid = motion.valid;
            for (size_t surface = 0; surface < part.surfaces.size(); ++surface)
                add(part.surfaces[surface],part.surface_base+uint32_t(surface),part.surfaces[surface].light_indices);
            if (dynamic_lighting && !part.light_candidates.empty()) {
                const auto& geometry = model.asset->geometry().parts[&part-model.parts.data()].geometry;
                const auto transform = d3scene::compose(object.current,part.current);
                const size_t table = indices.size();
                indices.resize(table+geometry.polygons.size()*2,0);
                part_ranges.light_table = uint32_t(table)+1;
                for (const auto& polygon : geometry.polygons) {
                    const uint32_t start = uint32_t(indices.size());
                    d3scene::appendSurfaceLightIndices(frame.data(),part.light_candidates,
                        d3scene::MeshView(geometry,part.vertices),polygon,transform,indices);
                    const auto lights = finish_lights(start);
                    const size_t entry = table+size_t(&polygon-geometry.polygons.data())*2;
                    indices[entry] = int32_t(lights.offset); indices[entry+1] = int32_t(lights.count);
                }
            }
          }
        }
        PerfMarkersEnd("Scene.Upload.Tables");
        PerfMarkersBegin("Scene.Upload.Positions");
        static thread_local std::vector<d3scene::PositionPair> positions;
        positions.clear();
        scene_mesh_position_ranges.resize(frame.data().meshes.size());
        for (const auto mesh_index : visible_meshes) {
            const auto& mesh = frame.data().meshes[mesh_index];
            auto& range = scene_mesh_position_ranges[mesh_index];
            range = {};
            // Transient meshes have no declared temporal identity yet. Publish
            // current/current positions without inventing valid object motion.
            const uint32_t start = static_cast<uint32_t>(positions.size());
            d3scene::appendPreparedPositionPairs(mesh.asset->geometry(),mesh.vertices,mesh.vertices,positions);
            if (positions.size() != start) range = ElementRange(start,static_cast<uint32_t>(positions.size())-start);
        }
        for (const auto& key : visible_models) {
          const auto& model = frame.resolvedModel(key.first,key.second);
          for (size_t i = 0; i < model.parts.size(); ++i) {
            const auto& part = model.parts[i];
            if (!wantsPart(part)) continue;
            const uint32_t start = static_cast<uint32_t>(positions.size());
            d3scene::appendPreparedPositionPairs(model.asset->geometry().parts[i].geometry,part.vertices,part.previous_vertices,positions);
            if (positions.size() != start)
                scene_part_ranges[part.compiled_index].positions = ElementRange(start,static_cast<uint32_t>(positions.size())-start);
          }
        }
        PerfMarkersEnd("Scene.Upload.Positions");
        PerfMarkersBegin("Scene.Upload.GLBuffers");
        if (rebuild_reflections) {
            if (reflections.empty()) reflections.push_back({});
            glBindBuffer(GL_TEXTURE_BUFFER,scene_reflection_buffer);
            glBufferData(GL_TEXTURE_BUFFER,reflections.size()*sizeof(d3scene::Vec3),reflections.data(),GL_STATIC_DRAW);
        }
        // All streams start with valid minimal GL storage. An absent range
        // has no shader reader, so it needs neither a dummy record nor a new
        // allocation/upload. Retained bytes cannot be addressed by this view.
        if (!positions.empty()) {
            glBindBuffer(GL_TEXTURE_BUFFER,scene_position_buffer);
            glBufferData(GL_TEXTURE_BUFFER,positions.size()*sizeof(d3scene::PositionPair),positions.data(),GL_STREAM_DRAW);
        }
        scene_view_fog_volumes.clear();
        static thread_local std::vector<d3scene::FogViewNode> fog_nodes;
        static thread_local std::vector<int32_t> fog_entries;
        static thread_local std::vector<float> fog_records;
        fog_entries.clear();
        fog_records.clear();
        // The same buffer has scalar-integer and vec4-float texture views.
        // Align the float records without introducing another upload/allocation.
        while (indices.size()%4) indices.push_back(0);
        const size_t fog_record_start = indices.size();
        size_t source_fog_nodes = 0,view_fog_nodes = 0;
        if (prepared.view().collection == 0) {
            const auto& volumes = d3scene::fogVolumes(frame.data());
            for (size_t i = 0; i < volumes.size(); ++i) {
                if (!d3scene::fogMayIntersectView(volumes[i].bounds,demand.camera,optical_frustum)) continue;
                d3scene::prepareFogViewNodes(volumes[i],demand.camera,optical_frustum,fog_nodes);
                // Source geometry is immutable. Camera containment belongs to
                // this view's packed traversal entry, alongside its node range.
                fog_entries.insert(fog_entries.end(),{int32_t(i),int32_t((fog_record_start+fog_records.size())/4),int32_t(fog_nodes.size()),
                    d3scene::fogWinding(volumes[i],demand.camera.transform.position)});
                d3scene::appendFogViewRecords(volumes[i],fog_nodes,fog_records);
                scene_view_fog_volumes.push_back(uint32_t(i));
                source_fog_nodes += volumes[i].nodes.size(); view_fog_nodes += fog_nodes.size();
            }
        }
        if (!fog_records.empty()) {
            static_assert(sizeof(float) == sizeof(int32_t),"Fog stream scalar width");
            indices.resize(fog_record_start+fog_records.size());
            std::memcpy(indices.data()+fog_record_start,fog_records.data(),fog_records.size()*sizeof(float));
            if (!scene_fog_nodes_texture) {
                glGenTextures(1,&scene_fog_nodes_texture);
                glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_nodes_texture);
                glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,scene_influence_buffer);
            }
        }
        scene_view_fog = ElementRange(uint32_t(indices.size()),uint32_t(scene_view_fog_volumes.size()));
        indices.insert(indices.end(),fog_entries.begin(),fog_entries.end());
        if (AutomatedCapturePending()) AutomatedCaptureLog("scene optical fog candidates=%u total=%u nodes=%u/%u",
            scene_view_fog.count,unsigned(d3scene::fogVolumes(frame.data()).size()),unsigned(view_fog_nodes),unsigned(source_fog_nodes));
        if (!uvs.empty()) {
            glBindBuffer(GL_TEXTURE_BUFFER,scene_uv_buffer);
            glBufferData(GL_TEXTURE_BUFFER,uvs.size()*sizeof(d3scene::Vec2),uvs.data(),GL_STREAM_DRAW);
        }
        if (new_frame) {
            glBindBuffer(GL_TEXTURE_BUFFER,scene_light_buffer);
            glBufferData(GL_TEXTURE_BUFFER,lights.size()*sizeof(float),lights.data(),GL_STREAM_DRAW);
        }
        if (!indices.empty()) {
            glBindBuffer(GL_TEXTURE_BUFFER,scene_influence_buffer);
            glBufferData(GL_TEXTURE_BUFFER,indices.size()*sizeof(int32_t),indices.data(),GL_STREAM_DRAW);
        }
        if (new_frame) scene_frame_owner = frame.shared_from_this();
        PerfMarkersEnd("Scene.Upload.GLBuffers");
        if (AutomatedCapturePending()) AutomatedCaptureLog("profile upload bindings=%u positions=%u uvs=%u influences=%u reflection_rebuild=%d frame_constants=%d",
            unsigned(scene_surface_ranges.size()),unsigned(positions.size()),unsigned(uvs.size()),unsigned(indices.size()),rebuild_reflections,new_frame);
    }
    BindSceneTables();
}
void GL4Renderer::BindSceneTables() {
    if (scene_view_buffer) glBindBufferBase(GL_UNIFORM_BUFFER,8,scene_view_buffer);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER,scene_light_texture);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_BUFFER,scene_influence_texture);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_BUFFER,scene_uv_texture);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_BUFFER,scene_position_texture);
    glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_BUFFER,scene_reflection_texture);
    glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_3D,scene_fog_field_texture);
    glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_nodes_texture);
    glActiveTexture(GL_TEXTURE0); Last_texel_unit_set = 0;
}

void GL4Renderer::ReleaseSceneMesh(SceneMeshGpu& gpu) {
    if (gpu.material_indices) glDeleteBuffers(1,&gpu.material_indices);
    scene_geometry_allocator.release(gpu.vertices);
    gpu = {};
}
void GL4Renderer::UploadSceneMesh(SceneMeshGpu& gpu, d3scene::AssetId id, const d3scene::MeshGeometry& geometry,const d3scene::ModelPart* model) {
    if (gpu.id == id) return;
    ReleaseSceneMesh(gpu);
    std::vector<SceneVertex> vertices;
    for (const auto& polygon : geometry.polygons) {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        d3scene::forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
                const auto& c = polygon.corners[corner];
                vertices.push_back({geometry.vertices[c.vertex],c.u,c.v,c.light_u,c.light_v,c.alpha/255.f,
                    geometry.vertex_normals.empty() ? polygon.normal : geometry.vertex_normals[c.vertex],
                    geometry.vertex_colors.empty() ? d3scene::Vec3{1,1,1} : geometry.vertex_colors[c.vertex],
                    geometry.raster_offsets.empty() ? d3scene::Vec2{} : geometry.raster_offsets[c.vertex],
                    uint32_t(&polygon-geometry.polygons.data())});
        });
        gpu.polygons.emplace_back(first,static_cast<uint32_t>(vertices.size())-first);
    }
    if (vertices.size() > INT32_MAX) throw std::overflow_error("Scene geometry exceeds draw address range");
    if (model) {
        std::vector<uint32_t> indices;
        indices.reserve(vertices.size());
        for (const auto& faces : model->surface_groups) {
            const auto first = uint32_t(indices.size());
            for (const auto face : faces) {
                const auto range = gpu.polygons[face];
                for (uint32_t vertex = range.offset; vertex < range.offset+range.count; ++vertex)
                    indices.push_back(vertex);
            }
            gpu.material_groups.emplace_back(first,uint32_t(indices.size())-first);
        }
        if (!indices.empty()) {
            glGenBuffers(1,&gpu.material_indices);
            // Upload without changing the element binding of an unrelated VAO.
            glBindBuffer(GL_COPY_WRITE_BUFFER,gpu.material_indices);
            glBufferData(GL_COPY_WRITE_BUFFER,indices.size()*sizeof(uint32_t),indices.data(),GL_STATIC_DRAW);
            glBindBuffer(GL_COPY_WRITE_BUFFER,0);
        }
    }
    if (!vertices.empty()) {
        gpu.vertices = scene_geometry_allocator.allocate(uint32_t(vertices.size()));
        if (scene_geometry_pages.size() <= gpu.vertices.page) scene_geometry_pages.resize(gpu.vertices.page+1);
        auto& page = scene_geometry_pages[gpu.vertices.page];
        if (!page.vao) {
            glGenVertexArrays(1,&page.vao); glGenBuffers(1,&page.buffer);
            glBindVertexArray(page.vao); glBindBuffer(GL_ARRAY_BUFFER,page.buffer);
            glBufferData(GL_ARRAY_BUFFER,size_t(scene_geometry_allocator.capacity(gpu.vertices.page))*sizeof(SceneVertex),nullptr,GL_STATIC_DRAW);
            const int counts[7] = {3,2,2,1,3,3,2};
            const size_t offsets[7] = {offsetof(SceneVertex,position),offsetof(SceneVertex,u),offsetof(SceneVertex,lu),offsetof(SceneVertex,alpha),offsetof(SceneVertex,normal),offsetof(SceneVertex,color),offsetof(SceneVertex,raster_offset)};
            for (GLuint attribute = 0; attribute < 7; ++attribute) {
                glEnableVertexAttribArray(attribute);
                glVertexAttribPointer(attribute,counts[attribute],GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsets[attribute]));
            }
            glEnableVertexAttribArray(7);
            glVertexAttribIPointer(7,1,GL_UNSIGNED_INT,sizeof(SceneVertex),reinterpret_cast<void*>(offsetof(SceneVertex,polygon)));
        } else glBindBuffer(GL_ARRAY_BUFFER,page.buffer);
        // GL preserves queued readers of a reused range. Static assets upload
        // once; only revised/retired geometry participates in allocator traffic.
        glBufferSubData(GL_ARRAY_BUFFER,size_t(gpu.vertices.offset)*sizeof(SceneVertex),vertices.size()*sizeof(SceneVertex),vertices.data());
        gpu.vao = page.vao;
    }
    gpu.id = id;
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
        const auto coverage = request.view.coverage.width ?
            d3scene::rasterViewport(request.view.coverage,image.buffer.Height(),request.scale) : d3scene::Rect{};
        const auto prepared = d3scene::prepareView(presentation,request.view);
        ExecuteSceneView(prepared,{image.buffer,raster,SceneOutputRole::Image,image.depth,image.history,coverage});
        image.matrices = d3scene::viewMatrices(prepared.visibility().camera);
        image.sequence = sequence;
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene view image key=%llu size=%ux%u samples=%u sequence=%llu coverage=%d,%d,%d,%d",
                static_cast<unsigned long long>(request.image),image.buffer.Width(),image.buffer.Height(),image.buffer.Samples(),
                static_cast<unsigned long long>(sequence),request.view.coverage.x,request.view.coverage.y,request.view.coverage.width,request.view.coverage.height);
    } catch (...) { restore(); throw; }
    restore();
}
d3scene::ImageMip GL4Renderer::ReadViewImage(const d3scene::Presentation& presentation,uint64_t key) {
    const auto found = scene_view_images.find(key);
    if (scene_asset_lifetime != presentation.data().scene_lifetime || found == scene_view_images.end() ||
        found->second.sequence != presentation.data().sequence)
        throw std::logic_error("Readback requested a missing or stale view image");
    auto& buffer = found->second.buffer;
    d3scene::ImageMip result; result.width = buffer.Width(); result.height = buffer.Height();
    result.pixels.resize(size_t(result.width)*result.height*4);
    GLint read,draw,pack_buffer,alignment,row_length,skip_rows,skip_pixels;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack_buffer); glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH,&row_length); glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    buffer.BindForRead();
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,result.width,result.height,GL_RGBA,GL_UNSIGNED_BYTE,result.pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack_buffer); glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH,row_length); glPixelStorei(GL_PACK_SKIP_ROWS,skip_rows); glPixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("View image readback failed");
    return result;
}
d3scene::VisibilityReport GL4Renderer::RenderSceneView(const d3scene::PreparedView& prepared) {
    const auto& presentation = prepared.frame();
    const auto& view = prepared.view();
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
    return ExecuteSceneView(prepared,{output,raster,display_target ? SceneOutputRole::Display :
        (view.kind == d3scene::ViewKind::Main ? SceneOutputRole::MainWorld : SceneOutputRole::SceneLayer),
        scene_depth_snapshots[{view.kind,view.history_key}],scene_view_history});
}
void GL4Renderer::EnsureSceneLifetime(const d3scene::Presentation& presentation) {
    if (scene_asset_lifetime != presentation.data().scene_lifetime) {
        DestroySceneResources();
        scene_asset_lifetime = presentation.data().scene_lifetime;
    }
}
d3scene::VisibilityReport GL4Renderer::ExecuteSceneView(const d3scene::PreparedView& prepared,const SceneRasterTarget& target) {
    PERF_MARKER_SCOPE("Scene.ExecuteView");
    const auto& presentation = prepared.frame();
    const auto& view = prepared.view();
    if (!view.geometry_inspection)
        Error("Scene executor migration incomplete: only explicit geometry inspection is currently supported.");
    if (scene_asset_lifetime != presentation.data().scene_lifetime)
        Error("Scene target was selected before establishing its scene lifetime");
    auto& output = target.framebuffer;
    auto& depth = target.depth;
    GLuint depth_for_sampling = depth.texture;
    const auto raster = target.raster;
    const bool owns_world_products = target.role == SceneOutputRole::MainWorld;
    const bool writes_protection = post_protection_mask.mask_texture != 0 &&
        (owns_world_products || target.role == SceneOutputRole::SceneLayer);
    const auto& visibility = prepared.visibility();
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
                object.owner,variant,presentation.data().variants(object)[variant].asset->id().slot,polygons[i]);
        }
    }
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0) {
        unsigned receivers = 0;
        for (const auto& item : visibility.surfaces)
            if (item.mesh && presentation.data().meshes[item.mesh_index].receiver_polygon >= 0) ++receivers;
        if (receivers) AutomatedCaptureLog("scene receiver meshes visible=%u",receivers);
    }
    // Driver error polling is diagnostic work, not part of each view's normal
    // submission contract. Keep scene-data validation unconditional; enable
    // these stage-local GL checks explicitly in Release qualification runs.
#ifndef NDEBUG
    constexpr bool audit_scene_gl = true;
#else
    static const bool audit_scene_gl = FindArg("-scene-gl-audit") != 0;
#endif
    const d3scene::ViewProducts products{owns_world_products,
        owns_world_products && OpenGL_preferred_state.ao_enabled && framebuffer_ok,
        owns_world_products && !target.coverage.width && FindArg("-scene-display-particles") &&
            (SupersamplingFactor() > 1 || output.Samples() >= 2)};
    const auto passes = d3scene::buildViewPasses(presentation,visibility,products);
    const auto history = target.history.prepare(presentation.data(),view,visibility.camera);
    const bool clears_sky = visibility.exterior_visible && presentation.data().sky_enabled;
    // Plan before uploading or touching driver state. An empty overlay has no
    // raster work; a depth clear or opaque-world export is still real work even
    // without surfaces. Image targets have already been initialized by their
    // owning producer. Camera history advances independently of raster work.
    if (passes.empty() && !view.clear_depth && !clears_sky) {
        target.history.commit(history);
        d3scene::VisibilityReport result;
        result.frame_sequence = presentation.data().sequence;
        result.scene_lifetime = presentation.data().scene_lifetime;
        result.view_history_key = view.history_key;
        return result;
    }
    const auto check_scene_error = [&](const char* stage) {
        if (!audit_scene_gl) return;
        const auto error = glGetError();
        if (error != GL_NO_ERROR) Error("Scene GL error 0x%x at %s",error,stage);
    };
    check_scene_error("entry");
    FlushFontBatch();
    const GLboolean inherited_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLint inherited_scissor_box[4]{};
    if (target.coverage.width) glGetIntegerv(GL_SCISSOR_BOX,inherited_scissor_box);
    GLint inherited_viewport[4]; glGetIntegerv(GL_VIEWPORT,inherited_viewport);
    glDisable(GL_SCISSOR_TEST);
    check_scene_error("font flush");
    if (!scene_sample_resolve_verified) {
        GL_VerifySceneSampleResolve();
        scene_sample_resolve_verified = true;
        check_scene_error("sample resolve self-test");
    }
    if (owns_world_products) {
        scene_motion = d3scene::prepareImageMotion(presentation.data(),history,completed_scene_motion);
        scene_temporal_reset = !history.previous_valid;
        have_previous_view_projection = history.previous_valid;
        std::memcpy(previous_view_projection,history.previous.view_projection.data(),sizeof(previous_view_projection));
    }
    PrepareSceneViewTables(prepared);
    check_scene_error("frame upload");
    const bool field_fog = FindArg("-scene-fog-field") && scene_view_fog.count &&
        visibility.camera.projection.orthographic_depth_scale == 0;
    const size_t fog_mode = scene_view_fog.count == 0 ? 0 : (field_fog ? 2 : 1);
    auto& scene_shader = scene_programs[fog_mode].shader;
    auto& scene_uniforms = scene_programs[fog_mode].uniforms;
    if (!scene_shader.Handle()) {
        if (AutomatedCaptureFrameIndex() >= 0) {
            GLint varying = 0, vertex = 0, fragment = 0;
            glGetIntegerv(GL_MAX_VARYING_COMPONENTS,&varying);
            glGetIntegerv(GL_MAX_VERTEX_OUTPUT_COMPONENTS,&vertex);
            glGetIntegerv(GL_MAX_FRAGMENT_INPUT_COMPONENTS,&fragment);
            AutomatedCaptureLog("scene shader device=%s varying_components=%d vertex_outputs=%d fragment_inputs=%d",
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)),varying,vertex,fragment);
        }
        auto fragment = SceneFragmentShader;
        fragment.insert(fragment.find('\n')+1,"#define SCENE_FOG_MODE "+std::to_string(fog_mode)+"\n");
        scene_shader.AttachSource(SceneVertexShader.c_str(), fragment.c_str());
        // Resolve the shader interface at link time, never by string in a view.
        scene_uniforms.colortexture = scene_shader.FindUniform("colortexture");
        scene_uniforms.planar_texture = scene_shader.FindUniform("planar_texture");
        scene_uniforms.lightmaptexture = scene_shader.FindUniform("lightmaptexture");
        scene_uniforms.scene_lights = scene_shader.FindUniform("scene_lights");
        scene_uniforms.scene_influences = scene_shader.FindUniform("scene_influences");
        scene_uniforms.instance_uvs = scene_shader.FindUniform("instance_uvs");
        scene_uniforms.instance_positions = scene_shader.FindUniform("instance_positions");
        scene_uniforms.scene_reflections = scene_shader.FindUniform("scene_reflections");
        scene_uniforms.specular_texture = scene_shader.FindUniform("specular_texture");
        scene_uniforms.scene_fog = scene_shader.FindUniform("scene_fog");
        scene_uniforms.fog_field_range = scene_shader.FindUniform("fog_field_range");
        scene_uniforms.fog_field_color = scene_shader.FindUniform("fog_field_color");
        scene_uniforms.fog_debug = scene_shader.FindUniform("fog_debug");
        scene_uniforms.surface_prepass = scene_shader.FindUniform("surface_prepass");
        scene_uniforms.scene_occlusion_enabled = scene_shader.FindUniform("scene_occlusion_enabled");
        scene_uniforms.scene_occlusion = scene_shader.FindUniform("scene_occlusion");
        scene_uniforms.scene_occlusion_intensity = scene_shader.FindUniform("scene_occlusion_intensity");
        scene_uniforms.scene_highlight_suppression = scene_shader.FindUniform("scene_highlight_suppression");
        scene_uniforms.scene_highlight_gamma = scene_shader.FindUniform("scene_highlight_gamma");
        scene_uniforms.scene_highlight_threshold = scene_shader.FindUniform("scene_highlight_threshold");
        scene_uniforms.opaque_depth = scene_shader.FindUniform("opaque_depth");
        scene_uniforms.depth_snapshot_origin = scene_shader.FindUniform("depth_snapshot_origin");
        scene_uniforms.fragment_mapping = scene_shader.FindUniform("fragment_mapping");
        scene_uniforms.planar_projection = scene_shader.FindUniform("planar_projection");
        glUniformBlockBinding(scene_shader.Handle(),glGetUniformBlockIndex(scene_shader.Handle(),"SceneDrawBlock"),7);
        glUniformBlockBinding(scene_shader.Handle(),glGetUniformBlockIndex(scene_shader.Handle(),"SceneViewBlock"),8);
        scene_shader.Use();
        glUniform1i(scene_uniforms.colortexture,0);
        glUniform1i(scene_uniforms.planar_texture,10);
        glUniform1i(scene_uniforms.lightmaptexture,1);
        glUniform1i(scene_uniforms.scene_lights,2);
        glUniform1i(scene_uniforms.scene_influences,3);
        glUniform1i(scene_uniforms.instance_uvs,4);
        glUniform1i(scene_uniforms.instance_positions,5);
        glUniform1i(scene_uniforms.scene_reflections,9);
        glUniform1i(scene_uniforms.specular_texture,8);
        glUniform1i(scene_uniforms.scene_fog,6);
        glUniform1i(scene_shader.FindUniform("scene_fog_field"),12);
        glUniform1i(scene_shader.FindUniform("scene_fog_nodes"),13);
        glUniform1i(scene_uniforms.scene_occlusion,11);
        glUniform1i(scene_uniforms.opaque_depth,7);
    }

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
    if (clears_sky) {
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
    const auto& camera = visibility.camera.transform;
    const auto clip = view.has_clip_plane ? view.clip_plane : d3scene::Plane{};
    const auto fog_clip = view.fog_path_origin == d3scene::FogPathOrigin::ClipPlane ? clip : d3scene::Plane{};
    const auto& projection = visibility.camera.projection;
    glUniform1i(scene_uniforms.surface_prepass,0);
    glUniform4f(scene_uniforms.fragment_mapping,1,1,0,0);
    glUniform1i(scene_uniforms.scene_occlusion_enabled,0);
    const auto& fog_volumes = d3scene::fogVolumes(presentation.data());
    const auto fog_id = presentation.data().fog ? presentation.data().fog->id() : d3scene::AssetId{};
    const bool new_fog_buffer = !scene_fog_buffer;
    const bool upload_fog = new_fog_buffer || scene_fog_id != fog_id;
    if (!scene_fog_buffer) { glGenBuffers(1,&scene_fog_buffer); glGenTextures(1,&scene_fog_texture); }
    glBindBuffer(GL_TEXTURE_BUFFER,scene_fog_buffer);
    if (upload_fog) {
    std::vector<float> fog(fog_volumes.size()*12,0);
    for (size_t index = 0; index < fog_volumes.size(); ++index) {
        const auto& volume = fog_volumes[index];
        const auto& low = volume.bounds.minimum; const auto& high = volume.bounds.maximum;
        const float record[12] = {volume.color.x,volume.color.y,volume.color.z,volume.depth,
            low.x,low.y,low.z,0,high.x,high.y,high.z,float(fog.size()/4)};
        std::copy(record,record+12,fog.begin()+index*12);
        for (const auto& triangle : volume.boundary) {
            const auto& a=triangle.a; const auto& b=triangle.b; const auto& c=triangle.c;
            const d3scene::Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z},ac{c.x-a.x,c.y-a.y,c.z-a.z};
            const d3scene::Vec3 normal{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
            // Plane coefficients are asset data, not per-ray arithmetic. Reuse
            // the padding in the existing three vertex records.
            fog.insert(fog.end(),{a.x,a.y,a.z,normal.x,b.x,b.y,b.z,normal.y,c.x,c.y,c.z,normal.z,
                triangle.inward.x,triangle.inward.y,triangle.inward.z,0});
        }
    }
    if (fog.empty()) fog.resize(4,0);
    glBufferData(GL_TEXTURE_BUFFER,fog.size()*sizeof(float),fog.data(),GL_STATIC_DRAW);
    scene_fog_id = fog_id;
    }
    // Normal execution never patches camera state into the geometry buffer.
    // Revisit containment only for this explicit boundary diagnostic.
    if (AutomatedCapturePending() && FindArg("-scene-fog-boundary-audit"))
    for (const auto index : scene_view_fog_volumes) {
        const int winding = d3scene::fogWinding(fog_volumes[index],camera.position);
        const float inside = winding > 0 ? 1.f : 0.f;
        {
            const auto& volume = fog_volumes[index];
            const bool room_member = std::find(volume.rooms.begin(),volume.rooms.end(),view.room) != volume.rooms.end();
            AutomatedCaptureLog("fog boundary volume=%u room=%d member=%d contains=%d depth=%.9g triangles=%u eye=%.9g,%.9g,%.9g",
                index,view.room,room_member,int(inside),volume.depth,unsigned(volume.boundary.size()),camera.position.x,camera.position.y,camera.position.z);
            for (auto number : volume.rooms) {
                const auto* found = presentation.roomIndices().find(number);
                if (!found) continue;
                const auto& geometry = presentation.data().rooms[found->second].asset->geometry();
                unsigned concave_planes = 0,nonplanar_faces = 0;
                for (const auto& face : geometry.polygons) {
                    if (!face.bounds_volume || face.corners.empty()) continue;
                    const auto a = geometry.vertices[face.corners[0].vertex]; const auto n = face.normal;
                    const auto side = [&](d3scene::Vec3 p) { return double(n.x)*(p.x-a.x)+double(n.y)*(p.y-a.y)+double(n.z)*(p.z-a.z); };
                    bool concave = false,nonplanar = false;
                    for (const auto& p : geometry.vertices) if (side(p) < -.01) { concave = true; break; }
                    for (const auto& c : face.corners) if (std::abs(side(geometry.vertices[c.vertex])) > .01) { nonplanar = true; break; }
                    concave_planes += concave; nonplanar_faces += nonplanar;
                }
                AutomatedCaptureLog("fog room=%d vertices=%u faces=%u concave_planes=%u nonplanar_faces=%u",
                    number,unsigned(geometry.vertices.size()),unsigned(geometry.polygons.size()),concave_planes,nonplanar_faces);
            }
            if (FindArg("-scene-fog-boundary-dump")) {
                AutomatedCaptureLog("fog camera right=%.9g,%.9g,%.9g up=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g winding=%d",
                    camera.right.x,camera.right.y,camera.right.z,camera.up.x,camera.up.y,camera.up.z,
                    camera.forward.x,camera.forward.y,camera.forward.z,winding);
                for (const auto& t : volume.boundary)
                    AutomatedCaptureLog("fog triangle %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g",
                        t.a.x,t.a.y,t.a.z,t.b.x,t.b.y,t.b.z,t.c.x,t.c.y,t.c.z,t.inward.x,t.inward.y,t.inward.z);
            }
        }
    }
    if (AutomatedCapturePending() && FindArg("-scene-fog-boundary-dump")) {
        const auto* found = presentation.roomIndices().find(view.room);
        if (found) {
            const auto& room = presentation.data().rooms[found->second];
            const auto& geometry = room.asset->geometry();
            AutomatedCaptureLog("fog camera room=%d enabled=%d depth=%.9g flags=%u",room.number,int(room.fog_enabled),room.fog_depth,room.authored_flags);
            for (const auto& portal : room.portals) {
                const auto* other = presentation.roomIndices().find(portal.destination_room);
                if (!other || portal.polygon < 0 || size_t(portal.polygon) >= geometry.polygons.size()) continue;
                const auto& neighbor = presentation.data().rooms[other->second];
                const auto& face = geometry.polygons[portal.polygon];
                AutomatedCaptureLog("fog portal polygon=%d destination=%d enabled=%d depth=%.9g normal=%.9g,%.9g,%.9g",portal.polygon,neighbor.number,int(neighbor.fog_enabled),neighbor.fog_depth,face.normal.x,face.normal.y,face.normal.z);
                for (const auto& corner : face.corners) {
                    const auto p = geometry.vertices[corner.vertex];
                    AutomatedCaptureLog("fog portal vertex %.9g %.9g %.9g",p.x,p.y,p.z);
                }
            }
        }
    }
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
    if (new_fog_buffer) glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,scene_fog_buffer);
    const auto& exterior_fog = presentation.data().exterior_fog;
    struct ViewRecord { std::array<float,56> values{}; std::array<int32_t,4> options{}; } view_record;
    static_assert(sizeof(ViewRecord) == 15*16,"Scene view std140 layout mismatch");
    const auto put = [&](size_t row,d3scene::Vec3 v,float w = 0) {
        const float values[4] = {v.x,v.y,v.z,w}; std::copy_n(values,4,view_record.values.data()+row*4);
    };
    put(0,camera.position,projection.near_distance); put(1,camera.right,projection.orthographic_depth_scale);
    put(2,camera.up); put(3,camera.forward);
    put(4,clip.normal,clip.constant); put(5,fog_clip.normal,fog_clip.constant);
    put(6,{projection.x_scale,projection.y_scale,projection.x_offset},projection.y_offset);
    put(7,{2.f/view.viewport.width,2.f/view.viewport.height,float(output.Width())},float(output.Height()));
    std::copy_n(history.previous.view_projection.data(),16,view_record.values.data()+32);
    put(12,exterior_fog.color); put(13,{exterior_fog.start,exterior_fog.end,0});
    view_record.options = {{int32_t(presentation.data().lighting_quality == d3scene::LightingQuality::Pixel),
        int32_t(scene_view_fog.count),int32_t(scene_view_fog.offset),0}};
    if (!scene_view_buffer) glGenBuffers(1,&scene_view_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER,scene_view_buffer);
    glBufferData(GL_UNIFORM_BUFFER,sizeof(view_record),&view_record,GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,8,scene_view_buffer);
    // Experimental view-owned optical field. Resolution is deliberately
    // independent of SSAA/MSAA; all surface depths sample the same integral.
    float field_range = 0;
    d3scene::Vec3 field_color{};
    bool scalar_field = false;
    GLuint field_queries[2]{}; // capture frame only; read after all view work
    if (field_fog) {
        PERF_MARKER_SCOPE("Scene.FogField");
        field_range = 1;
        // A shared color factors out of the optical integral. Store only depth
        // for this medium, regardless of volume count or differing densities.
        scalar_field = !FindArg("-scene-fog-vector-field") &&
            d3scene::fogFieldCommonColor(fog_volumes,scene_view_fog_volumes,field_color);
        double density_bound = 0;
        for (auto index : scene_view_fog_volumes) {
            const auto& volume = fog_volumes[index];
            const auto& bounds = volume.bounds;
            const auto& n = camera.forward;
            const d3scene::Vec3 p{n.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
                n.y >= 0 ? bounds.maximum.y : bounds.minimum.y,n.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
            field_range = (std::max)(field_range,(p.x-camera.position.x)*n.x+(p.y-camera.position.y)*n.y+(p.z-camera.position.z)*n.z+1.f);
            density_bound += (std::max)({1.0,std::abs(double(volume.color.x)),std::abs(double(volume.color.y)),
                std::abs(double(volume.color.z))})/volume.depth;
        }
        // Every per-volume occupied prefix is bounded by the field's forward
        // range. Include repeated half-store rounding and a factor-two margin;
        // dense/long-range content keeps full precision rather than overflowing.
        const double storage_bound = density_bound*field_range;
        const bool half_field = !FindArg("-scene-fog-full-precision") &&
            d3scene::fogFieldFitsHalf(storage_bound,scene_view_fog_volumes.size());
        const GLenum field_format = scalar_field ? (half_field ? GL_R16F : GL_R32F) : (half_field ? GL_RGBA16F : GL_RGBA32F);
        auto& program = scene_fog_field_programs[(scalar_field ? 2 : 0)+(half_field ? 1 : 0)];
        auto& scene_fog_field_shader = program.shader;
        auto& scene_fog_field_range = program.range;
        if (!scene_fog_field_shader.Handle()) {
            std::string source = SceneLightingShader;
            source.replace(0,source.find('\n'),"#version 430 core");
            source += "\nuniform samplerBuffer scene_fog;\n";
            source += "\nuniform samplerBuffer scene_fog_nodes;\n";
            source += SceneFogIntersectionShader;
            source += "\n#define SCENE_FOG_SLICES "+std::to_string(SceneFogFieldSlices)+"\n";
            source += "\n#define SCENE_FOG_LANES "+std::to_string(SceneFogFieldLanes)+"\n";
            source += scalar_field ? "\n#define SCENE_FOG_SCALAR_FIELD 1\n" : "\n#define SCENE_FOG_SCALAR_FIELD 0\n";
            source += "\n#define SCENE_FOG_FIELD_FORMAT ";
            source += scalar_field ? (half_field ? "r16f\n" : "r32f\n") : (half_field ? "rgba16f\n" : "rgba32f\n");
            source += SceneFogFieldComputeShader;
            scene_fog_field_shader.AttachComputeSource(source.c_str());
            glUniformBlockBinding(scene_fog_field_shader.Handle(),glGetUniformBlockIndex(scene_fog_field_shader.Handle(),"SceneViewBlock"),8);
            scene_fog_field_shader.Use();
            glUniform1i(scene_fog_field_shader.FindUniform("scene_fog"),6);
            glUniform1i(scene_fog_field_shader.FindUniform("scene_fog_nodes"),13);
            glUniform1i(scene_fog_field_shader.FindUniform("scene_influences"),3);
            scene_fog_field_range = scene_fog_field_shader.FindUniform("fog_field_range");
        }
        const int width_arg = FindArg("-scene-fog-field-width");
        const char* width_text = width_arg ? GetArg(width_arg+1) : nullptr;
        const int width = width_text ? std::atoi(width_text) : 160;
        if (width < 32 || width > 640) Error("Fog field experiment width must be 32..640");
        const int height = (std::max)(1,(std::min)(320,int(std::ceil(double(width)*view.viewport.height/view.viewport.width))));
        const auto allocate = [&](GLuint& texture,int depth,GLint format = GL_RGBA32F) {
            if (!texture) glGenTextures(1,&texture);
            glBindTexture(GL_TEXTURE_3D,texture);
            glTexImage3D(GL_TEXTURE_3D,0,format,width,height,depth,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        };
        glActiveTexture(GL_TEXTURE12);
        if (width != scene_fog_field_width || height != scene_fog_field_height || field_format != scene_fog_field_format) {
            allocate(scene_fog_field_texture,SceneFogFieldSlices+1,field_format);
            scene_fog_field_width = width; scene_fog_field_height = height;
            scene_fog_field_format = field_format;
        }
        // Wait for the preceding view's sampler reads before reusing scratch.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        scene_fog_field_shader.Use();
        glUniform1f(scene_fog_field_range,field_range);
        glBindImageTexture(0,scene_fog_field_texture,0,GL_TRUE,0,GL_READ_WRITE,field_format);
        if (AutomatedCapturePending()) {
            glGenQueries(2,field_queries);
            glQueryCounter(field_queries[0],GL_TIMESTAMP);
        }
        glDispatchCompute((width+SceneFogFieldLanes-1)/SceneFogFieldLanes,height,1);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        if (field_queries[0]) glQueryCounter(field_queries[1],GL_TIMESTAMP);
        glBindImageTexture(0,0,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
        glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_3D,scene_fog_field_texture);
        if (AutomatedCapturePending() && FindArg("-scene-fog-field-audit"))
            AuditSceneFogField(view,visibility.camera,fog_volumes,scene_view_fog_volumes,width,height,field_range,scalar_field,field_color);
        if (AutomatedCapturePending()) AutomatedCaptureLog("scene fog field %dx%dx%d range=%.6g storage=%d channels=%d bound=%.9g",
            width,height,SceneFogFieldSlices,field_range,half_field ? 16 : 32,scalar_field ? 1 : 4,storage_bound);
    }
    scene_shader.Use();
    glUniform1f(scene_uniforms.fog_field_range,field_range);
    glUniform4f(scene_uniforms.fog_field_color,field_color.x,field_color.y,field_color.z,scalar_field ? 1.f : 0.f);
    glUniform1i(scene_uniforms.fog_debug,FindArg("-scene-fog-debug") != 0);
    const auto exteriorRoom = [&](int room) {
        if (room < 0) return true;
        const auto* found = presentation.roomIndices().find(room);
        return !found || presentation.data().rooms[found->second].exterior;
    };

    check_scene_error("pass setup");

    // Build compact GPU draw records once, then submit by buffer offset. The
    // program no longer receives dozens of individually named uniforms per face.
    auto& packets = scene_draw_packets; packets.resize(visibility.surfaces.size());
    if (!scene_draw_record_bytes) {
        GLint alignment = 16;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&alignment);
        scene_draw_record_bytes = ((22*16+size_t(alignment)-1)/size_t(alignment))*size_t(alignment);
    }
    const size_t record_bytes = scene_draw_record_bytes;
    auto& records = scene_draw_records; records.assign(visibility.surfaces.size()*(record_bytes/sizeof(float)),0);
    {
    PERF_MARKER_SCOPE("Scene.DrawRecords");
    for (size_t item_index = 0; item_index < visibility.surfaces.size(); ++item_index) {
        const auto& item = visibility.surfaces[item_index];
        const d3scene::SurfaceBinding* binding;
        const d3scene::ResolvedModel* resolved_model = nullptr;
        const d3scene::PreparedModelPart* resolved_part = nullptr;
        SceneMeshGpu* mesh;
        ElementRange prepared_positions(0,0);
        if (item.mesh) {
            const auto& instance = presentation.data().meshes[item.mesh_index];
            const auto id = instance.asset->id();
            mesh = &scene_meshes.get(instance.asset); UploadSceneMesh(*mesh,id,instance.asset->geometry());
            binding = &instance.surface;
            prepared_positions = scene_mesh_position_ranges[item.mesh_index];
        } else if (item.terrain) {
            const auto& terrain = presentation.data().terrain;
            if (scene_terrain.size() <= item.terrain_patch) scene_terrain.resize(item.terrain_patch+1);
            auto& batches = scene_terrain[item.terrain_patch];
            if (batches.size() <= item.terrain_batch) batches.resize(item.terrain_batch+1);
            mesh = &batches[item.terrain_batch];
            UploadSceneMesh(*mesh,terrain.asset->id(),terrain.asset->patches()[item.terrain_patch].batches[item.terrain_batch].geometry);
            binding = &terrain.surface(item.terrain_patch,item.terrain_batch);
        } else if (item.model) {
            const auto& object = presentation.data().model_instances[item.model_instance_index];
            const auto& model = presentation.resolvedModel(item.model_instance_index,item.model_variant);
            resolved_model = &model; resolved_part = &model.parts[item.part_index];
            const auto id = model.asset->id();
            if (scene_models.size() <= id.slot) scene_models.resize(static_cast<size_t>(id.slot)+1);
            auto& parts = scene_models[id.slot];
            if (parts.size() <= item.part_index) parts.resize(static_cast<size_t>(item.part_index)+1);
            mesh = &parts[item.part_index];
            const auto& asset_part = model.asset->geometry().parts[item.part_index];
            UploadSceneMesh(*mesh,id,asset_part.geometry,&asset_part);
            binding = &model.parts[item.part_index].surface(model.asset->geometry().parts[item.part_index],item.polygon_index);
            prepared_positions = scene_part_ranges[model.parts[item.part_index].compiled_index].positions;
        } else {
            const auto& room = presentation.data().rooms[item.room_index];
            if (scene_rooms.size() <= static_cast<size_t>(room.number)) scene_rooms.resize(room.number+1);
            mesh = &scene_rooms[room.number];
            UploadSceneMesh(*mesh,room.asset->id(),room.asset->geometry());
            binding = &room.surfaces[item.polygon_index];
        }
        const auto& surface = *binding;

        const auto& gpu = *mesh;
        const auto instance_modulation = resolved_model ? resolved_model->modulation : d3scene::SurfaceModulation{};
        const auto modulation = d3scene::modulatedSurface(surface,instance_modulation);
        const auto policy = d3scene::modulatedMaterialPolicy(surface,instance_modulation.alpha);
        const bool planar_image = policy.planar_slot && view.reflections == d3scene::ReflectionPolicy::Images;
        auto* record = records.data()+item_index*(record_bytes/sizeof(float));
        record[79] = float(policy.planar_slot != 0);
        record[80] = float(planar_image);
        uint32_t surface_index = binding->compiled_index;
        if (item.terrain) surface_index = presentation.data().terrain.patches[item.terrain_patch].surface_base+item.terrain_batch;
        if (item.model) {
            const auto& model = *resolved_model;
            const auto& part = *resolved_part;
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
            const auto& part = *resolved_part;
            if (part.light_influences_prepared) {
                influences = ElementRange();
                const uint32_t table = view.dynamic_lighting && !surface.unlit ? scene_part_ranges[part.compiled_index].light_table : 0;
                std::memcpy(record+86,&table,sizeof(table));
            }
        }
        record[62] = float(influences.offset);
        record[63] = float(view.dynamic_lighting && !surface.unlit ? influences.count : 0);
        record[68] = float(surface.unlit);
        record[66] = float(static_cast<unsigned>(surface.occlusion_class));
        record[67] = float(policy.layer == d3scene::RenderLayer::World ? surface.soft_depth_range : 0);
        record[69] = float(policy.layer == d3scene::RenderLayer::World);
        record[70] = float(policy.blend == d3scene::BlendMode::Additive);
        record[71] = float(policy.blend == d3scene::BlendMode::Multiply);
        const bool exterior = item.terrain || (item.model ? exteriorRoom(presentation.data().model_instances[item.model_instance_index].room) :
            (item.mesh ? exteriorRoom(presentation.data().meshes[item.mesh_index].room) : presentation.data().rooms[item.room_index].exterior));
        record[72] = float(view.collection == 0 && exterior && exterior_fog.enabled);
        const auto& world = visibility.world(item);
        { const auto value = world.position; record[0] = value.x; record[1] = value.y; record[2] = value.z; } { const auto value = world.right; record[4] = value.x; record[5] = value.y; record[6] = value.z; }
        { const auto value = world.up; record[8] = value.x; record[9] = value.y; record[10] = value.z; } { const auto value = world.forward; record[12] = value.x; record[13] = value.y; record[14] = value.z; }
        d3scene::SurfaceMotion motion;
        motion.previous = world;
        if (resolved_part) {
            const auto& prepared = scene_part_ranges[resolved_part->compiled_index];
            motion.previous = prepared.previous_world; motion.identity = prepared.motion_identity;
            motion.valid = history.previous_valid && prepared.motion_valid;
        }
        { const auto value = motion.previous.position; record[16] = value.x; record[17] = value.y; record[18] = value.z; } { const auto value = motion.previous.right; record[20] = value.x; record[21] = value.y; record[22] = value.z; }
        { const auto value = motion.previous.up; record[24] = value.x; record[25] = value.y; record[26] = value.z; } { const auto value = motion.previous.forward; record[28] = value.x; record[29] = value.y; record[30] = value.z; }
        { const uint32_t value = uint32_t(motion.identity); std::memcpy(record+84,&value,sizeof(value)); }
        { const uint32_t value = uint32_t(motion.valid); std::memcpy(record+85,&value,sizeof(value)); }
        { const auto value = surface.base_color; record[40] = value.x; record[41] = value.y; record[42] = value.z; }
        { const auto value = modulation.color; record[44] = value.x; record[45] = value.y; record[46] = value.z; }
        { const auto value = modulation.ambient; record[32] = value.x; record[33] = value.y; record[34] = value.z; } { const auto value = surface.ambient_direction; record[36] = value.x; record[37] = value.y; record[38] = value.z; }
        record[47] = float(surface.directional_ambient);
        record[65] = float(!item.model && !item.mesh && !item.terrain &&
            presentation.data().rooms[item.room_index].uses_room_illumination ? presentation.data().rooms[item.room_index].light_scalar : surface.lighting_modulation);
        record[73] = float(surface.color_image != 0 || surface.view_image_slot != 0);
        record[74] = float(surface.baked_image != 0);
        record[75] = float(policy.texture_alpha != d3scene::TextureAlpha::Ignore);
        record[76] = float(policy.alpha_cutoff);
        record[60] = float(surface.lightmap_u_scale); record[61] = float(surface.lightmap_v_scale);
        const float time = float(presentation.data().simulation_time);
        record[58] = surface.u_offset+(surface.scroll_period.x ? std::fmod(time/surface.scroll_period.x,1.f) : 0.f);
        record[59] = surface.v_offset+(surface.scroll_period.y ? std::fmod(time/surface.scroll_period.y,1.f) : 0.f);
        record[56] = float(surface.texture_scale.x); record[57] = float(surface.texture_scale.y);
        { const auto value = surface.raster_stripe; record[48] = value.x; record[49] = value.y; record[50] = value.z; }
        record[51] = modulation.alpha;
        record[3] = float(surface.effect_depth_offset);
        auto range = item.terrain || item.mesh ? ElementRange(0,gpu.vertices.count) : gpu.polygons[item.polygon_index];
        ElementRange material_indices;
        if (item.model_material_group) {
            const auto group = resolved_model->asset->geometry().parts[item.part_index].surface_for_polygon[item.polygon_index];
            material_indices = gpu.material_groups[group];
        }
        range.offset += gpu.vertices.offset;
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
        record[11] = float(int64_t(prepared_positions.offset)-gpu.vertices.offset);
        // Disabled streams have no address. Canonical records let adjacent
        // draws share a UBO without introducing fragment-side draw indirection.
        for (const auto pair : {std::pair<int,int>{15,19},{23,27},{31,35},{39,43},{7,11},{63,62},{78,77}})
            if (!record[pair.first]) record[pair.second] = 0;

        packets[item_index] = {binding,gpu.vao,range,policy,
            item.model_material_group ? gpu.material_indices : 0,material_indices,GLint(gpu.vertices.offset)};
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
        BindSceneTables();
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth_for_sampling);
        GL_BindFramebufferTexture(estimate.ao_texture,11,GL_LINEAR);
        scene_shader.Use();
        glUniform1i(scene_uniforms.scene_occlusion_enabled,1);
        glUniform1f(scene_uniforms.scene_occlusion_intensity,estimate.debug_display ? 1.f :
            (std::max)(0.f,(std::min)(4.f,OpenGL_preferred_state.ao_intensity)));
        glUniform1i(scene_uniforms.scene_highlight_suppression,OpenGL_preferred_state.bloom_enabled && !estimate.debug_display);
        glUniform1f(scene_uniforms.scene_highlight_gamma,OpenGL_preferred_state.gamma != 0 ? 1.f/OpenGL_preferred_state.gamma : 1.f);
        glUniform1f(scene_uniforms.scene_highlight_threshold,OpenGL_preferred_state.bloom_threshold);
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
        BindSceneTables();
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER,scene_fog_texture);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth_for_sampling);
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
        const bool force_copy = AutomatedCaptureFrameIndex() >= 0 && FindArg("-scene-depth-copy-reference");
        const bool resolved_storage = output.Samples() >= 2 && !force_copy;
        if (resolved_storage) {
            // The resolve texture is distinct from the multisampled draw depth.
            // LateWorld only writes the latter; no resolve occurs inside that
            // pass. Borrow this immutable opaque snapshot until the view ends,
            // rather than copying the full supersampled depth a second time.
            depth_for_sampling = output.DepthTextureForRead();
        } else {
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
        depth_for_sampling = depth.texture;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_draw);
        const auto snapshot_error = glGetError();
        if (snapshot_error != GL_NO_ERROR) Error("Scene opaque-depth snapshot failed: GL 0x%x source=%d viewport=%d,%d,%d,%d",
            snapshot_error,previous_draw,viewport[0],viewport[1],viewport[2],viewport[3]);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,depth_for_sampling);
        scene_shader.Use();
        glUniform2i(scene_uniforms.depth_snapshot_origin,resolved_storage ? 0 : viewport[0],resolved_storage ? 0 : viewport[1]);
        continue;
      }
      // Image consumers do not write clip distances; never leak this state.
      depth_prepass = pass.kind == d3scene::ViewPassKind::DepthSurface;
      const bool volume_layer = pass.kind == d3scene::ViewPassKind::SoftVolumes;
      bind_outputs();
      if (target.coverage.width) {
          glEnable(GL_SCISSOR_TEST);
          glScissor(target.coverage.x,target.coverage.y,target.coverage.width,target.coverage.height);
      }
      if (depth_prepass) {
          glViewport(raster.x/SupersamplingFactor(),raster.y/SupersamplingFactor(),
              raster.width/SupersamplingFactor(),raster.height/SupersamplingFactor());
          glDisable(GL_MULTISAMPLE);
      } else {
          glViewport(raster.x,raster.y,raster.width,raster.height);
          if (output.Samples() >= 2) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
      }
      glUniform1i(scene_uniforms.surface_prepass,depth_prepass);
      if (volume_layer) {
          const int width = (std::max)(1,raster.width/SupersamplingFactor());
          const int height = (std::max)(1,raster.height/SupersamplingFactor());
          if (!scene_volume_texture) glGenTextures(1,&scene_volume_texture);
          if (!scene_volume_framebuffer) glGenFramebuffers(1,&scene_volume_framebuffer);
          glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_2D,scene_volume_texture);
          glBindFramebuffer(GL_DRAW_FRAMEBUFFER,scene_volume_framebuffer);
          if (width != scene_volume_width || height != scene_volume_height) {
              glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
              glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
              glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
              glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
              glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
              glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scene_volume_texture,0);
              glDrawBuffer(GL_COLOR_ATTACHMENT0);
              if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Error("Soft volume target incomplete");
              scene_volume_width = width; scene_volume_height = height;
          }
          glDrawBuffer(GL_COLOR_ATTACHMENT0); glDisable(GL_SCISSOR_TEST);
          glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
          const GLfloat clear[4]{}; glClearBufferfv(GL_COLOR,0,clear);
          glViewport(0,0,width,height); glDisable(GL_MULTISAMPLE);
          glUniform4f(scene_uniforms.fragment_mapping,float(raster.width)/width,float(raster.height)/height,float(raster.x),float(raster.y));
      }
      if (view.has_clip_plane) glEnable(GL_CLIP_DISTANCE0);
      else glDisable(GL_CLIP_DISTANCE0);
      // State is scoped to this pass, not guessed from a prior view or an
      // image-space consumer. Preserve draw order while sharing consecutive
      // raster policies; texture/geometry identities do not change that policy.
      using RasterKey = std::tuple<d3scene::RenderLayer,d3scene::BlendMode,d3scene::DepthMode,bool,bool>;
      RasterKey previous_raster;
      bool raster_bound = false;
      GLuint previous_vao = 0;
      struct ImageBinding { uint32_t image = 0; bool wrap = false; };
      std::array<ImageBinding,9> image_bindings{};
      const auto bind_image = [&](uint32_t image,int unit,bool wrap,bool baked) {
          if (!image) return;
          auto& previous = image_bindings[unit];
          // Each unit has a fixed baked/non-baked role throughout this pass.
          if (previous.image == image && previous.wrap == wrap) return;
          BindSceneImage(presentation.image(image),unit,wrap,baked);
          previous = {image,wrap};
      };
      const auto compatible = [&](uint32_t first,uint32_t next) {
          const auto& a = packets[first]; const auto& b = packets[next];
          const auto& x = *a.binding; const auto& y = *b.binding;
          const auto& ia = visibility.surfaces[first]; const auto& ib = visibility.surfaces[next];
          const auto images = [](const d3scene::SurfaceBinding& s) {
              return std::make_tuple(s.color_image,s.baked_image,s.specular_image,s.texture_wrap,s.lightmap_wrap);
          };
          const auto raster = [](const d3scene::MaterialPolicy& p) { return std::make_tuple(p.layer,p.blend,p.depth); };
          return !a.index_buffer && !b.index_buffer && a.vao == b.vao && images(x) == images(y) && raster(a.policy) == raster(b.policy) &&
              ia.model == ib.model &&
              (ia.terrain || ia.model_material_group || (ia.mesh && !x.two_sided)) ==
                  (ib.terrain || ib.model_material_group || (ib.mesh && !y.two_sided)) &&
              !x.view_image_slot && !y.view_image_slot && !a.policy.planar_slot && !b.policy.planar_slot &&
              std::memcmp(records.data()+first*(record_bytes/sizeof(float)),
                  records.data()+next*(record_bytes/sizeof(float)),88*sizeof(float)) == 0;
      };
      static thread_local std::vector<GLint> draw_starts;
      static thread_local std::vector<GLsizei> draw_counts;
      for (uint32_t item_index = pass.begin; item_index < pass.end; ++item_index) {
        const auto& item = visibility.surfaces[item_index];
        const auto& packet = packets[item_index];
        const auto* binding = packet.binding;
        const auto& surface = *binding;
        const auto range = packet.range;
        const auto& policy = packet.policy;
        if ((surface.bitmap >= 0 && !surface.color_image) || (surface.lightmap_bitmap >= 0 && !surface.baked_image) ||
            (surface.specular_bitmap >= 0 && !surface.specular_image))
            Error("Scene surface reached execution without prepared image assets");
        bind_image(surface.color_image,0,surface.texture_wrap,false);
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
            image_bindings = {};
            glBindSampler(unit,0);
            glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            return found->second;
        };
        if (surface.view_image_slot) bind_view_image(surface.view_image_slot,0);
        const bool planar_image = policy.planar_slot && view.reflections == d3scene::ReflectionPolicy::Images;
        if (planar_image) {
            const auto& image = bind_view_image(policy.planar_slot,10);
            scene_shader.Use();
            glUniformMatrix4fv(scene_uniforms.planar_projection,1,GL_FALSE,image.matrices.view_projection.data());
        }
        bind_image(surface.baked_image,1,surface.lightmap_wrap,true);
        bind_image(surface.specular_image,8,surface.texture_wrap,false);
        scene_shader.Use();
        glBindBufferRange(GL_UNIFORM_BUFFER,7,scene_draw_buffer,item_index*record_bytes,22*16);
        const bool cull = item.terrain || item.model_material_group || (item.mesh && !surface.two_sided);
        // Authored polymodel fronts use clockwise winding, unlike the
        // terrain batches. CPU face-normal culling previously hid this
        // distinction; grouped model draws must preserve it explicitly.
        const bool clockwise = cull && ((d3scene::reversesWinding(camera) != d3scene::reversesWinding(visibility.world(item))) != item.model);
        const RasterKey raster_key{policy.layer,policy.blend,policy.depth,cull,clockwise};
        if (!raster_bound || raster_key != previous_raster) {
        if(policy.blended()) {
            glEnablei(GL_BLEND,0);
            if (policy.blend == d3scene::BlendMode::Multiply) glBlendFuncSeparatei(0,GL_DST_COLOR,GL_ZERO,GL_ZERO,GL_ONE);
            else if (policy.blend == d3scene::BlendMode::Additive) glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_ONE);
            else glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        } else glDisablei(GL_BLEND,0);
        // Metadata belongs to the solid depth sample. Blended radiance changes
        // color/remaining opaque coverage, never the hidden surface's class.
        const GLboolean solid = policy.writesSolidDepth() ? GL_TRUE : GL_FALSE;
        glDisablei(GL_BLEND,2);
        glColorMaski(2,solid,solid,solid,solid);
        const GLboolean motion_write = solid && writes_motion ? GL_TRUE : GL_FALSE;
        glColorMaski(1,motion_write,motion_write,motion_write,motion_write);
        glColorMaski(4,motion_write,motion_write,motion_write,motion_write);
        glDepthMask(policy.writesDepth() ? GL_TRUE : GL_FALSE);
        if (policy.testsDepth() && !volume_layer) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cull) {
            glEnable(GL_CULL_FACE);
            glFrontFace(clockwise ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        else glDisable(GL_CULL_FACE);
        previous_raster = raster_key; raster_bound = true;
        }
        if (packet.vao != previous_vao) { glBindVertexArray(packet.vao); previous_vao = packet.vao; }
        uint32_t end = item_index+1;
        while (end < pass.end && compatible(item_index,end)) ++end;
        if (packet.index_buffer) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,packet.index_buffer);
            glDrawElementsBaseVertex(GL_TRIANGLES,packet.indices.count,GL_UNSIGNED_INT,
                reinterpret_cast<void*>(size_t(packet.indices.offset)*sizeof(uint32_t)),packet.base_vertex);
        }
        else if (end == item_index+1) glDrawArrays(GL_TRIANGLES,range.offset,range.count);
        else {
            // Same shader, UBO, textures, raster state and geometry allocation.
            // Keep primitive order exactly; only eliminate redundant submissions.
            draw_starts.clear(); draw_counts.clear();
            for (uint32_t i = item_index; i < end; ++i) {
                draw_starts.push_back(GLint(packets[i].range.offset));
                draw_counts.push_back(GLsizei(packets[i].range.count));
            }
            glMultiDrawArrays(GL_TRIANGLES,draw_starts.data(),draw_counts.data(),GLsizei(draw_starts.size()));
            item_index = end-1;
        }
        rend_RecordDrawCall(RENDERER_DRAW_CALL_3D);
      }
      // Image-space consumers and the remaining canvas adapter own their
      // texture policies. Scene samplers must not override those policies.
      glBindSampler(0,0); glBindSampler(1,0); glBindSampler(8,0);
      glDisable(GL_CLIP_DISTANCE0);
      if (volume_layer) {
          if (!scene_volume_composite.Handle()) {
              extern const char* blitVertexSrc;
              scene_volume_composite.AttachSource(blitVertexSrc,R"GLSL(#version 330 core
uniform sampler2D particles;
uniform vec4 rectangle;
out vec4 color;
void main() { color = texture(particles,(gl_FragCoord.xy-rectangle.xy)/rectangle.zw); }
)GLSL");
              scene_volume_composite.Use();
              glUniform1i(scene_volume_composite.FindUniform("particles"),14);
              scene_volume_rectangle = scene_volume_composite.FindUniform("rectangle");
          }
          glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.Handle()); glDrawBuffer(GL_COLOR_ATTACHMENT0);
          glViewport(raster.x,raster.y,raster.width,raster.height);
          if (output.Samples() >= 2) glEnable(GL_MULTISAMPLE);
          glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
          glEnablei(GL_BLEND,0);
          glBlendFuncSeparatei(0,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
          scene_volume_composite.Use();
          glUniform4f(scene_volume_rectangle,float(raster.x),float(raster.y),float(raster.width),float(raster.height));
          glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_2D,scene_volume_texture); glBindSampler(14,0);
          glBindVertexArray(GL_GetFramebufferVAO()); glDrawArrays(GL_TRIANGLES,0,3);
          rend_RecordDrawCall(RENDERER_DRAW_CALL_POSTPROCESS);
          scene_shader.Use(); glUniform4f(scene_uniforms.fragment_mapping,1,1,0,0);
      }
    }
    if (measure_passes) {
        glQueryCounter(pass_queries.back(),GL_TIMESTAMP);
        if (field_queries[0]) {
            GLuint64 begin = 0,end = 0;
            glGetQueryObjectui64v(field_queries[0],GL_QUERY_RESULT,&begin);
            glGetQueryObjectui64v(field_queries[1],GL_QUERY_RESULT,&end);
            AutomatedCaptureLog("scene fog field GPU ms=%.6f",double(end-begin)*1.e-6);
            glDeleteQueries(2,field_queries);
        }
        uint64_t geometry_bytes = 0;
        for (uint32_t page = 0; page < scene_geometry_allocator.pageCount(); ++page)
            geometry_bytes += uint64_t(scene_geometry_allocator.capacity(page))*sizeof(SceneVertex);
        AutomatedCaptureLog("scene geometry pages=%u bytes=%llu",unsigned(scene_geometry_pages.size()),
            static_cast<unsigned long long>(geometry_bytes));
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
    if (target.coverage.width) {
        glScissor(inherited_scissor_box[0],inherited_scissor_box[1],inherited_scissor_box[2],inherited_scissor_box[3]);
        glDisable(GL_SCISSOR_TEST);
    }
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
