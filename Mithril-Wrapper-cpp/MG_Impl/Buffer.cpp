// Mithril-Wrapper - MG_Impl/Buffer.cpp
// Buffer object (VBO/IBO/UBO) management. CPU-side shadow storage in
// mithril::glstate::BufferObject::data plus a paired VkBuffer via
// backend_get_or_create_buffer.
//
// Migrated to the modular GLContext API: buffer state lives in
// mithril::glstate::BufferState (owned by g_state) and per-name records are
// mithril::glstate::BufferObject (SharedPtr-owned). GL_ELEMENT_ARRAY_BUFFER is
// bound into the currently-bound VAO (VertexArrayState), not a global slot.
// The Vulkan backend C API (backend_get_or_create_buffer /
// backend_buffer_upload / backend_delete_buffer) is unchanged.
#include "includes.h"

#include <cstring>
#include <vector>

extern "C" {

void glGenBuffers(GLsizei n, GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !buffers) return;
    std::vector<uint32_t> names;
    g_state->GetBufferState().GenBufferNames(static_cast<uint32_t>(n), names);
    for (GLsizei i = 0; i < n; ++i) {
        buffers[i] = names[static_cast<size_t>(i)];
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !buffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = buffers[i];
        if (name == 0) continue;
        // GL name-layer deletion: the object is detached from the table; any
        // binding slot still holding a SharedPtr keeps the BufferObject alive
        // until it is unbound. The underlying VkBuffer is released by the
        // backend disposal queue (backend_delete_buffer) once in-flight GPU
        // work referencing it completes.
        g_state->GetBufferState().MarkBufferForDeletion(name);
        backend_delete_buffer(name);
    }
}

// Resolve the buffer object currently bound to `target`. Returns a null
// SharedPtr when nothing is bound (or when the target is unrecognised, in
// which case GL_INVALID_ENUM is recorded first). GL_ELEMENT_ARRAY_BUFFER is
// bound into the currently-bound VAO rather than a global slot, so it is
// resolved through VertexArrayState.
static mithril::glstate::SharedPtr<mithril::glstate::BufferObject>
bound_buffer_for_target(GLenum target) {
    using namespace mithril::glstate;
    BufferTarget bt = GLToBufferTarget(target);
    if (bt == BufferTarget::Unknown) {
        g_state->RecordError(ErrorState::GLToErrorCode(GL_INVALID_ENUM));
        return nullptr;
    }
    if (bt == BufferTarget::ElementArray) {
        const SharedPtr<VertexArrayObject>& vao =
            g_state->GetVertexArrayState().GetCurrentVertexArray();
        if (!vao || vao->elementArrayBuffer == 0) return nullptr;
        return g_state->GetBufferState().GetBufferObject(vao->elementArrayBuffer);
    }
    return g_state->GetBufferState().GetBoundBuffer(bt);
}

