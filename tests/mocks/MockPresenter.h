#pragma once

#include "backend/Presenter.h"

#include <string>
#include <vector>

namespace mithril::tests {

class MockPresenter final : public backend::Presenter {
public:
    std::vector<std::string> calls;
    std::uint64_t generation{1};
    bool drawableAvailable{true};
    core::ValueResult<core::SurfaceHandle> createSurface(const backend::SurfaceDesc& desc) override { calls.emplace_back("create"); generation = desc.generation; return core::SurfaceHandle{1, 1}; }
    core::Result resize(core::SurfaceHandle, const backend::SurfaceDesc& desc) override { calls.emplace_back("resize"); generation = desc.generation; return {}; }
    core::ValueResult<backend::Frame> acquire(core::SurfaceHandle) override {
        calls.emplace_back("acquire");
        if (!drawableAvailable) return core::ValueResult<backend::Frame>::failure(core::Error::make(core::ErrorDomain::surface, core::ErrorCode::unavailable, "drawable unavailable"));
        return backend::Frame{1, generation, core::TextureHandle{1, 1}};
    }
    core::Result present(const backend::Frame&) override { calls.emplace_back("present"); return {}; }
    core::Result release(core::SurfaceHandle) override { calls.emplace_back("release"); return {}; }
};

} // namespace mithril::tests
