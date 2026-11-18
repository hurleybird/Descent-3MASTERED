// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
// A receiver-focused projection of the optical field. Each column provides
// a line in forward depth. Four columns must share a valid interval and pass
// the original edge test throughout it. Fit one screen-space plane at each
// end of that interval. The eight corner residuals bound the fit everywhere
// inside: both reference and reconstruction are multi-affine. One packed
// record supplies both planes; out-of-range receivers use the original field.
static constexpr const char* SceneDisplayFogCompute=R"GLSL(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(binding=7) uniform sampler2D depth_image;
layout(binding=12) uniform sampler3D field_image;
layout(rgba32ui,binding=0) writeonly uniform uimage2D display_fog;
uniform ivec4 depth_rect;
uniform vec2 near_range;
shared vec4 lines[100];
shared float receivers[100];
const float eps=.00020, far_z=60000.0;
vec2 halfPair(vec2 v){return unpackHalf2x16(packHalf2x16(v));}
float knot(int j,float unit){return float(j*j)*unit;}
float val(ivec2 p,int j){return texelFetch(field_image,ivec3(p,j),0).r;}
bool bent(ivec2 p,int j){return texelFetch(field_image,ivec3(p,j),0).z!=0.0;}
vec4 lineAt(ivec2 p,ivec3 dim,out float receiver) {
    p=clamp(p,ivec2(0),dim.xy-1);
    ivec2 q=ivec2((vec2(p)+.5)/vec2(dim.xy)*vec2(depth_rect.zw))+depth_rect.xy;
    float d=texelFetch(depth_image,q,0).r;
    int n=dim.z-1;
    float unit=near_range.y/float(n*n), z=near_range.x/max(1.e-9,1.0-d);
    receiver=min(z,far_z);
    int i=min(n-1,int(floor(sqrt(clamp(z/near_range.y,0.,1.0))*float(n))));
    vec4 rec=texelFetch(field_image,ivec3(p,i+1),0);
    float zl=knot(i,unit),zh=knot(i+1,unit),a=rec.y,b=rec.x;
    bool extend_low=true,extend_high=true;
    if(z>=near_range.y){zl=near_range.y;zh=far_z;a=b;i=n;}
    else if(rec.z!=0.0 && rec.w>0.0 && rec.w<1.0) {
        // Certify only the straight side containing this receiver. Never
        // extrapolate through the fog boundary when growing the interval.
        float at=mix(zl,zh,rec.w),v=mix(a,b,rec.w)+rec.z*rec.w*(1.0-rec.w);
        if(z<at){zh=at;b=v;extend_high=false;}
        else {zl=at;a=v;extend_low=false;}
    }
    if(zh<=zl || abs(b-a)>.09 || abs(a)>32.0 || abs(b)>32.0) return vec4(1,-1,0,0);
    float slope=(b-a)/(zh-zl), intercept=a-slope*zl;
    vec2 line=halfPair(vec2(slope,intercept));
    if(abs((slope-line.x)*zl+intercept-line.y)>eps || abs((slope-line.x)*zh+intercept-line.y)>eps)
        return vec4(1,-1,0,0);
    slope=line.x;intercept=line.y;
    for(int j=i-1;extend_low && j>=max(0,i-16);--j) {
        float t=knot(j,unit);
        if(bent(p,j+1)) break;
        if(abs(val(p,j)-(slope*t+intercept))>eps || abs(slope)*(zl-t)>.09) break;
        zl=t;
    }
    for(int j=i+2;extend_high && j<=min(n,i+17);++j) {
        float t=knot(j,unit);
        if(bent(p,j)) break;
        if(abs(val(p,j)-(slope*t+intercept))>eps || abs(slope)*(t-zh)>.09) break;
        zh=t;
    }
    if(zh>=near_range.y && slope==0.0) zh=far_z;
    return vec4(zl,zh,slope,intercept);
}
float low(vec4 v){return min(min(v.x,v.y),min(v.z,v.w));}
float high(vec4 v){return max(max(v.x,v.y),max(v.z,v.w));}
vec2 certify(int k) {
    vec4 a=lines[k],b=lines[k+1],c=lines[k+10],d=lines[k+11];
    vec2 span=vec2(max(max(a.x,b.x),max(c.x,d.x)),min(min(a.y,b.y),min(c.y,d.y)));
    if(span.x>=span.y) return vec2(65000,-65000);
    vec4 slopes=vec4(a.z,b.z,c.z,d.z), bias=vec4(a.w,b.w,c.w,d.w);
    // Pairwise absolute and relative differences are affine. Passing the
    // same test at both endpoints bounds every intermediate depth.
    for(int attempt=0;attempt<2;++attempt) {
        vec4 l=slopes*span.x+bias,h=slopes*span.y+bias;
        bool ok=max(high(l)-low(l),high(h)-low(h))<.0027 ||
            min(low(l)-.75*high(l),low(h)-.75*high(h))>.0012 || min(low(l),low(h))>1.0012;
        if(ok) return span;
        vec4 zs=vec4(receivers[k],receivers[k+1],receivers[k+10],receivers[k+11]);
        float z=clamp((dot(zs,vec4(1))-low(zs)-high(zs))*.5,span.x,span.y);
        span=vec2(max(span.x,z*.96),min(span.y,z*1.04));
    }
    return vec2(65000,-65000);
}
void main() {
    int lane=int(gl_LocalInvocationIndex);
    ivec2 base=ivec2(gl_WorkGroupID.xy)*8-1;
    ivec3 dim=textureSize(field_image,0);
    for(int i=lane;i<100;i+=64) lines[i]=lineAt(base+ivec2(i%10,i/10),dim,receivers[i]);
    barrier();
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThan(p,dim.xy))) return;
    int k=int(gl_LocalInvocationID.x)+int(gl_LocalInvocationID.y)*10;
    vec2 span=halfPair(certify(k)*vec2(1.002,.998));
    uvec4 result=uvec4(packHalf2x16(vec2(1,-1)),0,0,0);
    if(span.y>span.x) {
        vec4 a=lines[k],b=lines[k+1],c=lines[k+10],d=lines[k+11];
        vec4 slopes=vec4(a.z,b.z,c.z,d.z),bias=vec4(a.w,b.w,c.w,d.w);
        vec4 v=slopes*span.x+bias,w=slopes*span.y+bias;
        if(min(low(v),low(w))>1.0012) {v=vec4(1);w=vec4(1);}
        vec2 m=halfPair(vec2(dot(v,vec4(.25)),dot(w,vec4(.25))));
        vec2 dx=halfPair(vec2(dot(v,vec4(-.5,.5,-.5,.5)),dot(w,vec4(-.5,.5,-.5,.5))));
        vec2 dy=halfPair(vec2(dot(v,vec4(-.5,-.5,.5,.5)),dot(w,vec4(-.5,-.5,.5,.5))));
        vec4 sx=vec4(-.5,.5,-.5,.5), sy=vec4(-.5,-.5,.5,.5);
        if(max(high(abs(m.x+dx.x*sx+dy.x*sy-v)),high(abs(m.y+dx.y*sx+dy.y*sy-w)))<.00045)
            result=uvec4(packHalf2x16(span),packHalf2x16(m),packHalf2x16(dx),packHalf2x16(dy));
    }
    imageStore(display_fog,p,result);
}
)GLSL";
