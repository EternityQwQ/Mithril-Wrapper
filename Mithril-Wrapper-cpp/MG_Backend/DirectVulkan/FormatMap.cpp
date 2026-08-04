// Mithril-Wrapper - MG_Backend/DirectVulkan/FormatMap.cpp
// Pure-logic GL internalFormat -> VkFormat / host texel size / aspect mask
// helpers. Extracted from Resources.cpp so they can be unit-tested without
// linking the rest of the Vulkan backend.
//
// These functions are pure data tables: given a GLenum they return a VkFormat
// / byte count / VkImageAspectFlags. They do NOT call into Vulkan and are safe
// to invoke from a unit-test binary with no VkDevice available.
#include "FormatMap.h"

// Legacy GL internalFormat enums not declared by our minimal glcorearb.h.
// Minecraft's font renderer and several legacy mod textures still upload
// with these as internalFormat; without the mappings below,
// gl_internal_to_vk returns VK_FORMAT_UNDEFINED and the texture silently
// falls back to a wrong color format (visible as corrupt/garbled font
// glyphs). Hex values are from the GL registry (GL_VERSION_1_1 / EXT_sRGB).
// Guarded so a future glcorearb.h update that adds them won't conflict.
#ifndef GL_ALPHA8
#define GL_ALPHA8                       0x803C
#endif
#ifndef GL_LUMINANCE8
#define GL_LUMINANCE8                   0x8040
#endif
#ifndef GL_LUMINANCE8_ALPHA8
#define GL_LUMINANCE8_ALPHA8            0x8045
#endif
#ifndef GL_INTENSITY
#define GL_INTENSITY                    0x8049
#endif
#ifndef GL_INTENSITY8
#define GL_INTENSITY8                   0x804B
#endif
#ifndef GL_SLUMINANCE8
#define GL_SLUMINANCE8                  0x8C47
#endif

