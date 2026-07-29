// Mithril-Wrapper - MG_State/GLState/VertexArrayState/VertexArrayObject.h
//
// VertexArrayObject: the per-name vertex-array-object record migrated from
// the flat MG_State/State.h `struct VertexArray` + `struct VertexAttrib`. It
// carries the GL-side metadata for a single VAO name: the per-attribute array
// state (enabled / size / type / normalized / integer / stride / pointer /
// bound buffer / divisor), the GL_ELEMENT_ARRAY_BUFFER bound into the VAO, and
// a `markedForDeletion` flag. The Vulkan-side vertex-input / pipeline state is
// NOT owned here: the DirectVulkan backend builds it from this record and
// caches it keyed on the version counter owned by VertexArrayState.
//
// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the legacy `struct VertexArray` usage
// pattern. Defaults mirror the OpenGL 3.3 Core values inherited from State.h
// (disabled arrays, size 4, type GL_FLOAT, no bound element buffer).
#pragma once

#include <array>
#include <cstdint>

#include <GL/gl.h>

#include "../Common.h"

namespace mithril::glstate {

// Per-attribute array state. Mirrors the legacy `struct VertexAttrib` in
// MG_State/State.h (boundBuffer / divisor widened to uint32_t to match the
// shared object-table key type).
struct VertexAttrib {
    bool enabled = false;
    GLint size = 4;
    GLenum type = GL_FLOAT;
    bool normalized = false;
    bool integer = false;
    GLsizei stride = 0;
    const void* pointer = nullptr;     // offset when a VBO is bound
    uint32_t boundBuffer = 0;          // GL_ARRAY_BUFFER at attrib-pointer time
    uint32_t divisor = 0;
};

// Single VAO record. `attribs` is a fixed-size array sized by the shared
// kMaxVertexAttribs limit (16, matching GL_MAX_VERTEX_ATTRIBS for GL 3.3 Core
// and the legacy State.h layout).
struct VertexArrayObject {
    uint32_t id = 0;
    std::array<VertexAttrib, kMaxVertexAttribs> attribs{};
    uint32_t elementArrayBuffer = 0;   // GL_ELEMENT_ARRAY_BUFFER bound into the VAO
    bool markedForDeletion = false;

    VertexArrayObject();
    explicit VertexArrayObject(uint32_t id_);
};

} // namespace mithril::glstate
