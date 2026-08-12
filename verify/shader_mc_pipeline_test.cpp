// shader_mc_pipeline_test.cpp — Run the REAL Shader.cpp translation pipeline
// (shader_translate) against a battery of authentic Minecraft Java Edition
// core shaders, for both the non-flipped (user FBO) and Y-flipped (default
// FBO) vertex variants. This directly exercises the code path whose failure
// triggers the fallback degenerate-vertex shader -> "only clear color visible"
// -> red Mojang loading screen / black main menu.
//
// It links the ACTUAL Mithril-Wrapper translation units (Shader.cpp + Log.cpp)
// so we test the real production pipeline (ensure_glsl_version,
// rewrite_desktop_builtins, apply_attrib_bindings, normalize_*, 
// inject_position_fixup, wrap_loose_uniforms, inject_opaque_bindings,
// apply_stage_binding_shift), NOT a re-implementation.
//
// Build (against the glslang submodule):
//   GL=Mithril-Wrapper-cpp/3rdparty/glslang
//   clang++ -std=c++20 -O0 \
//     -IMithril-Wrapper-cpp/include -IMithril-Wrapper-cpp/gl \
//     -I$GL -I$GL/build-test/External/spirv-tools/include \
//     verify/shader_mc_pipeline_test.cpp \
//     Mithril-Wrapper-cpp/gl/Shader.cpp Mithril-Wrapper-cpp/gl/Log.cpp \
//     $GL/build-test/glslang/libglslang.a $GL/build-test/glslang/libMachineIndependent.a \
//     $GL/build-test/glslang/libGenericCodeGen.a \
//     $GL/build-test/glslang/OSDependent/Unix/libOSDependent.a \
//     $GL/build-test/SPIRV/libSPIRV.a \
//     $GL/build-test/glslang/libglslang-default-resource-limits.a \
//     -lpthread -o shader_mc_pipeline_test
//
// Exit code 0 => all real MC shaders compile through the production pipeline.
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <GL/gl.h>
#include "Shader.h"

static int failures = 0, checks = 0;
#define CHECK(cond, fmt, ...) do { ++checks; if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

// Compile one shader stage through the real pipeline. Returns true if the
// production shader_translate succeeds.
static bool translate(GLenum stage, const std::string& src, bool flip_y,
                      const std::unordered_map<std::string, GLuint>* attribs,
                      std::string& err) {
    std::vector<uint32_t> spirv;
    if (!mithril::shader_translate(stage, src, spirv, err, attribs, flip_y)) {
        return false;
    }
    return !spirv.empty();
}

// Run the full battery for one source + stage pair, both Y orientations.
static void run_shader(const char* label, GLenum stage, const std::string& src,
                       const std::unordered_map<std::string, GLuint>* attribs = nullptr) {
    // Non-flipped variant (user FBO).
    std::string errN;
    bool okN = translate(stage, src, false, attribs, errN);
    CHECK(okN, "%s [user-FBO / no-flip]: compiles -> %s",
          label, okN ? "OK" : ("FAIL: " + errN).c_str());

    // Y-flipped variant (default FBO) for vertex shaders only.
    if (stage == GL_VERTEX_SHADER) {
        std::string errY;
        bool okY = translate(stage, src, true, attribs, errY);
        CHECK(okY, "%s [default-FBO / Y-flip]: compiles -> %s",
              label, okY ? "OK" : ("FAIL: " + errY).c_str());
    }
}

// Scan a SPIR-V module (skip the 5-word header) for an OpTypePointer whose
// storage class is PushConstant (9). Returns the count.
static int count_pushconstant_ptrs(const std::vector<uint32_t>& spirv) {
    if (spirv.size() < 5) return 0;
    size_t i = 5;
    int pc = 0;
    while (i < spirv.size()) {
        uint32_t w0 = spirv[i];
        uint16_t op = w0 & 0xffffu;   // opcode in low 16 bits
        uint16_t wc = w0 >> 16;       // word count in high 16 bits
        if (wc == 0 || i + wc > spirv.size()) break;
        // OpTypePointer == 32; operand[1] == storage class (9 = PushConstant).
        if (op == 32 && spirv[i + 2] == 9) ++pc;
        i += wc;
    }
    return pc;
}

