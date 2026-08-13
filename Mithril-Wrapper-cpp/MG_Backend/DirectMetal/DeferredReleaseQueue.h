#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace mithril::metal {

class DeferredReleaseQueue final {
public:
    void retire(std::uint64_t afterSerial, std::shared_ptr<void> object);
    void collect(std::uint64_t completedSerial);
    [[nodiscard]] std::size_t pending() const;

private:
    struct Entry {
        std::uint64_t serial;
        std::shared_ptr<void> object;
    };
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace mithril::metal
