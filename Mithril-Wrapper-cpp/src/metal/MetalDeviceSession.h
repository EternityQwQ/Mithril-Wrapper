#pragma once

#include "backend/CommandEncoder.h"
#include "backend/Presenter.h"
#include "backend/ResourceDevice.h"
#include "backend/ShaderCompilerDevice.h"
#include "gl/Capability.h"

#include <memory>
#include <vector>

namespace mithril::metal {

class MetalCommandEncoder;

class MetalDeviceSession final : public backend::ResourceDevice,
                                 public backend::ShaderCompilerDevice,
                                 public backend::Presenter,
                                 public std::enable_shared_from_this<MetalDeviceSession> {
public:
    ~MetalDeviceSession() override;
    MetalDeviceSession(const MetalDeviceSession&) = delete;
    MetalDeviceSession& operator=(const MetalDeviceSession&) = delete;

    [[nodiscard]] static core::ValueResult<std::shared_ptr<MetalDeviceSession>> create();
    [[nodiscard]] const gl::CapabilityManifest& capabilities() const noexcept;
    [[nodiscard]] core::ValueResult<std::unique_ptr<backend::CommandEncoder>> createCommandEncoder();
    // Explicitly synchronous diagnostic path. Normal presentation remains
    // fully asynchronous and never calls this helper.
    [[nodiscard]] core::ValueResult<std::vector<std::byte>> readbackRgba8(core::TextureHandle);
    [[nodiscard]] core::ValueResult<backend::SurfaceDesc> describeSurface(core::SurfaceHandle) const;
    [[nodiscard]] core::Result waitIdle(std::uint64_t timeoutNanoseconds);

    core::ValueResult<core::BufferHandle> createBuffer(const backend::BufferDesc&) override;
    core::ValueResult<core::TextureHandle> createTexture(const backend::TextureDesc&) override;
    core::ValueResult<core::SamplerHandle> createSampler(const backend::SamplerDesc&) override;
    core::Result upload(core::BufferHandle, std::size_t, std::span<const std::byte>) override;
    core::Result release(core::BufferHandle) override;
    core::Result release(core::TextureHandle) override;
    core::Result release(core::SamplerHandle) override;

    core::ValueResult<core::ShaderHandle> createShader(const shader::MslArtifact&) override;
    core::ValueResult<core::PipelineHandle> createPipeline(
        std::span<const core::ShaderHandle>, const backend::PipelineDesc&) override;
    core::Result release(core::ShaderHandle) override;
    core::Result release(core::PipelineHandle) override;

    core::ValueResult<core::SurfaceHandle> createSurface(const backend::SurfaceDesc&) override;
    core::Result resize(core::SurfaceHandle, const backend::SurfaceDesc&) override;
    core::ValueResult<backend::Frame> acquire(core::SurfaceHandle) override;
    core::Result present(const backend::Frame&) override;
    core::Result release(core::SurfaceHandle) override;

private:
    struct Impl;
    explicit MetalDeviceSession(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend class MetalCommandEncoder;
};

} // namespace mithril::metal
