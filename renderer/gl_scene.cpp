// Descent 3MASTERED - GPL-3.0-or-later
#include "gl_local.h"
#include "gl_mesh.h"
#include "SceneView.h"
#include <cmath>

namespace {
struct SceneVertex { d3scene::Vec3 position; float u, v, lu, lv, alpha; };
const char* SceneVertexShader = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec2 light_uv;
layout(location=3) in float alpha;
uniform vec3 eye, camera_right, camera_up, camera_forward;
uniform vec4 projection;
uniform float near_distance;
out vec2 texcoord, lightcoord;
out float vertex_alpha;
void main() {
    vec3 relative = position-eye;
    float z = dot(relative,camera_forward);
    gl_Position = vec4(dot(relative,camera_right)*projection.x + z*projection.z,
                       dot(relative,camera_up)*projection.y + z*projection.w, z-2.0*near_distance,z);
    texcoord=uv; lightcoord=light_uv; vertex_alpha=alpha;
}
)GLSL";
const char* SceneFragmentShader = R"GLSL(#version 330 core
uniform sampler2D colortexture, lightmaptexture;
uniform int use_lightmap, use_texture_alpha;
uniform vec2 light_scale, texture_offset;
uniform float material_alpha;
in vec2 texcoord, lightcoord;
in float vertex_alpha;
layout(location=0) out vec4 color;
void main() {
    vec4 base=texture(colortexture,texcoord+texture_offset);
    base.a=(use_texture_alpha != 0 ? base.a : 1.0)*vertex_alpha*material_alpha;
    if(base.a < 0.01) discard;
    vec3 illumination = use_lightmap != 0 ? texture(lightmaptexture,lightcoord*light_scale).rgb : vec3(1);
    color=vec4(base.rgb*illumination,base.a);
}
)GLSL";
}

void GL4Renderer::DestroySceneResources() {
    for (auto& room : scene_rooms) {
        if (room.vao) glDeleteVertexArrays(1, &room.vao);
        if (room.buffer) glDeleteBuffers(1, &room.buffer);
    }
    scene_rooms.clear();
    scene_asset_lifetime = 0;
    scene_shader.Destroy();
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

    std::vector<bool> visible_rooms(presentation.data().rooms.size(),false);
    for (const auto& surface : visibility.surfaces) visible_rooms[surface.room_index] = true;
    for (size_t room_index = 0; room_index < presentation.data().rooms.size(); ++room_index) {
        if (!visible_rooms[room_index]) continue;
        const auto& room = presentation.data().rooms[room_index];
        const auto& geometry = room.asset->geometry();
        if (scene_rooms.size() <= static_cast<size_t>(room.number)) scene_rooms.resize(room.number+1);
        auto& gpu = scene_rooms[room.number];
        if (gpu.id != room.asset->id()) {
            if (gpu.vao) glDeleteVertexArrays(1,&gpu.vao);
            if (gpu.buffer) glDeleteBuffers(1,&gpu.buffer);
            gpu = {};
            gpu.id = room.asset->id();
            std::vector<SceneVertex> vertices;
            for (const auto& polygon : geometry.polygons) {
                const uint32_t first = static_cast<uint32_t>(vertices.size());
                for (size_t triangle=1; triangle+1<polygon.corners.size(); ++triangle) {
                    const size_t corners[3] = {0,triangle,triangle+1};
                    for (size_t corner : corners) {
                        const auto& c=polygon.corners[corner];
                        vertices.push_back({geometry.vertices[c.vertex],c.u,c.v,c.light_u,c.light_v,c.alpha/255.f});
                    }
                }
                gpu.polygons.emplace_back(first,static_cast<uint32_t>(vertices.size())-first);
            }
            glGenVertexArrays(1,&gpu.vao); glGenBuffers(1,&gpu.buffer);
            glBindVertexArray(gpu.vao); glBindBuffer(GL_ARRAY_BUFFER,gpu.buffer);
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(SceneVertex),vertices.data(),GL_STATIC_DRAW);
            const int counts[4]={3,2,2,1};
            const size_t offsets[4]={offsetof(SceneVertex,position),offsetof(SceneVertex,u),offsetof(SceneVertex,lu),offsetof(SceneVertex,alpha)};
            for (GLuint attribute=0; attribute<4; ++attribute) {
                glEnableVertexAttribArray(attribute);
                glVertexAttribPointer(attribute,counts[attribute],GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsets[attribute]));
            }
        }
    }
    for (const auto& item : visibility.surfaces) {
        const auto& room = presentation.data().rooms[item.room_index];
        const auto& surface=room.surfaces[item.polygon_index];
        const auto& gpu=scene_rooms[room.number];
        BindBitmap(surface.bitmap);
        if(surface.lightmap_bitmap>=0) BindLightmap(surface.lightmap_bitmap);
        scene_shader.Use();
        glUniform1i(scene_shader.FindUniform("use_lightmap"),surface.lightmap_bitmap>=0);
        glUniform1i(scene_shader.FindUniform("use_texture_alpha"),surface.texture_alpha);
        glUniform2f(scene_shader.FindUniform("light_scale"),surface.lightmap_u_scale,surface.lightmap_v_scale);
        glUniform2f(scene_shader.FindUniform("texture_offset"),surface.u_offset,surface.v_offset);
        glUniform1f(scene_shader.FindUniform("material_alpha"),surface.alpha);
        if(item.blended) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,surface.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
        glBindVertexArray(gpu.vao);
        const auto range=gpu.polygons[item.polygon_index];
        glDrawArrays(GL_TRIANGLES,range.offset,range.count);
        rend_RecordDrawCall(RENDERER_DRAW_CALL_3D);
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    ShaderProgram::ClearBinding();
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
