#include "shader/GlslangCompiler.h"

#include "shader/GlslPreprocessor.h"

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include <mutex>

namespace mithril::shader {
namespace {

void initializeGlslang() {
    static std::once_flag flag;
    std::call_once(flag, [] { glslang::InitializeProcess(); });
}

EShLanguage stageFor(ShaderStage stage) {
    return stage == ShaderStage::vertex ? EShLangVertex : EShLangFragment;
}

} // namespace

core::Expected<SpirvArtifact, ShaderDiagnostic> GlslangCompiler::compile(
    const ShaderSource& input, const PreprocessOptions& options) {
    auto source = preprocessGlsl(input, options);
    if (!source) return core::Expected<SpirvArtifact, ShaderDiagnostic>::failure(source.error());

    initializeGlslang();
    const EShLanguage stage = stageFor(input.stage);
    glslang::TShader shader(stage);
    const char* sourcePointer = source.value().c_str();
    shader.setStrings(&sourcePointer, 1);
    shader.setEntryPoint(input.entryPoint.c_str());
    shader.setSourceEntryPoint(input.entryPoint.c_str());
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL,
                       static_cast<int>(options.targetVersion));
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);

    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules);
    if (!shader.parse(GetDefaultResources(), static_cast<int>(options.targetVersion), false, messages)) {
        return core::Expected<SpirvArtifact, ShaderDiagnostic>::failure(sanitizeDiagnostic({
            ShaderErrorCode::compileFailed, shader.getInfoLog(), input.sourceName}));
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        return core::Expected<SpirvArtifact, ShaderDiagnostic>::failure(sanitizeDiagnostic({
            ShaderErrorCode::compileFailed, program.getInfoLog(), input.sourceName}));
    }
    SpirvArtifact result;
    result.stage = input.stage;
    result.entryPoint = input.entryPoint;
    glslang::GlslangToSpv(*program.getIntermediate(stage), result.words);
    return result;
}

} // namespace mithril::shader
