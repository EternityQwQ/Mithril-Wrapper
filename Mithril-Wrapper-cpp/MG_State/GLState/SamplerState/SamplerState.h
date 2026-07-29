// Mithril-Wrapper - MG_State/GLState/SamplerState/SamplerState.h
//
// SamplerState: the sampler-object table + per-texture-unit binding owner of
// the modular OpenGL state machine. GL 3.3 Core sampler objects
// (glGenSamplers / glBindSampler / glSamplerParameter*) override the per-texture
// sample state for whatever texture is bound to a given texture unit. A unit
// with no sampler bound (null) falls back to the bound texture's own sample
// parameters, matching GL semantics.
//
// It owns the GL name -> SharedPtr<SamplerObject> table, the name allocator
// (m_nextName), a fixed-size array of per-unit bindings (m_unitBindings, sized
// by kMaxTextureUnits), and a version counter:
//
//   * m_version (uint16_t): bumped on every bind / parameter change /
//     deletion so a backend can cheaply tell whether cached sampler /
//     descriptor state needs rebuilding. Wraps at 16 bits (only
//     equality-vs-snapshot is meaningful).
//
// Shared API contract (mirrors TextureState / BufferState / VertexArrayState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Object table entry points use the unified names GenSamplerNames /
//     GetSamplerObject / CreateSamplerObject / MarkSamplerForDeletion /
//     ValidateSamplerName / ValidateSamplerObject.
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). GetSamplerObject / CreateSamplerObject return a const
//     reference to the stored SharedPtr (or to a static null SharedPtr when
//     absent), so callers can hold the reference without copying the refcount.
//
// Simplifications vs. the legacy flat GLState (see MG_State/State.h):
//   * The legacy state machine had no independent sampler objects at all —
//     sample state lived inline in `struct Texture`. Name 0 is treated as
//     "no sampler" here. GetSamplerObject(0) returns null and
//     ValidateSamplerName(0) returns false, matching glIsSampler(0) ==
//     GL_FALSE.
//   * Out-of-range texture-unit indices are clamped into [0, kMaxTextureUnits)
//     on access so a stray unit never reads out of bounds.
#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../Common.h"
#include "SamplerObject.h"

namespace mithril::glstate {

class SamplerState {
public:
    SamplerState();

    // Allocate `n` fresh sampler names from m_nextName. Names are reserved
    // only; no SamplerObject is created until BindSampler / CreateSamplerObject
    // is called. Appends to `out` (does not clear it, matching glGenSamplers).
    void GenSamplerNames(uint32_t n, std::vector<uint32_t>& out);

    // Look up a sampler object by name. Returns the stored SharedPtr, or a
    // static null SharedPtr if the name is not allocated. Name 0 returns null
    // (see "Simplifications" in the file header).
    const SharedPtr<SamplerObject>& GetSamplerObject(uint32_t index);

    // Look up a sampler object by name, creating it (with the given id) if the
    // name is allocated but has no object yet. If an object already exists for
    // `index`, it is returned as-is. Returns the stored SharedPtr.
    const SharedPtr<SamplerObject>& CreateSamplerObject(uint32_t index);

    // GL name-layer deletion. This component erases the name from the object
    // table and unbinds it from every texture unit; the underlying Vulkan
    // resource (VkSampler) is released asynchronously by the backend disposal
    // queue once in-flight GPU work referencing it completes, so no backend
    // handle is freed here. Bumps m_version. Name 0 is a no-op.
    void MarkSamplerForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in the object table).
    // Name 0 is never valid (treated as "no sampler").
    bool ValidateSamplerName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live SamplerObject.
    bool ValidateSamplerObject(uint32_t index) const;

    // Bind a sampler name to a texture unit's slot. index == 0 unbinds the
    // unit (so the bound texture's own sample parameters apply); otherwise the
    // object is created on demand and bound. `unit` is the GL_TEXTURE0..N
    // numeric index (0 == GL_TEXTURE0). Bumps m_version on every call.
    void BindSampler(uint32_t unit, uint32_t index);

    // Currently-bound sampler for a unit (may be a null SharedPtr). `unit` is
    // the GL_TEXTURE0..N numeric index (0 == GL_TEXTURE0).
    const SharedPtr<SamplerObject>& GetBoundSampler(uint32_t unit) const;

    // Notify that a sampler parameter (glSamplerParameter*) changed on the
    // named object. Bumps m_version so a cached backend sampler is rebuilt.
    // No-op if the name is not a live sampler.
    void NotifySamplerParamChanged(uint32_t index);

    uint16_t GetVersion() const;

private:
    // Clamp a unit index into [0, kMaxTextureUnits) so a stray index never
    // reads out of bounds (mirrors TextureState::UnitIndex /
    // BufferState::TargetIndex's defensive fallback).
    static uint32_t UnitIndex(uint32_t unit);

    std::unordered_map<uint32_t, SharedPtr<SamplerObject>> m_objects;
    uint32_t m_nextName = 1;
    uint16_t m_version = 0;
    // Per texture-unit sampler binding. A null entry means "use the bound
    // texture's own sample parameters" (glBindSampler(unit, 0)), matching GL
    // semantics where no sampler object is bound to the unit.
    std::array<SharedPtr<SamplerObject>, kMaxTextureUnits> m_unitBindings{};
};

} // namespace mithril::glstate
