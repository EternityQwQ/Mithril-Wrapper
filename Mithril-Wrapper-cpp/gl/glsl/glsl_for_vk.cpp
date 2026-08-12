// Mithril-Wrapper - gl/glsl/glsl_for_vk.cpp
// GLSL (desktop Core Profile) -> Vulkan SPIR-V translation via glslang.
//
// This is a verbatim relocation of the former gl/Shader.cpp translation logic
// into namespace mithril::glsl, with the in-memory FNV-1a cache replaced by
// the SHA-256 LRU persistent cache in cache.{cpp,h}. Preprocessing steps 1-8,
// the glslang compile options (EShClientOpenGL + EShMsgVulkanRules +
// EShTargetSpv_1_5), the two-level strict fallback (wrapped -> unwrapped),
// the per-stage binding shift, and the thread-safety guarantees are unchanged
// -- only the file location, namespace, and cache backend changed.
//
// See glsl_for_vk.h for the pipeline overview and the cache rationale.
#include "glsl_for_vk.h"
#include "cache.h"
#include "../Log.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril::glsl {
namespace {

struct GlslangInit {
    GlslangInit()  { glslang::InitializeProcess(); }
    ~GlslangInit() { /* process-lifetime; no finalize needed */ }
};
GlslangInit& glslang_init() {
    static GlslangInit g;
    return g;
}

EShLanguage to_esh_stage(GLenum gl) {
    switch (gl) {
        case GL_VERTEX_SHADER:          return EShLangVertex;
        case GL_FRAGMENT_SHADER:        return EShLangFragment;
        case GL_GEOMETRY_SHADER:        return EShLangGeometry;
        case GL_TESS_CONTROL_SHADER:    return EShLangTessControl;
        case GL_TESS_EVALUATION_SHADER: return EShLangTessEvaluation;
        case GL_COMPUTE_SHADER:         return EShLangCompute;
        default:                        return EShLangCount;
    }
}

// Extract the GLSL #version number. Returns -1 if not found.
int get_glsl_version(const std::string& src) {
    static std::regex version_pattern(R"(#version\s+(\d{3}))");
    std::smatch match;
    if (std::regex_search(src, match, version_pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

// Ensure the GLSL source has a version usable by the Vulkan client. Vulkan
// GLSL requires #version 330 minimum (GL_KHR_vulkan_glsl). 本渲染器在 GLSL 源
// 码层面注入 layout(binding=N) 来分离 VS/FS 的 descriptor binding（见
// inject_opaque_bindings），而 layout(binding=) 要到 #version 420 才进核心
// （GL_ARB_shading_language_420pack），所以统一升到 420 core。
// Returns the resolved GLSL version number.
int ensure_glsl_version(std::string& src) {
    int ver = get_glsl_version(src);
    if (ver == -1) {
        ver = 420;
        src.insert(0, "#version 420 core\n");
        return ver;
    }
    if (ver < 420) {
        size_t pos = src.find("#version");
        size_t line_end = src.find('\n', pos);
        if (line_end == std::string::npos) line_end = src.length();
        src.replace(pos, line_end - pos, "#version 420 core");
        ver = 420;
    } else {
        size_t pos = src.find("#version");
        size_t line_end = src.find('\n', pos);
        if (line_end == std::string::npos) line_end = src.length();
        std::string line = src.substr(pos, line_end - pos);
        if (line.find("core") == std::string::npos &&
            line.find("compatibility") == std::string::npos &&
            line.find("es") == std::string::npos) {
            src.replace(pos, line_end - pos, line + " core");
        }
    }
    return ver;
}

// Rewrite desktop-GLSL built-in identifiers that Vulkan GLSL renames.
// gl_InstanceID -> gl_InstanceIndex. gl_VertexID is NOT renamed here; it is
// handled by inject_vertex_id_fixup()'s macro (which references the Vulkan
// builtin gl_VertexIndex directly and adds the baseVertex push-constant
// compensation that restores GL semantics). Vertex shaders only.
void rewrite_desktop_builtins(std::string& src, GLenum gl_stage) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    static const std::regex re(R"(\bgl_InstanceID\b)", std::regex::optimize);
    std::string out;
    out.reserve(src.size());
    std::string::const_iterator it = src.cbegin();
    std::smatch m;
    while (std::regex_search(it, src.cend(), m, re)) {
        out.append(it, m[0].first);
        out.append("gl_InstanceIndex");
        it = m[0].second;
    }
    out.append(it, src.cend());
    src = std::move(out);
}

// Inject a vertex-shader push-constant compensation block + gl_VertexID macro
// (root cause: gl_VertexID baseVertex semantics). Desktop GL's gl_VertexID in
// an indexed draw == index + baseVertex; Vulkan's gl_VertexIndex == the raw
// index (vertexOffset does NOT feed the shader-visible index). The injected
//   layout(push_constant) uniform _MithrilBaseVertex { int _mithrilBaseVertex; } _mbv;
//   #define gl_VertexID (gl_VertexIndex + _mbv._mithrilBaseVertex)
// restores GL semantics. _mbv._mithrilBaseVertex is written to
// currentBaseVertex on every draw (default 0). Idempotent; inserted after
// #version. Must run AFTER ensure_glsl_version().
void inject_vertex_id_fixup(std::string& src, GLenum gl_stage) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    if (src.find("_MithrilBaseVertex") != std::string::npos) return;  // idempotent

    size_t pos = src.find("#version");
    if (pos == std::string::npos) return;
    size_t insert_at = src.find('\n', pos);
    if (insert_at == std::string::npos) insert_at = src.length();
    insert_at += 1;

    const std::string snippet =
        "layout(push_constant) uniform _MithrilBaseVertex {\n"
        "    int _mithrilBaseVertex;\n"
        "} _mbv;\n"
        "#define gl_VertexID (gl_VertexIndex + _mbv._mithrilBaseVertex)\n";
    src.insert(insert_at, snippet);
}

// Inject `layout(location=N)` qualifiers into GLSL `in` declarations based on
// the application's glBindAttribLocation() mappings. Vertex shaders only.
void apply_attrib_bindings(std::string& src, GLenum gl_stage,
                           const std::unordered_map<std::string, GLuint>* bindings) {
    if (!bindings || bindings->empty()) return;
    if (gl_stage != GL_VERTEX_SHADER) return;

    static std::regex in_decl_re(
        R"(^\s*(?:layout\s*\([^)]*\)\s*)?(in|attribute)\s+(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;)",
        std::regex::optimize | std::regex::multiline);

    std::string out;
    out.reserve(src.size() + bindings->size() * 24);
    std::string::const_iterator search_start(src.cbegin());
    std::smatch m;
    size_t last_pos = 0;

    while (std::regex_search(search_start, src.cend(), m, in_decl_re)) {
        size_t match_pos = m.position(0) + (search_start - src.cbegin());
        out.append(src, last_pos, match_pos - last_pos);

        const std::string& keyword = m[1].str();
        const std::string& vartype = m[2].str();
        const std::string& varname = m[3].str();
        const std::string& array_suffix = m[4].matched ? m[4].str() : std::string();
        (void)keyword;

        auto it = bindings->find(varname);
        if (it != bindings->end()) {
            out += "layout(location=";
            out += std::to_string(it->second);
            out += ") in ";
            out += vartype;
            out += ' ';
            out += varname;
            if (!array_suffix.empty()) out += array_suffix;
            out += ';';
        } else {
            out += m[0].str();
        }

        last_pos = match_pos + m[0].length();
        search_start = m.suffix().first;
    }
    out.append(src, last_pos, std::string::npos);
    src.swap(out);
}

// Opaque GLSL type detection: types that MUST remain standalone (samplers,
// images, atomic counters, subpass inputs) because they cannot sit in a UBO.
static bool is_opaque_glsl_type(const std::string& name) {
    if (name.find("sampler") != std::string::npos) return true;
    if (name.find("image")   != std::string::npos) return true;
    if (name == "atomic_uint") return true;
    if (name.find("subpass") != std::string::npos) return true;
    return false;
}

// Find the '}' that closes the '{' at open_idx (brace-depth scan).
static size_t find_matching_brace(const std::string& s, size_t open_idx) {
    int depth = 0;
    for (size_t i = open_idx; i < s.size(); ++i) {
        if (s[i] == '{')       ++depth;
        else if (s[i] == '}') { --depth; if (depth == 0) return i; }
    }
    return std::string::npos;
}

// True if offset `off` sits inside a // or /* */ comment.
static bool is_in_comment(const std::string& s, size_t off) {
    size_t line_start = s.rfind('\n', off);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    if (s.find("//", line_start) < off) return true;
    int depth = 0;
    for (size_t i = 0; i < off; ++i) {
        if (i + 1 < off && s[i] == '/' && s[i + 1] == '*') { ++depth; ++i; }
        else if (i + 1 < off && s[i] == '*' && s[i + 1] == '/') { if (depth) --depth; ++i; }
    }
    return depth > 0;
}

// Split a declarator list ("a, b[2], c") into individual trimmed declarators,
// tracking [] depth so commas inside array sizes are ignored.
static void split_declarator(const std::string& list,
                             std::vector<std::string>& out) {
    std::string cur;
    int depth = 0;
    for (char c : list) {
        if (c == '[')      ++depth;
        else if (c == ']') { if (depth) --depth; }
        if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
}

// Extract (name, arraySuffix) from a single declarator "arr[2][3]".
static bool parse_declarator(const std::string& d, std::string& name,
                             std::string& arr) {
    static const std::regex nm_re(R"(^\s*(\w+)\s*((?:\[[^\]]*\])*)\s*$)");
    std::smatch m;
    if (!std::regex_match(d, m, nm_re)) return false;
    name = m[1].str();
    arr  = m[2].str();
    return true;
}

// Fold every non-block non-opaque uniform into a synthetic mithril_GlobalBlock
// UBO (mirrors ANGLE's ANGLE_DefaultUniformBlock). Handles precision
// qualifiers, multi-dimensional arrays, multiple declarators, named and
// anonymous struct uniforms, and skips declarations inside comments. The
// #define renames let the shader body reference members by their original
// names without a block prefix.
static void wrap_loose_uniforms(std::string& source) {
    struct Member { std::string decl; std::string name; };
    struct Erase  { size_t pos; size_t len; };
    std::vector<Member> members;
    std::vector<Erase>  erases;

    static const std::regex simple_re(
        R"(^[ \t]*((?:layout\s*\([^)]*\)\s*)?uniform\s+(?!struct\b)(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([^;]+?)\s*;))",
        std::regex::multiline | std::regex::optimize);

    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, simple_re)) {
            size_t off  = m.position(0) + (cur - source.cbegin());
            size_t full = m[1].str().size();
            std::string type      = m[2].str();
            std::string decllist  = m[3].str();
            cur = m.suffix().first;

            if (is_in_comment(source, off)) continue;
            if (is_opaque_glsl_type(type))  continue;

            std::vector<std::string> names;
            split_declarator(decllist, names);
            for (const auto& n : names) {
                std::string var, arr;
                if (parse_declarator(n, var, arr)) {
                    members.push_back({type + " " + var + arr, var});
                    erases.push_back({off, full});
                }
            }
        }
    }

