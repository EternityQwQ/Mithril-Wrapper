// Mithril-Wrapper - MG_State/GLState/TextureState/TextureTypes.h
//
// Small GL/Vulkan-agnostic helper types for the texture-state component. Kept
// free of <GL/gl.h> so it can be included from headers that wish to stay
// GL-agnostic (matching the Common.h discipline).
//
// Shared API contract (mirrors the other GLState components):
//   * namespace mithril::glstate, #pragma once, C++20.
#pragma once

#include <cstdint>

namespace mithril::glstate {

// Proxy texture query state. glTexImage2D(GL_PROXY_TEXTURE_2D, ...) does not
// create a real texture; it just validates the format/size combo. The result
// is queried via glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, ...). If the
// combo is unsupported, width/height == 0. Minecraft uses this to probe the
// max texture size. Migrated from the flat MG_State/State.h
// `GLState::ProxyTextureState` (fields widened to plain int so this header
// stays GL-agnostic; the GL entry layer casts as needed).
struct ProxyTextureState {
    int width = 0;
    int height = 0;
    int depth = 0;
    int internalFormat = 0;
    bool valid = false;
};

} // namespace mithril::glstate
