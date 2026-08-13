#include "MG_Impl/DirectGlApi.h"

#include "egl/EglBridge.h"
#include "MG_State/DirectGlContext.h"

#include <GL/gl.h>

#include <cstring>
#include <new>
#include <mutex>
#include <unordered_set>
#include <string_view>

#if defined(__GNUC__)
#define MITHRIL_GL_EXPORT __attribute__((visibility("default")))
#else
#define MITHRIL_GL_EXPORT
#endif

namespace {

std::mutex syncMutex;
std::unordered_set<GLsync> syncObjects;

mithril::frontend::gl::DirectGlContext* context() {
    return mithril::egl::bridge::currentGlContext();
}

template <typename Fn>
void withContext(Fn&& callback) {
    if (auto* current = context()) callback(*current);
}

void unsupported() { withContext([](auto& c) { c.setError(GL_INVALID_OPERATION); }); }

} // namespace

extern "C" {

MITHRIL_GL_EXPORT GLenum glGetError() {
    auto* current = context();
    return current != nullptr ? current->takeError() : GL_INVALID_OPERATION;
}

MITHRIL_GL_EXPORT const GLubyte* glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR: return reinterpret_cast<const GLubyte*>("Mithril Direct Metal");
        case GL_RENDERER: return reinterpret_cast<const GLubyte*>("Apple Metal 2+");
        case GL_VERSION: return reinterpret_cast<const GLubyte*>("3.3 Mithril Direct Metal");
        case GL_SHADING_LANGUAGE_VERSION: return reinterpret_cast<const GLubyte*>("3.30 Mithril GLSL-to-MSL");
        case GL_EXTENSIONS: return reinterpret_cast<const GLubyte*>("");
        default: if (auto* current = context()) current->setError(GL_INVALID_ENUM); return nullptr;
    }
}
MITHRIL_GL_EXPORT const GLubyte* glGetStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS) { if (auto* current = context()) current->setError(GL_INVALID_ENUM); return nullptr; }
    if (index != 0) { if (auto* current = context()) current->setError(GL_INVALID_VALUE); return nullptr; }
    return nullptr;
}

MITHRIL_GL_EXPORT void glGetIntegerv(GLenum name, GLint* output) { withContext([&](auto& c) { c.getIntegerv(name, output); }); }
MITHRIL_GL_EXPORT void glGetFloatv(GLenum name, GLfloat* output) { withContext([&](auto& c) { c.getFloatv(name, output); }); }
MITHRIL_GL_EXPORT void glGetDoublev(GLenum name, GLdouble* output) { if (!output) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } GLfloat values[4]{}; withContext([&](auto& c) { c.getFloatv(name, values); }); const std::size_t count = name == GL_VIEWPORT || name == GL_COLOR_CLEAR_VALUE || name == GL_BLEND_COLOR ? 4U : name == GL_DEPTH_RANGE ? 2U : 1U; for (std::size_t i = 0; i < count; ++i) output[i] = values[i]; }
MITHRIL_GL_EXPORT void glGetInteger64v(GLenum name, GLint64* output) { if (!output) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } GLint value = 0; withContext([&](auto& c) { c.getIntegerv(name, &value); }); *output = value; }
MITHRIL_GL_EXPORT void glGetIntegeri_v(GLenum name, GLuint index, GLint* output) { withContext([&](auto& c) { c.getIntegeri(name, index, output); }); }
MITHRIL_GL_EXPORT void glGetBooleanv(GLenum name, GLboolean* output) { withContext([&](auto& c) { c.getBooleanv(name, output); }); }
MITHRIL_GL_EXPORT void glPixelStorei(GLenum name, GLint value) { withContext([&](auto& c) { c.pixelStore(name, value); }); }
MITHRIL_GL_EXPORT void glPixelStoref(GLenum name, GLfloat value) { glPixelStorei(name, static_cast<GLint>(value)); }
MITHRIL_GL_EXPORT void glGenBuffers(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genBuffers(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteBuffers(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteBuffers(n, v); }); }
MITHRIL_GL_EXPORT void glBindBuffer(GLenum t, GLuint v) { withContext([&](auto& c) { c.bindBuffer(t, v); }); }
MITHRIL_GL_EXPORT void glBufferData(GLenum t, GLsizeiptr s, const void* d, GLenum u) { withContext([&](auto& c) { c.bufferData(t, s, d, u); }); }
MITHRIL_GL_EXPORT void glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const void* d) { withContext([&](auto& c) { c.bufferSubData(t, o, s, d); }); }
MITHRIL_GL_EXPORT void* glMapBuffer(GLenum t, GLenum a) { auto* c = context(); return c ? c->mapBuffer(t, a) : nullptr; }
MITHRIL_GL_EXPORT void* glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a) { auto* c = context(); return c ? c->mapBufferRange(t, o, l, a) : nullptr; }
MITHRIL_GL_EXPORT GLboolean glUnmapBuffer(GLenum t) { auto* c = context(); return c ? c->unmapBuffer(t) : GL_FALSE; }
MITHRIL_GL_EXPORT void glFlushMappedBufferRange(GLenum t, GLintptr o, GLsizeiptr l) { withContext([&](auto& c) { c.flushMappedBufferRange(t, o, l); }); }
MITHRIL_GL_EXPORT void glGetBufferParameteriv(GLenum t, GLenum p, GLint* v) { withContext([&](auto& c) { c.getBufferParameteriv(t, p, v); }); }
MITHRIL_GL_EXPORT void glBindBufferBase(GLenum t, GLuint i, GLuint b) { withContext([&](auto& c) { c.bindBufferBase(t, i, b); }); }
MITHRIL_GL_EXPORT void glBindBufferRange(GLenum t, GLuint i, GLuint b, GLintptr o, GLsizeiptr s) { withContext([&](auto& c) { c.bindBufferRange(t, i, b, o, s); }); }