    static const std::regex struct_re(
        R"(^[ \t]*(uniform\s+struct\s+(\w+)?\s*\{))",
        std::regex::multiline | std::regex::optimize);

    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, struct_re)) {
            size_t off     = m.position(0) + (cur - source.cbegin());
            size_t brace   = off + m[1].str().size() - 1;
            bool  named    = m[2].matched;
            std::string struct_name = named ? m[2].str() : "";
            size_t close   = find_matching_brace(source, brace);
            cur = m.suffix().first;
            if (close == std::string::npos) continue;
            if (is_in_comment(source, off)) continue;

            std::string struct_def = source.substr(brace, close - brace + 1);
            size_t after = close + 1;
            size_t semi  = source.find(';', after);
            if (semi == std::string::npos) continue;
            std::string decllist = source.substr(after, semi - after);

            std::vector<std::string> names;
            split_declarator(decllist, names);
            for (const auto& n : names) {
                std::string var, arr;
                if (!parse_declarator(n, var, arr)) continue;
                if (named) {
                    erases.push_back({off, 7});                  // "uniform "
                    erases.push_back({after, semi - after + 1}); // " u;"
                    members.push_back({struct_name + " " + var + arr, var});
                } else {
                    erases.push_back({off, semi - off + 1});
                    members.push_back({struct_def + " " + var + arr, var});
                }
            }
        }
    }

    if (members.empty()) return;

    size_t version_end = 0;
    {
        auto vp = source.find("#version");
        if (vp != std::string::npos) {
            auto nl = source.find('\n', vp);
            version_end = (nl != std::string::npos) ? nl + 1 : source.size();
        }
    }

    std::string injection;
    injection += "\nuniform mithril_GlobalBlock {\n";
    for (const auto& u : members)
        injection += "    " + u.decl + ";\n";
    injection += "} _m;\n\n";
    for (const auto& u : members)
        injection += "#define " + u.name + " _m." + u.name + "\n";
    injection += "\n";

    std::sort(erases.begin(), erases.end(),
              [](const Erase& a, const Erase& b) { return a.pos > b.pos; });
    for (const auto& e : erases)
        source.erase(e.pos, e.len);

    source.insert(version_end, injection);
}

