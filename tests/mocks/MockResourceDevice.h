#pragma once

#include "backend/ResourceDevice.h"

#include <string>
#include <vector>

namespace mithril::tests {

class MockResourceDevice final : public backend::ResourceDevice {
public:
    std::vector<std::string> calls;
    bool failNext{};
    core::ValueResult<core::BufferHandle> createBuffer(const backend::BufferDesc&) override {
        calls.emplace_back("createBuffer");
        if (failNext) return core::ValueResult<core::BufferHandle>::failure(core::Error::make(
            core::ErrorDomain::resource, core::ErrorCode::unavailable, "injected failure"));
        return core::BufferHandle{1, 1};
    }
    core::ValueResult<core::TextureHandle> createTexture(const backend::TextureDesc&) override {
        calls.emplace_back("createTexture"); return core::TextureHandle{1, 1};
    }
    core::ValueResult<core::SamplerHandle> createSampler(const backend::SamplerDesc&) override {
        calls.emplace_back("createSampler"); return core::SamplerHandle{1, 1};
    }
    core::Result upload(core::BufferHandle, std::size_t, std::span<const std::byte>) override { calls.emplace_back("upload"); return {}; }
    core::Result release(core::BufferHandle) override { calls.emplace_back("releaseBuffer"); return {}; }
    core::Result release(core::TextureHandle) override { calls.emplace_back("releaseTexture"); return {}; }
    core::Result release(core::SamplerHandle) override { calls.emplace_back("releaseSampler"); return {}; }
};

} // namespace mithril::tests
