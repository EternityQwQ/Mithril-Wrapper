// Mithril-Wrapper - MG_State/GLState/ProgramState/ProgramObject.h
//
// ProgramObject: the per-name program record migrated from the flat
// MG_State/State.h `struct Program`, together with its `Uniform` and `Attrib`
// reflection records (also migrated from State.h). It carries the GL-side
// metadata for a single program object name (id, attached shader names, link
// status, info log, the uniform/attribute/uniform-block reflection tables, the
// pre-link attribute binding overrides, and the per-stage SPIR-V produced by
// glLinkProgram) plus a `markedForDeletion` flag.
//
// The Vulkan-side VkPipeline / VkShaderModule handles are NOT owned here: the
// DirectVulkan backend allocates/destroys the real pipeline and releases it
// through its disposal queue once in-flight GPU work completes. ProgramObject
// is purely the GL name-layer state, mirroring the historical split in State.h
// where `Program` held only GL fields.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

namespace mithril::glstate {

// Migrated from State.h `struct Uniform`. Carries the reflected metadata for a
// single active uniform (name, GL type, array size, location, owning uniform
// block index + std140 layout offsets/strides, row-major flag) and a shadow
// cache of its current value (floats).
struct Uniform {
    std::string name;
    GLenum type = 0;
    GLint size = 0;
    GLint location = -1;
    GLint blockIndex = -1;
    GLint offset = -1;
    GLint arrayStride = 0;
    GLint matrixStride = 0;
    bool rowMajor = false;
    std::vector<float> value;
};

// Migrated from State.h `struct Attrib`. Carries the reflected metadata for a
// single active vertex attribute (name, GL type, array size, location).
struct Attrib {
    std::string name;
    GLenum type = 0;
    GLint size = 0;
    GLint location = -1;
};

// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the legacy `struct Program` usage pattern.
// Defaults mirror the OpenGL 3.3 Core values inherited from State.h.
struct ProgramObject {
    uint32_t id = 0;
    std::vector<uint32_t> attachedShaders;
    bool linked = false;
    std::string infoLog;
    std::unordered_map<std::string, Uniform> uniforms;
    std::unordered_map<GLint, std::string> uniformByLocation;
    std::unordered_map<std::string, Attrib> attribs;
    std::unordered_map<std::string, GLuint> uniformBlocks;
    std::unordered_map<std::string, GLuint> attribBindings;
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    bool markedForDeletion = false;

    ProgramObject();
    explicit ProgramObject(uint32_t id_);
};

} // namespace mithril::glstate
