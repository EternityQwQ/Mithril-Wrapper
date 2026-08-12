#include "frontend/gl/DirectGlApi.h"

#include "egl/EglBridge.h"
#include "frontend/gl/DirectGlContext.h"

#include <GL/gl.h>

#include <cstring>
#include <string_view>

#if defined(__GNUC__)
#define MITHRIL_GL_EXPORT __attribute__((visibility("default")))
#else
#define MITHRIL_GL_EXPORT
#endif

namespace {

mithril::frontend::gl::DirectGlContext* context() {
    return mithril::egl::bridge::currentGlContext();
}

template <typename Fn>
void withContext(Fn&& callback) {
    if (auto* current = context()) callback(*current);
}

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

MITHRIL_GL_EXPORT void glGetIntegerv(GLenum name, GLint* output) { withContext([&](auto& c) { c.getIntegerv(name, output); }); }
MITHRIL_GL_EXPORT void glGenBuffers(GLsizei n, GLuint* v) { withContext([&](auto& c) { c.genBuffers(n, v); }); }
MITHRIL_GL_EXPORT void glDeleteBuffers(GLsizei n, const GLuint* v) { withContext([&](auto& c) { c.deleteBuffers(n, v); }); }
MITHRIL_GL_EXPORT void glBindBuffer(GLenum t, GLuint v) { withContext([&](auto& c) { c.bindBuffer(t, v); }); }
MITHRIL_GL_EXPORT void glBufferData(GLenum t, GLsizeiptr s, const void* d, GLenum u) { withContext([&](auto& c) { c.bufferData(t, s, d, u); }); }
MITHRIL_GL_EXPORT void glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const void* d) { withContext([&](auto& c) { c.bufferSubData(t, o, s, d); }); }

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

MITHRIL_GL_EXPORT GLuint glCreateShader(GLenum t) { auto* c = context(); return c ? c->createShader(t) : 0; }
MITHRIL_GL_EXPORT void glDeleteShader(GLuint v) { withContext([&](auto& c) { c.deleteShader(v); }); }
MITHRIL_GL_EXPORT void glShaderSource(GLuint s, GLsizei n, const GLchar* const* v, const GLint* l) { withContext([&](auto& c) { c.shaderSource(s, n, v, l); }); }
MITHRIL_GL_EXPORT void glCompileShader(GLuint s) { withContext([&](auto& c) { c.compileShader(s); }); }
MITHRIL_GL_EXPORT void glGetShaderiv(GLuint s, GLenum p, GLint* v) { withContext([&](auto& c) { c.getShaderiv(s, p, v); }); }
MITHRIL_GL_EXPORT void glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei* l, GLchar* v) { withContext([&](auto& c) { c.getShaderInfoLog(s, n, l, v); }); }

MITHRIL_GL_EXPORT GLuint glCreateProgram() { auto* c = context(); return c ? c->createProgram() : 0; }
MITHRIL_GL_EXPORT void glDeleteProgram(GLuint v) { withContext([&](auto& c) { c.deleteProgram(v); }); }
MITHRIL_GL_EXPORT void glAttachShader(GLuint p, GLuint s) { withContext([&](auto& c) { c.attachShader(p, s); }); }
MITHRIL_GL_EXPORT void glDetachShader(GLuint p, GLuint s) { withContext([&](auto& c) { c.detachShader(p, s); }); }
MITHRIL_GL_EXPORT void glLinkProgram(GLuint p) { withContext([&](auto& c) { c.linkProgram(p); }); }
MITHRIL_GL_EXPORT void glUseProgram(GLuint p) { withContext([&](auto& c) { c.useProgram(p); }); }
MITHRIL_GL_EXPORT void glGetProgramiv(GLuint p, GLenum n, GLint* v) { withContext([&](auto& c) { c.getProgramiv(p, n, v); }); }
MITHRIL_GL_EXPORT void glGetProgramInfoLog(GLuint p, GLsizei n, GLsizei* l, GLchar* v) { withContext([&](auto& c) { c.getProgramInfoLog(p, n, l, v); }); }

