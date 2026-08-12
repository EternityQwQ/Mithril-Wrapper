#pragma once

#include "core/Error.h"
#include "core/Expected.h"
#include "core/Handles.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace mithril::core {

template <typename T, ObjectKind KindValue>
class ObjectStore {
public:
    using HandleType = Handle<KindValue>;
    using Reference = std::shared_ptr<T>;

    template <typename... Args>
    Expected<HandleType, Error> create(Args&&... args) {
        std::unique_lock lock(mutex_);
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            if (!slot.value) {
                slot.value = std::make_shared<T>(std::forward<Args>(args)...);
                return HandleType{index, slot.generation};
            }
        }
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
            return Expected<HandleType, Error>::failure(
                Error::make(ErrorDomain::resource, ErrorCode::out_of_memory, "object store exhausted"));
        }
        Slot slot;
        slot.value = std::make_shared<T>(std::forward<Args>(args)...);
        slots_.push_back(std::move(slot));
        return HandleType{static_cast<std::uint32_t>(slots_.size() - 1U), 1U};
    }

    Expected<Reference, Error> get(HandleType handle) const {
        std::shared_lock lock(mutex_);
        if (!handle.valid() || handle.slot >= slots_.size()) {
            return Expected<Reference, Error>::failure(
                Error::make(ErrorDomain::resource, ErrorCode::not_found, "object handle does not exist"));
        }
        const Slot& slot = slots_[handle.slot];
        if (!slot.value || slot.generation != handle.generation) {
            return Expected<Reference, Error>::failure(
                Error::make(ErrorDomain::resource, ErrorCode::stale_handle, "object handle generation is stale"));
        }
        return slot.value;
    }

    Expected<Reference, Error> erase(HandleType handle) {
        std::unique_lock lock(mutex_);
        if (!handle.valid() || handle.slot >= slots_.size()) {
            return Expected<Reference, Error>::failure(
                Error::make(ErrorDomain::resource, ErrorCode::not_found, "object handle does not exist"));
        }
        Slot& slot = slots_[handle.slot];
        if (!slot.value || slot.generation != handle.generation) {
            return Expected<Reference, Error>::failure(
                Error::make(ErrorDomain::resource, ErrorCode::stale_handle, "object handle generation is stale"));
        }
        Reference retired = std::move(slot.value);
        slot.generation = detail::nextGeneration(slot.generation);
        return retired;
    }

    [[nodiscard]] std::size_t liveCount() const {
        std::shared_lock lock(mutex_);
        std::size_t result = 0;
        for (const Slot& slot : slots_) {
            result += slot.value ? 1U : 0U;
        }
        return result;
    }

private:
    struct Slot {
        std::uint32_t generation{1};
        Reference value;
    };

    mutable std::shared_mutex mutex_;
    std::vector<Slot> slots_;
};

} // namespace mithril::core