namespace mithril {
namespace vk {

// ---- GL internalFormat -> VkFormat ----
// Covers the formats exercised by Minecraft Java's modern pipeline.
VkFormat gl_internal_to_vk(GLenum internal) {
    switch (internal) {
        case GL_RGBA8:                return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_SRGB8_ALPHA8:         return VK_FORMAT_R8G8B8A8_SRGB;
        // ---- FIX (根因 W - CRITICAL): Metal 无 3 分量 MTLPixelFormat ----
        // Metal 的 MTLPixelFormat 枚举不含 3 分量格式（无 RGB8/RGB16F/RGB32F，
        // 从 RG8 直接跳到 RGBA8）。MoltenVK 无法映射 VK_FORMAT_R8G8B8_* →
        // vkCreateImage/vkCreateImageView 失败 → 用作 FBO 颜色附件时 pipeline
        // 创建失败（pColorAttachmentFormats 含不支持格式）→ draw 被跳过 → 红屏。
        // 修复：将 3 分量 GL 格式统一展开为 4 分量 RGBA VkFormat；alpha 由上传
        // 路径（Resources.cpp:stage_and_copy_image 的 RGB→RGBA 展开）补 0xFF
        // （unorm）/1.0 位模式（sfloat）。
        // 对照 MobileGL ResolveTextureFormatInfo (VkTextureManager.cpp:374-427)。
        case GL_RGB8:                 return VK_FORMAT_R8G8B8A8_UNORM;  // 根因 W: 原 R8G8B8_UNORM
        case GL_RGB565:               return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case GL_RGBA4:                return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case GL_RGB5_A1:              return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        case GL_RGBA16F:              return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GL_RGB16F:               return VK_FORMAT_R16G16B16A16_SFLOAT;  // 根因 W: 原 R16G16B16_SFLOAT（Metal 无 3 分量）
        case GL_RGBA32F:              return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GL_RGB32F:               return VK_FORMAT_R32G32B32A32_SFLOAT;  // 根因 W: 原 R32G32B32_SFLOAT（Metal 无 3 分量）
        case GL_R8:                   return VK_FORMAT_R8_UNORM;
        case GL_R8_SNORM:             return VK_FORMAT_R8_SNORM;
        case GL_R16F:                 return VK_FORMAT_R16_SFLOAT;
        case GL_R32F:                 return VK_FORMAT_R32_SFLOAT;
        case GL_RG8:                  return VK_FORMAT_R8G8_UNORM;
        case GL_RG16F:                return VK_FORMAT_R16G16_SFLOAT;
        case GL_RG32F:                return VK_FORMAT_R32G32_SFLOAT;
        case GL_RGBA8_SNORM:          return VK_FORMAT_R8G8B8A8_SNORM;
        case GL_RGB10_A2:             return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case GL_RGB10_A2UI:           return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case GL_RGBA16:               return VK_FORMAT_R16G16B16A16_UNORM;
        case GL_DEPTH_COMPONENT16:    return VK_FORMAT_D16_UNORM;
        case GL_DEPTH_COMPONENT24:    return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH_COMPONENT32F:   return VK_FORMAT_D32_SFLOAT;
        case GL_DEPTH24_STENCIL8:     return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH32F_STENCIL8:    return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case GL_STENCIL_INDEX8:       return VK_FORMAT_S8_UINT;
        // Unsized internal formats (Minecraft passes these for many textures
        // and for depth). glslang/MoltenVK need a concrete VkFormat, so map
        // them to the natural sized equivalent. Without these, gl_internal_to_vk
        // returns VK_FORMAT_UNDEFINED and the texture falls back to a color
        // format — which is especially wrong for GL_DEPTH_COMPONENT.
        case GL_RGBA:             return VK_FORMAT_R8G8B8A8_UNORM;  // 0x1908
        case GL_RGB:              return VK_FORMAT_R8G8B8A8_UNORM;    // 根因 W: 原 R8G8B8_UNORM（unsized GL_RGB，Metal 无 3 分量）0x1907
        case GL_DEPTH_COMPONENT:  return VK_FORMAT_D32_SFLOAT;      // 0x1902
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: return VK_FORMAT_BC2_UNORM_BLOCK;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return VK_FORMAT_BC3_UNORM_BLOCK;
        // ---- Legacy fixed-function internal formats (Task 5) ----
        // GL_ALPHA / GL_LUMINANCE / GL_LUMINANCE_ALPHA / GL_INTENSITY are
        // pre-Core-Profile internal formats. Minecraft's font renderer (and
        // legacy mod textures) still upload with these. Vulkan has no direct
        // alpha-only / luminance-only image format, so we map each to the
        // smallest UNORM VkFormat whose channel count matches the host data,
        // and rely on a component swizzle set at VkImageView creation time
        // (Resources.cpp / Texture.cpp glTexImage2D path, keyed off
        // internalFormat) to replicate the channel(s) into the RGBA slots:
        //   GL_ALPHA            -> R8_UNORM,  swizzle AAAA (alpha -> RGBA)
        //   GL_LUMINANCE        -> R8_UNORM,  swizzle RRR1  (red -> RGB, A=1)
        //   GL_LUMINANCE_ALPHA  -> R8G8_UNORM,swizzle RRRG  (red -> RGB, A=G)
        //   GL_INTENSITY        -> R8_UNORM,  swizzle RRRR  (red -> RGBA)
        // The sized variants (GL_ALPHA8 / GL_LUMINANCE8 / GL_LUMINANCE8_ALPHA8
        // / GL_INTENSITY8) map to the same VkFormat as their unsized
        // counterparts. GL_SLUMINANCE8 is the sRGB-encoded variant of
        // LUMINANCE8_ALPHA8; we simplify it to UNORM (no sRGB decode) so the
        // uploaded bytes map 1:1 — applying the sRGB curve would require a
        // separate _SRGB VkFormat and a matching swizzle path, which is not
        // wired up here. For Minecraft's font textures this is correct
        // (they are uploaded as linear UNORM and sampled via gamma-corrected
        // blend state in the host shader).
        // NOTE: this function only returns the VkFormat. The swizzle itself
        // must be applied where the VkImageView is created — out of scope for
        // FormatMap.cpp (see Texture.cpp / Resources.cpp). Returning the right
        // VkFormat here is the necessary first step; without it the texture
        // creation falls through to VK_FORMAT_UNDEFINED and the image is never
        // created at all.
        case GL_ALPHA:                return VK_FORMAT_R8_UNORM;       // 0x1906 (swizzle AAAA)
        case GL_ALPHA8:               return VK_FORMAT_R8_UNORM;       // 0x803C
        case GL_LUMINANCE:            return VK_FORMAT_R8_UNORM;       // 0x1909 (swizzle RRR1)
        case GL_LUMINANCE8:            return VK_FORMAT_R8_UNORM;       // 0x8040
        case GL_LUMINANCE_ALPHA:       return VK_FORMAT_R8G8_UNORM;    // 0x190A (swizzle RRRG)
        case GL_LUMINANCE8_ALPHA8:    return VK_FORMAT_R8G8_UNORM;    // 0x8045
        case GL_SLUMINANCE8:           return VK_FORMAT_R8G8_UNORM;    // 0x8C47 (sRGB -> UNORM simplified)
        case GL_INTENSITY:             return VK_FORMAT_R8_UNORM;       // 0x8049 (swizzle RRRR)
        case GL_INTENSITY8:            return VK_FORMAT_R8_UNORM;       // 0x804B
        default:                      return VK_FORMAT_UNDEFINED;
    }
}

// Bytes per pixel for the (format,type) pair as seen on the host side. Used to
// size the staging buffer for glTexImage* uploads.
int host_texel_bytes(GLenum format, GLenum type) {
    int comp = 4;
    switch (format) {
        case GL_RED:
        case GL_RED_INTEGER:
        case GL_LUMINANCE:
        case GL_ALPHA:
        case GL_INTENSITY:        comp = 1; break;  // Task 5: GL_INTENSITY is 1 byte/texel (replicated to RGBA)
        case GL_RG:
        case GL_RG_INTEGER:
        case GL_LUMINANCE_ALPHA:  comp = 2; break;
        case GL_RGB:
        case GL_RGB_INTEGER:
        case GL_BGR:              comp = 3; break;
        case GL_RGBA:
        case GL_RGBA_INTEGER:
        case GL_BGRA:             comp = 4; break;
        default: break;
    }
    switch (type) {
        case GL_UNSIGNED_BYTE:           return comp;
        case GL_BYTE:                    return comp;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:              return comp * 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:                   return comp * 4;
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:  return 2;
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV: return 4;
        default:                         return comp;
    }
}

// VkImageAspectFlags for a VkFormat (color / depth / depth+stencil / stencil).
// Mirrors the aspect-for-format helper in ImageOps.cpp (kept here so callers
// that only depend on Resources.cpp don't need to link ImageOps).
VkImageAspectFlags aspect_for_format(VkFormat fmt) {
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (fmt == VK_FORMAT_S8_UINT)
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

// FIX (Root Cause AH - depth-stencil descriptor layout):
// 返回采样纹理应处的 read-only 布局，按 VkFormat 区分：
//   - depth-stencil 格式 (D24S8/D32S8/S8) -> DEPTH_STENCIL_READ_ONLY_OPTIMAL
//   - depth-only 格式 (D16/D32/X8_D24)    -> DEPTH_READ_ONLY_OPTIMAL
//   - 其他 (color)                        -> SHADER_READ_ONLY_OPTIMAL
//
// 根因机制：DescriptorSet.cpp 写 descriptor imageInfo 时硬编码
// SHADER_READ_ONLY_OPTIMAL，Resources.cpp 上传后 layout transition 也硬编码
// SHADER_READ_ONLY_OPTIMAL。对 depth-stencil 纹理，image 实际布局（经根因 Y
// 修复后为 DEPTH_STENCIL_READ_ONLY_OPTIMAL）与 descriptor 声明不匹配 →
// MoltenVK 验证错误或静默丢 draw → 黑屏。
//
// 对照 MobileGL ResolveSampledReadOnlyLayout (VkTextureManager.cpp:177)：
// 对 depth-stencil aspect 返回 DEPTH_STENCIL_READ_ONLY_OPTIMAL。
VkImageLayout sampled_layout_for_format(VkFormat fmt) {
    switch (fmt) {
        // depth-stencil (含 stencil aspect): 采样时必须用 DEPTH_STENCIL_READ_ONLY_OPTIMAL
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        // depth-only (无 stencil aspect): 用 DEPTH_READ_ONLY_OPTIMAL
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        // color 及其他: 用 SHADER_READ_ONLY_OPTIMAL
        default:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

} // namespace vk
} // namespace mithril
