#pragma once

#include "core/Result.h"

#include <cstdint>
#include <memory>

namespace mithril::metal {

class FrameScheduler final {
public:
    explicit FrameScheduler(std::uint32_t maximumFramesInFlight = 3);
    ~FrameScheduler();
    FrameScheduler(FrameScheduler&&) noexcept;
    FrameScheduler& operator=(FrameScheduler&&) noexcept;
    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    [[nodiscard]] core::ValueResult<std::uint64_t> reserve();
    void submit(void* commandBuffer, std::uint64_t serial);
    void cancel(std::uint64_t serial) noexcept;
    [[nodiscard]] std::uint64_t lastSubmitted() const noexcept;
    [[nodiscard]] std::uint64_t completed() const noexcept;
    [[nodiscard]] core::Result waitIdle(std::uint64_t timeoutNanoseconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mithril::metal
