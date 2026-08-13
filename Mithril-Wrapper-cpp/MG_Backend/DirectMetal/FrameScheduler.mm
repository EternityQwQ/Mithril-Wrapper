#import "MG_Backend/DirectMetal/FrameScheduler.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_set>

namespace mithril::metal {

struct FrameScheduler::Impl {
    dispatch_semaphore_t permits;
    dispatch_group_t pending;
    std::atomic<std::uint64_t> nextSerial{1};
    std::atomic<std::uint64_t> lastSubmitted{0};
    std::atomic<std::uint64_t> completed{0};
    std::mutex completionMutex;
    std::unordered_set<std::uint64_t> finishedOutOfOrder;
};

namespace {

template <typename State>
void markCompleted(State* state, std::uint64_t serial) {
    std::lock_guard lock(state->completionMutex);
    state->finishedOutOfOrder.insert(serial);
    std::uint64_t contiguous = state->completed.load(std::memory_order_relaxed);
    while (state->finishedOutOfOrder.erase(contiguous + 1U) != 0) ++contiguous;
    state->completed.store(contiguous, std::memory_order_release);
}

} // namespace

FrameScheduler::FrameScheduler(std::uint32_t maximumFramesInFlight)
    : impl_(std::make_unique<Impl>()) {
    impl_->permits = dispatch_semaphore_create(std::max(1U, maximumFramesInFlight));
    impl_->pending = dispatch_group_create();
}

FrameScheduler::~FrameScheduler() {
    if (impl_) dispatch_group_wait(impl_->pending, DISPATCH_TIME_FOREVER);
}
FrameScheduler::FrameScheduler(FrameScheduler&&) noexcept = default;
FrameScheduler& FrameScheduler::operator=(FrameScheduler&&) noexcept = default;

core::ValueResult<std::uint64_t> FrameScheduler::reserve() {
    constexpr std::uint64_t timeout = 2ULL * NSEC_PER_SEC;
    if (dispatch_semaphore_wait(impl_->permits, dispatch_time(DISPATCH_TIME_NOW, timeout)) != 0) {
        return core::ValueResult<std::uint64_t>::failure(core::Error::make(
            core::ErrorDomain::device, core::ErrorCode::unavailable,
            "GPU frame queue did not make progress"));
    }
    dispatch_group_enter(impl_->pending);
    return impl_->nextSerial.fetch_add(1, std::memory_order_relaxed);
}

void FrameScheduler::submit(void* rawCommandBuffer, std::uint64_t serial) {
    id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)rawCommandBuffer;
    std::uint64_t observed = impl_->lastSubmitted.load(std::memory_order_relaxed);
    while (observed < serial && !impl_->lastSubmitted.compare_exchange_weak(
        observed, serial, std::memory_order_release, std::memory_order_relaxed)) {}
    Impl* state = impl_.get();
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
        markCompleted(state, serial);
        dispatch_semaphore_signal(state->permits);
        dispatch_group_leave(state->pending);
    }];
    [commandBuffer commit];
}

void FrameScheduler::cancel(std::uint64_t serial) noexcept {
    markCompleted(impl_.get(), serial);
    dispatch_semaphore_signal(impl_->permits);
    dispatch_group_leave(impl_->pending);
}

std::uint64_t FrameScheduler::lastSubmitted() const noexcept {
    return impl_->lastSubmitted.load(std::memory_order_acquire);
}

std::uint64_t FrameScheduler::completed() const noexcept {
    return impl_->completed.load(std::memory_order_acquire);
}

core::Result FrameScheduler::waitIdle(std::uint64_t timeoutNanoseconds) {
    if (dispatch_group_wait(impl_->pending,
            dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(timeoutNanoseconds))) != 0) {
        return core::Result::failure(core::Error::make(core::ErrorDomain::device,
            core::ErrorCode::unavailable, "timed out waiting for GPU completion"));
    }
    return {};
}

} // namespace mithril::metal