// Per-stage descriptor binding spaces. setAutoMapBindings() numbers bindings
// per TShader restarting at 0, and each GL stage is its own TShader here, so
// without a per-stage shift VS and FS would collide on (set,binding).
constexpr unsigned kBindingSpacePerStage = 64;

unsigned binding_base_for_stage(EShLanguage stage) {
    switch (stage) {
        case EShLangVertex:   return 0u * kBindingSpacePerStage;
        case EShLangFragment: return 1u * kBindingSpacePerStage;
        case EShLangCompute:  return 2u * kBindingSpacePerStage;
        default:              return 3u * kBindingSpacePerStage;
    }
}

// Per-stage explicit binding injection for opaque uniforms and UBO blocks
// (root cause AK fix: prevents VS/FS binding collisions). Injects
// layout(binding=N) at the GLSL source level, N = stage_base + sequential
// counter. Idempotent; skips comments and already-qualified declarations.
void inject_opaque_bindings(std::string& source, GLenum gl_stage) {
    EShLanguage stage = to_esh_stage(gl_stage);
    if (stage == EShLangCount) return;
    unsigned binding = binding_base_for_stage(stage);

    // Pass 1: UBO blocks (named + anonymous).
    static const std::regex block_re(
        R"((^[ \t]*)uniform\s+(?:layout\s*\([^)]*\)\s*)?(\w+)?\s*\{)",
        std::regex::multiline | std::regex::optimize);
    {
        auto cur = source.cbegin();
        std::smatch m;
        std::string out;
        out.reserve(source.size() + 64);
        size_t last = 0;
        while (std::regex_search(cur, source.cend(), m, block_re)) {
            size_t pos = m.position(0) + (cur - source.cbegin());
            if (is_in_comment(source, pos)) {
                out.append(source, last, pos - last);
                out += m[0].str();
                last = pos + m[0].length();
                cur = m.suffix().first;
                continue;
            }
            std::string full_match = m[0].str();
            if (full_match.find("layout(") != std::string::npos &&
                full_match.find("binding=") != std::string::npos) {
                out.append(source, last, pos - last);
                out += m[0].str();
                last = pos + m[0].length();
                cur = m.suffix().first;
                continue;
            }
            std::string indent = m[1].str();
            std::string blockname = m[2].matched ? m[2].str() : std::string();
            out.append(source, last, pos - last);
            out += indent;
            out += "layout(binding=";
            out += std::to_string(binding++);
            out += ") uniform ";
            if (!blockname.empty()) { out += blockname; out += ' '; }
            out += "{";
            last = pos + m[0].length();
            cur = m.suffix().first;
        }
        out.append(source, last, std::string::npos);
        source.swap(out);
    }

    // Pass 2: opaque uniforms (sampler*/image*).
    static const std::regex opaque_re(
        R"((^[ \t]*)uniform\s+(?:layout\s*\([^)]*\)\s*)?(\w+)\s+(\w+)\s*((?:\[[^\]]*\])*)\s*;)",
        std::regex::multiline | std::regex::optimize);
    {
        auto cur = source.cbegin();
        std::smatch m;
        std::string out;
        out.reserve(source.size() + 64);
        size_t last = 0;
        while (std::regex_search(cur, source.cend(), m, opaque_re)) {
            size_t pos = m.position(0) + (cur - source.cbegin());
            std::string vartype = m[2].str();
            if (!is_opaque_glsl_type(vartype)) {
                cur = m.suffix().first;
                continue;
            }
            if (is_in_comment(source, pos)) {
                cur = m.suffix().first;
                continue;
            }
            std::string full_match = m[0].str();
            if (full_match.find("layout(") != std::string::npos &&
                full_match.find("binding=") != std::string::npos) {
                cur = m.suffix().first;
                continue;
            }
            std::string indent = m[1].str();
            std::string varname = m[3].str();
            std::string arr = m[4].matched ? m[4].str() : std::string();
            out.append(source, last, pos - last);
            out += indent;
            out += "layout(binding=";
            out += std::to_string(binding++);
            out += ") uniform ";
            out += vartype;
            out += ' ';
            out += varname;
            out += arr;
            out += ';';
            last = pos + m[0].length();
            cur = m.suffix().first;
        }
        out.append(source, last, std::string::npos);
        source.swap(out);
    }
}

