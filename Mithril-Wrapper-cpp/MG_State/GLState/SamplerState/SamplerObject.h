// Mithril-Wrapper - MG_State/GLState/SamplerState/SamplerObject.h
//
// SamplerObject: the per-name sampler record for the modular OpenGL state
// machine. GL 3.3 Core introduced sampler objects (glGenSamplers /
// glBindSampler / glSamplerParameter*) that override the per-texture sample
// state for whatever texture is bound to a given texture unit. A sampler
// object carries its own filter / wrap / border-colour / LOD / compare
// parameters, decoupled from any texture.
//
// The filter/wrap/compare fields are stored directly as GLenum-valued GLint
// (or GLenum) values — the raw GL_* constants — rather than wrapped in our own
// strongly-typed enums, matching the legacy `struct Texture` sample-state
// fields in MG_State/State.h and the sibling TextureObject record; the
// abstraction layer may introduce typed enums later.
//
// The Vulkan-side VkSampler handle is NOT owned here: the DirectVulkan backend
// allocates/destroys the real sampler and releases it through its disposal
// queue once in-flight GPU work completes (see MarkSamplerForDeletion).
// SamplerObject is purely the GL name-layer state, mirroring the historical
// split in State.h where `Texture` held only GL fields.
#pragma once

#include <cstdint>

#include <GL/gl.h>

namespace mithril::glstate {

// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the TextureObject usage pattern. Defaults
// mirror the OpenGL 3.3 Core sampler-object defaults inherited from the
// legacy `struct Texture` sample-state fields (minFilter / magFilter / wrap* /
// borderColor) plus the sampler-specific LOD / compare defaults.
struct SamplerObject {
    uint32_t id = 0;
    GLint minFilter = GL_NEAREST_MIPMAP_LINEAR;
    GLint magFilter = GL_LINEAR;
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    GLint wrapR = GL_REPEAT;
    GLfloat borderColor[4] = {0, 0, 0, 0};
    GLfloat minLod = -1000.0f;
    GLfloat maxLod = 1000.0f;
    GLfloat lodBias = 0.0f;
    GLenum compareMode = GL_NONE;
    GLenum compareFunc = GL_LEQUAL;
    bool markedForDeletion = false;

    SamplerObject();
    explicit SamplerObject(uint32_t id_);
};

} // namespace mithril::glstate
