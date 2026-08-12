#include "shader/SpirvCrossMslCompiler.h"

#include <spirv_msl.hpp>

namespace mithril::shader {
namespace {

void appendBindings(std::vector<ReflectedBinding>& output,
                    const spirv_cross::CompilerMSL& compiler,
                    const spirv_cross::SmallVector<spirv_cross::Resource>& resources,
                    BindingKind kind) {
    for (const auto& resource : resources) {
        ReflectedBinding binding;
        binding.kind = kind;
        binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        binding.name = resource.name;
        const auto& type = compiler.get_type(resource.type_id);
        binding.arraySize = type.array.empty() ? 1U : type.array.front();
        output.push_back(std::move(binding));
    }
}

} // namespace

core::Expected<MslArtifact, ShaderDiagnostic> SpirvCrossMslCompiler::translate(
    const SpirvArtifact& input) {
    if (input.words.empty()) {
        return core::Expected<MslArtifact, ShaderDiagnostic>::failure({
            ShaderErrorCode::translationFailed, "SPIR-V input is empty", "shader"});
    }
    try {
        spirv_cross::CompilerMSL compiler(input.words);
        spirv_cross::CompilerMSL::Options options;
        options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 0);
#if defined(MITHRIL_TARGET_IOS)
        options.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
        options.platform = spirv_cross::CompilerMSL::Options::macOS;
#endif
        compiler.set_msl_options(options);
        auto commonOptions = compiler.get_common_options();
        commonOptions.vertex.fixup_clipspace = true;
        commonOptions.vertex.flip_vert_y = true;
        compiler.set_common_options(commonOptions);
        const auto resources = compiler.get_shader_resources();

        MslArtifact result;
        result.stage = input.stage;
        appendBindings(result.bindings, compiler, resources.uniform_buffers, BindingKind::uniformBuffer);
        appendBindings(result.bindings, compiler, resources.sampled_images, BindingKind::sampledTexture);
        result.source = compiler.compile();
        // MSL compilation performs the final identifier legalization (for
        // example, GLSL "main" becomes "main0"). Query the entry point only
        // after compile() has finalized that mapping.
        result.entryPoint = compiler.get_cleansed_entry_point_name(input.entryPoint,
            input.stage == ShaderStage::vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment);
        if (result.entryPoint.empty()) {
            return core::Expected<MslArtifact, ShaderDiagnostic>::failure({
                ShaderErrorCode::translationFailed, "MSL entry point mapping is empty", "shader"});
        }
        return result;
    } catch (const std::exception& error) {
        return core::Expected<MslArtifact, ShaderDiagnostic>::failure(sanitizeDiagnostic({
            ShaderErrorCode::translationFailed, error.what(), "shader"}));
    }
}

} // namespace mithril::shader
