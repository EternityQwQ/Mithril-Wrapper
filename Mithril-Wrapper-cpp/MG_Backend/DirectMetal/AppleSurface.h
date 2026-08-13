#pragma once

#include "backend/Surface.h"
#include "core/Result.h"

#include <memory>

namespace mithril::platform::apple {

class AppleSurface final {
public:
    AppleSurface() noexcept;
    ~AppleSurface();
    AppleSurface(AppleSurface&&) noexcept;
    AppleSurface& operator=(AppleSurface&&) noexcept;
    AppleSurface(const AppleSurface&) = delete;
    AppleSurface& operator=(const AppleSurface&) = delete;

    [[nodiscard]] static core::ValueResult<AppleSurface> create(const backend::SurfaceDesc&);
    [[nodiscard]] core::Result resize(const backend::SurfaceDesc&);
    [[nodiscard]] void* nextDrawable();
    [[nodiscard]] void* layer() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    struct Impl;
    explicit AppleSurface(std::unique_ptr<Impl>) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace mithril::platform::apple
