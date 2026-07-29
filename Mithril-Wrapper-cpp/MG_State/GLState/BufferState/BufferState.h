// Mithril-Wrapper - MG_State/GLState/BufferState/BufferState.h
//
// BufferState: the buffer-object table + binding-slot owner of the modular
// OpenGL state machine. It owns the GL name -> SharedPtr<BufferObject> table,
// the name allocator (m_nextName), a per-target binding slot and a version
// counter that is bumped whenever a binding changes, so a backend can cheaply
// tell whether the cached descriptor set / pipeline state needs rebuilding.
//
// Shared API contract (mirrors RenderState / ErrorState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Version counter: uint16_t m_version = 0; GetVersion(); ++m_version on
//     every binding change (bind / unbind).
//   * Object table entry points use the unified names GenBufferNames /
//     GetBufferObject / CreateBufferObject / MarkBufferForDeletion /
//     ValidateBufferName / ValidateBufferObject.
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). GetBufferObject / CreateBufferObject return a const reference
//     to the stored SharedPtr (or to a static null SharedPtr when absent), so
//     callers can hold the reference without copying the refcount.
//
// ElementArray binding note:
//   GL_ELEMENT_ARRAY_BUFFER is bound into the *currently-bound* VAO rather
//   than into a global slot, so its binding is owned by VertexArrayState, not
//   here. The ElementArray enumerator still occupies a position in m_bindings
//   so that target-enum-value == array-index stays trivial, but BufferState
//   never reads or writes that slot. Passing ElementArray to the binding
//   accessors is a caller bug; see TargetIndex() in the .cpp for the
//   defensive fallback.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

#include "../Common.h"
#include "BufferObject.h"

namespace mithril::glstate {

// Strongly-typed buffer target. Ordering matches glBindBuffer's GLenum domain
// (GL 3.3 Core + the ARB shader-storage / atomic-counter / indirect targets).
// `BufferTargetCount` is an array bound / iteration sentinel; `Unknown` is the
// result of translating an unrecognised GLenum (left for the MG_Impl layer to
// report as GL_INVALID_ENUM), matching the RenderState enum convention.
enum class BufferTarget {
    Array,              // GL_ARRAY_BUFFER
    ElementArray,       // GL_ELEMENT_ARRAY_BUFFER  (binding owned by VertexArrayState)
    Uniform,            // GL_UNIFORM_BUFFER
    PixelPack,          // GL_PIXEL_PACK_BUFFER
    PixelUnpack,        // GL_PIXEL_UNPACK_BUFFER
    TransformFeedback,  // GL_TRANSFORM_FEEDBACK_BUFFER
    CopyRead,           // GL_COPY_READ_BUFFER
    CopyWrite,          // GL_COPY_WRITE_BUFFER
    AtomicCounter,      // GL_ATOMIC_COUNTER_BUFFER
    ShaderStorage,      // GL_SHADER_STORAGE_BUFFER
    DrawIndirect,       // GL_DRAW_INDIRECT_BUFFER
    DispatchIndirect,   // GL_DISPATCH_INDIRECT_BUFFER
    TextureBuffer,      // GL_TEXTURE_BUFFER
    BufferTargetCount,
    Unknown = -1
};

// GL <-> BufferTarget translation. GLToBufferTarget returns BufferTarget::Unknown
// for an unrecognised GLenum; BufferTargetToGL returns GL_NONE for Unknown /
// BufferTargetCount, matching the RenderStateEnumConverter convention.
BufferTarget GLToBufferTarget(GLenum v);
GLenum BufferTargetToGL(BufferTarget v);

class BufferState {
public:
    BufferState();

    // Allocate `n` fresh buffer names from m_nextName. Names are reserved only;
    // no BufferObject is created until BindBuffer / CreateBufferObject is
    // called. Appends to `out` (does not clear it, matching glGenBuffers).
    void GenBufferNames(uint32_t n, std::vector<uint32_t>& out);

    // Look up a buffer object by name. Returns the stored SharedPtr, or a
    // static null SharedPtr if the name is not allocated.
    const SharedPtr<BufferObject>& GetBufferObject(uint32_t index);

    // Look up a buffer object by name, creating it (with the given id) if the
    // name is allocated but has no object yet. Returns the stored SharedPtr.
    const SharedPtr<BufferObject>& CreateBufferObject(uint32_t index);

    // GL name-layer deletion. This component erases the name from the object
    // table; the underlying Vulkan resource is released asynchronously by the
    // backend disposal queue once in-flight GPU work referencing it completes,
    // so no backend handle is freed here. Any binding slot still holding a
    // SharedPtr keeps the BufferObject alive until it is unbound.
    void MarkBufferForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in the object table).
    bool ValidateBufferName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live BufferObject.
    bool ValidateBufferObject(uint32_t index) const;

    // Per-target binding slot. The returned reference is stable for the
    // lifetime of this BufferState.
    BindingSlot<BufferObject>& GetBufferBindingSlot(BufferTarget target);

    // Bind / unbind a buffer name to a target's slot. index == 0 unbinds;
    // otherwise the object is created on demand, its lastTarget is updated to
    // this target, and it is bound to the slot. Bumps m_version on every call.
    void BindBuffer(BufferTarget target, uint32_t index);

    // Currently-bound buffer for a target (may be a null SharedPtr).
    const SharedPtr<BufferObject>& GetBoundBuffer(BufferTarget target) const;

    uint16_t GetVersion() const;

private:
    // Array index for a target. Collapses out-of-range / Unknown to 0 (Array)
    // so a stray enum never reads out of bounds. The ElementArray slot is
    // intentionally unused (see file header note).
    static int TargetIndex(BufferTarget target);

    std::unordered_map<uint32_t, SharedPtr<BufferObject>> m_objects;
    uint32_t m_nextName = 1;
    uint16_t m_version = 0;

    // One binding slot per buffer target, indexed by BufferTarget enum value.
    // The ElementArray slot is reserved (owned by VertexArrayState) and never
    // touched by BufferState; it exists only to keep the enum-value == index
    // invariant trivial.
    BindingSlot<BufferObject> m_bindings[static_cast<int>(BufferTarget::BufferTargetCount)];
};

} // namespace mithril::glstate
