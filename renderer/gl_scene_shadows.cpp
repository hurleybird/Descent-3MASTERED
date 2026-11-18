// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "SceneView.h"
#include "SceneLighting.h"
#include "gameloop.h"
#include "args.h"
#include <algorithm>
#include <cmath>

extern int Last_texel_unit_set;

bool GL4Renderer::PrepareDynamicShadow(const d3scene::PreparedView& prepared) {
    const auto& frame=prepared.frame();
    const auto& data=frame.data();
    if (!data.has_shadow_focus) return false;
    auto bindCube=[&] {
        glActiveTexture(GL_TEXTURE15); glBindSampler(15,0); glBindTexture(GL_TEXTURE_2D_ARRAY,shadow_cube);
        glActiveTexture(GL_TEXTURE0); Last_texel_unit_set=0;
    };
    if (shadow_lifetime==data.scene_lifetime && shadow_sequence==data.sequence) {
        if (has_shadow_sources) bindCube();
        return has_shadow_sources;
    }
    PERF_MARKER_SCOPE("Scene.DynamicShadow");
    const double elapsed=data.simulation_time-shadow_time;
    const bool keep=shadow_lifetime==data.scene_lifetime && elapsed>=0 && elapsed<.5;
    shadow_time=data.simulation_time;
    const auto previous=shadow_sources;
    shadow_sources={}; has_shadow_sources=false;
    shadow_lifetime=data.scene_lifetime; shadow_sequence=data.sequence;
    const auto eye=data.shadow_focus;
    std::array<ShadowSource,5> wanted{};
    const auto same_id=[](const auto& a,const auto& b) {
        return a.source==b.source && a.owner==b.owner && a.channel==b.channel;
    };
    for (const auto& m:data.model_instances) {
        if (!m.visible || m.collection || !m.has_render_bounds || !m.model_count) continue;
        const auto p=m.current.position;
        const float ex=p.x-eye.x,ey=p.y-eye.y,ez=p.z-eye.z;
        const float distance=std::sqrt(ex*ex+ey*ey+ez*ez);
        if(distance>180.f) continue;
        const auto& b=m.render_bounds;
        const auto* owner=m.has_owner && m.compiled_owner<data.objects.size()?&data.objects[m.compiled_owner]:nullptr;
        const float extent=owner && owner->handle==m.owner && owner->radius>0 ? owner->radius :
            .5f*std::max({b.maximum.x-b.minimum.x,b.maximum.y-b.minimum.y,b.maximum.z-b.minimum.z});
        if(extent<.35f) continue;
        const auto* ordinal=frame.roomIndices().find(m.room);
        const auto* room=ordinal?&data.rooms[ordinal->second]:nullptr;
        // Rank nearby light-caster pairs in world space. A spot projected
        // beyond the hull is not a bound on the actual receiving surface.
        // Its exit from a camera frustum must never extinguish a shadow.
        const auto score=[&](d3scene::Vec3 source,float radius,float power) {
            const float x=p.x-source.x,y=p.y-source.y,z=p.z-source.z,len=std::sqrt(x*x+y*y+z*z);
            if (len<extent*.6f || len>=radius || !(power>0)) return 0.f;
            const float travel=std::min(radius-len,std::max(8.f,extent*3.f));
            return power*(1-len/radius)*extent/(6.f+distance+travel);
        };
        if (room && room->lamps) for(const auto& lamp:*room->lamps) {
            const auto q=lamp.position,n=lamp.normal;
            if ((p.x-q.x)*n.x+(p.y-q.y)*n.y+(p.z-q.z)*n.z<=0) continue;
            float s=score(q,lamp.radius,1.0f);
            if(s<=0) continue;
            if (keep && std::any_of(previous.begin()+1,previous.end(),[&](const auto& v){return v.room==m.room && v.lamp.face==lamp.face;})) s*=1.25f;
            auto best=std::find_if(wanted.begin()+1,wanted.end(),[&](const auto& v){return v.room==m.room && v.lamp.face==lamp.face;});
            if(best==wanted.end()) best=std::min_element(wanted.begin()+1,wanted.end(),[](const auto& a,const auto& b){return a.score<b.score;});
            if(s>best->score) {best->score=s;best->room=m.room;best->lamp=lamp;}
        }
        const auto dynamic=[&](uint32_t i) {
            const auto& l=data.lights[i];
            if (m.has_owner && l.id.source==d3scene::LightSource::Object && l.id.owner==m.owner) return;
            float s=score(l.position,l.radius,std::max({l.color.x,l.color.y,l.color.z})*d3scene::lightAttenuation(l,p));
            const auto& old=previous[0];
            if (keep && old.index>=0 && same_id(l.id,old.id)) s*=1.25f;
            if (s>wanted[0].score) {wanted[0].score=s;wanted[0].index=int(i);wanted[0].id=l.id;}
        };
        if (room && room->light_influences_prepared)
            for(const auto i:data.lightCandidates(room->light_candidates)) dynamic(i);
        else for(size_t i=0;i<data.lights.size();++i) dynamic(uint32_t(i));
    }
    // Keep physical identities in their slots. For a genuine budget change,
    // release the old influence before using that map for a different light.
    // Never interpolate emitter positions or keep a destroyed lamp alive.
    for(size_t i=0;keep && i<previous.size();++i) {
        auto s=previous[i];
        if(s.weight<=0) continue;
        const float step=std::min(1.f,float(elapsed)*(i?7.f:18.f));
        auto desired=i?std::find_if(wanted.begin()+1,wanted.end(),[&](const auto& v){return v.room==s.room && v.lamp.face==s.lamp.face;}):
                       (wanted[0].index>=0 && same_id(wanted[0].id,s.id)?wanted.begin():wanted.end());
        if(desired!=wanted.end()) {
            const float weight=std::min(1.f,s.weight+step);
            s=*desired; s.weight=weight; *desired={};
        } else {
            s.weight-=step;
            if(s.weight<=0) continue;
            if(i) {
                const auto* room=frame.roomIndices().find(s.room);
                if(!room || !data.rooms[room->second].lamps) continue;
                const auto& lamps=*data.rooms[room->second].lamps;
                const auto lamp=std::find_if(lamps.begin(),lamps.end(),[&](const auto& l){return l.face==s.lamp.face;});
                if(lamp==lamps.end()) continue;
                s.lamp=*lamp;
            } else {
                const auto light=std::find_if(data.lights.begin(),data.lights.end(),[&](const auto& l){return same_id(l.id,s.id);});
                if(light==data.lights.end()) continue;
                s.index=int(light-data.lights.begin());
            }
        }
        s.mask=0; shadow_sources[i]=s;
    }
    for(size_t i=0;i<wanted.size();++i) {
        if(wanted[i].score<=0) continue;
        auto free=i?std::find_if(shadow_sources.begin()+1,shadow_sources.end(),[](const auto& s){return s.score==0;}):
                   (shadow_sources[0].score==0?shadow_sources.begin():shadow_sources.end());
        if(free==shadow_sources.end()) continue;
        *free=wanted[i];
        free->weight=keep ? std::min(1.f,std::max(float(elapsed),0.f)*(i?7.f:18.f)) : 1.f;
    }
    for(int i=0;i<int(shadow_sources.size());++i) if(shadow_sources[i].weight>0) {
        RasterShadowSource(prepared,i);
        has_shadow_sources|=shadow_sources[i].mask!=0;
    }
    if(has_shadow_sources) bindCube();
    return has_shadow_sources;
}
void GL4Renderer::RasterShadowSource(const d3scene::PreparedView& prepared,int slot) {
    const auto& frame=prepared.frame();
    const auto& data=frame.data();
    auto& source=shadow_sources[slot];
    d3scene::Light light;
    if (slot) {light.position=source.lamp.position;light.radius=source.lamp.radius;}
    else light=data.lights[source.index];
    const auto origin=light.position;
    const auto close=[&](const d3scene::Bounds& b) {
        const float x=std::max({b.minimum.x-origin.x,origin.x-b.maximum.x,0.f});
        const float y=std::max({b.minimum.y-origin.y,origin.y-b.maximum.y,0.f});
        const float z=std::max({b.minimum.z-origin.z,origin.z-b.maximum.z,0.f});
        return x*x+y*y+z*z<=light.radius*light.radius;
    };
    shadow_batches.clear();
    auto collect=[&](const SceneMeshGpu& gpu,const d3scene::Transform& pose,const d3scene::Bounds& bounds,auto surface) {
        // Conservative overlap with each square cube-face pyramid. Do this
        // once per room/part, not once per polygon or draw.
        const float lo[]={bounds.minimum.x-origin.x,bounds.minimum.y-origin.y,bounds.minimum.z-origin.z};
        const float hi[]={bounds.maximum.x-origin.x,bounds.maximum.y-origin.y,bounds.maximum.z-origin.z};
        unsigned faces=0;
        for (int axis=0;axis<3;++axis) {
            const int a=(axis+1)%3,b=(axis+2)%3;
            const float side=std::max({.05f,lo[a],-hi[a],lo[b],-hi[b]});
            if (hi[axis]>=side) faces|=1u<<(axis*2);
            if (-lo[axis]>=side) faces|=2u<<(axis*2);
        }
        if (!faces) return;
        const auto begin=shadow_batches.size();
        for (size_t f=0;f<gpu.polygons.size();++f) {
            const auto& s=surface(f); const auto& policy=s.material;
            if (!s.draw_surface || !policy.writesSolidDepth() || policy.texture_alpha!=d3scene::TextureAlpha::Ignore) continue;
            const auto range=gpu.polygons[f];
            if (!range.count) continue;
            const GLint first=GLint(gpu.vertices.offset+range.offset);
            if (shadow_batches.size()>begin && shadow_batches.back().first+shadow_batches.back().count==first)
                shadow_batches.back().count+=range.count;
            else shadow_batches.push_back({gpu.vao,first,GLsizei(range.count),pose,faces});
        }
    };
    // Models cast onto architecture; the immutable room shell never casts
    // onto its own polygon edges. Static room shadowing remains in the bake.
    // Capture-only control: keep the exact same lights and visible models,
    // but remove model occluders from the light's view for an attribution pair.
    const bool capture=AutomatedCaptureFrameIndex()>=0;
    const bool omit_camera=capture && FindArg("-capture-no-pilot-shadow");
    const size_t caster_models=capture && (FindArg("-capture-no-shadow-casters") || FindArg("-capture-room-casters-only")) ?
        0 : data.model_instances.size();
    for (size_t i=0;i<caster_models;++i) {
        const auto& instance=data.model_instances[i];
        if (omit_camera && prepared.view().attached_camera && instance.has_owner && instance.owner==prepared.view().camera_owner) continue;
        if (!instance.visible || instance.collection || !instance.model_count || !instance.has_render_bounds ||
            !close(instance.render_bounds) || (slot && instance.room!=source.room) ||
            (!slot && instance.has_owner && light.id.source==d3scene::LightSource::Object &&
            instance.owner==light.id.owner)) continue;
        // Shadow geometry must not change when the observing camera turns or
        // when a mirror happens to render first.
        const auto variant=instance.lod_models[0];
        if (data.variants(instance)[variant].deformation!=0) continue;
        const auto& model=frame.resolvedModel(i,variant);
        if (!model.asset || model.modulation.alpha<.999f) continue;
        const auto id=model.asset->id();
        if (scene_models.size()<=id.slot) scene_models.resize(size_t(id.slot)+1);
        auto& meshes=scene_models[id.slot];
        if (meshes.size()<model.parts.size()) meshes.resize(model.parts.size());
        for (size_t j=0;j<model.parts.size();++j) {
            const auto& part=model.parts[j];
            if (!part.draw || !part.has_render_bounds || !close(part.render_bounds) || !part.vertices.empty()) continue;
            const auto& asset=model.asset->geometry().parts[j];
            UploadSceneMesh(meshes[j],id,asset.geometry,&asset);
            collect(meshes[j],d3scene::compose(instance.current,part.current),part.render_bounds,
                [&](size_t f)->const auto& {return part.surface(asset,f);});
        }
    }
    for(const auto& b:shadow_batches) source.mask|=b.faces;
    if(!source.mask) return;
    constexpr int side=384;
    if (!shadow_writer.Handle()) {
        shadow_writer.AttachSource(R"GLSL(#version 330 core
layout(location=0) in vec3 position;
uniform mat4 model;
uniform vec4 lamp;
uniform int face;
out vec3 relative;
void main() {
    relative=(model*vec4(position,1)).xyz-lamp.xyz;
    vec3 p=relative;
    vec3 v=face==0?vec3(-p.z,-p.y,p.x):face==1?vec3(p.z,-p.y,-p.x):
           face==2?vec3(p.x,p.z,p.y):face==3?vec3(p.x,-p.z,-p.y):
           face==4?vec3(p.x,-p.y,p.z):vec3(-p.x,-p.y,-p.z);
    float n=.05,f=lamp.w;
    gl_Position=vec4(v.xy,((f+n)*v.z-2.0*f*n)/(f-n),v.z);
})GLSL",R"GLSL(#version 330 core
in vec3 relative;layout(location=0) out float distance_to_light;
void main() {
    distance_to_light=length(relative);
})GLSL");
        shadow_model_uniform=shadow_writer.FindUniform("model");
        shadow_face_uniform=shadow_writer.FindUniform("face");
        shadow_lamp_uniform=shadow_writer.FindUniform("lamp");
    }
    if (!shadow_cube) {
        glGenTextures(1,&shadow_cube);glActiveTexture(GL_TEXTURE15);glBindTexture(GL_TEXTURE_2D_ARRAY,shadow_cube);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_R32F,side,side,6*GLsizei(shadow_sources.size()),0,GL_RED,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        for (auto wrap:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T}) glTexParameteri(GL_TEXTURE_2D_ARRAY,wrap,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&shadow_fbo);glGenRenderbuffers(1,&shadow_depth);
        glBindRenderbuffer(GL_RENDERBUFFER,shadow_depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,side,side);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,shadow_fbo);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,shadow_depth);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,shadow_cube,0,0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) Error("Dynamic shadow framebuffer incomplete");
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,shadow_fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_CLIP_DISTANCE0);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,side,side);shadow_writer.Use();
    glUniform4f(shadow_lamp_uniform,origin.x,origin.y,origin.z,light.radius);
    const GLfloat blank[]={1.e20f,0,0,0},far_value=1;
    const bool clear_only=capture && FindArg("-capture-shadow-clear-only");
    for (int face=0;face<6;++face) {
        if (!(source.mask&(1<<face))) continue;
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,shadow_cube,0,slot*6+face);
        glClearBufferfv(GL_COLOR,0,blank);glClearBufferfv(GL_DEPTH,0,&far_value);
        if(clear_only) continue;
        glUniform1i(shadow_face_uniform,face);
        for (const auto& b:shadow_batches) {
            if (!(b.faces&(1u<<face))) continue;
            const auto& t=b.pose;
            const GLfloat matrix[]={t.right.x,t.right.y,t.right.z,0,t.up.x,t.up.y,t.up.z,0,
                t.forward.x,t.forward.y,t.forward.z,0,t.position.x,t.position.y,t.position.z,1};
            glUniformMatrix4fv(shadow_model_uniform,1,GL_FALSE,matrix);
            glBindVertexArray(b.vao);glDrawArrays(GL_TRIANGLES,b.first,b.count);
        }
    }
    // ExecuteSceneView explicitly binds the destination and all pass state.
    glBindVertexArray(0);
    if (AutomatedCapturePending())
        AutomatedCaptureLog("shadow slot=%d emitter=%d room=%d face=%d radius=%.2f batches=%u faces=%d weight=%.3f headlight=%d owner=%llu",
            slot,source.index,source.room,source.lamp.face,light.radius,unsigned(shadow_batches.size()),source.mask,source.weight,
            int(light.headlight),static_cast<unsigned long long>(light.id.owner));
}
