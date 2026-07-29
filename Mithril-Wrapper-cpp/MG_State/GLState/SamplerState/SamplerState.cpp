// Mithril-Wrapper - MG_State/GLState/SamplerState/SamplerState.cpp
//
// Implementation of the SamplerState domain component. See SamplerState.h for
// the shared API contract (unified object-table names, SharedPtr ownership,
// m_version version counter).
#include "SamplerState.h"

#include <memory>
#include <utility>

namespace mithril::glstate {

namespace {

// Stable empty SharedPtr returned by GetSamplerObject / GetBoundSampler when a
// name / unit has no bound object. Returning a const reference to it lets
// callers hold the result without copying the refcount and without risking a
// dangling reference (mirrors TextureState::NullTexture /
// BufferState::NullBuffer).
const SharedPtr<SamplerObject>& NullSampler() {
    static const SharedPtr<SamplerObject> null;
    return null;
}

} // namespace

SamplerState::SamplerState() = default;

uint32_t SamplerState::UnitIndex(uint32_t unit) {
    return unit < static_cast<uint32_t>(kMaxTextureUnits) ? unit : 0u;
}

void SamplerState::GenSamplerNames(uint32_t n, std::vector<uint32_t>& out) {
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(m_nextName++);
    }
}

const SharedPtr<SamplerObject>& SamplerState::GetSamplerObject(uint32_t index) {
    // Name 0 is "no sampler" (see SamplerState.h simplifications).
    if (index == 0) {
        return NullSampler();
    }
    auto it = m_objects.find(index);
    if (it == m_objects.end()) {
        return NullSampler();
    }
    return it->second;
}

const SharedPtr<SamplerObject>& SamplerState::CreateSamplerObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it != m_objects.end()) {
        // Object already exists: return the existing record untouched.
        return it->second;
    }
    auto obj = std::make_shared<SamplerObject>(index);
    auto result = m_objects.emplace(index, std::move(obj));
    return result.first->second;
}

void SamplerState::MarkSamplerForDeletion(uint32_t index) {
    // Name 0 is "no sampler" and never lives in the table; GL silently ignores
    // a 0 passed to glDeleteSamplers.
    if (index == 0) {
        return;
    }
    // GL name-layer deletion only: drop the name from the object table. The
    // underlying Vulkan resource (VkSampler) is released by the backend
    // disposal queue once in-flight GPU work referencing it completes, so this
    // component frees no backend handle here. A texture unit that still holds
    // a SharedPtr to this object keeps the SamplerObject alive until the unit
    // is unbound (which we do immediately below).
    m_objects.erase(index);
    // Unbind this sampler from every unit, matching GL's semantics where a
    // deleted sampler is detached from any unit it was bound to.
    for (SharedPtr<SamplerObject>& binding : m_unitBindings) {
        if (binding && binding->id == index) {
            binding.reset();
        }
    }
    ++m_version;
}

bool SamplerState::ValidateSamplerName(uint32_t index) const {
    if (index == 0) {
        return false;
    }
    return m_objects.count(index) > 0;
}

bool SamplerState::ValidateSamplerObject(uint32_t index) const {
    if (index == 0) {
        return false;
    }
    auto it = m_objects.find(index);
    return it != m_objects.end() && it->second != nullptr;
}

void SamplerState::BindSampler(uint32_t unit, uint32_t index) {
    SharedPtr<SamplerObject>& binding = m_unitBindings[UnitIndex(unit)];
    if (index == 0) {
        // Unbind: GL's glBindSampler(unit, 0) detaches whatever sampler was
        // bound, so the bound texture's own sample parameters apply.
        binding.reset();
    } else {
        const SharedPtr<SamplerObject>& obj = CreateSamplerObject(index);
        binding = obj;
    }
    ++m_version;
}

const SharedPtr<SamplerObject>& SamplerState::GetBoundSampler(uint32_t unit) const {
    return m_unitBindings[UnitIndex(unit)];
}

void SamplerState::NotifySamplerParamChanged(uint32_t index) {
    // Only bump the version if the name still resolves to a live sampler; a
    // parameter change on a freed / non-existent name is a no-op.
    if (index == 0) {
        return;
    }
    auto it = m_objects.find(index);
    if (it == m_objects.end() || it->second == nullptr) {
        return;
    }
    ++m_version;
}

uint16_t SamplerState::GetVersion() const {
    return m_version;
}

} // namespace mithril::glstate
