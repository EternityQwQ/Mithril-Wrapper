// Mithril-Wrapper - MG_State/GLState/TextureState/TextureState.cpp
//
// Implementation of the TextureState domain component. See TextureState.h for
// the shared API contract (unified object-table names, SharedPtr ownership,
// m_version + m_textureBindGeneration version counters).
//
// The GL <-> TextureTarget converter free functions declared in TextureEnum.h
// are implemented here (co-located with the state class that consumes them),
// matching the convention used by BufferState.cpp for GLToBufferTarget /
// BufferTargetToGL.
#include "TextureState.h"

#include <memory>
#include <utility>

namespace mithril::glstate {

// GL constants missing from this repository's minimal glcorearb.h. Defined
// under #ifndef guards so this translation unit is self-contained without
// modifying the shared GL headers — the same workaround already used by
// RenderStateEnumConverter.cpp, BufferState.cpp and include/GL/gl.h.
// (GL_PROXY_TEXTURE_2D is already provided by include/GL/gl.h.)
#ifndef GL_TEXTURE_CUBE_MAP_ARRAY
#define GL_TEXTURE_CUBE_MAP_ARRAY         0x9009
#endif
#ifndef GL_PROXY_TEXTURE_1D
#define GL_PROXY_TEXTURE_1D               0x8063
#endif
#ifndef GL_PROXY_TEXTURE_3D
#define GL_PROXY_TEXTURE_3D               0x8070
#endif
#ifndef GL_PROXY_TEXTURE_CUBE_MAP
#define GL_PROXY_TEXTURE_CUBE_MAP         0x851B
#endif
#ifndef GL_PROXY_TEXTURE_1D_ARRAY
#define GL_PROXY_TEXTURE_1D_ARRAY         0x8C19
#endif
#ifndef GL_PROXY_TEXTURE_2D_ARRAY
#define GL_PROXY_TEXTURE_2D_ARRAY         0x8C1B
#endif
#ifndef GL_PROXY_TEXTURE_CUBE_MAP_ARRAY
#define GL_PROXY_TEXTURE_CUBE_MAP_ARRAY   0x900B
#endif
#ifndef GL_PROXY_TEXTURE_RECTANGLE
#define GL_PROXY_TEXTURE_RECTANGLE        0x84F7
#endif

// ---- GL <-> TextureTarget translation ----
TextureTarget GLToTextureTarget(GLenum v) {
    switch (v) {
        case GL_TEXTURE_1D:                   return TextureTarget::Texture1D;
        case GL_TEXTURE_2D:                   return TextureTarget::Texture2D;
        case GL_TEXTURE_3D:                   return TextureTarget::Texture3D;
        case GL_TEXTURE_CUBE_MAP:             return TextureTarget::TextureCubeMap;
        case GL_TEXTURE_1D_ARRAY:             return TextureTarget::Texture1DArray;
        case GL_TEXTURE_2D_ARRAY:             return TextureTarget::Texture2DArray;
        case GL_TEXTURE_CUBE_MAP_ARRAY:       return TextureTarget::TextureCubeMapArray;
        case GL_TEXTURE_RECTANGLE:            return TextureTarget::TextureRectangle;
        case GL_TEXTURE_BUFFER:               return TextureTarget::TextureBuffer;
        case GL_TEXTURE_2D_MULTISAMPLE:       return TextureTarget::Texture2DMultisample;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return TextureTarget::Texture2DMultisampleArray;
        case GL_PROXY_TEXTURE_1D:             return TextureTarget::ProxyTexture1D;
        case GL_PROXY_TEXTURE_2D:             return TextureTarget::ProxyTexture2D;
        case GL_PROXY_TEXTURE_3D:             return TextureTarget::ProxyTexture3D;
        case GL_PROXY_TEXTURE_CUBE_MAP:       return TextureTarget::ProxyTextureCubeMap;
        case GL_PROXY_TEXTURE_1D_ARRAY:       return TextureTarget::ProxyTexture1DArray;
        case GL_PROXY_TEXTURE_2D_ARRAY:       return TextureTarget::ProxyTexture2DArray;
        case GL_PROXY_TEXTURE_CUBE_MAP_ARRAY: return TextureTarget::ProxyTextureCubeMapArray;
        case GL_PROXY_TEXTURE_RECTANGLE:      return TextureTarget::ProxyTextureRectangle;
        default:                              return TextureTarget::Unknown;
    }
}

GLenum TextureTargetToGL(TextureTarget v) {
    switch (v) {
        case TextureTarget::Texture1D:                   return GL_TEXTURE_1D;
        case TextureTarget::Texture2D:                   return GL_TEXTURE_2D;
        case TextureTarget::Texture3D:                   return GL_TEXTURE_3D;
        case TextureTarget::TextureCubeMap:              return GL_TEXTURE_CUBE_MAP;
        case TextureTarget::Texture1DArray:              return GL_TEXTURE_1D_ARRAY;
        case TextureTarget::Texture2DArray:              return GL_TEXTURE_2D_ARRAY;
        case TextureTarget::TextureCubeMapArray:         return GL_TEXTURE_CUBE_MAP_ARRAY;
        case TextureTarget::TextureRectangle:            return GL_TEXTURE_RECTANGLE;
        case TextureTarget::TextureBuffer:               return GL_TEXTURE_BUFFER;
        case TextureTarget::Texture2DMultisample:        return GL_TEXTURE_2D_MULTISAMPLE;
        case TextureTarget::Texture2DMultisampleArray:   return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        case TextureTarget::ProxyTexture1D:              return GL_PROXY_TEXTURE_1D;
        case TextureTarget::ProxyTexture2D:              return GL_PROXY_TEXTURE_2D;
        case TextureTarget::ProxyTexture3D:              return GL_PROXY_TEXTURE_3D;
        case TextureTarget::ProxyTextureCubeMap:         return GL_PROXY_TEXTURE_CUBE_MAP;
        case TextureTarget::ProxyTexture1DArray:         return GL_PROXY_TEXTURE_1D_ARRAY;
        case TextureTarget::ProxyTexture2DArray:         return GL_PROXY_TEXTURE_2D_ARRAY;
        case TextureTarget::ProxyTextureCubeMapArray:    return GL_PROXY_TEXTURE_CUBE_MAP_ARRAY;
        case TextureTarget::ProxyTextureRectangle:       return GL_PROXY_TEXTURE_RECTANGLE;
        default:                                         return GL_NONE;
    }
}

namespace {

// Stable empty SharedPtr returned by GetTextureObject / GetBoundTexture when a
// name / unit has no bound object. Returning a const reference to it lets
// callers hold the result without copying the refcount and without risking a
// dangling reference (mirrors BufferState::NullBuffer /
// VertexArrayState::NullVertexArray).
const SharedPtr<TextureObject>& NullTexture() {
    static const SharedPtr<TextureObject> null;
    return null;
}

} // namespace

TextureState::TextureState() = default;

uint32_t TextureState::UnitIndex(uint32_t unit) {
    return unit < static_cast<uint32_t>(kMaxTextureUnits) ? unit : 0u;
}

void TextureState::GenTextureNames(uint32_t n, std::vector<uint32_t>& out) {
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(m_nextName++);
    }
}

