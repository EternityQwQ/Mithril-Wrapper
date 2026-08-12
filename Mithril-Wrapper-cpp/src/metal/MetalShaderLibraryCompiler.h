#pragma once

#include "core/Result.h"
#include "shader/ShaderTypes.h"

#include <cstdint>
#include <memory>

namespace mithril::metal {

class CompiledMetalShader final {
public:
    ~CompiledMetalShader();
    CompiledMetalShader(const CompiledMetalShader&) = delete;
    CompiledMetalShader& operator=(const CompiledMetalShader&) = delete;

    [[nodiscard]] shader::ShaderStage stage() const noexcept;
    [[nodiscard]] std::uint64_t hash() const noexcept;

private:
    struct Impl;
    explicit CompiledMetalShader(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend class MetalShaderLibraryCompiler;
    friend class MetalDeviceSession;
};

class MetalShaderLibraryCompiler final {
public:
    explicit MetalShaderLibraryCompiler(void* metalDevice) noexcept;
    [[nodiscard]] core::ValueResult<std::shared_ptr<CompiledMetalShader>> compile(
        const shader::MslArtifact&) const;

private:
    void* device_{};
};

} // namespace mithril::metal