// Root cause regression: gl_VertexID baseVertex semantics.
// A vertex shader that uses gl_VertexID MUST, after the Shader.cpp fix,
// compile with an injected _MithrilBaseVertex push-constant block (storage
// class PushConstant) so gl_VertexID == gl_VertexIndex + baseVertex. BEFORE
// the fix, gl_VertexID was naively renamed to gl_VertexIndex with no
// compensation — the module emitted NO push-constant block, so this check
// failed (a valid Fail-before / Pass-after discriminator).
//
// Calls shader_translate directly (not the translate() helper) so the
// resulting SPIR-V words are available for the storage-class scan.
static void check_vertex_id_pushconstant() {
    const char* vs = "#version 150 core\n"
        "in vec3 Position;\n"
        "void main() {\n"
        "    float x = float(gl_VertexID & 1);\n"
        "    gl_Position = vec4(Position.xy + vec2(x, 0.0), Position.z, 1.0);\n"
        "}\n";

    // Non-flipped variant.
    {
        std::vector<uint32_t> spirv; std::string err;
        bool ok = mithril::shader_translate(GL_VERTEX_SHADER, vs, spirv, err, nullptr, false);
        CHECK(ok, "gl_VertexID [user-FBO] compiles -> %s", ok ? "OK" : ("FAIL: " + err).c_str());
        if (ok) {
            int pc = count_pushconstant_ptrs(spirv);
            CHECK(pc >= 1, "gl_VertexID [user-FBO] emits push-constant block (found %d, want >=1)", pc);
        } else {
            CHECK(false, "gl_VertexID [user-FBO] emits push-constant block (compile failed)");
        }
    }
    // Y-flipped variant.
    {
        std::vector<uint32_t> spirv; std::string err;
        bool ok = mithril::shader_translate(GL_VERTEX_SHADER, vs, spirv, err, nullptr, true);
        CHECK(ok, "gl_VertexID [default-FBO / Y-flip] compiles -> %s", ok ? "OK" : ("FAIL: " + err).c_str());
        if (ok) {
            int pc = count_pushconstant_ptrs(spirv);
            CHECK(pc >= 1, "gl_VertexID [default-FBO / Y-flip] emits push-constant block (found %d, want >=1)", pc);
        } else {
            CHECK(false, "gl_VertexID [default-FBO / Y-flip] emits push-constant block (compile failed)");
        }
    }

    // A shader that does NOT use gl_VertexID still carries the (harmless)
    // push-constant block; a fragment shader must be completely unaffected
    // (no push constant in FS).
    {
        const char* blit = "#version 150 core\n"
            "in vec2 Position;\n"
            "void main() { gl_Position = vec4(Position, 0.0, 1.0); }\n";
        std::vector<uint32_t> spirv; std::string err;
        bool ok = mithril::shader_translate(GL_VERTEX_SHADER, blit, spirv, err, nullptr, false);
        CHECK(ok, "blit (no gl_VertexID) compiles -> %s", ok ? "OK" : ("FAIL: " + err).c_str());
        if (ok) CHECK(count_pushconstant_ptrs(spirv) >= 1,
                      "blit (no gl_VertexID) carries push-constant block (found %d)",
                      count_pushconstant_ptrs(spirv));
    }
    {
        const char* frag = "#version 150 core\n"
            "uniform sampler2D S; out vec4 c; void main() { c = texture(S, vec2(0.5)); }\n";
        std::vector<uint32_t> spirv; std::string err;
        bool ok = mithril::shader_translate(GL_FRAGMENT_SHADER, frag, spirv, err, nullptr, false);
        CHECK(ok, "fragment sampler compiles -> %s", ok ? "OK" : ("FAIL: " + err).c_str());
        if (ok) CHECK(count_pushconstant_ptrs(spirv) == 0,
                      "fragment sampler has NO push-constant block (found %d, want 0)",
                      count_pushconstant_ptrs(spirv));
    }
}

