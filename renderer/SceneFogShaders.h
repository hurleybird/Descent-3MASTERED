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

// Each tile stores a compact bitset over the view-owned boundary candidates.
// No variable-capacity lists, atomics, CPU tile construction, or GPU readback.
static const char* SceneFogTileComputeShader = R"GLSL(
layout(local_size_x=64) in;
layout(std430,binding=0) writeonly buffer FogTiles {uint fog_tiles[];};
void main() {
    int entry=fog_volume_offset+int(gl_GlobalInvocationID.z)*4;
    int base=texelFetch(scene_influences,entry+1).r*4-SCENE_FOG_HEADER_WORDS;
    int first=texelFetch(scene_influences,base).r;
    int count=texelFetch(scene_influences,base+1).r;
    int destination=texelFetch(scene_influences,base+2).r;
    int stride=texelFetch(scene_influences,base+3).r;
    int tile=int(gl_GlobalInvocationID.x),word=int(gl_GlobalInvocationID.y);
    if (word>=stride || tile>=SCENE_FOG_TILE_SIDE*SCENE_FOG_TILE_SIDE) return;
    vec2 low=vec2(tile%SCENE_FOG_TILE_SIDE,tile/SCENE_FOG_TILE_SIDE)*(2.0/float(SCENE_FOG_TILE_SIDE))-1.0;
    vec2 high=low+2.0/float(SCENE_FOG_TILE_SIDE);
    uint mask=0u;
    for (int bit=0;bit<32 && word*32+bit<count;++bit) {
        int record=(base+first)/4+(word*32+bit)*(SCENE_FOG_TRIANGLE_WORDS/4);
        vec4 bounds=texelFetch(scene_fog_nodes,record);
        bool overlaps=all(lessThanEqual(bounds.xy,high)) && all(greaterThanEqual(bounds.zw,low));
        if (overlaps) for (int edge=1;edge<=3;++edge) {
            vec4 plane=texelFetch(scene_fog_nodes,record+edge);
            vec2 corner=vec2(plane.x>=0.0 ? high.x : low.x,plane.y>=0.0 ? high.y : low.y);
            if (dot(plane.xy,corner)+plane.z < -plane.w) overlaps=false;
        }
        if (overlaps) mask|=1u<<bit;
    }
    fog_tiles[destination+tile*stride+word]=mask;
}
)GLSL";

// Compile centered optical columns in one pass. Boundary crossings are binned
// once per ray, analytically integrated per cell, then accumulated in depth.
static const char* SceneFogFieldComputeShader = R"GLSL(
layout(local_size_x=SCENE_FOG_LANES,local_size_y=1) in;
layout(SCENE_FOG_FIELD_FORMAT,binding=0) coherent uniform image3D fog_field_output;
uniform float fog_field_range;
layout(std430,binding=0) readonly buffer FogTiles {uint fog_tiles[];};
const int FOG_SLICES = SCENE_FOG_SLICES;
shared float slope_events[FOG_SLICES*SCENE_FOG_LANES];
shared float depth_events[FOG_SLICES*SCENE_FOG_LANES];
bool fieldEdge(vec4 plane,vec2 sample_position,int flags,int edge) {
    float side=dot(plane.xy,sample_position)+plane.z;
    return side>0.0 || (side==0.0 && (flags&(1<<edge))!=0);
}
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
        int entry=fog_volume_offset+candidate*4;
        int volume=texelFetch(scene_influences,entry).r;
        int view_nodes=texelFetch(scene_influences,entry+1).r;
        vec4 appearance=texelFetch(scene_fog,volume*3);
        vec4 low=texelFetch(scene_fog,volume*3+1), high=texelFetch(scene_fog,volume*3+2);
        if (!fogBounds(low.xyz,high.xyz,inverse_direction,parallel,length)) continue;
        // Each invocation owns an entire output column. Commit the preceding
        // volume before reading its prefix sums back for the next contribution.
        if (!first_volume) memoryBarrierImage();
        vec4 density=vec4(appearance.rgb,1.0)/appearance.w;
        for (int i=0;i<FOG_SLICES;++i) {
            slope_events[i*SCENE_FOG_LANES+lane]=0.0; depth_events[i*SCENE_FOG_LANES+lane]=0.0;
        }
        int field_base=view_nodes*4-SCENE_FOG_HEADER_WORDS;
        int first=field_base+texelFetch(scene_influences,field_base).r;
        int mask_base=texelFetch(scene_influences,field_base+2).r;
        int stride=texelFetch(scene_influences,field_base+3).r;
        ivec2 tile=min(ivec2(SCENE_FOG_TILE_SIDE-1),ivec2((vec2(pixel)+.5)/vec2(size.xy)*float(SCENE_FOG_TILE_SIDE)));
        for (int word=0;word<stride;++word) {
            uint mask=fog_tiles[mask_base+(tile.y*SCENE_FOG_TILE_SIDE+tile.x)*stride+word];
            while (mask!=0u) {
                int triangle=word*32+findLSB(mask);
                mask&=mask-1u;
                int record=(first+triangle*SCENE_FOG_TRIANGLE_WORDS)/4;
                vec4 plane=texelFetch(scene_fog_nodes,record+4);
                float denominator=dot(plane.xyz,direction);
                float crossing=plane.w/denominator;
                vec4 inward=texelFetch(scene_fog_nodes,record+5);
                int flags=int(inward.w);
                if (abs(denominator)>=1.e-12 && crossing>1.e-5 && crossing<length &&
                    fieldEdge(texelFetch(scene_fog_nodes,record+1),ndc,flags,0) &&
                    fieldEdge(texelFetch(scene_fog_nodes,record+2),ndc,flags,1) &&
                    fieldEdge(texelFetch(scene_fog_nodes,record+3),ndc,flags,2)) {
                    float sign=dot(inward.xyz,direction)>0.0 ? 1.0 : -1.0;
                    crossingEvent(crossing*forward_scale,sign,start*forward_scale);
                }
            }
        }
        float winding=float(texelFetch(scene_influences,entry+3).r);
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
