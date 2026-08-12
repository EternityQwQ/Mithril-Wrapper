#include "gl/Context.h"

namespace mithril::gl {

thread_local Context* Context::current_ = nullptr;
std::mutex Context::ownershipMutex_;

Context::Context(std::shared_ptr<ShareGroup> shareGroup, CapabilityManifest capabilities)
    : shareGroup_(std::move(shareGroup)), capabilities_(std::move(capabilities)) {}

const std::shared_ptr<ShareGroup>& Context::shareGroup() const noexcept { return shareGroup_; }
const CapabilityManifest& Context::capabilities() const noexcept { return capabilities_; }

core::Result Context::makeCurrent() {
    std::lock_guard lock(ownershipMutex_);
    if (!shareGroup_) {
        return core::Result::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "context has no share group"));
    }
    if (owner_ != std::thread::id{} && owner_ != std::this_thread::get_id()) {
        return core::Result::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "context is current on another thread"));
    }
    if (current_ != nullptr && current_ != this) {
        current_->owner_ = {};
    }
    current_ = this;
    owner_ = std::this_thread::get_id();
    return {};
}

core::Result Context::releaseCurrent() {
    std::lock_guard lock(ownershipMutex_);
    if (current_ != this || owner_ != std::this_thread::get_id()) {
        return core::Result::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "context is not current on this thread"));
    }
    current_ = nullptr;
    owner_ = {};
    return {};
}

bool Context::isCurrent() const noexcept {
    std::lock_guard lock(ownershipMutex_);
    return current_ == this && owner_ == std::this_thread::get_id();
}

Context* Context::current() noexcept { return current_; }

void Context::pushError(core::Error error) {
    std::lock_guard lock(mutex_);
    constexpr std::size_t maxErrors = 16;
    if (errors_.size() < maxErrors) {
        errors_.push_back(error);
    }
}

core::Error Context::popError() noexcept {
    std::lock_guard lock(mutex_);
    if (errors_.empty()) {
        return core::Error::make(core::ErrorDomain::gl, core::ErrorCode::invalid_state, "no error");
    }
    core::Error result = errors_.front();
    errors_.pop_front();
    return result;
}

} // namespace mithril::gl
