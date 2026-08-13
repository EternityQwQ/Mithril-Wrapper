// Mithril-Wrapper - MG_Backend/DirectVulkan/LogRing.h
// Minimal ring-buffer of resource-operation log lines. On GPU fault
// (vkQueueSubmit / vkWaitForFences failure), the recent N entries are
// dumped to the logger so the fault frame's resource churn can be
// reconstructed post-mortem.
//
// Currently the dump is a no-op-friendly stub: writes one error line per
// ring entry via MITHRIL_LOG_ERROR. A future iteration can swap the body
// for a real ring buffer with structured entries (texture/buffer IDs,
// sizes, op type, frame slot) when more granular diagnostics are needed.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "../../MG_Impl/Log.h"

namespace mithril {
namespace vk {

class LogRing {
public:
    // Record a printf-style resource operation. The format string is
    // evaluated immediately; entries are stored verbatim until dump() is
    // called.
    void record(const char* fmt, ...) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        entries.push_back(std::string(buf));
        if (entries.size() > kCapacity) entries.erase(entries.begin());
    }

    // Dump all buffered entries with a header line. No-op when empty.
    void dump(const char* reason) {
        if (entries.empty()) return;
        MITHRIL_LOG_ERROR("vk", "LogRing dump: %s (%zu entries)", reason, entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            MITHRIL_LOG_ERROR("vk", "  [%zu] %s", i, entries[i].c_str());
        }
    }

private:
    static constexpr size_t kCapacity = 256;
    std::vector<std::string> entries;
};

// Process-wide singleton accessor.
inline LogRing& log_ring() {
    static LogRing r;
    return r;
}

}  // namespace vk
}  // namespace mithril

// Convenience macro: record a resource operation with printf-style args.
#define LOG_RESOURCE(...) ::mithril::vk::log_ring().record(__VA_ARGS__)
