#pragma once

#include "backend/ResourceTypes.h"
#include "core/Handles.h"
#include "core/Result.h"

#include <span>
#include <cstddef>

namespace mithril::backend {

class ResourceDevice {
public:
    virtual ~ResourceDevice() = default;
    virtual core::ValueResult<core::BufferHandle> createBuffer(const BufferDesc&) = 0;
    virtual core::ValueResult<core::TextureHandle> createTexture(const TextureDesc&) = 0;
    virtual core::ValueResult<core::SamplerHandle> createSampler(const SamplerDesc&) = 0;
    virtual core::Result upload(core::BufferHandle, std::size_t, std::span<const std::byte>) = 0;
    virtual core::Result upload(core::TextureHandle, std::uint32_t mipLevel,
                                std::uint32_t x, std::uint32_t y,
                                std::uint32_t width, std::uint32_t height,
                                std::span<const std::byte>) = 0;
    virtual core::Result release(core::BufferHandle) = 0;
    virtual core::Result release(core::TextureHandle) = 0;
    virtual core::Result release(core::SamplerHandle) = 0;
};

} // namespace mithril::backend
