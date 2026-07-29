// Mithril-Wrapper - MG_State/GLState/TextureState/TextureUnit.h
//
// TextureUnit: the per-texture-unit binding state. GL maintains up to
// kMaxTextureUnits (32) texture image units, each of which holds at most one
// bound texture object plus the target it was bound to (GL_TEXTURE_2D,
// GL_TEXTURE_CUBE_MAP, ...). This struct is the value type stored in
// TextureState's `std::array<TextureUnit, kMaxTextureUnits> m_units`.
//
// Sampler-object binding is intentionally NOT tracked here: it is owned by the
// separate SamplerState component (SamplerObject) to avoid a circular
// dependency between the texture and sampler domains. The historical
// `boundTextures[32]` / `boundTextureTargets[32]` arrays in the flat
// MG_State/State.h GLState are the lineage of this struct.
//
// Shared API contract (mirrors the other GLState components):
//   * namespace mithril::glstate, #pragma once, C++20.
#pragma once

#include "../Common.h"
#include "TextureEnum.h"
#include "TextureObject.h"

namespace mithril::glstate {

struct TextureUnit {
    SharedPtr<TextureObject> bound;
    TextureTarget boundTarget = TextureTarget::Texture2D;

    TextureUnit();
};

} // namespace mithril::glstate
