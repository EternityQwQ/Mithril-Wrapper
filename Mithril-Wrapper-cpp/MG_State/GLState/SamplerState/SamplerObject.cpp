// Mithril-Wrapper - MG_State/GLState/SamplerState/SamplerObject.cpp
//
// Implementation of the SamplerObject constructors. All other state is left to
// default member initializers (declared in SamplerObject.h), which carry the
// OpenGL 3.3 Core sampler-object defaults (minFilter = GL_NEAREST_MIPMAP_LINEAR,
// magFilter = GL_LINEAR, wrap* = GL_REPEAT, zero border colour, minLod = -1000,
// maxLod = 1000, lodBias = 0, compareMode = GL_NONE, compareFunc = GL_LEQUAL).
#include "SamplerObject.h"

namespace mithril::glstate {

SamplerObject::SamplerObject() = default;

SamplerObject::SamplerObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
