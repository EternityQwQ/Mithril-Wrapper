#pragma once

#include "shader/ShaderError.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace mithril::shader {

enum class ShaderStage : std::uint8_t { vertex, fragment };
enum class BindingKind : std::uint8_t { uniformBuffer, plainUniform, sampledTexture, stageInput };
enum class ScalarKind : std::uint8_t { floating, signedInteger, unsignedInteger, boolean, sampler };

struct ShaderSource {
    ShaderStage stage{ShaderStage::vertex};
    std::string source;
    std::string sourceName{"shader"};
    std::string entryPoint{"main"};
};

struct PreprocessOptions {
    std::uint32_t targetVersion{330};
    bool flipY{};
};

struct SpirvArtifact {
    ShaderStage stage{ShaderStage::vertex};
    std::vector<std::uint32_t> words;
    std::string entryPoint{"main"};
};

struct ReflectedBinding {
    BindingKind kind{BindingKind::uniformBuffer};
    ShaderStage stage{ShaderStage::vertex};
    ScalarKind scalar{ScalarKind::floating};
    std::uint32_t set{};
    std::uint32_t binding{};
    std::uint32_t arraySize{1};
    std::uint32_t vectorSize{1};
    std::uint32_t columns{1};
    std::uint32_t byteSize{};
    std::uint32_t mslBuffer{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t mslTexture{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t mslSampler{std::numeric_limits<std::uint32_t>::max()};
    std::string name;
};

struct MslArtifact {
    ShaderStage stage{ShaderStage::vertex};
    std::string source;
    std::string entryPoint{"main0"};
    std::vector<ReflectedBinding> bindings;
};

struct ShaderCacheKey {
    std::uint64_t sourceHash{};
    std::uint32_t schemaVersion{1};
    std::uint32_t targetVersion{330};
    ShaderStage stage{ShaderStage::vertex};
    bool flipY{};

    auto operator<=>(const ShaderCacheKey&) const = default;
};

[[nodiscard]] ShaderCacheKey makeShaderCacheKey(const ShaderSource&, const PreprocessOptions&) noexcept;

} // namespace mithril::shader