int main() {
    // ---- Mojang loading screen / boot path shaders -------------------------
    // blit_screen.vert / blit_screen.frag — used for the loading screen
    // full-screen blit (renders the Mojang logo and background).
    run_shader("blit_screen.vert",
        GL_VERTEX_SHADER,
        "#version 150 core\n"
        "in vec2 Position;\n"
        "in vec2 UV0;\n"
        "out vec2 texCoord0;\n"
        "uniform mat4 ModelViewMat;\n"
        "uniform mat4 ProjMat;\n"
        "void main() {\n"
        "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 0.0, 1.0);\n"
        "    texCoord0 = UV0;\n"
        "}\n");

    run_shader("blit_screen.frag",
        GL_FRAGMENT_SHADER,
        "#version 150 core\n"
        "uniform sampler2D DiffuseSampler;\n"
        "in vec2 texCoord0;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(DiffuseSampler, texCoord0);\n"
        "}\n");

    // rendertype_text.vert / .frag — used for GUI text (main menu buttons,
    // FPS counter overlay). Uses a loose ColorModulator uniform.
    run_shader("rendertype_text.vert",
        GL_VERTEX_SHADER,
        "#version 150 core\n"
        "in vec3 Position;\n"
        "in vec2 UV0;\n"
        "in vec2 UV2;\n"
        "in vec4 Color;\n"
        "uniform mat4 ModelViewMat;\n"
        "uniform mat4 ProjMat;\n"
        "out vec2 texCoord0;\n"
        "out vec2 texCoord2;\n"
        "out vec4 vertexColor;\n"
        "void main() {\n"
        "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
        "    vertexColor = Color;\n"
        "    texCoord0 = UV0;\n"
        "    texCoord2 = UV2;\n"
        "}\n");

    run_shader("rendertype_text.frag",
        GL_FRAGMENT_SHADER,
        "#version 150 core\n"
        "in vec2 texCoord0;\n"
        "in vec2 texCoord2;\n"
        "in vec4 vertexColor;\n"
        "uniform sampler2D Sampler0;\n"
        "uniform sampler2D Sampler2;\n"
        "uniform vec4 ColorModulator;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    vec4 color = texture(Sampler0, texCoord0) * vertexColor;\n"
        "    if (color.a < 0.1) discard;\n"
        "    fragColor = color * ColorModulator;\n"
        "}\n");

    // rendertype_gui.vert / .frag — main menu GUI panels (background,
    // buttons). Uses a loose ColorModulator and ChunkOffset.
    run_shader("rendertype_gui.vert",
        GL_VERTEX_SHADER,
        "#version 150 core\n"
        "in vec3 Position;\n"
        "in vec4 Color;\n"
        "uniform mat4 ModelViewMat;\n"
        "uniform mat4 ProjMat;\n"
        "out vec4 vertexColor;\n"
        "void main() {\n"
        "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
        "    vertexColor = Color;\n"
        "}\n");

    run_shader("rendertype_gui.frag",
        GL_FRAGMENT_SHADER,
        "#version 150 core\n"
        "in vec4 vertexColor;\n"
        "uniform sampler2D Sampler0;\n"
        "uniform vec4 ColorModulator;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    vec4 color = texture(Sampler0, gl_FragCoord.xy / 0.0) ;\n"
        "    fragColor = vec4(0.5) * vertexColor * ColorModulator;\n"
        "}\n");

    // ---- Main menu specific: solid color / position (world background) -----
    run_shader("rendertype_solid.vert",
        GL_VERTEX_SHADER,
        "#version 150 core\n"
        "in vec3 Position;\n"
        "in vec4 Color;\n"
        "uniform mat4 ModelViewMat;\n"
        "uniform mat4 ProjMat;\n"
        "out vec4 vertexColor;\n"
        "void main() {\n"
        "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
        "    vertexColor = Color;\n"
        "}\n");

    run_shader("rendertype_solid.frag",
        GL_FRAGMENT_SHADER,
        "#version 150 core\n"
        "in vec4 vertexColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = vertexColor;\n"
        "}\n");

    // ---- gl_FragColor legacy style (older MC versions / some GUI shaders) ---
    run_shader("legacy_gl_FragColor.frag",
        GL_FRAGMENT_SHADER,
        "#version 330 core\n"
        "uniform sampler2D Sampler0;\n"
        "uniform vec4 ColorModulator;\n"
        "in vec2 texCoord0;\n"
        "void main() {\n"
        "    gl_FragColor = texture(Sampler0, texCoord0) * ColorModulator;\n"
        "}\n");

    // ---- layout(packed) UBO (pre-1.17 style / mod shaders) -----------------
    run_shader("packed_ubo.frag",
        GL_FRAGMENT_SHADER,
        "#version 330 core\n"
        "layout(packed) uniform Block { mat4 MVP; vec4 Tint; } _b;\n"
        "uniform sampler2D Sampler0;\n"
        "in vec2 texCoord0;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(Sampler0, texCoord0) * _b.Tint;\n"
        "}\n");

    // ---- gl_VertexID usage (rendertype_lines / debug overlays) -------------
    run_shader("rendertype_lines.vert",
        GL_VERTEX_SHADER,
        "#version 150 core\n"
        "in vec3 Position;\n"
        "in vec4 Color;\n"
        "uniform mat4 ModelViewMat;\n"
        "uniform mat4 ProjMat;\n"
        "uniform vec4 ColorModulator;\n"
        "out vec4 vertexColor;\n"
        "void main() {\n"
        "    int id = gl_VertexID;\n"
        "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
        "    vertexColor = Color * ColorModulator;\n"
        "}\n");

    // ---- position-only fallback shader (what MC uses when no MVP) ----------
    run_shader("position_only.vert",
        GL_VERTEX_SHADER,
        "#version 330 core\n"
        "void main() {\n"
        "    gl_Position = vec4(0.0, 0.0, 0.5, 1.0);\n"
        "}\n");

    // ---- Root cause regression: gl_VertexID baseVertex push-constant -------
    check_vertex_id_pushconstant();

    printf("\nMC pipeline test: %d/%d checks passed, %d failure(s)\n",
           checks - failures, checks, failures);
    return failures ? 1 : 0;
}