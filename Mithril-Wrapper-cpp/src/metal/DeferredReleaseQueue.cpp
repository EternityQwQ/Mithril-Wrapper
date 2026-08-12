#include "metal/DeferredReleaseQueue.h"

#include <algorithm>

namespace mithril::metal {

void DeferredReleaseQueue::retire(std::uint64_t afterSerial, std::shared_ptr<void> object) {
    if (!object) return;
    std::lock_guard lock(mutex_);
    entries_.push_back({afterSerial, std::move(object)});
}

void DeferredReleaseQueue::collect(std::uint64_t completedSerial) {
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [completedSerial](const Entry& entry) {
        return entry.serial <= completedSerial;
    });
}

std::size_t DeferredReleaseQueue::pending() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

} // namespace mithril::metal