// Inject GL->Vulkan position fixups (Z remap always; Y flip when flip_y) by
// wrapping main() (vertex shaders only). Mirrors MobileGL
// ProgramFactory::InsertPositionFixup at the GLSL level (Mithril does not
// link SPIRV-Tools).
void inject_position_fixup(std::string& src, GLenum gl_stage, bool flip_y) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    static const std::regex main_re(R"(\bvoid\s+main\s*\()");
    if (!std::regex_search(src, main_re)) return;
    src = std::regex_replace(src, main_re, "void _mithril_original_main(");
    src += "\nvoid main() {\n    _mithril_original_main();\n";
    if (flip_y) {
        src += "    gl_Position.y = -gl_Position.y;\n";
    }
    src += "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
}

// Vulkan-incompatible layout qualifier normalization: rewrite layout(packed)
// / layout(shared) to layout(std140) for UBOs and layout(std430) for SSBOs so
// strict EShClientOpenGL + EShMsgVulkanRules compiles them directly. Idempotent
// and comment-aware.
void normalize_vulkan_incompatible_layouts(std::string& source) {
    if (source.find("packed") == std::string::npos &&
        source.find("shared") == std::string::npos) {
        return;
    }

    static const std::regex re(
        R"(layout\s*\(([^)]*)\)\s*(uniform|buffer)\b)",
        std::regex::optimize | std::regex::multiline);

    auto process_arg = [](const std::string& arg, const std::string& replacement,
                          bool* changed) -> std::string {
        size_t b = arg.find_first_not_of(" \t");
        size_t e = arg.find_last_not_of(" \t");
        if (b == std::string::npos) return std::string();
        std::string trimmed = arg.substr(b, e - b + 1);
        if (trimmed == "packed" || trimmed == "shared") {
            *changed = true;
            return replacement;
        }
        return trimmed;
    };

    std::string out;
    out.reserve(source.size());
    std::string::const_iterator it = source.cbegin();
    std::smatch m;
    while (std::regex_search(it, source.cend(), m, re)) {
        size_t match_pos = m.position(0) + (it - source.cbegin());
        if (is_in_comment(source, match_pos)) {
            out.append(it, m[0].second);
            it = m[0].second;
            continue;
        }

        std::string args = m[1].str();
        std::string storage = m[2].str();
        std::string replacement = (storage == "uniform") ? "std140" : "std430";

        std::string new_args;
        bool changed = false;
        std::string cur;
        auto flush = [&]() {
            std::string processed = process_arg(cur, replacement, &changed);
            if (!processed.empty()) {
                if (!new_args.empty()) new_args += ", ";
                new_args += processed;
            }
            cur.clear();
        };
        for (char c : args) {
            if (c == ',') flush();
            else cur += c;
        }
        flush();

        if (!changed) {
            out.append(it, m[0].second);
        } else {
            out.append(it, m[0].first);
            out += "layout(";
            out += new_args;
            out += ") ";
            out += storage;
        }
        it = m[0].second;
    }
    out.append(it, source.cend());
    source.swap(out);
}

