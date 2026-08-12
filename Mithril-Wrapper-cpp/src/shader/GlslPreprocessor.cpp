#include "shader/GlslPreprocessor.h"

#include <regex>

namespace mithril::shader {

core::Expected<std::string, ShaderDiagnostic> preprocessGlsl(
    const ShaderSource& input, const PreprocessOptions& options) {
    if (input.source.empty() || input.entryPoint.empty()) {
        return core::Expected<std::string, ShaderDiagnostic>::failure(sanitizeDiagnostic({
            ShaderErrorCode::invalidSource, "shader source and entry point must not be empty", input.sourceName}));
    }
    if (options.targetVersion < 330) {
        return core::Expected<std::string, ShaderDiagnostic>::failure(sanitizeDiagnostic({
            ShaderErrorCode::preprocessFailed, "target version must be at least GLSL 330", input.sourceName}));
    }

    std::string body = std::regex_replace(
        input.source,
        std::regex(R"((^|\n)[ \t]*#version[^\r\n]*(\r?\n|$))"),
        "$1");
    std::string output;
    output.reserve(body.size() + 128);
    output += "#version " + std::to_string(options.targetVersion) + " core\n";
    output += "#define MITHRIL_DIRECT_BACKEND 1\n";
    output += options.flipY ? "#define MITHRIL_FLIP_Y 1\n" : "#define MITHRIL_FLIP_Y 0\n";
    output += "#line 1\n";
    output += body;
    return output;
}

} // namespace mithril::shader