MITHRIL_GL_EXPORT void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { withContext([&](auto& c) { c.viewport(x, y, w, h); }); }
MITHRIL_GL_EXPORT void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { withContext([&](auto& c) { c.clearColor(r, g, b, a); }); }
MITHRIL_GL_EXPORT void glClearDepth(GLdouble v) { withContext([&](auto& c) { c.clearDepth(v); }); }
MITHRIL_GL_EXPORT void glClearDepthf(GLfloat v) { glClearDepth(v); }
MITHRIL_GL_EXPORT void glClearStencil(GLint v) { withContext([&](auto& c) { c.clearStencil(v); }); }
MITHRIL_GL_EXPORT void glClear(GLbitfield v) { withContext([&](auto& c) { c.clear(v); }); }
MITHRIL_GL_EXPORT void glDrawArrays(GLenum m, GLint f, GLsizei n) { withContext([&](auto& c) { c.drawArrays(m, f, n); }); }
MITHRIL_GL_EXPORT void glDrawArraysInstanced(GLenum m, GLint f, GLsizei n, GLsizei i) { withContext([&](auto& c) { c.drawArrays(m, f, n, i); }); }
MITHRIL_GL_EXPORT void glDrawElements(GLenum m, GLsizei n, GLenum t, const void* i) { withContext([&](auto& c) { c.drawElements(m, n, t, i); }); }
MITHRIL_GL_EXPORT void glDrawElementsBaseVertex(GLenum m, GLsizei n, GLenum t, const void* i, GLint b) { withContext([&](auto& c) { c.drawElements(m, n, t, i, 1, b); }); }
MITHRIL_GL_EXPORT void glDrawElementsInstanced(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c) { withContext([&](auto& x) { x.drawElements(m, n, t, i, c); }); }
MITHRIL_GL_EXPORT void glDrawElementsInstancedBaseVertex(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c, GLint b) { withContext([&](auto& x) { x.drawElements(m, n, t, i, c, b); }); }
MITHRIL_GL_EXPORT void glFlush() {}
MITHRIL_GL_EXPORT void glFinish() { withContext([](auto& c) { c.finish(); }); }

} // extern "C"

namespace mithril::frontend::gl {

GlProc lookupDirectGlProc(const char* name) noexcept {
    if (name == nullptr) return nullptr;
#define GL_PROC(symbol) if (std::string_view{name} == #symbol) return reinterpret_cast<GlProc>(&symbol)
    GL_PROC(glGetError); GL_PROC(glGetString); GL_PROC(glGetIntegerv);
    GL_PROC(glGenBuffers); GL_PROC(glDeleteBuffers); GL_PROC(glBindBuffer); GL_PROC(glBufferData); GL_PROC(glBufferSubData);
    GL_PROC(glGenVertexArrays); GL_PROC(glDeleteVertexArrays); GL_PROC(glBindVertexArray);
    GL_PROC(glEnableVertexAttribArray); GL_PROC(glDisableVertexAttribArray); GL_PROC(glVertexAttribPointer); GL_PROC(glVertexAttribIPointer);
    GL_PROC(glCreateShader); GL_PROC(glDeleteShader); GL_PROC(glShaderSource); GL_PROC(glCompileShader); GL_PROC(glGetShaderiv); GL_PROC(glGetShaderInfoLog);
    GL_PROC(glCreateProgram); GL_PROC(glDeleteProgram); GL_PROC(glAttachShader); GL_PROC(glDetachShader); GL_PROC(glLinkProgram); GL_PROC(glUseProgram); GL_PROC(glGetProgramiv); GL_PROC(glGetProgramInfoLog);
    GL_PROC(glViewport); GL_PROC(glClearColor); GL_PROC(glClearDepth); GL_PROC(glClearDepthf); GL_PROC(glClearStencil); GL_PROC(glClear);
    GL_PROC(glDrawArrays); GL_PROC(glDrawArraysInstanced); GL_PROC(glDrawElements); GL_PROC(glDrawElementsBaseVertex); GL_PROC(glDrawElementsInstanced); GL_PROC(glDrawElementsInstancedBaseVertex);
    GL_PROC(glFlush); GL_PROC(glFinish);
#undef GL_PROC
    return nullptr;
}

} // namespace mithril::frontend::gl
