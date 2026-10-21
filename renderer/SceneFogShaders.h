// Descent 3MASTERED - GPL-3.0-or-later
#pragma once

static constexpr int SceneFogFieldSlices = 64;
static constexpr int SceneFogFieldLanes = 8;

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
bool fogHit(vec4 packed_a,vec4 packed_b,vec4 packed_c,vec3 direction,out float distance) {
    vec3 a=packed_a.xyz,b=packed_b.xyz,c=packed_c.xyz;
    vec3 n = vec3(packed_a.w,packed_b.w,packed_c.w);
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

// Compile centered optical columns in one pass. Boundary crossings are binned
// once per ray, analytically integrated per cell, then accumulated in depth.
static const char* SceneFogFieldComputeShader = R"GLSL(
layout(local_size_x=SCENE_FOG_LANES,local_size_y=1) in;
layout(SCENE_FOG_FIELD_FORMAT,binding=0) coherent uniform image3D fog_field_output;
uniform float fog_field_range;
const int FOG_SLICES = SCENE_FOG_SLICES;
shared float slope_events[FOG_SLICES*SCENE_FOG_LANES];
shared float depth_events[FOG_SLICES*SCENE_FOG_LANES];
void crossingEvent(float z,float sign,float start) {
    int slice = max(0,int(floor(sqrt(max(0.0,z/fog_field_range))*float(FOG_SLICES))));
    if (slice>=FOG_SLICES) return;
    int address=slice*SCENE_FOG_LANES+int(gl_LocalInvocationIndex);
    slope_events[address] += sign;
    depth_events[address] += sign*max(z,start);
}
void main() {
    ivec3 size = imageSize(fog_field_output);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel,size.xy))) return;
    int lane=int(gl_LocalInvocationIndex);
    bool first_volume=true;
    vec2 ndc = (vec2(pixel)+.5)/vec2(size.xy)*2.0-1.0;
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
        // Each invocation owns an entire output column. Commit the preceding
        // volume before reading its prefix sums back for the next contribution.
        if (!first_volume) memoryBarrierImage();
        vec4 density=vec4(appearance.rgb,1.0)/appearance.w;
        for (int i=0;i<FOG_SLICES;++i) {
            slope_events[i*SCENE_FOG_LANES+lane]=0.0; depth_events[i*SCENE_FOG_LANES+lane]=0.0;
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
                if (fogHit(texelFetch(scene_fog,offset),texelFetch(scene_fog,offset+1),
                    texelFetch(scene_fog,offset+2),direction,crossing) && crossing>1.e-5 && crossing<length) {
                    float sign=dot(texelFetch(scene_fog,offset+3).xyz,direction)>0.0 ? 1.0 : -1.0;
                    crossingEvent(crossing*forward_scale,sign,start*forward_scale);
                }
            }
            ++node;
        }
        float winding=texelFetch(scene_fog,volume*4+3).z;
        float integrated_length=0.0;
        if (first_volume) imageStore(fog_field_output,ivec3(pixel,0),vec4(0));
        for (int slice=0;slice<FOG_SLICES;++slice) {
            int address=slice*SCENE_FOG_LANES+lane;
            float low=float(slice)/float(FOG_SLICES),high=float(slice+1)/float(FOG_SLICES);
            float low_z=low*low*fog_field_range,high_z=high*high*fog_field_range;
            float interval=max(0.0,high_z-max(low_z,start*forward_scale));
            // Integrate the oriented occupancy through the cell instead of
            // accepting/missing the whole interval at one jittered depth.
            // Closed non-overlapping boundaries are exact along this column.
            // Clamp overlapping sheets per cell, never amplify their density.
            float occupied=winding*interval+slope_events[address]*high_z-depth_events[address];
            if (interval>0.0) integrated_length+=clamp(occupied,0.0,high_z-low_z);
            ivec3 output_cell=ivec3(pixel,slice+1);
#if SCENE_FOG_SCALAR_FIELD
            float prior=first_volume ? 0.0 : imageLoad(fog_field_output,output_cell).r;
            imageStore(fog_field_output,output_cell,vec4(prior+density.a*integrated_length));
#else
            vec4 prior=first_volume ? vec4(0) : imageLoad(fog_field_output,output_cell);
            imageStore(fog_field_output,output_cell,prior+density*integrated_length);
#endif
            winding+=slope_events[address];
        }
        first_volume=false;
    }
    if (first_volume) for (int slice=0;slice<=FOG_SLICES;++slice)
        imageStore(fog_field_output,ivec3(pixel,slice),vec4(0));
}
)GLSL";
