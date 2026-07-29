// Mithril-Wrapper - MG_State/GLState/VertexArrayState/VertexArrayState.cpp
//
// Implementation of the VertexArrayState domain component. See
// VertexArrayState.h for the shared API contract (unified object-table names,
// SharedPtr ownership, single version bump per binding / attribute change).
#include "VertexArrayState.h"

#include <cstring>
#include <memory>

namespace mithril::glstate {

namespace {

// Stable empty SharedPtr returned by GetVertexArrayObject when a name is
// absent. Returning a const reference to it lets callers hold the result
// without copying the refcount and without risking a dangling reference.
const SharedPtr<VertexArrayObject>& NullVertexArray() {
    static const SharedPtr<VertexArrayObject> null;
    return null;
}

} // namespace

VertexArrayState::VertexArrayState() {
    // Pre-install the default VAO (name 0) so binding name 0 always resolves
    // to a live object and GetCurrentVertexArray never returns null.
    m_objects.emplace(0u, std::make_shared<VertexArrayObject>(0u));
}

uint32_t VertexArrayState::AttribIndex(uint32_t index) {
    return index < static_cast<uint32_t>(kMaxVertexAttribs) ? index : 0u;
}

void VertexArrayState::GenVertexArrayNames(uint32_t n, std::vector<uint32_t>& out) {
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(m_nextName++);
    }
}

const SharedPtr<VertexArrayObject>& VertexArrayState::GetVertexArrayObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it == m_objects.end()) {
        return NullVertexArray();
    }
    return it->second;
}

const SharedPtr<VertexArrayObject>& VertexArrayState::CreateVertexArrayObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it != m_objects.end()) {
        return it->second;
    }
    auto result = m_objects.emplace(index, std::make_shared<VertexArrayObject>(index));
    return result.first->second;
}

void VertexArrayState::BindVertexArray(uint32_t index) {
    // GL would raise GL_INVALID_OPERATION for a name not returned by
    // glGenVertexArrays; this component simplifies that to auto-create. The
    // real name-allocation validation is the MG_Impl entry layer's job.
    if (index == 0) {
        m_currentVAO = 0;
    } else {
        CreateVertexArrayObject(index);
        m_currentVAO = index;
    }
    ++m_version;
}

void VertexArrayState::MarkVertexArrayForDeletion(uint32_t index) {
    // Name 0 is the default VAO and must always remain in the table (GL
    // silently ignores a 0 passed to glDeleteVertexArrays).
    if (index == 0) {
        return;
    }
    m_objects.erase(index);
    if (m_currentVAO == index) {
        m_currentVAO = 0;
    }
}

bool VertexArrayState::ValidateVertexArrayName(uint32_t index) const {
    // Name 0 is the always-valid default VAO.
    if (index == 0) {
        return true;
    }
    return m_objects.count(index) > 0;
}

bool VertexArrayState::ValidateVertexArrayObject(uint32_t index) const {
    auto it = m_objects.find(index);
    return it != m_objects.end() && it->second != nullptr;
}

const SharedPtr<VertexArrayObject>& VertexArrayState::GetCurrentVertexArray() const {
    auto it = m_objects.find(m_currentVAO);
    if (it == m_objects.end()) {
        return NullVertexArray();
    }
    return it->second;
}

void VertexArrayState::SetCurrentVertexAttributeFloat(uint32_t index, const float values[4]) {
    CurrentVertexAttributeValue& v = m_currentAttribs[AttribIndex(index)];
    if (std::memcmp(v.floatValue, values, sizeof(v.floatValue)) != 0) {
        std::memcpy(v.floatValue, values, sizeof(v.floatValue));
        ++m_version;
    }
}

void VertexArrayState::SetCurrentVertexAttributeInt(uint32_t index, const int32_t values[4]) {
    CurrentVertexAttributeValue& v = m_currentAttribs[AttribIndex(index)];
    if (std::memcmp(v.intValue, values, sizeof(v.intValue)) != 0) {
        std::memcpy(v.intValue, values, sizeof(v.intValue));
        ++m_version;
    }
}

void VertexArrayState::SetCurrentVertexAttributeUint(uint32_t index, const uint32_t values[4]) {
    CurrentVertexAttributeValue& v = m_currentAttribs[AttribIndex(index)];
    if (std::memcmp(v.uintValue, values, sizeof(v.uintValue)) != 0) {
        std::memcpy(v.uintValue, values, sizeof(v.uintValue));
        ++m_version;
    }
}

const CurrentVertexAttributeValue& VertexArrayState::GetCurrentVertexAttribute(uint32_t index) const {
    return m_currentAttribs[AttribIndex(index)];
}

void VertexArrayState::NotifyAttribChanged(uint32_t index) {
    (void)index;
    ++m_version;
}

uint16_t VertexArrayState::GetVersion() const {
    return m_version;
}

} // namespace mithril::glstate
