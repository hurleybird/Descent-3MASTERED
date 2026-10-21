// Descent 3MASTERED - GPL-3.0-or-later
#pragma once

// Shared by the reference surface integrator and the view-field compiler.
static const char* SceneFogIntersectionShader = R"GLSL(
vec2 fogProjectedPoint(vec3 p,int drop) {
    return drop == 0 ? p.yz : (drop == 1 ? p.zx : p.xy);
}
bool fogEdge(vec2 start,vec2 finish,vec2 p,float orientation) {
    vec2 edge = (finish-start)*orientation;
    float side = edge.x*(p.y-start.y)-edge.y*(p.x-start.x);
    return side > 0.0 || (side == 0.0 && (edge.y > 0.0 || (edge.y == 0.0 && edge.x < 0.0)));
}
bool fogHit(vec3 a,vec3 b,vec3 c,vec3 direction,out float distance) {
    vec3 n = cross(b-a,c-a);
    float denominator = dot(n,direction);
    if (abs(denominator) < 1.e-12) return false;
    distance = dot(n,a-eye)/denominator;
    if (distance < 0.0) return false;
    vec3 p = eye+direction*distance;
    vec3 magnitude = abs(n);
    int drop = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0 : (magnitude.y >= magnitude.z ? 1 : 2);
    float component = drop == 0 ? n.x : (drop == 1 ? n.y : n.z);
    float orientation = component < 0.0 ? -1.0 : 1.0;
    vec2 pa = fogProjectedPoint(a,drop), pb = fogProjectedPoint(b,drop), pc = fogProjectedPoint(c,drop);
    vec2 point = fogProjectedPoint(p,drop);
    return fogEdge(pa,pb,point,orientation) && fogEdge(pb,pc,point,orientation) && fogEdge(pc,pa,point,orientation);
}
bool fogBounds(vec3 low,vec3 high,vec3 inverse_direction,bvec3 parallel,float length) {
    if (any(bvec3(parallel.x && (eye.x < low.x || eye.x > high.x),
                  parallel.y && (eye.y < low.y || eye.y > high.y),
                  parallel.z && (eye.z < low.z || eye.z > high.z)))) return false;
    vec3 a = (low-eye)*inverse_direction, b = (high-eye)*inverse_direction;
    vec3 entry = min(a,b), exit = max(a,b);
    entry = vec3(parallel.x ? 0.0 : entry.x,parallel.y ? 0.0 : entry.y,parallel.z ? 0.0 : entry.z);
    exit = vec3(parallel.x ? length : exit.x,parallel.y ? length : exit.y,parallel.z ? length : exit.z);
    return max(0.0,max(entry.x,max(entry.y,entry.z))) <= min(length,min(exit.x,min(exit.y,exit.z)));
}
)GLSL";

