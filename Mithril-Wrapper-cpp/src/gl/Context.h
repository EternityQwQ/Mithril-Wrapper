#pragma once

#include "core/Error.h"
#include "core/Result.h"
#include "gl/Capability.h"
#include "gl/ShareGroup.h"

#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace mithril::gl {

class Context {
public:
    explicit Context(std::shared_ptr<ShareGroup> shareGroup,
                     CapabilityManifest capabilities = {});

    [[nodiscard]] const std::shared_ptr<ShareGroup>& shareGroup() const noexcept;
    [[nodiscard]] const CapabilityManifest& capabilities() const noexcept;

    core::Result makeCurrent();
    core::Result releaseCurrent();
    [[nodiscard]] bool isCurrent() const noexcept;
    static Context* current() noexcept;

    void pushError(core::Error error);
    [[nodiscard]] core::Error popError() noexcept;

private:
    std::shared_ptr<ShareGroup> shareGroup_;
    CapabilityManifest capabilities_;
    std::thread::id owner_;
    std::deque<core::Error> errors_;
    mutable std::mutex mutex_;
    // owner_ and the thread-local current_ are always inspected or changed
    // while ownershipMutex_ is held; this avoids lock-order cycles when a
    // thread switches from one context to another.
    static std::mutex ownershipMutex_;
    static thread_local Context* current_;
};

} // namespace mithril::gl