// GL legacy construct normalization (fragment shaders only): rewrite
// gl_FragColor -> synthetic layout(location=0) out vec4 _mithril_FragColor.
// Idempotent and comment-aware.
void normalize_gl_legacy_constructs(std::string& source, GLenum gl_stage) {
    if (gl_stage != GL_FRAGMENT_SHADER) return;
    if (source.find("gl_FragColor") == std::string::npos) return;

    static const std::regex re(R"(\bgl_FragColor\b)", std::regex::optimize);

    std::string out;
    out.reserve(source.size());
    std::string::const_iterator it = source.cbegin();
    std::smatch m;
    bool rewritten = false;
    while (std::regex_search(it, source.cend(), m, re)) {
        size_t match_pos = m.position(0) + (it - source.cbegin());
        out.append(it, m[0].first);
        if (is_in_comment(source, match_pos)) {
            out.append(m[0].str());
        } else {
            out.append("_mithril_FragColor");
            rewritten = true;
        }
        it = m[0].second;
    }
    out.append(it, source.cend());
    if (!rewritten) return;
    source.swap(out);

    size_t vp = source.find("#version");
    size_t insert_at = 0;
    if (vp != std::string::npos) {
        size_t nl = source.find('\n', vp);
        insert_at = (nl != std::string::npos) ? nl + 1 : source.size();
    }
    source.insert(insert_at, "layout(location = 0) out vec4 _mithril_FragColor;\n");
}