MITHRIL_GL_EXPORT void glGenVertexArrays(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genVertexArrays(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteVertexArrays(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteVertexArrays(n, v); }); }
MITHRIL_GL_EXPORT void glBindVertexArray(GLuint v) { withContext([&](auto& c) { c.bindVertexArray(v); }); }
MITHRIL_GL_EXPORT void glEnableVertexAttribArray(GLuint i) { withContext([&](auto& c) { c.enableVertexAttrib(i, true); }); }
MITHRIL_GL_EXPORT void glDisableVertexAttribArray(GLuint i) { withContext([&](auto& c) { c.enableVertexAttrib(i, false); }); }
MITHRIL_GL_EXPORT void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei stride, const void* p) {
    withContext([&](auto& c) { c.vertexAttribPointer(i, s, t, n, stride, p, false); });
}
MITHRIL_GL_EXPORT void glVertexAttribIPointer(GLuint i, GLint s, GLenum t, GLsizei stride, const void* p) {
    withContext([&](auto& c) { c.vertexAttribPointer(i, s, t, GL_FALSE, stride, p, true); });
}
MITHRIL_GL_EXPORT void glVertexAttribDivisor(GLuint i, GLuint d) { withContext([&](auto& c) { c.vertexAttribDivisor(i, d); }); }

MITHRIL_GL_EXPORT GLuint glCreateShader(GLenum t) { auto* c = context(); return c ? c->createShader(t) : 0; }
MITHRIL_GL_EXPORT void glDeleteShader(GLuint v) { withContext([&](auto& c) { c.deleteShader(v); }); }
MITHRIL_GL_EXPORT void glShaderSource(GLuint s, GLsizei n, const GLchar* const* v, const GLint* l) { withContext([&](auto& c) { c.shaderSource(s, n, v, l); }); }
MITHRIL_GL_EXPORT void glCompileShader(GLuint s) { withContext([&](auto& c) { c.compileShader(s); }); }
MITHRIL_GL_EXPORT void glGetShaderiv(GLuint s, GLenum p, GLint* v) { withContext([&](auto& c) { c.getShaderiv(s, p, v); }); }
MITHRIL_GL_EXPORT void glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei* l, GLchar* v) { withContext([&](auto& c) { c.getShaderInfoLog(s, n, l, v); }); }
MITHRIL_GL_EXPORT void glGetShaderSource(GLuint s, GLsizei n, GLsizei* l, GLchar* v) { withContext([&](auto& c) { c.getShaderSource(s, n, l, v); }); }
MITHRIL_GL_EXPORT void glReleaseShaderCompiler() {}
MITHRIL_GL_EXPORT void glShaderBinary(GLsizei, const GLuint*, GLenum, const void*, GLsizei) { unsupported(); }

MITHRIL_GL_EXPORT GLuint glCreateProgram() { auto* c = context(); return c ? c->createProgram() : 0; }
MITHRIL_GL_EXPORT void glDeleteProgram(GLuint v) { withContext([&](auto& c) { c.deleteProgram(v); }); }
MITHRIL_GL_EXPORT void glAttachShader(GLuint p, GLuint s) { withContext([&](auto& c) { c.attachShader(p, s); }); }
MITHRIL_GL_EXPORT void glDetachShader(GLuint p, GLuint s) { withContext([&](auto& c) { c.detachShader(p, s); }); }
MITHRIL_GL_EXPORT void glLinkProgram(GLuint p) { withContext([&](auto& c) { c.linkProgram(p); }); }
MITHRIL_GL_EXPORT void glUseProgram(GLuint p) { withContext([&](auto& c) { c.useProgram(p); }); }
MITHRIL_GL_EXPORT void glGetProgramiv(GLuint p, GLenum n, GLint* v) { withContext([&](auto& c) { c.getProgramiv(p, n, v); }); }
MITHRIL_GL_EXPORT void glGetProgramInfoLog(GLuint p, GLsizei n, GLsizei* l, GLchar* v) { withContext([&](auto& c) { c.getProgramInfoLog(p, n, l, v); }); }
MITHRIL_GL_EXPORT void glGetAttachedShaders(GLuint p, GLsizei n, GLsizei* c, GLuint* s) { withContext([&](auto& x) { x.getAttachedShaders(p, n, c, s); }); }
MITHRIL_GL_EXPORT void glBindAttribLocation(GLuint p, GLuint i, const GLchar* n) { withContext([&](auto& c) { c.bindAttribLocation(p, i, n); }); }
MITHRIL_GL_EXPORT GLint glGetAttribLocation(GLuint p, const GLchar* n) { auto* c = context(); return c ? c->getAttribLocation(p, n) : -1; }
MITHRIL_GL_EXPORT void glGetActiveUniform(GLuint p, GLuint i, GLsizei n, GLsizei* l, GLint* s, GLenum* t, GLchar* v) { withContext([&](auto& c) { c.getActiveUniform(p, i, n, l, s, t, v); }); }
MITHRIL_GL_EXPORT void glGetActiveAttrib(GLuint p, GLuint i, GLsizei n, GLsizei* l, GLint* s, GLenum* t, GLchar* v) { withContext([&](auto& c) { c.getActiveAttrib(p, i, n, l, s, t, v); }); }
MITHRIL_GL_EXPORT GLuint glGetUniformBlockIndex(GLuint p, const GLchar* n) { auto* c = context(); return c ? c->getUniformBlockIndex(p, n) : GL_INVALID_INDEX; }
MITHRIL_GL_EXPORT void glGetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint* v) { withContext([&](auto& c) { c.getActiveUniformBlockiv(p, i, n, v); }); }
MITHRIL_GL_EXPORT void glUniformBlockBinding(GLuint p, GLuint i, GLuint b) { withContext([&](auto& c) { c.uniformBlockBinding(p, i, b); }); }
MITHRIL_GL_EXPORT void glGetUniformfv(GLuint p, GLint l, GLfloat* v) { withContext([&](auto& c) { c.getUniform(p, l, mithril::shader::ScalarKind::floating, v); }); }
MITHRIL_GL_EXPORT void glGetUniformiv(GLuint p, GLint l, GLint* v) { withContext([&](auto& c) { c.getUniform(p, l, mithril::shader::ScalarKind::signedInteger, v); }); }
MITHRIL_GL_EXPORT GLint glGetUniformLocation(GLuint p, const GLchar* n) { auto* c = context(); return c ? c->getUniformLocation(p, n) : -1; }
#define MITHRIL_UNIFORM_1(name, type, kind) \
    MITHRIL_GL_EXPORT void name(GLint l, type v0) { const type v[] = {v0}; withContext([&](auto& c) { c.setUniform(l, 1, kind, 1, 1, GL_FALSE, v); }); }
