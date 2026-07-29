// Mithril-Wrapper - MG_State/GLState/TextureState/TextureUnit.cpp
//
// Implementation of the TextureUnit default constructor. The bound SharedPtr
// value-initialises to null (no texture bound) and boundTarget defaults to
// Texture2D, mirroring the GL_TEXTURE_2D default in the legacy
// `boundTextureTargets[32]` array of MG_State/State.h.
#include "TextureUnit.h"

namespace mithril::glstate {

TextureUnit::TextureUnit() = default;

} // namespace mithril::glstate