// setShiftBinding must run on every TShader before parse(), the unwrapped
// fallback included. NOTE: setShiftBinding 经 glslang probe 验证对 auto-mapped
// combined sampler 完全不生效；真正的 binding 分离由 inject_opaque_bindings
// 在源码层面注入 layout(binding=N) 实现。本函数保留调用作为辅助。
void apply_stage_binding_shift(glslang::TShader& sh, EShLanguage stage) {
    const unsigned base = binding_base_for_stage(stage);
    if (base == 0) return;
    sh.setShiftBinding(glslang::EResUbo,     base);
    sh.setShiftBinding(glslang::EResTexture, base);
    sh.setShiftBinding(glslang::EResSampler, base);
    sh.setShiftBinding(glslang::EResImage,   base);
    sh.setShiftBinding(glslang::EResSsbo,    base);
}

// Build the cache key material: a deterministic byte string hashed by the
// cache. Includes every input that affects the emitted SPIR-V -- source,
// stage, flip_y, attrib bindings (sorted), and the MG version (so a version
// bump invalidates stale entries). Mirrors the old FNV-1a key's coverage but
// as a single string for SHA-256.
std::string build_cache_key(GLenum gl_stage, const std::string& glsl_source,
                            const std::unordered_map<std::string, GLuint>* attrib_bindings,
                            bool flip_y) {
    std::string key;
    key.reserve(glsl_source.size() + 96);
    key.append("MG_MITHRIL_VERSION=1000000|stage=");
    key.append(std::to_string(static_cast<unsigned>(gl_stage)));
    key.append("|flip_y=");
    key.append(flip_y ? "1" : "0");
    key.append("|bindings=");
    if (attrib_bindings && !attrib_bindings->empty()) {
        std::vector<std::pair<std::string, GLuint>> sorted(
            attrib_bindings->begin(), attrib_bindings->end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& kv : sorted) {
            key.append(kv.first);
            key.push_back(':');
            key.append(std::to_string(kv.second));
            key.push_back(';');
        }
    }
    key.append("|source=");
    key.append(glsl_source);
    return key;
}

