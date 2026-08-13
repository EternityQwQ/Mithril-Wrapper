#include "shader/SpirvCrossMslCompiler.h"

#include <spirv_msl.hpp>

#include <limits>

namespace mithril::shader {
namespace {

std::uint32_t arraySize(const spirv_cross::SPIRType& type) {
    std::uint32_t result = 1;
    for (const std::uint32_t count : type.array) result *= count;
    return result;
}

ScalarKind scalarKind(spirv_cross::SPIRType::BaseType type) {
    using Base = spirv_cross::SPIRType;
    switch (type) {
        case Base::Float:
        case Base::Half:
        case Base::Double: return ScalarKind::floating;
        case Base::Int:
        case Base::Short:
        case Base::SByte: return ScalarKind::signedInteger;
        case Base::UInt:
        case Base::UShort:
        case Base::UByte: return ScalarKind::unsignedInteger;
        case Base::Boolean: return ScalarKind::boolean;
        default: return ScalarKind::floating;
    }
}

std::uint32_t metalTypeSize(const spirv_cross::SPIRType& type) {
    const std::uint32_t scalarBytes = type.basetype == spirv_cross::SPIRType::Boolean
        ? 1U : std::max(1U, type.width / 8U);
    const std::uint32_t vectorWidth = type.vecsize == 3U ? 4U : type.vecsize;
    return scalarBytes * vectorWidth * type.columns * arraySize(type);
}

spv::ExecutionModel executionModel(ShaderStage stage) {
    return stage == ShaderStage::vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
}

void assignResource(spirv_cross::CompilerMSL& compiler,
                    const spirv_cross::Resource& resource,
                    std::uint32_t descriptorBinding,
                    spirv_cross::MSLResourceBinding metalBinding) {
    compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0);
    compiler.set_decoration(resource.id, spv::DecorationBinding, descriptorBinding);
    metalBinding.desc_set = 0;
    metalBinding.binding = descriptorBinding;
    compiler.add_msl_resource_binding(metalBinding);
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
        const auto activeVariables = compiler.get_active_interface_variables();
        const auto resources = compiler.get_shader_resources(activeVariables);

        MslArtifact result;
        result.stage = input.stage;
        const spv::ExecutionModel model = executionModel(input.stage);
        std::uint32_t descriptorBinding = 0;
        std::uint32_t bufferIndex = input.stage == ShaderStage::vertex ? 16U : 0U;
        std::uint32_t textureIndex = 0;
        std::uint32_t samplerIndex = 0;

        if (input.stage == ShaderStage::vertex) {
            for (const auto& resource : resources.stage_inputs) {
                const auto& type = compiler.get_type(resource.type_id);
                result.bindings.push_back({BindingKind::stageInput, input.stage, scalarKind(type.basetype),
                    0, compiler.get_decoration(resource.id, spv::DecorationLocation), arraySize(type),
                    type.vecsize, type.columns, metalTypeSize(type),
                    std::numeric_limits<std::uint32_t>::max(),
                    std::numeric_limits<std::uint32_t>::max(),
                    std::numeric_limits<std::uint32_t>::max(), resource.name});
            }
        }

        for (const auto& resource : resources.uniform_buffers) {
            const auto& type = compiler.get_type(resource.base_type_id);
            spirv_cross::MSLResourceBinding assigned{};
            assigned.stage = model;
            assigned.basetype = type.basetype;
            assigned.count = arraySize(compiler.get_type(resource.type_id));
            assigned.msl_buffer = bufferIndex;
            assignResource(compiler, resource, descriptorBinding, assigned);
            result.bindings.push_back({BindingKind::uniformBuffer, input.stage, ScalarKind::floating,
                0, descriptorBinding++, assigned.count, 1, 1,
                static_cast<std::uint32_t>(compiler.get_declared_struct_size(type)),
                bufferIndex++, std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(), resource.name});
        }
        for (const auto& resource : resources.gl_plain_uniforms) {
            const auto& type = compiler.get_type(resource.type_id);
            spirv_cross::MSLResourceBinding assigned{};
            assigned.stage = model;
            assigned.basetype = type.basetype;
            assigned.count = arraySize(type);
            assigned.msl_buffer = bufferIndex;
            assignResource(compiler, resource, descriptorBinding, assigned);
            result.bindings.push_back({BindingKind::plainUniform, input.stage, scalarKind(type.basetype),
                0, descriptorBinding++, arraySize(type), type.vecsize, type.columns,
                metalTypeSize(type), bufferIndex++, std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(), resource.name});
        }
        for (const auto& resource : resources.sampled_images) {
            const auto& type = compiler.get_type(resource.type_id);
            spirv_cross::MSLResourceBinding assigned{};
            assigned.stage = model;
            assigned.basetype = type.basetype;
            assigned.count = arraySize(type);
            assigned.msl_texture = textureIndex;
            assigned.msl_sampler = samplerIndex;
            assignResource(compiler, resource, descriptorBinding, assigned);
            result.bindings.push_back({BindingKind::sampledTexture, input.stage, ScalarKind::sampler,
                0, descriptorBinding++, arraySize(type), 1, 1, sizeof(std::int32_t),
                std::numeric_limits<std::uint32_t>::max(), textureIndex, samplerIndex,
                resource.name});
            textureIndex += assigned.count;
            samplerIndex += assigned.count;
        }
        if (bufferIndex > 31U || textureIndex > 31U || samplerIndex > 16U) {
            return core::Expected<MslArtifact, ShaderDiagnostic>::failure({
                ShaderErrorCode::translationFailed, "shader exceeds Direct Metal resource limits", "shader"});
        }
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
