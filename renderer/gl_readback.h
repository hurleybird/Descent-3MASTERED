// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "gl_local.h"
#include <stdexcept>
inline void GL_BindReadback(Framebuffer& buffer) { buffer.BindForRead(); }
inline void GL_BindReadback(ColorFramebuffer& buffer) { glBindFramebuffer(GL_READ_FRAMEBUFFER,buffer.Handle()); }
// GL bottom-up RGBA8. Every readback preserves the caller's targets and pack
// state, including PBO binding, row padding and offsets. No draw/composition.
template<class Buffer> d3scene::ImageMip GL_ReadColorImage(Buffer& buffer) {
    d3scene::ImageMip result; result.width = buffer.Width(); result.height = buffer.Height();
    if (!result.width || !result.height || !buffer.Handle()) throw std::logic_error("Readback has no owned image");
    result.pixels.resize(size_t(result.width)*result.height*4);
    GLint read,draw,read_buffer,pack_buffer,alignment,row_length,skip_rows,skip_pixels;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glGetIntegerv(GL_READ_BUFFER,&read_buffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack_buffer); glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH,&row_length); glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    GL_BindReadback(buffer); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,result.width,result.height,GL_RGBA,GL_UNSIGNED_BYTE,result.pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
    glReadBuffer(read_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack_buffer); glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH,row_length); glPixelStorei(GL_PACK_SKIP_ROWS,skip_rows); glPixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Owned image readback failed");
    return result;
}

inline void GL_VerifyColorReadback() {
    GLint read,draw,read_mode,texture,pack,alignment,row_length,skip_rows,skip_pixels;
    GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glGetIntegerv(GL_READ_BUFFER,&read_mode); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack); glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH,&row_length); glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows); glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
    Framebuffer source; source.Update(2,1,0,GL_RGBA8);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.Handle()); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const GLfloat color[4] = {1,0,0,1}; glClearBufferfv(GL_COLOR,0,color);
    GLuint sentinel; glGenBuffers(1,&sentinel); glBindBuffer(GL_PIXEL_PACK_BUFFER,sentinel);
    glBufferData(GL_PIXEL_PACK_BUFFER,1024,nullptr,GL_STREAM_READ);
    glPixelStorei(GL_PACK_ALIGNMENT,8); glPixelStorei(GL_PACK_ROW_LENGTH,7);
    glPixelStorei(GL_PACK_SKIP_ROWS,2); glPixelStorei(GL_PACK_SKIP_PIXELS,1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,source.Handle()); glReadBuffer(GL_NONE);
    const auto pixels = GL_ReadColorImage(source);
    GLint actual[8];
    const GLenum keys[8] = {GL_READ_FRAMEBUFFER_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_BUFFER,
        GL_PIXEL_PACK_BUFFER_BINDING,GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS};
    for (int i = 0; i < 8; ++i) glGetIntegerv(keys[i],&actual[i]);
    const GLint expected[8] = {GLint(source.Handle()),GLint(source.Handle()),GL_NONE,GLint(sentinel),8,7,2,1};
    bool valid = pixels.width == 2 && pixels.height == 1 && pixels.pixels == std::vector<uint8_t>({255,0,0,255,255,0,0,255});
    for (int i = 0; i < 8; ++i) valid = valid && actual[i] == expected[i];
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack); glDeleteBuffers(1,&sentinel);
    glPixelStorei(GL_PACK_ALIGNMENT,alignment); glPixelStorei(GL_PACK_ROW_LENGTH,row_length);
    glPixelStorei(GL_PACK_SKIP_ROWS,skip_rows); glPixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
    source.Destroy();
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read); glReadBuffer(read_mode); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
    glBindTexture(GL_TEXTURE_2D,texture); glColorMask(mask[0],mask[1],mask[2],mask[3]);
    if (!valid || glGetError() != GL_NO_ERROR) Error("Owned color readback failed target/PBO/packing qualification");
}
