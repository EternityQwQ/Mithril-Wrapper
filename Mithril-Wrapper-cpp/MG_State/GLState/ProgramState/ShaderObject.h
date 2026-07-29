// Mithril-Wrapper - MG_State/GLState/ProgramState/ShaderObject.h
//
// ShaderObject: the per-name shader record migrated from the flat
// MG_State/State.h `struct Shader`. It carries the GL-side metadata for a
// single shader object name (id, stage/type, GLSL source, compile status,
// info log, the GLSL->SPIR-V translation result) plus a `markedForDeletion`
// flag used by the deferred glDeleteShader-while-attached lifecycle.
//
// The Vulkan-side VkShaderModule handle is NOT owned here: the DirectVulkan
// backend allocates/destroys the real shader module and releases it through
// its disposal queue once in-flight GPU work completes. ShaderObject is purely
// the GL name-layer state, mirroring the historical split in State.h where
// `Shader` held only GL fields.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <GL/gl.h>

namespace mithril::glstate {

// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the legacy `struct Shader` usage pattern.
// Defaults mirror the OpenGL 3.3 Core values inherited from State.h.
struct ShaderObject {
    uint32_t id = 0;
    GLenum type = GL_VERTEX_SHADER;
    std::string source;
    bool compiled = false;
    std::string infoLog;
    std::vector<uint32_t> spirv;
    bool markedForDeletion = false;

    ShaderObject();
    explicit ShaderObject(uint32_t id_);
    ShaderObject(uint32_t id_, GLenum type_);
};

} // namespace mithril::glstate
