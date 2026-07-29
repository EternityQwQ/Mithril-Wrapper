// Mithril-Wrapper - MG_State/GLState/TextureState/TextureEnum.h
//
// TextureTarget: strongly-typed texture-target enum for the modular OpenGL
// state machine. Ordering covers the GL 3.3 Core + ARB texture targets plus
// their PROXY_* counterparts (used by glTexImage* to probe format/size combos
// without allocating a real texture). `TextureTargetCount` is an array bound /
// iteration sentinel; `Unknown` is the result of translating an unrecognised
// GLenum (left for the MG_Impl layer to report as GL_INVALID_ENUM), matching
// the BufferTarget / RenderState enum convention.
//
// Shared API contract (mirrors BufferState / RenderState / VertexArrayState):
//   * namespace mithril::glstate, #pragma once, C++20.
#pragma once

#include <GL/gl.h>

namespace mithril::glstate {

enum class TextureTarget {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCubeMap,
    Texture1DArray,
    Texture2DArray,
    TextureCubeMapArray,
    TextureRectangle,
    TextureBuffer,
    Texture2DMultisample,
    Texture2DMultisampleArray,
    ProxyTexture1D,
    ProxyTexture2D,
    ProxyTexture3D,
    ProxyTextureCubeMap,
    ProxyTexture1DArray,
    ProxyTexture2DArray,
    ProxyTextureCubeMapArray,
    ProxyTextureRectangle,
    TextureTargetCount,
    Unknown = -1
};

// GL <-> TextureTarget translation. GLToTextureTarget returns
// TextureTarget::Unknown for an unrecognised GLenum; TextureTargetToGL returns
// GL_NONE for Unknown / TextureTargetCount, matching the
// RenderStateEnumConverter / BufferState convention.
TextureTarget GLToTextureTarget(GLenum v);
GLenum TextureTargetToGL(TextureTarget v);

} // namespace mithril::glstate
