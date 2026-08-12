// Mithril-Wrapper - gl/glsl/glsl_for_vk.h
// GLSL (desktop Core Profile) -> Vulkan SPIR-V translation via glslang.
//
// This is the MobileGlues-style relocation of the former gl/Shader.cpp. The
// module structure (gl/glsl/glsl_for_*.{cpp,h} + cache.{cpp,h}) and the
// SHA-256 LRU persistence cache mirror MobileGlues gl/glsl/glsl_for_es + cache.
// The translation target differs: MobileGlues' glsl_for_es emits GLES source
// for a native GLES driver; Mithril's glsl_for_vk emits Vulkan SPIR-V words
// for MoltenVK (which cross-translates SPIR-V to MSL internally at
// vkCreateShaderModule time). spirv_cross reflection (building descriptor set
// layouts) lives in MG_Backend/DirectVulkan/{Reflect,DescriptorSet}.cpp and is
// intentionally NOT in this module.
//
// Pipeline (single EShClientOpenGL input dialect; no Vulkan-client dialect):
//   1. Preprocess: inject MG_MITHRIL / MG_MITHRIL_VERSION macros so host
//      shaders can branch on the Mithril backend (mirrors MobileGlues'
//      MG_MOBILEGLUES injection). Upgrade GLSL versions below 420 so desktop
//      GLSL 150 shaders like Minecraft's blit_screen compile and
//      layout(binding=) is legal.
//   2. Inject layout(location=N) into vertex `in` declarations from
//      glBindAttribLocation mappings so the SPIR-V stage_input locations match
//      the application's vertex descriptor.
//   3. Normalize Vulkan-incompatible layout qualifiers: rewrite UBO
//      layout(packed)/layout(shared) to layout(std140) and SSBO equivalents
//      to layout(std430) so strict EShClientOpenGL + EShMsgVulkanRules
//      compiles them directly.
//   4. Normalize GL legacy constructs: in fragment shaders using
//      gl_FragColor, inject a synthetic layout(location=0) out vec4
//      _mithril_FragColor declaration and rewrite all references.
//   5. Inject GL->Vulkan position fixups (Z remap always; Y flip when
//      flip_y) by wrapping main() (vertex shaders only).
//   6. Fold loose non-opaque uniforms into a synthetic mithril_GlobalBlock UBO
//      (mirrors ANGLE's ANGLE_DefaultUniformBlock).
//   7. Per-stage explicit binding injection for opaque uniforms and UBO blocks
//      (root cause AK fix: prevents VS/FS binding collisions).
//   8. glslang compiles GLSL to Vulkan SPIR-V via EShClientOpenGL +
//      EShMsgVulkanRules. Two-level strict fallback: wrapped source (after
//      step 6) -> unwrapped source (pre-step-6 backup, still normalized by
//      steps 3-5).
//   9. SPIR-V words are returned directly; MoltenVK cross-translates to MSL.
//
// Results are cached by SHA-256(source + stage + attrib bindings + flip_y +
// MG version) via the persistent LRU cache in cache.{cpp,h}.
#ifndef MITHRIL_GLSL_FOR_VK_H
#define MITHRIL_GLSL_FOR_VK_H

#include <GL/gl.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril::glsl {

// Translate a desktop GLSL Core Profile source string into Vulkan SPIR-V
// words. Returns true on success. On failure, out_info_log is populated.
//
// attrib_bindings maps attribute names to the location the application
// requested via glBindAttribLocation() (vertex shaders only). Pass nullptr when
// no explicit bindings are needed (falls back to glslang auto-mapping).
//
// flip_y (vertex shaders only): when true, injects a Y-flip in addition to the
// always-applied Z remap. Fragment shaders ignore flip_y.
bool shader_translate(GLenum gl_stage, const std::string& glsl_source,
                      std::vector<uint32_t>& out_spirv, std::string& out_info_log,
                      const std::unordered_map<std::string, GLuint>* attrib_bindings = nullptr,
                      bool flip_y = false);

// Generate a minimal fallback SPIR-V for a shader stage that failed to
// compile. Vertex fallback: degenerate position (no attribute read, avoids
// Metal page-fault device-loss). Fragment fallback: solid gray output. Other
// stages: returns false. Results are cached in an in-memory (non-persistent)
// cache so repeated failures of the same stage do not re-invoke glslang.
bool get_fallback_spirv(GLenum gl_stage, bool flip_y,
                        std::vector<uint32_t>& out_spirv);

} // namespace mithril::glsl

#endif // MITHRIL_GLSL_FOR_VK_H