void glBindBuffer(GLenum target, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    using namespace mithril::glstate;
    BufferTarget bt = GLToBufferTarget(target);
    if (bt == BufferTarget::Unknown) {
        g_state->RecordError(ErrorState::GLToErrorCode(GL_INVALID_ENUM));
        return;
    }
    if (bt == BufferTarget::ElementArray) {
        // ElementArray binding is owned by the currently-bound VAO
        // (BufferState does not touch this slot). GetCurrentVertexArray() is
        // never null because the default VAO (name 0) is always installed.
        const SharedPtr<VertexArrayObject>& vao =
            g_state->GetVertexArrayState().GetCurrentVertexArray();
        if (vao) {
            vao->elementArrayBuffer = buffer;
            if (buffer != 0) {
                const SharedPtr<BufferObject>& obj =
                    g_state->GetBufferState().CreateBufferObject(buffer);
                obj->lastTarget = target;
            }
            // Bump the VAO version so the backend rebuilds the index binding.
            g_state->GetVertexArrayState().NotifyAttribChanged(0);
        }
        return;
    }
    g_state->GetBufferState().BindBuffer(bt, buffer);
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    MITHRIL_ENSURE_INIT();
    if (size < 0) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    auto b = bound_buffer_for_target(target);
    if (!b) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_OPERATION));
        return;
    }
    b->size  = size;
    b->usage = usage;
    b->data.assign((size_t)size, 0);
    if (data && size > 0) std::memcpy(b->data.data(), data, (size_t)size);
    b->mapped = nullptr;
    // Recreate the VkBuffer (allocates + uploads).
    backend_get_or_create_buffer(b->id, data && size ? b->data.data() : nullptr, (size_t)size);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    MITHRIL_ENSURE_INIT();
    if (!data || size <= 0) return;
    auto b = bound_buffer_for_target(target);
    if (!b) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_OPERATION));
        return;
    }
    if (offset < 0 || offset + size > b->size) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    std::memcpy(b->data.data() + offset, data, (size_t)size);
    backend_buffer_upload(b->id, offset, data, (size_t)size);
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    if (size <= 0) return;
    auto src = bound_buffer_for_target(readTarget);
    auto dst = bound_buffer_for_target(writeTarget);
    if (!src || !dst) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_OPERATION));
        return;
    }
    if (readOffset < 0 || writeOffset < 0 ||
        readOffset + size > src->size || writeOffset + size > dst->size) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    std::memmove(dst->data.data() + writeOffset, src->data.data() + readOffset, (size_t)size);
    backend_buffer_upload(dst->id, writeOffset, dst->data.data() + writeOffset, (size_t)size);
}

void* glMapBuffer(GLenum target, GLenum access) {
    MITHRIL_ENSURE_INIT();
    auto b = bound_buffer_for_target(target);
    if (!b) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_OPERATION));
        return nullptr;
    }
    b->mapAccess  = access;
    b->mapOffset  = 0;
    b->mapLength  = b->size;
    b->mapped     = b->data.data();
    return b->mapped;
}

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    MITHRIL_ENSURE_INIT();
    auto b = bound_buffer_for_target(target);
    if (!b) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_OPERATION));
        return nullptr;
    }
    if (offset < 0 || length <= 0 || offset + length > b->size) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return nullptr;
    }
    if (access & GL_MAP_INVALIDATE_BUFFER_BIT) {
        std::memset(b->data.data(), 0, (size_t)b->size);
    }
    b->mapAccess  = access;
    b->mapOffset  = offset;
    b->mapLength  = length;
    b->mapped     = b->data.data() + offset;
    return b->mapped;
}

GLboolean glUnmapBuffer(GLenum target) {
    MITHRIL_ENSURE_INIT();
    auto b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return GL_FALSE;
    // Upload the (possibly) modified range to the VkBuffer.
    backend_buffer_upload(b->id, b->mapOffset, b->mapped, (size_t)b->mapLength);
    b->mapped = nullptr;
    return GL_TRUE;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    auto b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return;
    GLintptr base = b->mapOffset + offset;
    if (base < 0 || length <= 0 || base + length > b->size) return;
    backend_buffer_upload(b->id, base, (uint8_t*)b->mapped + offset, (size_t)length);
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    auto b = bound_buffer_for_target(target);
    if (!b) { *params = 0; return; }
    switch (pname) {
        case GL_BUFFER_SIZE:  *params = (GLint)b->size;  break;
        case GL_BUFFER_USAGE: *params = (GLint)b->usage; break;
        case GL_BUFFER_ACCESS:*params = (GLint)b->mapAccess; break;
        default:              *params = 0; break;
    }
}

void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data) {
    MITHRIL_ENSURE_INIT();
    if (!data || size <= 0) return;
    auto b = bound_buffer_for_target(target);
    if (!b) return;
    if (offset < 0 || offset + size > b->size) return;
    std::memcpy(data, b->data.data() + offset, (size_t)size);
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glBindBuffer(target, buffer);
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)offset; (void)size;
    glBindBuffer(target, buffer);
}

} // extern "C"
