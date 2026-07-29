// Mithril-Wrapper - MG_State/GLState/TextureState/TextureObject.cpp
//
// Implementation of the TextureObject constructors. All other state is left to
// default member initializers (declared in TextureObject.h), which carry the
// OpenGL 3.3 Core defaults inherited from the legacy `struct Texture` in
// MG_State/State.h (target = GL_TEXTURE_2D, internalFormat = GL_RGBA8,
// width/height = 0, depth = 1, levels = 1, minFilter = GL_NEAREST_MIPMAP_LINEAR,
// magFilter = GL_LINEAR, wrap* = GL_REPEAT, zero border colour).
#include "TextureObject.h"

namespace mithril::glstate {

TextureObject::TextureObject() = default;

TextureObject::TextureObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