bool glsl_to_spirv(GLenum gl_stage, const std::string& src,
                   std::vector<uint32_t>& spirv, std::string& info,
                   const std::unordered_map<std::string, GLuint>* attrib_bindings,
                   bool flip_y) {
    glslang_init();
    EShLanguage stage = to_esh_stage(gl_stage);
    if (stage == EShLangCount) { info = "unsupported shader stage"; return false; }

    std::string source = src;
    int glsl_version = ensure_glsl_version(source);
    rewrite_desktop_builtins(source, gl_stage);
    // Root cause: gl_VertexID baseVertex semantics. Placed BEFORE the
    // source_unwrapped backup so BOTH fallback paths inherit the injection.
    inject_vertex_id_fixup(source, gl_stage);
    apply_attrib_bindings(source, gl_stage, attrib_bindings);

    normalize_vulkan_incompatible_layouts(source);
    normalize_gl_legacy_constructs(source, gl_stage);

    inject_position_fixup(source, gl_stage, flip_y);

    // wrap_loose_uniforms() can throw std::regex_error on pathological inputs;
    // fall back to the un-wrapped source and let glslang's auto-wrap run.
    std::string source_unwrapped = source;
    bool wrapped = false;
    try {
        wrap_loose_uniforms(source);
        wrapped = true;
    } catch (const std::exception& e) {
        MITHRIL_LOG_WARN("shader", "wrap_loose_uniforms threw: %s; falling back "
                          "to unwrapped source (glslang auto-wrap will run)",
                          e.what());
        source = source_unwrapped;
    }

    // Per-stage explicit binding injection (root cause AK fix). Applied to
    // both wrapped and unwrapped paths so the fallback is also correct.
    inject_opaque_bindings(source, gl_stage);
    inject_opaque_bindings(source_unwrapped, gl_stage);

    glslang::TShader shader(stage);
    const char* s = source.c_str();
    shader.setStrings(&s, 1);

    // GL_KHR_vulkan_glsl path: parse as OpenGL GLSL but emit Vulkan SPIR-V.
    // EShClientOpenGL is required because the Vulkan client dialect forbids
    // non-block uniforms outright; loose uniforms have already been wrapped
    // into a synthetic block by wrap_loose_uniforms() above. Two-level strict
    // fallback below; no third relaxed fallback (its coverage is replaced by
    // normalize_vulkan_incompatible_layouts / normalize_gl_legacy_constructs).
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, glsl_version);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);

    apply_stage_binding_shift(shader, stage);

    shader.setPreamble(
        "#define MG_MITHRIL 1\n"
        "#define MG_MITHRIL_VERSION 1000000\n"
    );

    const EShMessages messages = static_cast<EShMessages>(
        EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(GetDefaultResources(), glsl_version, true, messages)) {
        info = shader.getInfoLog();
        info += shader.getInfoDebugLog();
        // Retry without wrap_loose_uniforms if we wrapped.
        if (wrapped) {
            MITHRIL_LOG_WARN("shader", "glslang parse failed after wrap; retrying "
                              "with unwrapped source");
            source = source_unwrapped;
            glslang::TShader shader2(stage);
            const char* s2 = source.c_str();
            shader2.setStrings(&s2, 1);
            shader2.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, glsl_version);
            shader2.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
            shader2.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
            shader2.setAutoMapLocations(true);
            shader2.setAutoMapBindings(true);
            apply_stage_binding_shift(shader2, stage);
            shader2.setPreamble(
                "#define MG_MITHRIL 1\n"
                "#define MG_MITHRIL_VERSION 1000000\n"
            );
            if (!shader2.parse(GetDefaultResources(), glsl_version, true, messages)) {
                return false;
            }
            glslang::TProgram program2;
            program2.addShader(&shader2);
            if (!program2.link(messages)) {
                info = program2.getInfoLog();
                info += program2.getInfoDebugLog();
                return false;
            }
            glslang::TIntermediate* inter2 = program2.getIntermediate(stage);
            if (!inter2) { info = "no intermediate after link (unwrapped retry)"; return false; }
            glslang::SpvOptions spv_opts2;
            spv_opts2.disableOptimizer = false;
            glslang::GlslangToSpv(*inter2, spirv, &spv_opts2);
            if (spirv.empty()) { info = "SPIR-V generation produced no words (unwrapped retry)"; return false; }
            return true;
        }
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        info = program.getInfoLog();
        info += program.getInfoDebugLog();
        return false;
    }

    glslang::TIntermediate* inter = program.getIntermediate(stage);
    if (!inter) { info = "no intermediate after link"; return false; }

    glslang::SpvOptions spv_opts;
    spv_opts.disableOptimizer = false;
    glslang::GlslangToSpv(*inter, spirv, &spv_opts);
    if (spirv.empty()) { info = "SPIR-V generation produced no words"; return false; }
    return true;
}

} // namespace

