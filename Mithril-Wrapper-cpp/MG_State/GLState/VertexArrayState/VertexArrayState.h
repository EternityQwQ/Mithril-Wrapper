// Mithril-Wrapper - MG_State/GLState/VertexArrayState/VertexArrayState.h
//
// VertexArrayState: the vertex-array-object table + current-binding owner of
// the modular OpenGL state machine. It owns the GL name ->
// SharedPtr<VertexArrayObject> table, the name allocator (m_nextName), the
// currently-bound VAO name (m_currentVAO, default 0) and a version counter
// bumped whenever the binding or any vertex-attribute state changes, so a
// backend can cheaply tell whether the cached vertex-input / pipeline state
// needs rebuilding.
//
// Shared API contract (mirrors RenderState / BufferState / ErrorState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Version counter: uint16_t m_version = 0; GetVersion(); ++m_version on
//     VAO bind or vertex-attribute change.
//   * Object table entry points use the unified names GenVertexArrayNames /
//     GetVertexArrayObject / CreateVertexArrayObject /
//     MarkVertexArrayForDeletion / ValidateVertexArrayName /
//     ValidateVertexArrayObject.
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). GetVertexArrayObject / CreateVertexArrayObject return a const
//     reference to the stored SharedPtr (or to a static null SharedPtr when
//     absent), so callers can hold the reference without copying the refcount.
//   * The default VAO (name 0) is pre-installed into the object table at
//     construction and never deleted, so binding name 0 always resolves to a
//     live object and GetCurrentVertexArray never returns null.
#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

#include "../Common.h"
#include "VertexArrayObject.h"

namespace mithril::glstate {

// Current (non-array) vertex attribute value, read by shaders for disabled
// attribute arrays. Mirrors the GL default of (0,0,0,1) for the float / int /
// uint channels. Stored on VertexArrayState (context-level) rather than per
// VAO, matching the Core-profile semantics where current attribute values are
// global context state set via glVertexAttrib*.
struct CurrentVertexAttributeValue {
    float floatValue[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int32_t intValue[4] = {0, 0, 0, 1};
    uint32_t uintValue[4] = {0, 0, 0, 1};
};

class VertexArrayState {
public:
    VertexArrayState();

    // Allocate `n` fresh VAO names from m_nextName. Names are reserved only;
    // no VertexArrayObject is created until BindVertexArray /
    // CreateVertexArrayObject is called. Appends to `out` (does not clear it,
    // matching glGenVertexArrays).
    void GenVertexArrayNames(uint32_t n, std::vector<uint32_t>& out);

    // Look up a VAO by name. Returns the stored SharedPtr, or a static null
    // SharedPtr if the name is not allocated. Name 0 returns the pre-installed
    // default VAO.
    const SharedPtr<VertexArrayObject>& GetVertexArrayObject(uint32_t index);

    // Look up a VAO by name, creating it (with the given id) if the name is
    // allocated but has no object yet. Returns the stored SharedPtr.
    const SharedPtr<VertexArrayObject>& CreateVertexArrayObject(uint32_t index);

    // Bind a VAO name as the current VAO. index == 0 binds the default VAO;
    // otherwise the object is created on demand and bound. Bumps m_version on
    // every call.
    //
    // Note: in GL, glBindVertexArray to a name not returned by
    // glGenVertexArrays is GL_INVALID_OPERATION. This component simplifies that
    // to auto-create; the real name-allocation validation is done by the
    // MG_Impl entry layer.
    void BindVertexArray(uint32_t index);

    // GL name-layer deletion. Erases the name from the object table (name 0,
    // the default VAO, is never erased). If the deleted VAO is currently bound,
    // the binding falls back to the default VAO (name 0). The underlying
    // Vulkan vertex-input state is released asynchronously by the backend
    // disposal queue once in-flight GPU work referencing it completes.
    void MarkVertexArrayForDeletion(uint32_t index);

    // True if `index` is an allocated name (or name 0, the always-valid
    // default VAO).
    bool ValidateVertexArrayName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live VertexArrayObject
    // (name 0 counts via the pre-installed default VAO).
    bool ValidateVertexArrayObject(uint32_t index) const;

    // Currently-bound VAO. Always non-null because the default VAO (name 0)
    // is pre-installed and the binding falls back to it.
    const SharedPtr<VertexArrayObject>& GetCurrentVertexArray() const;

    // Current (non-array) vertex attribute value accessors. The setters copy
    // the four components into the per-attribute slot and bump m_version only
    // when a value actually changes.
    void SetCurrentVertexAttributeFloat(uint32_t index, const float values[4]);
    void SetCurrentVertexAttributeInt(uint32_t index, const int32_t values[4]);
    void SetCurrentVertexAttributeUint(uint32_t index, const uint32_t values[4]);
    const CurrentVertexAttributeValue& GetCurrentVertexAttribute(uint32_t index) const;

    // Convenience hook for MG_Impl: call after mutating the current VAO's
    // attribs[] / elementArrayBuffer directly (via GetCurrentVertexArray()), so
    // the version counter reflects the change. Only bumps m_version (the
    // mutation itself is performed by the caller). The `index` parameter is
    // kept in the signature for API symmetry / future per-attribute tracking
    // and is intentionally unused here.
    void NotifyAttribChanged(uint32_t index);

    uint16_t GetVersion() const;

private:
    // Clamp an attribute index into [0, kMaxVertexAttribs) so a stray index
    // never reads out of bounds (mirrors BufferState::TargetIndex's defensive
    // fallback).
    static uint32_t AttribIndex(uint32_t index);

    std::unordered_map<uint32_t, SharedPtr<VertexArrayObject>> m_objects;
    uint32_t m_nextName = 1;
    uint16_t m_version = 0;
    uint32_t m_currentVAO = 0;   // default VAO name 0
    std::array<CurrentVertexAttributeValue, kMaxVertexAttribs> m_currentAttribs{};
};

} // namespace mithril::glstate
