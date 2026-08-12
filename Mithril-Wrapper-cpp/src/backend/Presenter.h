#pragma once

#include "backend/Surface.h"
#include "core/Handles.h"
#include "core/Result.h"

#include <cstdint>

namespace mithril::backend {

struct Frame {
    std::uint64_t serial{};
    std::uint64_t surfaceGeneration{};
    core::TextureHandle drawable;
    std::uint32_t width{};
    std::uint32_t height{};
};

class Presenter {
public:
    virtual ~Presenter() = default;
    virtual core::ValueResult<core::SurfaceHandle> createSurface(const SurfaceDesc&) = 0;
    virtual core::Result resize(core::SurfaceHandle, const SurfaceDesc&) = 0;
    virtual core::ValueResult<Frame> acquire(core::SurfaceHandle) = 0;
    virtual core::Result present(const Frame&) = 0;
    virtual core::Result release(core::SurfaceHandle) = 0;
};

} // namespace mithril::backend
