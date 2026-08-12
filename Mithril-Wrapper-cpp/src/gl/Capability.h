#pragma once

#include <cstdint>
#include <string>

namespace mithril::gl {

struct CapabilityManifest {
    std::string deviceName;
    std::uint32_t maxTextureSize{4096};
    std::uint32_t maxColorAttachments{4};
    bool argumentBuffers{};
    bool sharedEvents{};
    bool counterSampling{};
    bool binaryArchives{};
};

struct CapabilityPlan {
    enum class BindingMode : std::uint8_t { fixedSlots, argumentBuffer };
    enum class SyncMode : std::uint8_t { commandCompletion, sharedEvent };
    enum class PipelineCacheMode : std::uint8_t { memory, binaryArchive };

    BindingMode binding{BindingMode::fixedSlots};
    SyncMode synchronization{SyncMode::commandCompletion};
    PipelineCacheMode pipelineCache{PipelineCacheMode::memory};
    bool exposeTimerQuery{};
};

[[nodiscard]] CapabilityPlan chooseCapabilityPlan(const CapabilityManifest&) noexcept;

} // namespace mithril::gl
