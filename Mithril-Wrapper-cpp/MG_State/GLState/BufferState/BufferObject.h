// Mithril-Wrapper - MG_State/GLState/BufferState/BufferObject.h
//
// BufferObject: the per-name buffer record migrated from the flat
// MG_State/State.h `struct Buffer`. It carries the GL-side metadata for a
// single buffer object name (id, last bound target, size, usage, the shadow
// CPU-side backing store, and the persistent-map bookkeeping) plus a
// `markedForDeletion` flag.
//
// The Vulkan-side VkBuffer handle is NOT owned here: the DirectVulkan backend
// allocates/destroys the real device memory and releases it through its
// disposal queue once in-flight GPU work completes. BufferObject is purely the
// GL name-layer state, mirroring the historical split in State.h where
// `Buffer` held only GL fields.
#pragma once

#include <cstdint>
#include <vector>

#include <GL/gl.h>

namespace mithril::glstate {

// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the legacy `struct Buffer` usage pattern.
struct BufferObject {
    uint32_t id = 0;
    GLenum lastTarget = GL_ARRAY_BUFFER;
    int64_t size = 0;
    GLenum usage = GL_STATIC_DRAW;
    std::vector<uint8_t> data;
    void* mapped = nullptr;
    GLbitfield mapAccess = 0;
    GLintptr mapOffset = 0;
    GLsizeiptr mapLength = 0;
    bool markedForDeletion = false;

    BufferObject();
    explicit BufferObject(uint32_t id_);
};

} // namespace mithril::glstate
