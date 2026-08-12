#pragma once

#include "shader/ShaderError.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mithril::shader {

enum class ShaderStage : std::uint8_t { vertex, fragment };
enum class BindingKind : std::uint8_t { uniformBuffer, sampledTexture, sampler };

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
    std::uint32_t set{};
    std::uint32_t binding{};
    std::uint32_t arraySize{1};
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