#define MITHRIL_UNIFORM_2(name, type, kind) \
    MITHRIL_GL_EXPORT void name(GLint l, type v0, type v1) { const type v[] = {v0, v1}; withContext([&](auto& c) { c.setUniform(l, 1, kind, 1, 2, GL_FALSE, v); }); }
#define MITHRIL_UNIFORM_3(name, type, kind) \
    MITHRIL_GL_EXPORT void name(GLint l, type v0, type v1, type v2) { const type v[] = {v0, v1, v2}; withContext([&](auto& c) { c.setUniform(l, 1, kind, 1, 3, GL_FALSE, v); }); }
#define MITHRIL_UNIFORM_4(name, type, kind) \
    MITHRIL_GL_EXPORT void name(GLint l, type v0, type v1, type v2, type v3) { const type v[] = {v0, v1, v2, v3}; withContext([&](auto& c) { c.setUniform(l, 1, kind, 1, 4, GL_FALSE, v); }); }
MITHRIL_UNIFORM_1(glUniform1f, GLfloat, mithril::shader::ScalarKind::floating)
MITHRIL_UNIFORM_2(glUniform2f, GLfloat, mithril::shader::ScalarKind::floating)
MITHRIL_UNIFORM_3(glUniform3f, GLfloat, mithril::shader::ScalarKind::floating)
MITHRIL_UNIFORM_4(glUniform4f, GLfloat, mithril::shader::ScalarKind::floating)
MITHRIL_UNIFORM_1(glUniform1i, GLint, mithril::shader::ScalarKind::signedInteger)
MITHRIL_UNIFORM_2(glUniform2i, GLint, mithril::shader::ScalarKind::signedInteger)
MITHRIL_UNIFORM_3(glUniform3i, GLint, mithril::shader::ScalarKind::signedInteger)
MITHRIL_UNIFORM_4(glUniform4i, GLint, mithril::shader::ScalarKind::signedInteger)
MITHRIL_UNIFORM_1(glUniform1ui, GLuint, mithril::shader::ScalarKind::unsignedInteger)
MITHRIL_UNIFORM_2(glUniform2ui, GLuint, mithril::shader::ScalarKind::unsignedInteger)
MITHRIL_UNIFORM_3(glUniform3ui, GLuint, mithril::shader::ScalarKind::unsignedInteger)
MITHRIL_UNIFORM_4(glUniform4ui, GLuint, mithril::shader::ScalarKind::unsignedInteger)
#undef MITHRIL_UNIFORM_1
#undef MITHRIL_UNIFORM_2
#undef MITHRIL_UNIFORM_3
#undef MITHRIL_UNIFORM_4
#define MITHRIL_UNIFORM_VECTOR(name, type, kind, components) \
    MITHRIL_GL_EXPORT void name(GLint l, GLsizei n, const type* v) { \
        withContext([&](auto& c) { c.setUniform(l, n, kind, 1, components, GL_FALSE, v); }); \
    }
MITHRIL_UNIFORM_VECTOR(glUniform1fv, GLfloat, mithril::shader::ScalarKind::floating, 1)
MITHRIL_UNIFORM_VECTOR(glUniform2fv, GLfloat, mithril::shader::ScalarKind::floating, 2)
MITHRIL_UNIFORM_VECTOR(glUniform3fv, GLfloat, mithril::shader::ScalarKind::floating, 3)
MITHRIL_UNIFORM_VECTOR(glUniform4fv, GLfloat, mithril::shader::ScalarKind::floating, 4)
MITHRIL_UNIFORM_VECTOR(glUniform1iv, GLint, mithril::shader::ScalarKind::signedInteger, 1)
MITHRIL_UNIFORM_VECTOR(glUniform2iv, GLint, mithril::shader::ScalarKind::signedInteger, 2)
MITHRIL_UNIFORM_VECTOR(glUniform3iv, GLint, mithril::shader::ScalarKind::signedInteger, 3)
MITHRIL_UNIFORM_VECTOR(glUniform4iv, GLint, mithril::shader::ScalarKind::signedInteger, 4)
MITHRIL_UNIFORM_VECTOR(glUniform1uiv, GLuint, mithril::shader::ScalarKind::unsignedInteger, 1)
MITHRIL_UNIFORM_VECTOR(glUniform2uiv, GLuint, mithril::shader::ScalarKind::unsignedInteger, 2)
MITHRIL_UNIFORM_VECTOR(glUniform3uiv, GLuint, mithril::shader::ScalarKind::unsignedInteger, 3)
MITHRIL_UNIFORM_VECTOR(glUniform4uiv, GLuint, mithril::shader::ScalarKind::unsignedInteger, 4)
#undef MITHRIL_UNIFORM_VECTOR
#define MITHRIL_UNIFORM_MATRIX(name, columns, rows) \
    MITHRIL_GL_EXPORT void name(GLint l, GLsizei n, GLboolean t, const GLfloat* v) { \
        withContext([&](auto& c) { c.setUniform(l, n, mithril::shader::ScalarKind::floating, columns, rows, t, v); }); \
    }
