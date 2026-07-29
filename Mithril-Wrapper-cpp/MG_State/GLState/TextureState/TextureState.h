// Mithril-Wrapper - MG_State/GLState/TextureState/TextureState.h
//
// TextureState: the texture-object table + per-unit binding owner of the
// modular OpenGL state machine. It owns the GL name -> SharedPtr<TextureObject>
// table, the name allocator (m_nextName), the active texture unit selector, a
// fixed-size array of per-unit bindings (m_units, sized by kMaxTextureUnits),
// the GL_PROXY_TEXTURE_2D query state, plus two version counters:
//
//   * m_version (uint16_t): bumped on every bind / active-unit change so a
//     backend can cheaply tell whether cached pipeline / descriptor state needs
//     rebuilding. Wraps at 16 bits (only equality-vs-snapshot is meaningful).
//   * m_textureBindGeneration (uint64_t): monotonically increasing, bumped on
//     every texture bind / unbind / delete change. Never wraps. A backend that
//     caches the per-draw sampled-texture set can compare this against a
//     snapshot to skip re-resolving the binding set when no bind changed (the
//     block atlas + lightmap stay bound across a whole terrain batch).
//
// Shared API contract (mirrors RenderState / BufferState / VertexArrayState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Object table entry points use the unified names GenTextureNames /
//     GetTextureObject / CreateTextureObject / MarkTextureForDeletion /
//     ValidateTextureName / ValidateTextureObject.
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). GetTextureObject / CreateTextureObject return a const
//     reference to the stored SharedPtr (or to a static null SharedPtr when
//     absent), so callers can hold the reference without copying the refcount.
//
// Simplifications vs. the legacy flat GLState (see MG_State/State.h):
//   * Name 0 is treated as "no texture" (the legacy per-target default texture
//     objects are NOT modelled here). GetTextureObject(0) returns null and
//     ValidateTextureName(0) returns false, matching glIsTexture(0) == GL_FALSE.
//   * Sampler-object binding is owned by the separate SamplerState component;
//     TextureUnit only tracks the bound texture + its target.
#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

#include "../Common.h"
#include "TextureEnum.h"
#include "TextureObject.h"
#include "TextureTypes.h"
#include "TextureUnit.h"

namespace mithril::glstate {

class TextureState {
public:
    TextureState();

    // Allocate `n` fresh texture names from m_nextName. Names are reserved
    // only; no TextureObject is created until BindTexture /
    // CreateTextureObject is called. Appends to `out` (does not clear it,
    // matching glGenTextures).
    void GenTextureNames(uint32_t n, std::vector<uint32_t>& out);

    // Look up a texture object by name. Returns the stored SharedPtr, or a
    // static null SharedPtr if the name is not allocated. Name 0 returns null
    // (see "Simplifications" in the file header).
    const SharedPtr<TextureObject>& GetTextureObject(uint32_t index);

    // Look up a texture object by name, creating it (with the given id and
    // target) if the name is allocated but has no object yet. If an object
    // already exists for `index`, it is returned as-is (a texture's target is
    // fixed at first bind, per GL semantics). Returns the stored SharedPtr.
    const SharedPtr<TextureObject>& CreateTextureObject(uint32_t index, TextureTarget target);

    // GL name-layer deletion. This component erases the name from the object
    // table and unbinds it from every texture unit; the underlying Vulkan
    // resource (VkImage + device memory) is released asynchronously by the
    // backend disposal queue once in-flight GPU work referencing it completes,
    // so no backend handle is freed here. Bumps m_textureBindGeneration so a
    // cached per-draw binding set is invalidated. Name 0 is a no-op.
    void MarkTextureForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in the object table).
    // Name 0 is never valid (treated as "no texture").
    bool ValidateTextureName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live TextureObject.
    bool ValidateTextureObject(uint32_t index) const;

    // Bind a texture name to the currently-active texture unit's slot.
    // index == 0 unbinds the unit; otherwise the object is created on demand
    // (with `target` set on first creation) and bound. Bumps m_version and
    // m_textureBindGeneration on every call.
    void BindTexture(TextureTarget target, uint32_t index);

    // Currently-bound texture for a unit (may be a null SharedPtr). `unit` is
    // the GL_TEXTURE0..N numeric index (0 == GL_TEXTURE0).
    const SharedPtr<TextureObject>& GetBoundTexture(uint32_t unit) const;

    // Target of the currently-bound texture for a unit.
    TextureTarget GetBoundTextureTarget(uint32_t unit) const;

    // Active texture unit (numeric index, 0 == GL_TEXTURE0).
    uint32_t GetActiveTextureUnit() const;
    void SetActiveTextureUnit(uint32_t unit);  // bumps m_version

    // Monotonic bind/unbind/delete generation (never wraps).
    uint64_t GetTextureBindGeneration() const;
    void BumpTextureBindGeneration();

    uint16_t GetVersion() const;

    // Proxy texture query state for GL_PROXY_TEXTURE_2D. glTexImage2D against
    // the proxy target writes here; glGetTexLevelParameteriv reads it.
    ProxyTextureState& GetProxyTexture2D();
    const ProxyTextureState& GetProxyTexture2D() const;

private:
    // Clamp a unit index into [0, kMaxTextureUnits) so a stray index never
    // reads out of bounds (mirrors BufferState::TargetIndex /
    // VertexArrayState::AttribIndex's defensive fallback).
    static uint32_t UnitIndex(uint32_t unit);

    std::unordered_map<uint32_t, SharedPtr<TextureObject>> m_objects;
    uint32_t m_nextName = 1;
    uint16_t m_version = 0;
    uint64_t m_textureBindGeneration = 0;
    uint32_t m_activeTextureUnit = 0;  // GL_TEXTURE0..N numeric index
    std::array<TextureUnit, kMaxTextureUnits> m_units{};
    ProxyTextureState m_proxyTexture2D;
};

} // namespace mithril::glstate
