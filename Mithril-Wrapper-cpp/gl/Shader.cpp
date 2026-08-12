// Mithril-Wrapper - gl/Shader.cpp
// Thin forwarding layer.
//
// The GLSL->SPIR-V translation logic (preprocessing steps 1-8, glslang
// compile, two-level strict fallback, thread-safety) and the SHA-256 LRU
// persistent cache now live in gl/glsl/{glsl_for_vk,cache}.{cpp,h}, mirroring
// MobileGlues' gl/glsl/ module structure. This file preserves the historical
// mithril::shader_translate / mithril::get_fallback_spirv public API declared
// in gl/Shader.h by forwarding each call to its mithril::glsl:: counterpart.
// No translation logic lives here.
//
// gl/Shader.h is unchanged; gl/Program.cpp (the sole includer) is unchanged.
#include "Shader.h"
#include "glsl/glsl_for_vk.h"

namespace mithril {

bool shader_translate(GLenum gl_stage, const std::string& glsl_source,
                      std::vector<uint32_t>& out_spirv, std::string& out_info_log,
                      const std::unordered_map<std::string, GLuint>* attrib_bindings,
                      bool flip_y) {
    return ::mithril::glsl::shader_translate(gl_stage, glsl_source, out_spirv,
                                             out_info_log, attrib_bindings, flip_y);
}

bool get_fallback_spirv(GLenum gl_stage, bool flip_y,
                        std::vector<uint32_t>& out_spirv) {
    return ::mithril::glsl::get_fallback_spirv(gl_stage, flip_y, out_spirv);
}

} // namespace mithril
