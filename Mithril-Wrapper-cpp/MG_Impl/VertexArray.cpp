// Mithril-Wrapper - MG_Impl/VertexArray.cpp
// Vertex Array Objects and vertex attribute pointer state.
//
// Migrated to the modular GLContext API: VAO state lives in
// mithril::glstate::VertexArrayState (owned by g_state), per-name records are
// mithril::glstate::VertexArrayObject (SharedPtr-owned, default VAO name 0
// pre-installed so GetCurrentVertexArray() never returns null), and the current
// (non-array) generic vertex attribute values live on VertexArrayState. The
// currently-bound GL_ARRAY_BUFFER is read from BufferState. The only
// backend-specific touchpoint is that the drawing path (Drawing.cpp) reads these
// attribs to build the VkPipelineVertexInputState.
#include "includes.h"

#include <vector>

extern "C" {

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !arrays) return;
    std::vector<uint32_t> names;
    g_state->GetVertexArrayState().GenVertexArrayNames(static_cast<uint32_t>(n), names);
    for (GLsizei i = 0; i < n; ++i) {
        arrays[i] = names[static_cast<size_t>(i)];
    }
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !arrays) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = arrays[i];
        if (name == 0) continue;
        // GL name-layer deletion: erases the name from the table (name 0, the
        // default VAO, is never erased). If the deleted VAO is currently
        // bound, the binding falls back to the default VAO. The underlying
        // Vulkan vertex-input state is released asynchronously by the backend
        // disposal queue.
        g_state->GetVertexArrayState().MarkVertexArrayForDeletion(name);
    }
}

void glBindVertexArray(GLuint array) {
    MITHRIL_ENSURE_INIT();
    g_state->GetVertexArrayState().BindVertexArray(array);
}

void glEnableVertexAttribArray(GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (index >= static_cast<GLuint>(mithril::glstate::kMaxVertexAttribs)) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    const mithril::glstate::SharedPtr<mithril::glstate::VertexArrayObject>& vao =
        g_state->GetVertexArrayState().GetCurrentVertexArray();
    if (!vao) return;
    vao->attribs[index].enabled = true;
    g_state->GetVertexArrayState().NotifyAttribChanged(index);
}

void glDisableVertexAttribArray(GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (index >= static_cast<GLuint>(mithril::glstate::kMaxVertexAttribs)) return;
    const mithril::glstate::SharedPtr<mithril::glstate::VertexArrayObject>& vao =
        g_state->GetVertexArrayState().GetCurrentVertexArray();
    if (!vao) return;
    vao->attribs[index].enabled = false;
    g_state->GetVertexArrayState().NotifyAttribChanged(index);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= static_cast<GLuint>(mithril::glstate::kMaxVertexAttribs)) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    const mithril::glstate::SharedPtr<mithril::glstate::VertexArrayObject>& vao =
        g_state->GetVertexArrayState().GetCurrentVertexArray();
    if (!vao) return;
    // The currently-bound GL_ARRAY_BUFFER is captured at attrib-pointer time.
    const mithril::glstate::SharedPtr<mithril::glstate::BufferObject>& arrayBuf =
        g_state->GetBufferState().GetBoundBuffer(mithril::glstate::BufferTarget::Array);
    mithril::glstate::VertexAttrib& a = vao->attribs[index];
    a.size         = size;
    a.type         = type;
    a.normalized   = (normalized != 0);
    a.integer      = false;
    a.stride       = stride;
    a.pointer      = pointer;
    a.boundBuffer  = arrayBuf ? arrayBuf->id : 0;
    a.divisor      = a.divisor; // preserve
    g_state->GetVertexArrayState().NotifyAttribChanged(index);
}

void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= static_cast<GLuint>(mithril::glstate::kMaxVertexAttribs)) return;
    const mithril::glstate::SharedPtr<mithril::glstate::VertexArrayObject>& vao =
        g_state->GetVertexArrayState().GetCurrentVertexArray();
    if (!vao) return;
    const mithril::glstate::SharedPtr<mithril::glstate::BufferObject>& arrayBuf =
        g_state->GetBufferState().GetBoundBuffer(mithril::glstate::BufferTarget::Array);
    mithril::glstate::VertexAttrib& a = vao->attribs[index];
    a.size         = size;
    a.type         = type;
    a.normalized   = false;
    a.integer      = true;
    a.stride       = stride;
    a.pointer      = pointer;
    a.boundBuffer  = arrayBuf ? arrayBuf->id : 0;
    g_state->GetVertexArrayState().NotifyAttribChanged(index);
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    if (index >= static_cast<GLuint>(mithril::glstate::kMaxVertexAttribs)) return;
    const mithril::glstate::SharedPtr<mithril::glstate::VertexArrayObject>& vao =
        g_state->GetVertexArrayState().GetCurrentVertexArray();
    if (!vao) return;
    vao->attribs[index].divisor = divisor;
    g_state->GetVertexArrayState().NotifyAttribChanged(index);
}

void glVertexAttrib1f(GLuint index, GLfloat x) {
    MITHRIL_ENSURE_INIT();
    float vals[4] = {x, 0.0f, 0.0f, 1.0f};
    g_state->GetVertexArrayState().SetCurrentVertexAttributeFloat(index, vals);
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    MITHRIL_ENSURE_INIT();
    float vals[4] = {x, y, z, w};
    g_state->GetVertexArrayState().SetCurrentVertexAttributeFloat(index, vals);
}

void glVertexAttrib4fv(GLuint index, const GLfloat* v) {
    MITHRIL_ENSURE_INIT();
    float vals[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (v) {
        vals[0] = v[0]; vals[1] = v[1]; vals[2] = v[2]; vals[3] = v[3];
    }
    g_state->GetVertexArrayState().SetCurrentVertexAttributeFloat(index, vals);
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    const mithril::glstate::SharedPtr<mithril::glstate::ProgramObject>& p =
        g_state->GetProgramState().GetProgramObject(program);
    if (!p || !name) return;
    // Record the name -> location mapping. It is consumed by glLinkProgram
    // when translating GLSL to SPIR-V so the generated stage_input locations
    // match the application's vertex descriptor. GL spec: bindings take
    // effect at link time, replacing any previous binding for the same name.
    p->attribBindings[name] = index;
}

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)color; (void)name;
}

GLint glGetAttribLocation(GLuint program, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    const mithril::glstate::SharedPtr<mithril::glstate::ProgramObject>& p =
        g_state->GetProgramState().GetProgramObject(program);
    if (!p || !p->linked) return -1;
    auto it = p->attribs.find(name ? name : "");
    if (it == p->attribs.end()) return -1;
    return it->second.location;
}

} // extern "C"