// Density injection, temporal reconstruction, then integration. History stores
// a world-space medium property, never a camera-to-surface accumulated fog value.
// Boundary crossings are binned once per ray, not retraced for every depth cell.
static const char* SceneFogFieldComputeShader = R"GLSL(
layout(local_size_x=8,local_size_y=1) in;
layout(rgba16f,binding=0) writeonly uniform image3D fog_field_output;
uniform float fog_field_range;
uniform vec2 fog_jitter;
const int FOG_SLICES = 64;
shared float slope_events[FOG_SLICES*8];
shared float depth_events[FOG_SLICES*8];
shared vec4 density_samples[FOG_SLICES*8];
void crossingEvent(float z,float sign,float start) {
    int slice = max(0,int(floor(sqrt(max(0.0,z/fog_field_range))*float(FOG_SLICES))));
    if (slice>=FOG_SLICES) return;
    int address=slice*8+int(gl_LocalInvocationIndex);
    slope_events[address] += sign;
    depth_events[address] += sign*max(z,start);
}
void main() {
    ivec3 size = imageSize(fog_field_output);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel,size.xy))) return;
    int lane=int(gl_LocalInvocationIndex);
    for (int i=0;i<FOG_SLICES;++i) density_samples[i*8+lane]=vec4(0);
    vec2 ndc = (vec2(pixel)+fog_jitter)/vec2(size.xy)*2.0-1.0;
    vec3 direction = normalize(camera_forward+camera_right*((ndc.x-projection.z)/projection.x)+
        camera_up*((ndc.y-projection.w)/projection.y));
    float forward_scale = dot(direction,camera_forward);
    float length = fog_field_range/forward_scale;
    bvec3 parallel = lessThan(abs(direction),vec3(1.e-8));
    vec3 inverse_direction = 1.0/vec3(parallel.x ? 1.0 : direction.x,
        parallel.y ? 1.0 : direction.y,parallel.z ? 1.0 : direction.z);
    float start = 0.0;
    float origin_side = dot(vec4(eye,1),fog_path_plane);
    if (origin_side < 0.0) {
        float advance = dot(direction,fog_path_plane.xyz);
        start = advance > 0.0 ? min(length,-origin_side/advance) : length;
    }
    for (int candidate=0;candidate<fog_volume_count;++candidate) {
        int entry=fog_volume_offset+candidate*3;
        int volume=texelFetch(scene_influences,entry).r;
        int view_nodes=texelFetch(scene_influences,entry+1).r;
        int view_count=texelFetch(scene_influences,entry+2).r;
        vec4 appearance=texelFetch(scene_fog,volume*4);
        vec4 low=texelFetch(scene_fog,volume*4+1), high=texelFetch(scene_fog,volume*4+2);
        if (!fogBounds(low.xyz,high.xyz,inverse_direction,parallel,length)) continue;
        // Asset-wide density units keep half-float storage bounded independently
        // of authored fog depth. History uses the same units until asset reset.
        vec4 density=vec4(appearance.rgb,1.0)*texelFetch(scene_fog,volume*4+3).w;
        for (int i=0;i<FOG_SLICES;++i) {
            slope_events[i*8+lane]=0.0; depth_events[i*8+lane]=0.0;
        }
        int node=0;
        while (node<view_count) {
            int source_node=texelFetch(scene_influences,view_nodes+node*2).r;
            int record=int(low.w)+source_node*3;
            vec4 node_low=texelFetch(scene_fog,record),node_high=texelFetch(scene_fog,record+1);
            if (!fogBounds(node_low.xyz,node_high.xyz,inverse_direction,parallel,length)) {
                node=texelFetch(scene_influences,view_nodes+node*2+1).r; continue;
            }
            int count=int(texelFetch(scene_fog,record+2).x);
            for (int triangle=0;triangle<count;++triangle) {
                int offset=int(high.w)+(int(node_low.w)+triangle)*4;
                float crossing;
                if (fogHit(texelFetch(scene_fog,offset).xyz,texelFetch(scene_fog,offset+1).xyz,
                    texelFetch(scene_fog,offset+2).xyz,direction,crossing) && crossing>1.e-5 && crossing<length) {
                    float sign=dot(texelFetch(scene_fog,offset+3).xyz,direction)>0.0 ? 1.0 : -1.0;
                    crossingEvent(crossing*forward_scale,sign,start*forward_scale);
                }
            }
            ++node;
        }
        float winding=texelFetch(scene_fog,volume*4+3).z;
        for (int slice=0;slice<FOG_SLICES;++slice) {
            int address=slice*8+lane;
            float low=float(slice)/float(FOG_SLICES),high=float(slice+1)/float(FOG_SLICES);
            float low_z=low*low*fog_field_range,high_z=high*high*fog_field_range;
            float interval=max(0.0,high_z-max(low_z,start*forward_scale));
            // Integrate the oriented occupancy through the cell instead of
            // accepting/missing the whole interval at one jittered depth.
            // Closed non-overlapping boundaries are exact along this column.
            // Clamp overlapping sheets per cell, never amplify their density.
            float occupied=winding*interval+slope_events[address]*high_z-depth_events[address];
            if (interval>0.0)
                density_samples[address]+=density*clamp(occupied/(high_z-low_z),0.0,1.0);
            winding+=slope_events[address];
        }
    }
    for (int slice=0;slice<FOG_SLICES;++slice) {
        imageStore(fog_field_output,ivec3(pixel,slice),density_samples[slice*8+lane]);
    }
}
)GLSL";

