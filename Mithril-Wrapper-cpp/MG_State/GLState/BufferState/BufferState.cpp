// Mithril-Wrapper - MG_State/GLState/BufferState/BufferState.cpp
//
// Implementation of the BufferState domain component. See BufferState.h for
// the shared API contract (unified object-table names, SharedPtr ownership,
// single version bump per binding change).
#include "BufferState.h"

#include <memory>
#include <utility>

namespace mithril::glstate {

// GL_DISPATCH_INDIRECT_BUFFER (0x90EE) is part of the ARB_compute_shader
// surface but is missing from this repository's minimal glcorearb.h. Define it
// under an #ifndef guard so this translation unit is self-contained without
// modifying the shared GL headers — the same workaround already used by
// RenderStateEnumConverter.cpp and include/GL/gl.h.
#ifndef GL_DISPATCH_INDIRECT_BUFFER
#define GL_DISPATCH_INDIRECT_BUFFER 0x90EE
#endif

// ---- GL <-> BufferTarget translation ----
BufferTarget GLToBufferTarget(GLenum v) {
    switch (v) {
        case GL_ARRAY_BUFFER:               return BufferTarget::Array;
        case GL_ELEMENT_ARRAY_BUFFER:       return BufferTarget::ElementArray;
        case GL_UNIFORM_BUFFER:             return BufferTarget::Uniform;
        case GL_PIXEL_PACK_BUFFER:          return BufferTarget::PixelPack;
        case GL_PIXEL_UNPACK_BUFFER:        return BufferTarget::PixelUnpack;
        case GL_TRANSFORM_FEEDBACK_BUFFER:  return BufferTarget::TransformFeedback;
        case GL_COPY_READ_BUFFER:           return BufferTarget::CopyRead;
        case GL_COPY_WRITE_BUFFER:          return BufferTarget::CopyWrite;
        case GL_ATOMIC_COUNTER_BUFFER:      return BufferTarget::AtomicCounter;
        case GL_SHADER_STORAGE_BUFFER:      return BufferTarget::ShaderStorage;
        case GL_DRAW_INDIRECT_BUFFER:       return BufferTarget::DrawIndirect;
        case GL_DISPATCH_INDIRECT_BUFFER:   return BufferTarget::DispatchIndirect;
        case GL_TEXTURE_BUFFER:             return BufferTarget::TextureBuffer;
        default:                            return BufferTarget::Unknown;
    }
}

GLenum BufferTargetToGL(BufferTarget v) {
    switch (v) {
        case BufferTarget::Array:            return GL_ARRAY_BUFFER;
        case BufferTarget::ElementArray:     return GL_ELEMENT_ARRAY_BUFFER;
        case BufferTarget::Uniform:          return GL_UNIFORM_BUFFER;
        case BufferTarget::PixelPack:        return GL_PIXEL_PACK_BUFFER;
        case BufferTarget::PixelUnpack:      return GL_PIXEL_UNPACK_BUFFER;
        case BufferTarget::TransformFeedback:return GL_TRANSFORM_FEEDBACK_BUFFER;
        case BufferTarget::CopyRead:         return GL_COPY_READ_BUFFER;
        case BufferTarget::CopyWrite:        return GL_COPY_WRITE_BUFFER;
        case BufferTarget::AtomicCounter:    return GL_ATOMIC_COUNTER_BUFFER;
        case BufferTarget::ShaderStorage:    return GL_SHADER_STORAGE_BUFFER;
        case BufferTarget::DrawIndirect:     return GL_DRAW_INDIRECT_BUFFER;
        case BufferTarget::DispatchIndirect: return GL_DISPATCH_INDIRECT_BUFFER;
        case BufferTarget::TextureBuffer:    return GL_TEXTURE_BUFFER;
        default:                             return GL_NONE;
    }
}

namespace {

// Stable empty SharedPtr returned by GetBufferObject when a name is absent.
// Returning a const reference to it lets callers hold the result without
// copying the refcount and without risking a dangling reference.
const SharedPtr<BufferObject>& NullBuffer() {
    static const SharedPtr<BufferObject> null;
    return null;
}

} // namespace

BufferState::BufferState() = default;

int BufferState::TargetIndex(BufferTarget target) {
    int idx = static_cast<int>(target);
    if (idx < 0 || idx >= static_cast<int>(BufferTarget::BufferTargetCount)) {
        return 0;
    }
    return idx;
}

void BufferState::GenBufferNames(uint32_t n, std::vector<uint32_t>& out) {
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(m_nextName++);
    }
}

const SharedPtr<BufferObject>& BufferState::GetBufferObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it == m_objects.end()) {
        return NullBuffer();
    }
    return it->second;
}

const SharedPtr<BufferObject>& BufferState::CreateBufferObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it != m_objects.end()) {
        return it->second;
    }
    auto result = m_objects.emplace(index, std::make_shared<BufferObject>(index));
    return result.first->second;
}

void BufferState::MarkBufferForDeletion(uint32_t index) {
    // GL name-layer deletion only: drop the name from the object table. The
    // underlying Vulkan resource (VkBuffer + device memory) is released by the
    // backend disposal queue once in-flight GPU work referencing it completes,
    // so this component frees no backend handle here. A binding slot that
    // still holds a SharedPtr to this object keeps the BufferObject alive
    // until the slot is unbound.
    m_objects.erase(index);
}

bool BufferState::ValidateBufferName(uint32_t index) const {
    return m_objects.count(index) > 0;
}

bool BufferState::ValidateBufferObject(uint32_t index) const {
    auto it = m_objects.find(index);
    return it != m_objects.end() && it->second != nullptr;
}

BindingSlot<BufferObject>& BufferState::GetBufferBindingSlot(BufferTarget target) {
    return m_bindings[TargetIndex(target)];
}

void BufferState::BindBuffer(BufferTarget target, uint32_t index) {
    BindingSlot<BufferObject>& slot = m_bindings[TargetIndex(target)];
    if (index == 0) {
        slot.Unbind();
        ++m_version;
        return;
    }
    const SharedPtr<BufferObject>& obj = CreateBufferObject(index);
    obj->lastTarget = BufferTargetToGL(target);
    slot.Bind(obj);
    ++m_version;
}

const SharedPtr<BufferObject>& BufferState::GetBoundBuffer(BufferTarget target) const {
    return m_bindings[TargetIndex(target)].object;
}

uint16_t BufferState::GetVersion() const {
    return m_version;
}

} // namespace mithril::glstate