MITHRIL_UNIFORM_MATRIX(glUniformMatrix2fv, 2, 2)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix3fv, 3, 3)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix4fv, 4, 4)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix2x3fv, 2, 3)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix3x2fv, 3, 2)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix2x4fv, 2, 4)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix4x2fv, 4, 2)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix3x4fv, 3, 4)
MITHRIL_UNIFORM_MATRIX(glUniformMatrix4x3fv, 4, 3)
#undef MITHRIL_UNIFORM_MATRIX
MITHRIL_GL_EXPORT void glValidateProgram(GLuint p) { GLint linked = GL_FALSE; withContext([&](auto& c) { c.getProgramiv(p, GL_LINK_STATUS, &linked); }); }

MITHRIL_GL_EXPORT void glGenTextures(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genTextures(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteTextures(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteTextures(n, v); }); }
MITHRIL_GL_EXPORT void glBindTexture(GLenum t, GLuint v) { withContext([&](auto& c) { c.bindTexture(t, v); }); }
MITHRIL_GL_EXPORT void glActiveTexture(GLenum t) { withContext([&](auto& c) { c.activeTexture(t); }); }
MITHRIL_GL_EXPORT void glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum y, const void* p) { withContext([&](auto& c) { c.texImage2D(t, l, i, w, h, b, f, y, p); }); }
MITHRIL_GL_EXPORT void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum q, const void* p) { withContext([&](auto& c) { c.texSubImage2D(t, l, x, y, w, h, f, q, p); }); }
MITHRIL_GL_EXPORT void glTexStorage2D(GLenum t, GLsizei l, GLenum f, GLsizei w, GLsizei h) { withContext([&](auto& c) { c.texStorage2D(t, l, f, w, h); }); }
MITHRIL_GL_EXPORT void glTexParameteri(GLenum t, GLenum p, GLint v) { withContext([&](auto& c) { c.texParameteri(t, p, v); }); }
MITHRIL_GL_EXPORT void glTexParameterf(GLenum t, GLenum p, GLfloat v) { glTexParameteri(t, p, static_cast<GLint>(v)); }
MITHRIL_GL_EXPORT void glTexParameteriv(GLenum t, GLenum p, const GLint* v) { if (v) glTexParameteri(t, p, *v); else withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glTexParameterfv(GLenum t, GLenum p, const GLfloat* v) { if (v) glTexParameteri(t, p, static_cast<GLint>(*v)); else withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glGenerateMipmap(GLenum t) { withContext([&](auto& c) { c.generateMipmap(t); }); }

MITHRIL_GL_EXPORT void glGenFramebuffers(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genFramebuffers(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteFramebuffers(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteFramebuffers(n, v); }); }
MITHRIL_GL_EXPORT void glBindFramebuffer(GLenum t, GLuint v) { withContext([&](auto& c) { c.bindFramebuffer(t, v); }); }
MITHRIL_GL_EXPORT void glFramebufferTexture2D(GLenum t, GLenum a, GLenum q, GLuint v, GLint l) { withContext([&](auto& c) { c.framebufferTexture2D(t, a, q, v, l); }); }
MITHRIL_GL_EXPORT void glFramebufferTexture(GLenum t, GLenum a, GLuint v, GLint l) { glFramebufferTexture2D(t, a, GL_TEXTURE_2D, v, l); }
MITHRIL_GL_EXPORT void glFramebufferTextureLayer(GLenum t, GLenum a, GLuint v, GLint l, GLint layer) { if (layer != 0) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } glFramebufferTexture2D(t, a, GL_TEXTURE_2D, v, l); }
MITHRIL_GL_EXPORT GLenum glCheckFramebufferStatus(GLenum t) { auto* c = context(); return c ? c->checkFramebufferStatus(t) : 0; }
MITHRIL_GL_EXPORT void glDrawBuffer(GLenum v) { withContext([&](auto& c) { c.drawBuffer(v); }); }
MITHRIL_GL_EXPORT void glReadBuffer(GLenum v) { withContext([&](auto& c) { c.readBuffer(v); }); }
MITHRIL_GL_EXPORT void glDrawBuffers(GLsizei n, const GLenum* v) { withContext([&](auto& c) { c.drawBuffers(n, v); }); }
MITHRIL_GL_EXPORT void glGenRenderbuffers(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genRenderbuffers(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteRenderbuffers(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteRenderbuffers(n, v); }); }
MITHRIL_GL_EXPORT void glBindRenderbuffer(GLenum t, GLuint v) { withContext([&](auto& c) { c.bindRenderbuffer(t, v); }); }
MITHRIL_GL_EXPORT void glRenderbufferStorage(GLenum t, GLenum f, GLsizei w, GLsizei h) { withContext([&](auto& c) { c.renderbufferStorage(t, f, w, h); }); }
MITHRIL_GL_EXPORT void glRenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum f, GLsizei w, GLsizei h) { if (s != 1) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } glRenderbufferStorage(t, f, w, h); }
MITHRIL_GL_EXPORT void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum q, GLuint v) { withContext([&](auto& c) { c.framebufferRenderbuffer(t, a, q, v); }); }