static const char* SceneFogTemporalShader = R"GLSL(
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba16f,binding=0) writeonly uniform image3D fog_density_output;
uniform sampler3D fog_current_density,fog_previous_density;
uniform float fog_field_range,fog_previous_range,fog_history_weight;
uniform mat4 fog_previous_matrix;
void main() {
    ivec3 size=imageSize(fog_density_output),cell=ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(cell,size))) return;
    vec3 uv=(vec3(cell)+0.5)/vec3(size);
    vec2 ndc=uv.xy*2.0-1.0;
    // Average-density samples live at the midpoint in physical depth, not
    // the square of the midpoint in slice coordinates.
    float half_slice=0.5/float(size.z);
    float z=(uv.z*uv.z+half_slice*half_slice)*fog_field_range;
    vec3 world=eye+z*(camera_forward+camera_right*((ndc.x-projection.z)/projection.x)+
        camera_up*((ndc.y-projection.w)/projection.y));
    vec4 current=texelFetch(fog_current_density,cell,0);
    if (fog_history_weight>0.0) {
        vec4 previous=fog_previous_matrix*vec4(world,1);
        float previous_half_slice=0.5/float(textureSize(fog_previous_density,0).z);
        vec3 previous_uv=vec3(previous.xy/max(previous.w,1.e-6)*0.5+0.5,
            sqrt(max(previous.w/fog_previous_range-previous_half_slice*previous_half_slice,0.0)));
        // Reject history outside its actual sample support, including near/far
        // cells. No clamped border texel is allowed to masquerade as history.
        vec3 half_texel=0.5/vec3(textureSize(fog_previous_density,0));
        if (previous.w>0.0 && all(greaterThanEqual(previous_uv,half_texel)) && all(lessThanEqual(previous_uv,1.0-half_texel))) {
            // Repeated reprojection must not transport old density beyond the
            // current medium's local support. Keep sub-cell jitter coverage at
            // boundaries, but reject fog leaking into uniformly clear cells.
            vec4 low=current,high=current;
            for (int axis=0;axis<3;++axis) {
                ivec3 offset=ivec3(0); offset[axis]=1;
                vec4 a=texelFetch(fog_current_density,max(cell-offset,ivec3(0)),0);
                vec4 b=texelFetch(fog_current_density,min(cell+offset,size-1),0);
                low=min(low,min(a,b)); high=max(high,max(a,b));
            }
            vec4 previous_density=clamp(texture(fog_previous_density,previous_uv),low,high);
            current=mix(current,previous_density,fog_history_weight);
        }
    }
    imageStore(fog_density_output,cell,current);
}
)GLSL";

static const char* SceneFogIntegrateShader = R"GLSL(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,binding=0) writeonly uniform image3D fog_integral_output;
uniform sampler3D fog_density;
uniform float fog_field_range;
uniform float fog_density_scale;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    ivec3 size=textureSize(fog_density,0);
    if (any(greaterThanEqual(pixel,size.xy))) return;
    vec4 integrated=vec4(0);
    imageStore(fog_integral_output,ivec3(pixel,0),integrated);
    for (int slice=0;slice<size.z;++slice) {
        float low=float(slice)/float(size.z),high=float(slice+1)/float(size.z);
        integrated+=texelFetch(fog_density,ivec3(pixel,slice),0)*((high*high-low*low)*fog_field_range/fog_density_scale);
        imageStore(fog_integral_output,ivec3(pixel,slice+1),integrated);
    }
}
)GLSL";
