// Mithril-Wrapper - MG_State/GLState/ProgramState/ProgramState.h
//
// ProgramState: the program/shader object-table owner of the modular OpenGL
// state machine. It owns the GL name -> SharedPtr<ProgramObject> table and
// the GL name -> SharedPtr<ShaderObject> table, the two independent name
// allocators (m_nextProgramName / m_nextShaderName -- program and shader
// namespaces are independent in GL, unlike the flat State.h which shared a
// single nextName), the current-program binding (m_currentProgram) and a
// version counter that is bumped on useProgram / deletion changes so a backend
// can cheaply tell whether the cached pipeline state needs rebuilding.
//
// Shared API contract (mirrors RenderState / BufferState / TextureState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Version counter: uint16_t m_version = 0; GetVersion(); ++m_version on
//     useProgram and on every deletion change (MarkProgramForDeletion /
//     MarkShaderForDeletion / a name actually released by
//     ReleaseShaderNameIfOrphaned). CreateProgram/CreateShader/DetachShader
//     and the getters/validators do not bump.
//   * Object-table entry points use the unified names CreateProgram /
//     GetProgramObject / MarkProgramForDeletion / ValidateProgramName /
//     ValidateProgramObject (and the Shader equivalents).
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). GetProgramObject / GetShaderObject / GetCurrentProgram return
//     a const reference to the stored SharedPtr (or to a static null SharedPtr
//     when absent), so callers can hold the reference without copying the
//     refcount.
//
// Deferred shader deletion (glDeleteShader-while-attached):
//   glDeleteShader only flags a shader for deletion; its name stays valid (and
//   glShaderSource/glCompileShader keep working) until the shader is detached
//   from every program. MarkShaderForDeletion sets the flag and then tries to
//   release the name immediately via ReleaseShaderNameIfOrphaned; if the shader
//   is still attached, the release is deferred until the owning program is
//   deleted (MarkProgramForDeletion releases the attached shader names) or
//   glDetachShader is followed by a ReleaseShaderNameIfOrphaned call.
#pragma once

#include <cstdint>
#include <unordered_map>

#include <GL/gl.h>

#include "../Common.h"
#include "ProgramObject.h"
#include "ShaderObject.h"

namespace mithril::glstate {

class ProgramState {
public:
    ProgramState();

    // ---- Program ----
    // Allocate a fresh program name from m_nextProgramName, create the
    // ProgramObject, store it and return its id. Does not bump m_version
    // (creating an object is not a current-state change).
    uint32_t CreateProgram();

    // Look up a program object by name. Returns the stored SharedPtr, or a
    // static null SharedPtr if the name is not allocated.
    const SharedPtr<ProgramObject>& GetProgramObject(uint32_t index);

    // GL name-layer deletion: flags the program (markedForDeletion = true),
    // erases its name from m_programs, and tries to release every attached
    // shader name (deleting a program detaches its shaders). Bumps m_version.
    // The underlying Vulkan pipeline is released asynchronously by the backend
    // disposal queue; a holder of the SharedPtr keeps the ProgramObject alive.
    void MarkProgramForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in m_programs).
    bool ValidateProgramName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live ProgramObject.
    bool ValidateProgramObject(uint32_t index) const;

    // ---- Shader ----
    // Allocate a fresh shader name from m_nextShaderName (independent from the
    // program namespace), create the ShaderObject with the given stage, store
    // it and return its id. `stage` is a GL shader type enum
    // (GL_VERTEX_SHADER / GL_FRAGMENT_SHADER / GL_GEOMETRY_SHADER /
    // GL_TESS_CONTROL_SHADER / GL_TESS_EVALUATION_SHADER / GL_COMPUTE_SHADER).
    uint32_t CreateShader(GLenum stage);

    // Look up a shader object by name. Returns the stored SharedPtr, or a
    // static null SharedPtr if the name is not allocated.
    const SharedPtr<ShaderObject>& GetShaderObject(uint32_t index);

    // GL name-layer deletion: flags the shader (markedForDeletion = true) and
    // tries to release the name immediately via ReleaseShaderNameIfOrphaned.
    // If the shader is still attached to a program the release is deferred.
    // Bumps m_version.
    void MarkShaderForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in m_shaders).
    bool ValidateShaderName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live ShaderObject.
    bool ValidateShaderObject(uint32_t index) const;

    // Remove `shader` from ProgramObject::attachedShaders of `program`. Does
    // not release the shader name (the command layer calls
    // ReleaseShaderNameIfOrphaned afterwards). No-op if the program/shader is
    // unknown or the shader is not attached.
    void DetachShader(uint32_t program, uint32_t shader);

    // If the shader is flagged for deletion AND no program holds a
    // GL-visible attachment to it (scanning m_programs), erase it from
    // m_shaders. Bumps m_version only when it actually erases the name.
    void ReleaseShaderNameIfOrphaned(uint32_t index);

    // ---- Current program ----
    // Bind a program as current. index == 0 unbinds. Always bumps m_version.
    void UseProgram(uint32_t index);

    // Currently-bound program (may be a null SharedPtr when nothing is bound
    // or the bound name is no longer in the table).
    const SharedPtr<ProgramObject>& GetCurrentProgram() const;

    uint16_t GetVersion() const;

private:
    // True if any live ProgramObject in m_programs has `shader` in its
    // attachedShaders list. Used by ReleaseShaderNameIfOrphaned to keep a
    // flagged shader's name alive while it is still attached.
    bool ShaderHasProgramReference(uint32_t shader) const;

    std::unordered_map<uint32_t, SharedPtr<ProgramObject>> m_programs;
    std::unordered_map<uint32_t, SharedPtr<ShaderObject>> m_shaders;
    uint32_t m_nextProgramName = 1;
    uint32_t m_nextShaderName = 1;
    uint16_t m_version = 0;
    uint32_t m_currentProgram = 0;
};

} // namespace mithril::glstate
