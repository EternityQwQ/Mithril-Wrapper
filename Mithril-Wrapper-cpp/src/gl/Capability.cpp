#include "gl/Capability.h"

namespace mithril::gl {

CapabilityPlan chooseCapabilityPlan(const CapabilityManifest& capabilities) noexcept {
    CapabilityPlan result;
    result.binding = capabilities.argumentBuffers
        ? CapabilityPlan::BindingMode::argumentBuffer
        : CapabilityPlan::BindingMode::fixedSlots;
    result.synchronization = capabilities.sharedEvents
        ? CapabilityPlan::SyncMode::sharedEvent
        : CapabilityPlan::SyncMode::commandCompletion;
    result.pipelineCache = capabilities.binaryArchives
        ? CapabilityPlan::PipelineCacheMode::binaryArchive
        : CapabilityPlan::PipelineCacheMode::memory;
    result.exposeTimerQuery = capabilities.counterSampling;
    return result;
}

} // namespace mithril::gl