MITHRIL_GL_EXPORT void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { withContext([&](auto& c) { c.viewport(x, y, w, h); }); }
MITHRIL_GL_EXPORT void glDepthRange(GLdouble n, GLdouble f) { withContext([&](auto& c) { c.depthRange(n, f); }); }
MITHRIL_GL_EXPORT void glDepthRangef(GLfloat n, GLfloat f) { glDepthRange(n, f); }
MITHRIL_GL_EXPORT void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { withContext([&](auto& c) { c.scissor(x, y, w, h); }); }
MITHRIL_GL_EXPORT void glEnable(GLenum v) { withContext([&](auto& c) { c.enable(v, true); }); }
MITHRIL_GL_EXPORT void glDisable(GLenum v) { withContext([&](auto& c) { c.enable(v, false); }); }
MITHRIL_GL_EXPORT GLboolean glIsEnabled(GLenum v) { auto* c = context(); return c ? c->isEnabled(v) : GL_FALSE; }
MITHRIL_GL_EXPORT void glDepthFunc(GLenum v) { withContext([&](auto& c) { c.depthFunc(v); }); }
MITHRIL_GL_EXPORT void glDepthMask(GLboolean v) { withContext([&](auto& c) { c.depthMask(v); }); }
MITHRIL_GL_EXPORT void glBlendFunc(GLenum s, GLenum d) { withContext([&](auto& c) { c.blendFuncSeparate(s, d, s, d); }); }
MITHRIL_GL_EXPORT void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da) { withContext([&](auto& c) { c.blendFuncSeparate(sr, dr, sa, da); }); }
MITHRIL_GL_EXPORT void glBlendEquation(GLenum v) { withContext([&](auto& c) { c.blendEquationSeparate(v, v); }); }
MITHRIL_GL_EXPORT void glBlendEquationSeparate(GLenum r, GLenum a) { withContext([&](auto& c) { c.blendEquationSeparate(r, a); }); }
MITHRIL_GL_EXPORT void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { withContext([&](auto& c) { c.blendColor(r, g, b, a); }); }
MITHRIL_GL_EXPORT void glBlendFunci(GLuint b, GLenum s, GLenum d) { if (b != 0) { unsupported(); return; } glBlendFunc(s, d); }
MITHRIL_GL_EXPORT void glBlendFuncSeparatei(GLuint b, GLenum sr, GLenum dr, GLenum sa, GLenum da) { if (b != 0) { unsupported(); return; } glBlendFuncSeparate(sr, dr, sa, da); }
MITHRIL_GL_EXPORT void glBlendEquationi(GLuint b, GLenum m) { if (b != 0) { unsupported(); return; } glBlendEquation(m); }
MITHRIL_GL_EXPORT void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { withContext([&](auto& c) { c.colorMask(r, g, b, a); }); }
MITHRIL_GL_EXPORT void glCullFace(GLenum v) { withContext([&](auto& c) { c.cullFace(v); }); }
MITHRIL_GL_EXPORT void glFrontFace(GLenum v) { withContext([&](auto& c) { c.frontFace(v); }); }
MITHRIL_GL_EXPORT void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { withContext([&](auto& c) { c.clearColor(r, g, b, a); }); }
MITHRIL_GL_EXPORT void glClearDepth(GLdouble v) { withContext([&](auto& c) { c.clearDepth(v); }); }
MITHRIL_GL_EXPORT void glClearDepthf(GLfloat v) { glClearDepth(v); }
MITHRIL_GL_EXPORT void glClearStencil(GLint v) { withContext([&](auto& c) { c.clearStencil(v); }); }
MITHRIL_GL_EXPORT void glClear(GLbitfield v) { withContext([&](auto& c) { c.clear(v); }); }
MITHRIL_GL_EXPORT void glDrawArrays(GLenum m, GLint f, GLsizei n) { withContext([&](auto& c) { c.drawArrays(m, f, n); }); }
MITHRIL_GL_EXPORT void glDrawArraysInstanced(GLenum m, GLint f, GLsizei n, GLsizei i) { withContext([&](auto& c) { c.drawArrays(m, f, n, i); }); }
MITHRIL_GL_EXPORT void glDrawArraysInstancedBaseInstance(GLenum m, GLint f, GLsizei n, GLsizei i, GLuint b) { withContext([&](auto& c) { c.drawArrays(m, f, n, i, b); }); }
MITHRIL_GL_EXPORT void glDrawElements(GLenum m, GLsizei n, GLenum t, const void* i) { withContext([&](auto& c) { c.drawElements(m, n, t, i); }); }
MITHRIL_GL_EXPORT void glDrawElementsBaseVertex(GLenum m, GLsizei n, GLenum t, const void* i, GLint b) { withContext([&](auto& c) { c.drawElements(m, n, t, i, 1, b); }); }
MITHRIL_GL_EXPORT void glDrawElementsInstanced(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c) { withContext([&](auto& x) { x.drawElements(m, n, t, i, c); }); }
MITHRIL_GL_EXPORT void glDrawElementsInstancedBaseVertex(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c, GLint b) { withContext([&](auto& x) { x.drawElements(m, n, t, i, c, b); }); }
MITHRIL_GL_EXPORT void glDrawElementsInstancedBaseInstance(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c, GLuint b) { withContext([&](auto& x) { x.drawElements(m, n, t, i, c, 0, b); }); }
MITHRIL_GL_EXPORT void glDrawElementsBaseVertexBaseInstance(GLenum m, GLsizei n, GLenum t, const void* i, GLint v, GLuint b) { withContext([&](auto& x) { x.drawElements(m, n, t, i, 1, v, b); }); }
MITHRIL_GL_EXPORT void glDrawRangeElements(GLenum m, GLuint, GLuint, GLsizei n, GLenum t, const void* i) { glDrawElements(m, n, t, i); }
MITHRIL_GL_EXPORT void glMultiDrawArrays(GLenum m, const GLint* first, const GLsizei* count, GLsizei draws) { if (draws < 0 || (draws != 0 && (!first || !count))) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } for (GLsizei i = 0; i < draws; ++i) glDrawArrays(m, first[i], count[i]); }
MITHRIL_GL_EXPORT void glMultiDrawElements(GLenum m, const GLsizei* count, GLenum t, const void* const* indices, GLsizei draws) { if (draws < 0 || (draws != 0 && (!count || !indices))) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } for (GLsizei i = 0; i < draws; ++i) glDrawElements(m, count[i], t, indices[i]); }
MITHRIL_GL_EXPORT void glBindFragDataLocation(GLuint, GLuint color, const GLchar* name) { if (color != 0 || name == nullptr) unsupported(); }
MITHRIL_GL_EXPORT void glEnablei(GLenum cap, GLuint index) { if (index != 0) { unsupported(); return; } glEnable(cap); }
MITHRIL_GL_EXPORT void glDisablei(GLenum cap, GLuint index) { if (index != 0) { unsupported(); return; } glDisable(cap); }
MITHRIL_GL_EXPORT GLboolean glIsEnabledi(GLenum cap, GLuint index) { if (index != 0) { unsupported(); return GL_FALSE; } return glIsEnabled(cap); }
MITHRIL_GL_EXPORT void glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) { unsupported(); }
MITHRIL_GL_EXPORT void glCopyBufferSubData(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr) { unsupported(); }
MITHRIL_GL_EXPORT void glGetBufferSubData(GLenum, GLintptr, GLsizeiptr, void*) { unsupported(); }
MITHRIL_GL_EXPORT void glCopyTexImage2D(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) { unsupported(); }
MITHRIL_GL_EXPORT void glCopyTexSubImage2D(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) { unsupported(); }
MITHRIL_GL_EXPORT void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) { unsupported(); }
MITHRIL_GL_EXPORT void glTexImage3D(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) { unsupported(); }
MITHRIL_GL_EXPORT void glTexSubImage3D(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*) { unsupported(); }
MITHRIL_GL_EXPORT void glTexStorage3D(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) { unsupported(); }
MITHRIL_GL_EXPORT void glTexImage2DMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilMask(GLuint) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilFunc(GLenum, GLint, GLuint) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilOp(GLenum, GLenum, GLenum) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilMaskSeparate(GLenum, GLuint) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilFuncSeparate(GLenum, GLenum, GLint, GLuint) { unsupported(); }
MITHRIL_GL_EXPORT void glStencilOpSeparate(GLenum, GLenum, GLenum, GLenum) { unsupported(); }
MITHRIL_GL_EXPORT void glVertexAttrib1f(GLuint, GLfloat) { unsupported(); }
MITHRIL_GL_EXPORT void glVertexAttrib4f(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) { unsupported(); }
MITHRIL_GL_EXPORT void glVertexAttrib4fv(GLuint, const GLfloat*) { unsupported(); }
MITHRIL_GL_EXPORT void glPrimitiveRestartIndex(GLuint) { unsupported(); }
MITHRIL_GL_EXPORT void glPolygonMode(GLenum, GLenum) { unsupported(); }
MITHRIL_GL_EXPORT void glPolygonOffset(GLfloat, GLfloat) { unsupported(); }
MITHRIL_GL_EXPORT void glLineWidth(GLfloat width) { if (width <= 0) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glPointSize(GLfloat size) { if (size <= 0) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glHint(GLenum, GLenum) {}
MITHRIL_GL_EXPORT void* glXGetProcAddress(const char* name) { return reinterpret_cast<void*>(mithril::frontend::gl::lookupDirectGlProc(name)); }
MITHRIL_GL_EXPORT void* glXGetProcAddressARB(const char* name) { return glXGetProcAddress(name); }
MITHRIL_GL_EXPORT void glFlush() {}
MITHRIL_GL_EXPORT void glFinish() { withContext([](auto& c) { c.finish(); }); }
MITHRIL_GL_EXPORT GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    auto* c = context();
    if (!c) return nullptr;
    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) { c->setError(GL_INVALID_ENUM); return nullptr; }
    if (flags != 0) { c->setError(GL_INVALID_VALUE); return nullptr; }
    auto* sync = new (std::nothrow) std::uint64_t{0x4d49544852494cULL};
    if (!sync) c->setError(GL_OUT_OF_MEMORY);
    GLsync handle = reinterpret_cast<GLsync>(sync);
    { std::lock_guard lock(syncMutex); syncObjects.insert(handle); }
    return handle;
}
MITHRIL_GL_EXPORT void glDeleteSync(GLsync sync) { std::lock_guard lock(syncMutex); if (syncObjects.erase(sync) != 0) delete reinterpret_cast<std::uint64_t*>(sync); }
MITHRIL_GL_EXPORT GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64) {
    auto* c = context();
    { std::lock_guard lock(syncMutex); if (!c || !syncObjects.contains(sync)) { if (c) c->setError(GL_INVALID_VALUE); return GL_WAIT_FAILED; } }
    if ((flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) != 0) { c->setError(GL_INVALID_VALUE); return GL_WAIT_FAILED; }
    c->finish();
    return GL_CONDITION_SATISFIED;
}
MITHRIL_GL_EXPORT void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    auto* c = context();
    if (!c) return;
    { std::lock_guard lock(syncMutex); if (!syncObjects.contains(sync)) { c->setError(GL_INVALID_VALUE); return; } }
    if (flags != 0 || timeout != GL_TIMEOUT_IGNORED) { c->setError(GL_INVALID_VALUE); return; }
    c->finish();
}
MITHRIL_GL_EXPORT GLboolean glIsSync(GLsync sync) { std::lock_guard lock(syncMutex); return syncObjects.contains(sync) ? GL_TRUE : GL_FALSE; }
MITHRIL_GL_EXPORT void glDebugMessageControl(GLenum, GLenum, GLenum, GLsizei count, const GLuint* ids, GLboolean) { if (count < 0 || (count != 0 && ids == nullptr)) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message) { (void)source; (void)type; (void)id; (void)severity; if (length < 0 || (length != 0 && message == nullptr)) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glDebugMessageCallback(GLDEBUGPROC, const void*) {}
MITHRIL_GL_EXPORT GLuint glGetDebugMessageLog(GLuint, GLsizei, GLenum*, GLenum*, GLuint*, GLenum*, GLsizei*, GLchar*) { return 0; }
MITHRIL_GL_EXPORT void glPushDebugGroup(GLenum, GLuint, GLsizei length, const GLchar* message) { if (length < 0 || (length != 0 && message == nullptr)) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glPopDebugGroup() {}
MITHRIL_GL_EXPORT void glObjectLabel(GLenum, GLuint, GLsizei length, const GLchar* label) { if (length < -1 || (length != 0 && label == nullptr)) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glGetObjectLabel(GLenum, GLuint, GLsizei capacity, GLsizei* length, GLchar* label) { if (capacity < 0) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } if (length) *length = 0; if (capacity > 0 && label) *label = '\0'; }
MITHRIL_GL_EXPORT void glObjectPtrLabel(const void*, GLsizei length, const GLchar* label) { if (length < -1 || (length != 0 && label == nullptr)) withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); }
MITHRIL_GL_EXPORT void glGetObjectPtrLabel(const void*, GLsizei capacity, GLsizei* length, GLchar* label) { if (capacity < 0) { withContext([](auto& c) { c.setError(GL_INVALID_VALUE); }); return; } if (length) *length = 0; if (capacity > 0 && label) *label = '\0'; }

} // extern "C"

