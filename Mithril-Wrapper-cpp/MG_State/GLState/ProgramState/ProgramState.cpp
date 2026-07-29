// Mithril-Wrapper - MG_State/GLState/ProgramState/ProgramState.cpp
//
// Implementation of the ProgramState domain component. See ProgramState.h for
// the shared API contract (unified object-table names, SharedPtr ownership,
// independent program/shader name spaces, version bump on useProgram and
// deletion changes).
#include "ProgramState.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace mithril::glstate {

namespace {

// Stable empty SharedPtrs returned by the getters when a name is absent.
// Returning a const reference to them lets callers hold the result without
// copying the refcount and without risking a dangling reference.
const SharedPtr<ProgramObject>& NullProgram() {
    static const SharedPtr<ProgramObject> null;
    return null;
}

const SharedPtr<ShaderObject>& NullShader() {
    static const SharedPtr<ShaderObject> null;
    return null;
}

} // namespace

ProgramState::ProgramState() = default;

// ---- Program ----

uint32_t ProgramState::CreateProgram() {
    uint32_t id = m_nextProgramName++;
    m_programs.emplace(id, std::make_shared<ProgramObject>(id));
    return id;
}

const SharedPtr<ProgramObject>& ProgramState::GetProgramObject(uint32_t index) {
    auto it = m_programs.find(index);
    if (it == m_programs.end()) {
        return NullProgram();
    }
    return it->second;
}

void ProgramState::MarkProgramForDeletion(uint32_t index) {
    auto it = m_programs.find(index);
    if (it == m_programs.end() || it->second == nullptr) {
        return;
    }
    // Flag first so any SharedPtr holder (e.g. a backend cache) can see the
    // delete status before the table entry is dropped.
    it->second->markedForDeletion = true;
    // Snapshot the attached shader names: deleting a program is a detach
    // point for shaders that were flagged with glDeleteShader while still
    // attached, so each one gets a chance to release its name now.
    std::vector<uint32_t> attachedShaders = it->second->attachedShaders;
    m_programs.erase(it);
    for (uint32_t shader : attachedShaders) {
        ReleaseShaderNameIfOrphaned(shader);
    }
    ++m_version;
}

bool ProgramState::ValidateProgramName(uint32_t index) const {
    return m_programs.count(index) > 0;
}

bool ProgramState::ValidateProgramObject(uint32_t index) const {
    auto it = m_programs.find(index);
    return it != m_programs.end() && it->second != nullptr;
}

// ---- Shader ----

uint32_t ProgramState::CreateShader(GLenum stage) {
    uint32_t id = m_nextShaderName++;
    m_shaders.emplace(id, std::make_shared<ShaderObject>(id, stage));
    return id;
}

const SharedPtr<ShaderObject>& ProgramState::GetShaderObject(uint32_t index) {
    auto it = m_shaders.find(index);
    if (it == m_shaders.end()) {
        return NullShader();
    }
    return it->second;
}

void ProgramState::MarkShaderForDeletion(uint32_t index) {
    auto it = m_shaders.find(index);
    if (it == m_shaders.end() || it->second == nullptr) {
        return;
    }
    // glDeleteShader on an attached shader only FLAGS it; the name stays valid
    // (and glShaderSource/glCompileShader keep working) until the shader is
    // detached from every program. Try to release immediately for the
    // unattached case.
    it->second->markedForDeletion = true;
    ReleaseShaderNameIfOrphaned(index);
    ++m_version;
}

bool ProgramState::ValidateShaderName(uint32_t index) const {
    return m_shaders.count(index) > 0;
}

bool ProgramState::ValidateShaderObject(uint32_t index) const {
    auto it = m_shaders.find(index);
    return it != m_shaders.end() && it->second != nullptr;
}

void ProgramState::DetachShader(uint32_t program, uint32_t shader) {
    auto it = m_programs.find(program);
    if (it == m_programs.end() || it->second == nullptr) {
        return;
    }
    auto& attached = it->second->attachedShaders;
    attached.erase(std::remove(attached.begin(), attached.end(), shader),
                   attached.end());
}

void ProgramState::ReleaseShaderNameIfOrphaned(uint32_t index) {
    auto it = m_shaders.find(index);
    if (it == m_shaders.end() || it->second == nullptr) {
        return;
    }
    if (!it->second->markedForDeletion) {
        return;
    }
    // A shader flagged for deletion is released only once no program holds a
    // GL-visible attachment to it.
    if (ShaderHasProgramReference(index)) {
        return;
    }
    m_shaders.erase(it);
    ++m_version;
}

bool ProgramState::ShaderHasProgramReference(uint32_t shader) const {
    for (auto it = m_programs.begin(); it != m_programs.end(); ++it) {
        const SharedPtr<ProgramObject>& program = it->second;
        if (program == nullptr) {
            continue;
        }
        const auto& attached = program->attachedShaders;
        if (std::find(attached.begin(), attached.end(), shader) != attached.end()) {
            return true;
        }
    }
    return false;
}

// ---- Current program ----

void ProgramState::UseProgram(uint32_t index) {
    // index == 0 unbinds; otherwise the name is recorded as current. The
    // command layer is responsible for validating the name before calling.
    m_currentProgram = index;
    ++m_version;
}

const SharedPtr<ProgramObject>& ProgramState::GetCurrentProgram() const {
    if (m_currentProgram == 0) {
        return NullProgram();
    }
    auto it = m_programs.find(m_currentProgram);
    if (it == m_programs.end()) {
        return NullProgram();
    }
    return it->second;
}

uint16_t ProgramState::GetVersion() const {
    return m_version;
}

} // namespace mithril::glstate