bool shader_translate(GLenum gl_stage, const std::string& glsl_source,
                      std::vector<uint32_t>& out_spirv, std::string& out_info_log,
                      const std::unordered_map<std::string, GLuint>* attrib_bindings,
                      bool flip_y) {
    const char* stage_name =
        gl_stage == GL_VERTEX_SHADER ? "vertex" :
        gl_stage == GL_FRAGMENT_SHADER ? "fragment" : "other";

    // Cache lookup: SHA-256(source + stage + attrib bindings + flip_y + MG
    // version). A hit short-circuits glslang entirely. The key includes the
    // bindings so re-linking with a different VertexFormat produces fresh
    // SPIR-V, and flip_y so the Y-flipped (default-FBO) and non-flipped
    // (user-FBO) variants get distinct entries.
    std::string key = build_cache_key(gl_stage, glsl_source, attrib_bindings, flip_y);
    if (const std::vector<uint32_t>* hit = cache_get(key)) {
        out_spirv = *hit;
        MITHRIL_LOG_DEBUG("shader", "Cache hit for %s shader", stage_name);
        return true;
    }

    MITHRIL_LOG_INFO("shader", "Translating %s shader (%zu bytes GLSL, flip_y=%d)",
                     stage_name, glsl_source.size(), (int)flip_y);

    std::vector<uint32_t> spirv;
    if (!glsl_to_spirv(gl_stage, glsl_source, spirv, out_info_log, attrib_bindings, flip_y)) {
        MITHRIL_LOG_ERROR("shader", "GLSL->SPIR-V failed for %s shader: %s",
                          stage_name, out_info_log.c_str());
        return false;
    }

    MITHRIL_LOG_INFO("shader", "Translated %s shader: %zu SPIR-V words",
                     stage_name, spirv.size());

    out_spirv = spirv;
    cache_put(key, std::move(spirv));
    return true;
}

// ---- Fallback error shader (red-screen fix) ----
// When a shader fails to compile, substitute a minimal fallback so draws are
// not skipped (which would leave only the app's clear color visible --
// Minecraft's red loading screen). The fallback is cached in a separate
// in-memory (non-persistent) cache: fallbacks are tiny, generated on failure,
// and should NOT be persisted across restarts (that would mask real failures).
bool get_fallback_spirv(GLenum gl_stage, bool flip_y,
                        std::vector<uint32_t>& out_spirv) {
    static std::mutex fallback_mu;
    static std::unordered_map<uint64_t, std::vector<uint32_t>> fallback_cache;
    uint64_t key = (uint64_t)gl_stage | (flip_y ? (1ULL << 32) : 0);
    {
        std::lock_guard<std::mutex> lk(fallback_mu);
        auto it = fallback_cache.find(key);
        if (it != fallback_cache.end()) {
            out_spirv = it->second;
            return true;
        }
    }

    std::string source;
    if (gl_stage == GL_VERTEX_SHADER) {
        // CRITICAL: fallback VS must NOT read any vertex attributes. Reading
        // layout(location=0) in vec4 a_position when no vertex buffer is bound
        // causes a GPU page fault on Metal/MoltenVK -> VK_ERROR_DEVICE_LOST.
        // Output a fixed degenerate position so triangles collapse to a point
        // (nothing drawn, but no crash). The app's clear color remains visible.
        (void)flip_y;  // no position to flip
        source = "#version 330\n"
                 "void main() {\n"
                 "    gl_Position = vec4(0.0, 0.0, 0.5, 1.0);\n"
                 "}\n";
    } else if (gl_stage == GL_FRAGMENT_SHADER) {
        // Solid gray output -- visible against any clear color.
        source = "#version 330\n"
                 "layout(location = 0) out vec4 fragColor;\n"
                 "void main() {\n"
                 "    fragColor = vec4(0.5, 0.5, 0.5, 1.0);\n"
                 "}\n";
    } else {
        return false;
    }

    std::vector<uint32_t> spirv;
    std::string info;
    if (!glsl_to_spirv(gl_stage, source, spirv, info, nullptr, flip_y)) {
        MITHRIL_LOG_ERROR("shader", "FATAL: fallback shader compilation failed: %s",
                          info.c_str());
        return false;
    }

    MITHRIL_LOG_WARN("shader", "Generated fallback %s shader (flip_y=%d): %zu SPIR-V words",
                     gl_stage == GL_VERTEX_SHADER ? "vertex" : "fragment",
                     (int)flip_y, spirv.size());

    out_spirv = spirv;
    std::lock_guard<std::mutex> lk(fallback_mu);
    fallback_cache[key] = spirv;
    return true;
}

} // namespace mithril::glsl