const SharedPtr<TextureObject>& TextureState::GetTextureObject(uint32_t index) {
    // Name 0 is "no texture" (see TextureState.h simplifications).
    if (index == 0) {
        return NullTexture();
    }
    auto it = m_objects.find(index);
    if (it == m_objects.end()) {
        return NullTexture();
    }
    return it->second;
}

const SharedPtr<TextureObject>& TextureState::CreateTextureObject(uint32_t index, TextureTarget target) {
    auto it = m_objects.find(index);
    if (it != m_objects.end()) {
        // Object already exists: a texture's target is fixed at first bind, so
        // leave it untouched and return the existing record.
        return it->second;
    }
    auto obj = std::make_shared<TextureObject>(index);
    obj->target = target;
    auto result = m_objects.emplace(index, std::move(obj));
    return result.first->second;
}

void TextureState::MarkTextureForDeletion(uint32_t index) {
    // Name 0 is "no texture" and never lives in the table; GL silently ignores
    // a 0 passed to glDeleteTextures.
    if (index == 0) {
        return;
    }
    // GL name-layer deletion only: drop the name from the object table. The
    // underlying Vulkan resource (VkImage + device memory) is released by the
    // backend disposal queue once in-flight GPU work referencing it completes,
    // so this component frees no backend handle here. A texture unit that still
    // holds a SharedPtr to this object keeps the TextureObject alive until the
    // unit is unbound (which we do immediately below).
    m_objects.erase(index);
    // Unbind this texture from every unit, matching GL's semantics where a
    // deleted texture is detached from any unit it was bound to.
    for (TextureUnit& unit : m_units) {
        if (unit.bound && unit.bound->id == index) {
            unit.bound.reset();
            unit.boundTarget = TextureTarget::Texture2D;
        }
    }
    ++m_textureBindGeneration;
}

bool TextureState::ValidateTextureName(uint32_t index) const {
    if (index == 0) {
        return false;
    }
    return m_objects.count(index) > 0;
}

bool TextureState::ValidateTextureObject(uint32_t index) const {
    if (index == 0) {
        return false;
    }
    auto it = m_objects.find(index);
    return it != m_objects.end() && it->second != nullptr;
}

void TextureState::BindTexture(TextureTarget target, uint32_t index) {
    TextureUnit& unit = m_units[UnitIndex(m_activeTextureUnit)];
    if (index == 0) {
        // Unbind: GL's glBindTexture(target, 0) detaches whatever was bound.
        unit.bound.reset();
        unit.boundTarget = TextureTarget::Texture2D;
    } else {
        const SharedPtr<TextureObject>& obj = CreateTextureObject(index, target);
        unit.bound = obj;
        unit.boundTarget = target;
    }
    ++m_version;
    ++m_textureBindGeneration;
}

const SharedPtr<TextureObject>& TextureState::GetBoundTexture(uint32_t unit) const {
    return m_units[UnitIndex(unit)].bound;
}

TextureTarget TextureState::GetBoundTextureTarget(uint32_t unit) const {
    return m_units[UnitIndex(unit)].boundTarget;
}

uint32_t TextureState::GetActiveTextureUnit() const {
    return m_activeTextureUnit;
}

void TextureState::SetActiveTextureUnit(uint32_t unit) {
    // Store the raw index; out-of-range values are clamped on access via
    // UnitIndex so a stray value never reads out of bounds. The real range
    // validation against GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS is the MG_Impl
    // entry layer's job.
    m_activeTextureUnit = unit;
    ++m_version;
}

uint64_t TextureState::GetTextureBindGeneration() const {
    return m_textureBindGeneration;
}

void TextureState::BumpTextureBindGeneration() {
    ++m_textureBindGeneration;
}

uint16_t TextureState::GetVersion() const {
    return m_version;
}

ProxyTextureState& TextureState::GetProxyTexture2D() {
    return m_proxyTexture2D;
}

const ProxyTextureState& TextureState::GetProxyTexture2D() const {
    return m_proxyTexture2D;
}

} // namespace mithril::glstate