namespace mithril::frontend::gl {

GlProc lookupDirectGlProc(const char* name) noexcept {
    if (name == nullptr) return nullptr;
#define GL_PROC(symbol) if (std::string_view{name} == #symbol) return reinterpret_cast<GlProc>(&symbol)
    GL_PROC(glGetError); GL_PROC(glGetString); GL_PROC(glGetStringi); GL_PROC(glGetIntegerv); GL_PROC(glGetFloatv); GL_PROC(glGetDoublev); GL_PROC(glGetBooleanv); GL_PROC(glGetInteger64v); GL_PROC(glGetIntegeri_v);
    GL_PROC(glGenBuffers); GL_PROC(glDeleteBuffers); GL_PROC(glBindBuffer); GL_PROC(glBufferData); GL_PROC(glBufferSubData); GL_PROC(glCopyBufferSubData); GL_PROC(glGetBufferSubData);
    GL_PROC(glMapBuffer); GL_PROC(glMapBufferRange); GL_PROC(glUnmapBuffer); GL_PROC(glFlushMappedBufferRange); GL_PROC(glGetBufferParameteriv);
    GL_PROC(glBindBufferBase); GL_PROC(glBindBufferRange);
    GL_PROC(glGenVertexArrays); GL_PROC(glDeleteVertexArrays); GL_PROC(glBindVertexArray);
    GL_PROC(glEnableVertexAttribArray); GL_PROC(glDisableVertexAttribArray); GL_PROC(glVertexAttribPointer); GL_PROC(glVertexAttribIPointer); GL_PROC(glVertexAttribDivisor); GL_PROC(glVertexAttrib1f); GL_PROC(glVertexAttrib4f); GL_PROC(glVertexAttrib4fv);
    GL_PROC(glCreateShader); GL_PROC(glDeleteShader); GL_PROC(glShaderSource); GL_PROC(glShaderBinary); GL_PROC(glCompileShader); GL_PROC(glReleaseShaderCompiler); GL_PROC(glGetShaderiv); GL_PROC(glGetShaderInfoLog); GL_PROC(glGetShaderSource);
    GL_PROC(glCreateProgram); GL_PROC(glDeleteProgram); GL_PROC(glAttachShader); GL_PROC(glDetachShader); GL_PROC(glLinkProgram); GL_PROC(glUseProgram); GL_PROC(glValidateProgram); GL_PROC(glGetProgramiv); GL_PROC(glGetProgramInfoLog); GL_PROC(glGetAttachedShaders); GL_PROC(glBindAttribLocation); GL_PROC(glGetAttribLocation); GL_PROC(glGetActiveUniform); GL_PROC(glGetActiveAttrib); GL_PROC(glGetUniformBlockIndex); GL_PROC(glGetActiveUniformBlockiv); GL_PROC(glUniformBlockBinding); GL_PROC(glGetUniformfv); GL_PROC(glGetUniformiv);
    GL_PROC(glGetUniformLocation); GL_PROC(glUniform1f); GL_PROC(glUniform2f); GL_PROC(glUniform3f); GL_PROC(glUniform4f); GL_PROC(glUniform1i); GL_PROC(glUniform2i); GL_PROC(glUniform3i); GL_PROC(glUniform4i); GL_PROC(glUniform1ui); GL_PROC(glUniform2ui); GL_PROC(glUniform3ui); GL_PROC(glUniform4ui);
    GL_PROC(glUniform1fv); GL_PROC(glUniform2fv); GL_PROC(glUniform3fv); GL_PROC(glUniform4fv); GL_PROC(glUniform1iv); GL_PROC(glUniform2iv); GL_PROC(glUniform3iv); GL_PROC(glUniform4iv); GL_PROC(glUniform1uiv); GL_PROC(glUniform2uiv); GL_PROC(glUniform3uiv); GL_PROC(glUniform4uiv);
    GL_PROC(glUniformMatrix2fv); GL_PROC(glUniformMatrix3fv); GL_PROC(glUniformMatrix4fv); GL_PROC(glUniformMatrix2x3fv); GL_PROC(glUniformMatrix3x2fv); GL_PROC(glUniformMatrix2x4fv); GL_PROC(glUniformMatrix4x2fv); GL_PROC(glUniformMatrix3x4fv); GL_PROC(glUniformMatrix4x3fv);
    GL_PROC(glGenTextures); GL_PROC(glDeleteTextures); GL_PROC(glBindTexture); GL_PROC(glActiveTexture); GL_PROC(glTexImage2D); GL_PROC(glTexImage3D); GL_PROC(glTexSubImage2D); GL_PROC(glTexSubImage3D); GL_PROC(glTexStorage2D); GL_PROC(glTexStorage3D); GL_PROC(glTexImage2DMultisample); GL_PROC(glTexParameteri); GL_PROC(glTexParameterf); GL_PROC(glTexParameteriv); GL_PROC(glTexParameterfv); GL_PROC(glGenerateMipmap); GL_PROC(glCopyTexImage2D); GL_PROC(glCopyTexSubImage2D);
    GL_PROC(glGenFramebuffers); GL_PROC(glDeleteFramebuffers); GL_PROC(glBindFramebuffer); GL_PROC(glFramebufferTexture2D); GL_PROC(glFramebufferTexture); GL_PROC(glFramebufferTextureLayer); GL_PROC(glCheckFramebufferStatus); GL_PROC(glDrawBuffer); GL_PROC(glReadBuffer); GL_PROC(glDrawBuffers); GL_PROC(glBlitFramebuffer); GL_PROC(glReadPixels);
    GL_PROC(glGenRenderbuffers); GL_PROC(glDeleteRenderbuffers); GL_PROC(glBindRenderbuffer); GL_PROC(glRenderbufferStorage); GL_PROC(glRenderbufferStorageMultisample); GL_PROC(glFramebufferRenderbuffer);
    GL_PROC(glViewport); GL_PROC(glDepthRange); GL_PROC(glDepthRangef); GL_PROC(glScissor); GL_PROC(glEnable); GL_PROC(glDisable); GL_PROC(glIsEnabled); GL_PROC(glEnablei); GL_PROC(glDisablei); GL_PROC(glIsEnabledi); GL_PROC(glDepthFunc); GL_PROC(glDepthMask); GL_PROC(glBlendFunc); GL_PROC(glBlendFuncSeparate); GL_PROC(glBlendFunci); GL_PROC(glBlendFuncSeparatei); GL_PROC(glBlendEquation); GL_PROC(glBlendEquationSeparate); GL_PROC(glBlendEquationi); GL_PROC(glBlendColor); GL_PROC(glColorMask); GL_PROC(glCullFace); GL_PROC(glFrontFace); GL_PROC(glStencilMask); GL_PROC(glStencilFunc); GL_PROC(glStencilOp); GL_PROC(glStencilMaskSeparate); GL_PROC(glStencilFuncSeparate); GL_PROC(glStencilOpSeparate); GL_PROC(glPolygonMode); GL_PROC(glPolygonOffset); GL_PROC(glLineWidth); GL_PROC(glPointSize); GL_PROC(glHint); GL_PROC(glPixelStorei); GL_PROC(glPixelStoref); GL_PROC(glClearColor); GL_PROC(glClearDepth); GL_PROC(glClearDepthf); GL_PROC(glClearStencil); GL_PROC(glClear);
    GL_PROC(glBindFragDataLocation); GL_PROC(glDrawArrays); GL_PROC(glDrawArraysInstanced); GL_PROC(glDrawArraysInstancedBaseInstance); GL_PROC(glDrawElements); GL_PROC(glDrawElementsBaseVertex); GL_PROC(glDrawElementsInstanced); GL_PROC(glDrawElementsInstancedBaseVertex); GL_PROC(glDrawElementsInstancedBaseInstance); GL_PROC(glDrawElementsBaseVertexBaseInstance); GL_PROC(glDrawRangeElements); GL_PROC(glMultiDrawArrays); GL_PROC(glMultiDrawElements); GL_PROC(glPrimitiveRestartIndex);
    GL_PROC(glFlush); GL_PROC(glFinish);
    GL_PROC(glFenceSync); GL_PROC(glDeleteSync); GL_PROC(glClientWaitSync); GL_PROC(glWaitSync); GL_PROC(glIsSync);
    GL_PROC(glDebugMessageControl); GL_PROC(glDebugMessageInsert); GL_PROC(glDebugMessageCallback); GL_PROC(glGetDebugMessageLog); GL_PROC(glPushDebugGroup); GL_PROC(glPopDebugGroup); GL_PROC(glObjectLabel); GL_PROC(glGetObjectLabel); GL_PROC(glObjectPtrLabel); GL_PROC(glGetObjectPtrLabel);
    GL_PROC(glXGetProcAddress); GL_PROC(glXGetProcAddressARB);
#undef GL_PROC
    return nullptr;
}

} // namespace mithril::frontend::gl
